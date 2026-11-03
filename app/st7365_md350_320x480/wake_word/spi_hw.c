/*
  spi_hw.c - hardware SPI transport via /dev/spidev1.0 for the panel
  4-wire panel. CS is hardware (spidev toggles it per write()); DC and
  RES are GPIO chardev lines set before each transfer.
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

#include "log.h"

static int s_fd = -1;
static int s_res_fd = -1, s_res_hfd = -1;
static int s_dc_fd = -1, s_dc_hfd = -1;
static uint8_t s_mode = SPI_MODE_0;
static unsigned s_speed = 40000000;

void spi_res_open(int gpio)
{
    char dev[32];
    struct gpio_v2_line_request req;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", gpio / 32);
    s_res_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (s_res_fd < 0)
    {
        log_printf(LOG_LVL_ERROR, "open %s: %s", dev, strerror(errno));
        exit(1);
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = (unsigned int)(gpio % 32);
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "st7365_res");
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

void spi_dc_open(int gpio)
{
    char dev[32];
    struct gpio_v2_line_request req;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", gpio / 32);
    s_dc_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (s_dc_fd < 0)
    {
        log_printf(LOG_LVL_ERROR, "open %s: %s", dev, strerror(errno));
        exit(1);
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = (unsigned int)(gpio % 32);
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "st7365_dc");
    if (ioctl(s_dc_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        perror("dc request");
        exit(1);
    }
    s_dc_hfd = req.fd;
}

void spi_dc_set(int on)
{
    struct gpio_v2_line_values vals;

    vals.mask = 1;
    vals.bits = on ? 1 : 0;
    ioctl(s_dc_hfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

int spi_open(unsigned speed_hz)
{
    uint8_t bits = 8;

    s_speed = speed_hz;
    s_fd = open("/dev/spidev0.0", O_RDWR);
    if (s_fd < 0)
    {
        log_printf(LOG_LVL_ERROR, "open /dev/spidev0.0: %s", strerror(errno));
        return -1;
    }
    if (ioctl(s_fd, SPI_IOC_WR_MODE, &s_mode) < 0 ||
        ioctl(s_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(s_fd, SPI_IOC_WR_MAX_SPEED_HZ, &s_speed) < 0)
    {
        perror("spi ioctl");
        return -1;
    }
    log_printf(LOG_LVL_INFO, "spi0: /dev/spidev0.0 mode %u @ %u Hz (driver may clamp)",
           s_mode, s_speed);
    return 0;
}

void spi_close(void)
{
    if (s_dc_hfd >= 0) close(s_dc_hfd);
    if (s_dc_fd >= 0) close(s_dc_fd);
    if (s_res_hfd >= 0) close(s_res_hfd);
    if (s_res_fd >= 0) close(s_res_fd);
    if (s_fd >= 0) close(s_fd);
    s_dc_hfd = s_res_hfd = s_fd = -1;
}

void spi_write_byte(uint8_t b)
{
    if (write(s_fd, &b, 1) < 0)
        perror("spi write");
}

/* the panel wants each RGB565 word high-byte first, but the uint16
   buffers hold host (little-endian) order - swap into a staging buffer */
void spi_write_pixels16(const uint16_t *px, size_t count)
{
    static uint8_t stage[2048]; /* 1024 pixels < 4096 bufsiz */

    while (count)
    {
        size_t n = count > 1024 ? 1024 : count;

        for (size_t i = 0; i < n; i++)
        {
            stage[2 * i]     = (uint8_t)(px[i] >> 8);
            stage[2 * i + 1] = (uint8_t)(px[i] & 0xFF);
        }
        if (write(s_fd, stage, n * 2) < 0)
        {
            perror("spi write");
            return;
        }
        px += n;
        count -= n;
    }
}

void spi_write_buf(const uint8_t *b, size_t n)
{
    /* spidev limits a single transfer to its bufsiz (default 4096) */
    while (n)
    {
        size_t chunk = n > 4096 ? 4096 : n;

        if (write(s_fd, b, chunk) < 0)
        {
            perror("spi write");
            return;
        }
        b += chunk;
        n -= chunk;
    }
}
