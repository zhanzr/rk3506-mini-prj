/*
 * SSD1317 "MD066" 0.66" 64x48 mono OLED demo for the rk3506-mini
 * (Vanxoak HD-RK3506G-CORE + HD-RK3506-EVB), ported from
 * pbeagle_prj/app/ssd1317_md066_64x48_iic (PocketBeagle), which itself
 * came from the MP157 DK1 and AT32F425 references.
 *
 * Same Linux user-space architecture - i2c-dev for the module, GPIO
 * char-device uAPI for RES - no library dependencies:
 *
 *   SCL  -> JP4.28 (EXP_I2C0_SCL, LCD_D17)   /dev/i2c-0
 *   SDA  -> JP4.27 (EXP_I2C0_SDA, LCD_D18)
 *   RES  -> JP4.29 (LCD_D10 / GPIO1_C1),     active low
 *          = gpiochip1 line 17 (linux gpio number 49)
 *   V33  -> JP4.1 (3.3 V), GND -> JP4.9
 *
 * The bus is shared with the onboard RTC (0x68) and ES8388 codec (0x11),
 * 10k pull-ups are on the baseboard.
 *
 * Module 7-bit I2C address 0x3C. Gotcha (same as on the PocketBeagle):
 * the module does not ACK while RES is floating/low, so the line is
 * requested with an initial inactive (high) value before the probe.
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

#include "oled.h"

#define OLED_ADDR       0x3C
#define I2C_DEV         "/dev/i2c-0"
#define RES_GPIO_CHIP   1       /* GPIO1 */
#define RES_GPIO_OFFSET 17      /* C1 -> linux gpio 49 */
#define MAX_SECONDS     30

static volatile sig_atomic_t running = 1;
volatile sig_atomic_t oled_running = 1;

static void stop_handler(int sig)
{
    (void)sig;
    running = 0;
    oled_running = 0;
}

/* request one line as output; initial value high (RES inactive) */
static int line_request_output(int chip_fd, unsigned int offset)
{
    struct gpio_v2_line_request req;

    memset(&req, 0, sizeof(req));
    req.offsets[0] = offset;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "ssd1317_demo");

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

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0)
    {
        fprintf(stderr, "open %s: %s\n", I2C_DEV, strerror(errno));
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, OLED_ADDR) < 0)
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
        fprintf(stderr, "RES gpio%d.%d busy: %s (sysfs user? "
                "echo %d > /sys/class/gpio/unexport)\n",
                RES_GPIO_CHIP, RES_GPIO_OFFSET, strerror(errno),
                RES_GPIO_CHIP * 32 + RES_GPIO_OFFSET);
        return 1;
    }
    printf("RES = JP4.29 / GPIO1_C1 (gpiochip%d line %d, gpio %d) ok\n",
           RES_GPIO_CHIP, RES_GPIO_OFFSET,
           RES_GPIO_CHIP * 32 + RES_GPIO_OFFSET);

    oled_set_i2c_fd(i2c_fd);

    {
        uint8_t probe[2] = { 0x00, 0xAE };

        if (write(i2c_fd, probe, 2) != 2)
        {
            fprintf(stderr, "OLED not detected at 0x%02x on %s (%s) - "
                    "check wiring (SCL=JP4.28, SDA=JP4.27, RES=JP4.29, "
                    "V33=JP4.1, GND=JP4.9)\n",
                    OLED_ADDR, I2C_DEV, strerror(errno));
            return 1;
        }
    }

    printf("SSD1317 MD066 64x48 I2C demo start (%s @ 0x%02x)\n",
           I2C_DEV, OLED_ADDR);
    OLED_Init(res_fd);

    {
        struct timespec ts;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        long start_ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;

        while (running)
        {
            OLED_Demo();
            if (!running)
                break;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long now_ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
            if (now_ms - start_ms > MAX_SECONDS * 1000L)
            {
                printf("%d s cap reached\n", MAX_SECONDS);
                break;
            }
            oled_delay_ms(200);
        }
    }

    OLED_Clear();
    OLED_ShowString(0, 2, (const unsigned char *)"bye", 16);
    printf("bye\n");

    close(res_fd);
    close(res_chip);
    close(i2c_fd);
    return 0;
}
