/*
  main.c - ST7789 240x280 (MD169) software (bit-bang) demo for the
  rk3506-mini, ported from the f411/nano reference. Static screens only:
  a full 240x280x16bpp frame is ~1.08 Mbits, and the bit-bang spends two
  GPIO ioctls per bit, so expect ~10-30 s per full-frame screen.
*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "gpio_bb.h"
#include "lcd.h"

static double now_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void)
{
    double t0, t1;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (bb_open() != 0)
        return 1;

    printf("ST7789 MD169 240x280, bit-bang SPI mode 3:\n"
           "  SCL=gpio1(JP4.23) SDA=gpio3(JP4.19) CS=gpio7(JP4.24)\n"
           "  DC=gpio47(JP4.33) RES=gpio49(JP4.29) BL=gpio13(JP4.15)*\n"
           "  (*BL is CAN1-muxed on the stock image - jumper BLK to V33\n"
           "   if the panel stays dark)\n");

    t0 = now_s();
    lcd_init();   /* reset + init + black clear */
    t1 = now_s();
    printf("init done in %.1f s\n", t1 - t0);

    printf("screen 1/4: solid red...\n");
    t0 = now_s();
    disp_color(RED);
    t1 = now_s();
    printf("  %.1f s (%.1f kbit/s effective)\n", t1 - t0,
           1075200.0 / (t1 - t0) / 1000.0);

    printf("screen 2/4: 8-color band...\n");
    t0 = now_s();
    disp_band();
    t1 = now_s();
    printf("  %.1f s\n", t1 - t0);

    printf("screen 3/4: horizontal gray ramp...\n");
    disp_gray_hor16();

    printf("screen 4/4: text overlay...\n");
    lcd_fill_rect(0, 240, 240, 40, BLACK);
    lcd_draw_string(16, 248, "RK3506 240x280", YELLOW, BLACK);

    printf("done - screen keeps the last frame; Ctrl-C not needed\n");
    bb_close();
    return 0;
}
