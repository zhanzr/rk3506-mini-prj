/*
  spi_hw.c - hardware SPI transport via /dev/spidev0.0.
  9-bit frames (D/C + 8 data) packed MSB-first into a continuous byte
  bitstream. CS pulses per write(); stream flushes are cut only at
  9-byte boundaries (= exactly 8 complete frames), so CS never rises
  mid-frame. A trailing partial frame (<9 bits) is discarded by the
  panel when CS rises - same as the f411 reference HW path.
*/
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "spi_hw.h"

#define STREAM_BUF 288   /* 32 frames x 9 bytes */

static int  s_fd = -1;
static int  s_res_fd = -1;
static int  s_res_hfd = -1;
static uint32_t s_acc;
static int  s_nbits;     /* bits in accumulator (0..7 during stream) */
static uint8_t  s_buf[STREAM_BUF];
static size_t   s_len;

void spi_res_open(int gpio)
{
    char dev[32];
    struct gpio_v2_line_request req;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", gpio / 32);
    s_res_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (s_res_fd < 0)
    {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        exit(1);
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = (unsigned int)(gpio % 32);
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "ili9163c_res");
    if (ioctl(s_res_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        perror("res request");
        exit(1);
    }
    s_res_hfd = req.fd;
}

void spi_res_set(int on)
{
    struct gpio_v2_line_values vals;

    vals.mask = 1;
    vals.bits = on ? 1 : 0;
    ioctl(s_res_hfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

static void buf_flush(void)
{
    if (s_len)
    {
        if (write(s_fd, s_buf, s_len) < 0)
            perror("spi write");
        s_len = 0;
    }
}

static void buf_put(uint8_t b)
{
    s_buf[s_len++] = b;
}

int spi_open(unsigned speed_hz)
{
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;

    s_fd = open("/dev/spidev0.0", O_RDWR);
    if (s_fd < 0)
    {
        fprintf(stderr, "open /dev/spidev0.0: %s\n", strerror(errno));
        return -1;
    }
    if (ioctl(s_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(s_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(s_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0)
    {
        perror("spi ioctl");
        return -1;
    }
    printf("spi0: /dev/spidev0.0 mode 0 @ %u Hz (driver may clamp)\n",
           speed_hz);
    return 0;
}

int spi_set_speed(unsigned speed_hz)
{
    return ioctl(s_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz);
}

void spi_close(void)
{
    buf_flush();
    if (s_res_hfd >= 0) close(s_res_hfd);
    if (s_res_fd >= 0) close(s_res_fd);
    if (s_fd >= 0) close(s_fd);
    s_res_hfd = s_res_fd = s_fd = -1;
}

/* one 9-bit frame into the byte accumulator */
static void frame(uint8_t dc, uint8_t byte)
{
    uint32_t f = ((uint32_t)(dc & 1) << 8) | byte;
    int i;

    for (i = 8; i >= 0; i--)
    {
        s_acc = (s_acc << 1) | ((f >> i) & 1u);
        s_nbits++;
        if (s_nbits % 8 == 0)
            buf_put((uint8_t)(s_acc & 0xFF));
    }
}

void spi_cmd(uint8_t b)
{
    buf_flush();                 /* end any pending partial frame */
    frame(0, b);
    /* 9-bit frame spans 2 bytes: pad bit 9 into byte 2, then CS rise -
     * frame complete, padding bits discarded */
    if (s_nbits % 8)
        buf_put((uint8_t)((s_acc << (8 - (s_nbits % 8))) & 0xFF));
    buf_flush();
    s_acc = 0;
    s_nbits = 0;
}

void spi_data8(uint8_t b)
{
    buf_flush();
    frame(1, b);
    if (s_nbits % 8)
        buf_put((uint8_t)((s_acc << (8 - (s_nbits % 8))) & 0xFF));
    buf_flush();
    s_acc = 0;
    s_nbits = 0;
}

void spi_stream_begin(void)
{
    buf_flush();                 /* end any single-frame transfer */
    s_acc = 0;
    s_nbits = 0;
    s_len = 0;
}

void spi_stream_word(uint16_t v)
{
    frame(1, (uint8_t)(v >> 8));
    frame(1, (uint8_t)(v & 0xFF));
    /* flush in 9-byte groups (8 complete frames) - CS rises exactly at
     * a frame boundary, never mid-frame */
    if (s_len >= STREAM_BUF - 9 && s_len % 9 == 0)
        buf_flush();  /* 279 bytes = 248 complete frames - CS-safe */
}

void spi_stream_end(void)
{
    if (s_nbits % 8)
        buf_put((uint8_t)((s_acc << (8 - (s_nbits % 8))) & 0xFF));
    buf_flush();
    s_acc = 0;
    s_nbits = 0;
    s_len = 0;
}
