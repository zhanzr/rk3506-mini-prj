/*
  gpio_bb.c - GPIO char-device (uAPI v2) bit-bang bus, see gpio_bb.h.
  SPI mode 3 like the f411 reference: clock idle high, data set while
  clock low, sampled on the rising edge. MSB first.

  Pins are requested individually (chip derived from gpio/32). When SCL
  and SDA land on the same chip (the normal case), the bit loop uses one
  SET_VALUES ioctl per half-clock; otherwise it falls back to separate
  per-line ioctls.
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

struct bpin {
    int gpio;
    int fd;          /* gpiochip fd */
    int hfd;         /* line request fd */
    unsigned offset;
    unsigned bit;    /* 1u << offset */
    unsigned val;    /* current value */
};

static struct bpin p_scl, p_sda, p_cs, p_bl, p_dc, p_res;
static int same_chip_scl_sda;
static int g_slow = 1;    /* us per bit, 0/1 = no stretch */
static int g_edge_fall;   /* 0 = latch on rising edge, 1 = falling */

static int g_scl = 1, g_sda = 3, g_cs = 7, g_bl = 13, g_dc = 47, g_res = 49;

void bb_get_pins(int *scl, int *sda, int *cs, int *bl, int *dc, int *res)
{
    *scl = g_scl; *sda = g_sda; *cs = g_cs;
    *bl = g_bl; *dc = g_dc; *res = g_res;
}

void bb_configure(int scl, int sda, int cs, int bl, int dc, int res)
{
    if (scl >= 0) g_scl = scl;
    if (sda >= 0) g_sda = sda;
    if (cs  >= 0) g_cs  = cs;
    if (bl  >= 0) g_bl  = bl;
    if (dc  >= 0) g_dc  = dc;
    if (res >= 0) g_res = res;
}

static int req_line(int gpio, unsigned int initial, const char *consumer,
                    struct bpin *p)
{
    char dev[32];
    int fd;
    struct gpio_v2_line_request req;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", gpio / 32);
    fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = (unsigned int)(gpio % 32);
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "%s", consumer);
    if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        fprintf(stderr, "request %s line %d: %s\n", dev, gpio % 32,
                strerror(errno));
        close(fd);
        return -1;
    }
    {
        struct gpio_v2_line_values vals;

        vals.mask = 1;
        vals.bits = initial;
        ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
    }
    p->gpio = gpio;
    p->fd = fd;
    p->hfd = req.fd;
    p->offset = (unsigned int)(gpio % 32);
    p->bit = 1u << p->offset;
    p->val = initial ? p->bit : 0;
    return 0;
}

int bb_open(void)
{
    if (req_line(g_scl, 1, "st7789_scl", &p_scl) ||
        req_line(g_sda, 1, "st7789_sda", &p_sda) ||
        req_line(g_cs,  1, "st7789_cs",  &p_cs)  ||
        req_line(g_bl,  1, "st7789_bl",  &p_bl)  ||
        req_line(g_dc,  1, "st7789_dc",  &p_dc)  ||
        req_line(g_res, 1, "st7789_res", &p_res))
        return -1;

    same_chip_scl_sda = (p_scl.fd == p_sda.fd);
    return 0;
}

void bb_close(void)
{
    int *fds[] = { &p_scl.hfd, &p_sda.hfd, &p_cs.hfd,
                   &p_bl.hfd, &p_dc.hfd,  &p_res.hfd };
    for (int i = 0; i < 6; i++)
        if (*fds[i] >= 0) close(*fds[i]);
}

/* set one pin's value via its line handle */
static void pin_set(struct bpin *p, unsigned int on)
{
    struct gpio_v2_line_values vals;

    p->val = on ? p->bit : 0;
    vals.mask = p->bit;
    vals.bits = p->val;
    ioctl(p->hfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

/* set SCL + SDA in one ioctl (same-chip fast path) */
static void set_scl_sda(unsigned int scl_on, unsigned int sda_on)
{
    struct gpio_v2_line_values vals;

    p_scl.val = scl_on ? p_scl.bit : 0;
    p_sda.val = sda_on ? p_sda.bit : 0;
    vals.mask = p_scl.bit | p_sda.bit;
    vals.bits = p_scl.val | p_sda.val;
    ioctl(p_scl.hfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

void bb_res_set(int on) { pin_set(&p_res, (unsigned int)on); }
void bb_dc_set(int on)  { pin_set(&p_dc, (unsigned int)on); }
void bb_cs_set(int on)  { pin_set(&p_cs, (unsigned int)on); }
void bb_bl_set(int on)  { pin_set(&p_bl, (unsigned int)on); }

void bb_slow(int us_per_bit)
{
    g_slow = (us_per_bit < 1) ? 1 : us_per_bit;
}

void bb_edge_falling(void)
{
    g_edge_fall = 1;
}

int bb_edge_falling_active(void)
{
    return g_edge_fall;
}

void bb_write_byte(uint8_t b)
{
    if (same_chip_scl_sda)
    {
        for (int i = 7; i >= 0; i--)
        {
            unsigned int sda = (b >> i) & 1u;

            if (g_edge_fall)
            {
                /* true mode 3: data set while clock high, latched on
                 * the falling edge */
                set_scl_sda(1, sda);
                set_scl_sda(0, sda);
                set_scl_sda(1, sda);
            }
            else
            {
                set_scl_sda(0, sda);   /* falling edge + data      */
                set_scl_sda(1, sda);   /* rising edge: sampled     */
            }
            if (g_slow > 1)
                usleep((useconds_t)g_slow);
        }
    }
    else
    {
        for (int i = 7; i >= 0; i--)
        {
            unsigned int sda = (b >> i) & 1u;

            if (g_edge_fall)
            {
                pin_set(&p_sda, (int)(sda != 0));
                pin_set(&p_scl, 0);
                pin_set(&p_scl, 1);
            }
            else
            {
                pin_set(&p_scl, 0);
                pin_set(&p_sda, (int)sda);
                pin_set(&p_scl, 1);
            }
            if (g_slow > 1)
                usleep((useconds_t)g_slow);
        }
    }
}

void bb_write_bytes(const uint8_t *b, int n)
{
    while (n--)
        bb_write_byte(*b++);
}
