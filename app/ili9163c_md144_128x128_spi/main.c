/*
  main.c - ILI9163C 128x128 (MD144) software (bit-bang) demo for the
  rk3506-mini, ported from the f411 reference. 3-wire serial: 9-bit
  frames (D/C bit first), no D/C pin, backlight hardwired on-module.

  128x128x16bpp = 262 kbit/frame; at ~580 kbit/s effective that is
  ~0.5 s per full frame.

  Usage: ./ili9163c_demo [-c cs] [-k sclk] [-m mosi] [-r res] [-s us]
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

static void usage(void)
{
    printf("usage: ili9163c_demo [-c cs] [-k sclk] [-m mosi] [-r res] "
           "[-s us_per_bit]\n");
}

int main(int argc, char **argv)
{
    double t0, t1;
    int cs = 7, scl = 1, sda = 3, res = 49, slow = 0;
    int gs, gd, gc, gb, gdc, gr;

    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 'c': cs  = atoi(argv[++i]); break;
            case 'k': scl = atoi(argv[++i]); break;
            case 'm': sda = atoi(argv[++i]); break;
            case 'r': res = atoi(argv[++i]); break;
            case 's': slow = atoi(argv[++i]); break;
            default:  usage(); return 1;
            }
        }
        else
        {
            usage();
            return 1;
        }
    }

    bb_configure(scl, sda, cs, -1, -1, res);
    if (slow > 0)
        bb_slow(slow);

    if (bb_open() != 0)
        return 1;
    bb_get_pins(&gs, &gd, &gc, &gb, &gdc, &gr);

    printf("ILI9163C MD144 128x128, 3-wire 9-bit frames, bit-bang:\n"
           "  SCL=gpio%d(JP4.23) SDA=gpio%d(JP4.19) CS=gpio%d(JP4.24) "
           "RES=gpio%d(JP4.29) slow=%dus/bit\n",
           gs, gd, gc, gr, slow);

    t0 = now_s();
    lcd_init(); /* reset + init + black clear */
    t1 = now_s();
    printf("init done in %.1f s\n", t1 - t0);

    printf("screen 1/4: solid red...\n");
    t0 = now_s();
    disp_color(RED);
    t1 = now_s();
    printf("  %.1f s (%.1f kbit/s effective)\n", t1 - t0,
           262144.0 / (t1 - t0) / 1000.0);

    printf("screen 2/4: 8-color band...\n");
    t0 = now_s();
    disp_band();
    t1 = now_s();
    printf("  %.1f s\n", t1 - t0);

    printf("screen 3/4: horizontal gray ramp...\n");
    disp_gray_hor16();

    printf("screen 4/4: text overlay...\n");
    lcd_fill_rect(0, 112, 128, 16, BLACK);
    lcd_draw_string(8, 112, "RK3506 128x128", YELLOW, BLACK);

    printf("done - screen keeps the last frame\n");
    bb_close();
    return 0;
}
