/*
  gpio_bb.c - GPIO char-device (uAPI v2) bit-bang bus, see gpio_bb.h.
  SPI mode 3 like the f411 reference: clock idle high, data set while
  clock low, sampled on the rising edge. MSB first. Two SET_VALUES
  ioctls per bit (chip0 handle groups SCL+SDA+CS+BL on one chip).
*/
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gpio_bb.h"

#define CHIP0_DEV   "/dev/gpiochip0"
#define CHIP1_DEV   "/dev/gpiochip1"
#define OFF_SCL     1
#define OFF_SDA     3
#define OFF_CS      7
#define OFF_BL      13
#define OFF_DC      15   /* chip1 */
#define OFF_RES     17   /* chip1 */

#define BIT_SCL     (1u << OFF_SCL)
#define BIT_SDA     (1u << OFF_SDA)
#define BIT_CS      (1u << OFF_CS)
#define BIT_BL      (1u << OFF_BL)
#define BIT_DC      (1u << OFF_DC)   /* chip1 */
#define BIT_RES     (1u << OFF_RES)  /* chip1 */

static int fd0 = -1, fd1 = -1, h0 = -1, h1 = -1;
static unsigned int v0, v1; /* current output values (bit per offset) */

static int req_output(int chip_fd, const unsigned int *offsets, int n,
                      unsigned int initial, const char *consumer)
{
    struct gpio_v2_line_request req;

    memset(&req, 0, sizeof(req));
    req.num_lines = n;
    for (int i = 0; i < n; i++)
        req.offsets[i] = offsets[i];
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "%s", consumer);

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
        return -1;

    {
        struct gpio_v2_line_values vals;

        vals.mask = ((1u << n) - 1);
        vals.bits = initial;
        if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0)
            return -1;
    }
    return req.fd;
}

static void set0(unsigned int mask, unsigned int bits)
{
    struct gpio_v2_line_values vals;

    v0 = (v0 & ~mask) | (bits & mask);
    vals.mask = mask;
    vals.bits = v0 & mask;
    ioctl(h0, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

static void set1(unsigned int mask, unsigned int bits)
{
    struct gpio_v2_line_values vals;

    v1 = (v1 & ~mask) | (bits & mask);
    vals.mask = mask;
    vals.bits = v1 & mask;
    ioctl(h1, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

int bb_open(void)
{
    unsigned int offs0[4] = { OFF_SCL, OFF_SDA, OFF_CS, OFF_BL };
    unsigned int offs1[2] = { OFF_DC, OFF_RES };

    fd0 = open(CHIP0_DEV, O_RDWR | O_CLOEXEC);
    fd1 = open(CHIP1_DEV, O_RDWR | O_CLOEXEC);
    if (fd0 < 0 || fd1 < 0)
    {
        perror("open gpiochip");
        return -1;
    }

    /* idle: SCL high, SDA high, CS high (deselected), BL high */
    h0 = req_output(fd0, offs0, 4, BIT_SCL | BIT_SDA | BIT_CS | BIT_BL,
                    "st7789_bus");
    h1 = req_output(fd1, offs1, 2, BIT_DC | BIT_RES, "st7789_ctrl");
    if (h0 < 0 || h1 < 0)
    {
        perror("request lines");
        return -1;
    }
    return 0;
}

void bb_close(void)
{
    if (h0 >= 0) close(h0);
    if (h1 >= 0) close(h1);
    if (fd0 >= 0) close(fd0);
    if (fd1 >= 0) close(fd1);
    h0 = h1 = fd0 = fd1 = -1;
}

void bb_res_set(int on)   { set1(BIT_RES, on ? BIT_RES : 0); }
void bb_dc_set(int on)    { set1(BIT_DC, on ? BIT_DC : 0); }
void bb_cs_set(int on)    { set0(BIT_CS, on ? BIT_CS : 0); }
void bb_bl_set(int on)    { set0(BIT_BL, on ? BIT_BL : 0); }

void bb_write_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--)
    {
        unsigned int sda = (b >> i) & 1u ? BIT_SDA : 0;

        set0(BIT_SCL | BIT_SDA, sda);        /* falling edge + data   */
        set0(BIT_SCL, BIT_SCL | (v0 & BIT_SDA)); /* rising edge: sampled */
    }
}

void bb_write_bytes(const uint8_t *b, int n)
{
    while (n--)
        bb_write_byte(*b++);
}
