/*
 * lcm_logo - stock font/icon showcase for the rk3506-mini SSD1317 OLED
 * (64x48, /dev/i2c-0 @ 0x3C, RES = gpio 49), companion to ssd1317_demo.
 *
 * Shows the board's STOCK assets, scaled:
 *
 *   1. The stock boot logo (Vanxoak 'logo.bmp' from the boot partition's
 *      resource.img, 654x270 8bpp RLE) box-downscaled to 64x27 and centered
 *      - generated into logo64.h by tools at porting time.
 *   2. The project's stock F6x8 font scaled 1x / 2x / 3x, filling the whole
 *      panel: "RK3506-mini" (8 px), "RK35" (16 px), "RK" (24 px).
 *
 * Reuses oled.c as the transport (i2c-dev + RES char-device); the
 * framebuffer is composed here and streamed page by page.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "logo64.h"
#include "oled.h"

#define OLED_ADDR_8BIT  0x78    /* 0x3C << 1 */
#define I2C_DEV         "/dev/i2c-0"
#define RES_GPIO_CHIP   1
#define RES_GPIO_OFFSET 17      /* GPIO1_C1 = gpio 49 */
#define MAX_SECONDS     30

static volatile sig_atomic_t running = 1;
volatile sig_atomic_t oled_running = 1;

static void stop_handler(int sig)
{
    (void)sig;
    running = 0;
    oled_running = 0;
}

/* full 64x48 framebuffer, 6 pages of 64 bytes, bit0 = top row */
static unsigned char fb[6 * 64];

static void fb_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

static void fb_pixel(int x, int y, int on)
{
    if (x < 0 || x > 63 || y < 0 || y > 47)
        return;
    if (on)
        fb[(y >> 3) * 64 + x] |= 1u << (y & 7);
    else
        fb[(y >> 3) * 64 + x] &= ~(1u << (y & 7));
}

static void fb_blit(void)
{
    for (unsigned page = 0; page < 6; page++)
    {
        uint8_t buf[1 + 64];

        buf[0] = 0x40; /* control byte: data stream follows */
        memcpy(&buf[1], &fb[page * 64], 64);
        OLED_Set_Pos(0, page);
        Write_IIC_Byte_Buf(OLED_ADDR_8BIT, buf, sizeof(buf));
    }
}

/* draw the stock F6x8 glyph scaled by 'scale' with top-left at (x0, y0) */
static void draw_char_scaled(char chr, int x0, int y0, int scale)
{
    int c = chr - ' ';

    if (c < 0 || c > 94)
        c = 0;
    for (int col = 0; col < 6; col++)
    {
        unsigned char bits = F6x8[c][col];

        for (int row = 0; row < 8; row++)
        {
            if (bits & (1u << row))
            {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_pixel(x0 + col * scale + sx,
                                 y0 + row * scale + sy, 1);
            }
        }
    }
}

static void show_logo(void)
{
    memcpy(fb, logo64_fb, sizeof(fb));
    fb_blit();
}

static void show_scaled_font(void)
{
    fb_clear();
    /* 1x: 6 px/char -> 11 chars = 66 px, drop to 10 to fit 64 */
    for (int i = 0; i < 10; i++)
        draw_char_scaled("RK3506-mini"[i], i * 6, 0, 1);
    /* 2x: 12 px/char -> 4 chars = 48 px */
    for (int i = 0; i < 4; i++)
        draw_char_scaled("RK35"[i], i * 12, 10, 2);
    /* 3x: 18 px/char -> 2 chars = 36 px, centered */
    for (int i = 0; i < 2; i++)
        draw_char_scaled("RK"[i], (64 - 2 * 18) / 2 + i * 18, 26, 3);
    fb_blit();
}

static int line_request_output(int chip_fd, unsigned int offset)
{
    struct gpio_v2_line_request req;

    memset(&req, 0, sizeof(req));
    req.offsets[0] = offset;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "lcm_logo");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
        return -1;
    {
        struct gpio_v2_line_values vals;

        memset(&vals, 0, sizeof(vals));
        vals.mask = 1;
        vals.bits = 1; /* RES inactive (high) */
        ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
    }
    return req.fd;
}

int main(void)
{
    int i2c_fd, res_chip, res_fd;
    char res_chip_path[32];
    struct timespec ts;
    long start_ms;
    int phase = 0;

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0)
    {
        fprintf(stderr, "open %s: %s\n", I2C_DEV, strerror(errno));
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, 0x3C) < 0)
    {
        fprintf(stderr, "I2C_SLAVE: %s\n", strerror(errno));
        return 1;
    }
    snprintf(res_chip_path, sizeof(res_chip_path), "/dev/gpiochip%d",
             RES_GPIO_CHIP);
    res_chip = open(res_chip_path, O_RDWR | O_CLOEXEC);
    if (res_chip < 0)
    {
        fprintf(stderr, "open %s: %s\n", res_chip_path, strerror(errno));
        return 1;
    }
    res_fd = line_request_output(res_chip, RES_GPIO_OFFSET);
    if (res_fd < 0)
    {
        fprintf(stderr, "RES gpio%d.%d busy: %s\n",
                RES_GPIO_CHIP, RES_GPIO_OFFSET, strerror(errno));
        return 1;
    }

    oled_set_i2c_fd(i2c_fd);
    OLED_Init(res_fd);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;

    printf("lcm_logo: stock boot logo (scaled) + stock F6x8 font "
           "(1x/2x/3x), %s @ 0x3C\n", I2C_DEV);

    while (running)
    {
        if (phase & 1)
            show_scaled_font();
        else
            show_logo();
        oled_delay_ms(3000);
        if (!running)
            break;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long now_ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;

        if (now_ms - start_ms > MAX_SECONDS * 1000L)
        {
            printf("%d s cap reached\n", MAX_SECONDS);
            break;
        }
        phase++;
    }

    fb_clear();
    fb_blit();
    OLED_ShowString(0, 2, (const unsigned char *)"bye", 16);
    printf("bye\n");

    close(res_fd);
    close(res_chip);
    close(i2c_fd);
    return 0;
}
