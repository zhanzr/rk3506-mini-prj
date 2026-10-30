/*
  main.c - ST7789 240x280 (MD169) software (bit-bang) demo for the
  rk3506-mini, ported from the f411/nano reference.

  Usage:
    ./st7789_demo [options]
      -c N   CS gpio   (default 7,  = JP4.24; try 48 = JP4.31)
      -d N   DC gpio   (default 47, = JP4.33)
      -k N   SCLK gpio (default 1,  = JP4.23)
      -m N   MOSI gpio (default 3,  = JP4.19)
      -r N   RES gpio  (default 49, = JP4.29)
      -b N   BL  gpio  (default 13, = JP4.15)
      -s N   slow: N us per bit (default 0)
      -f     falling-edge latch (true mode 3) instead of rising-edge
      -n     no INVON (panel without IPS inversion)
      -S     swap SDA and RES gpio assignments (silk-lie workaround)
    Screen sequence: red -> color band -> gray ramp -> text.

  A BLACK panel after init means the commands never reached the
  controller; a WHITE panel means SPI works but the clear-to-black was
  inverted (IPS INVON semantics) - retry with -n.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("usage: st7789_demo [-c cs] [-d dc] [-k sclk] [-m mosi] "
           "[-r res] [-b bl]\n"
           "                   [-s us_per_bit] [-f] [-n] [-S]\n");
}

int main(int argc, char **argv)
{
    double t0, t1;
    int invon = 1, swap_sda_res = 0;
    int scl = 1, sda = 3, cs = 7, bl = 13, dc = 47, res = 49;
    int slow = 0;
    int one = 0;
    int gs, gd, gc, gb, gdc, gr;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 'c': cs  = atoi(argv[++i]); break;
            case 'd': dc  = atoi(argv[++i]); break;
            case 'k': scl = atoi(argv[++i]); break;
            case 'm': sda = atoi(argv[++i]); break;
            case 'r': res = atoi(argv[++i]); break;
            case 'b': bl  = atoi(argv[++i]); break;
            case 's': slow = atoi(argv[++i]); break;
            case 'f': bb_edge_falling(); break;
            case 'n': invon = 0; break;
            case 'S': swap_sda_res = 1; break;
            case '1': one = 1; break;
            default:  usage(); return 1;
            }
        }
        else
        {
            usage();
            return 1;
        }
    }

    if (swap_sda_res)
    {
        int t = sda;

        sda = res;
        res = t;
    }
    bb_configure(scl, sda, cs, bl, dc, res);
    lcd_set_inversion(invon);
    if (slow > 0)
        bb_slow(slow);

    if (bb_open() != 0)
        return 1;

    bb_get_pins(&gs, &gd, &gc, &gb, &gdc, &gr);
    printf("ST7789 MD169 240x280 bit-bang: SCL=gpio%d SDA=gpio%d CS=gpio%d\n"
           "  DC=gpio%d RES=gpio%d BL=gpio%d | slow=%dus/bit edge=%s "
           "invon=%d sda-res-swap=%d\n",
           gs, gd, gc, gdc, gr, gb, slow,
           bb_edge_falling_active() ? "falling" : "rising", invon,
           swap_sda_res);

    t0 = now_s();
    lcd_init(); /* reset + init + clear */
    t1 = now_s();
    printf("init done in %.1f s\n", t1 - t0);

    printf("screen 1/4: solid red...\n");
    t0 = now_s();
    disp_color(RED);
    t1 = now_s();
    printf("  %.1f s (%.1f kbit/s effective)\n", t1 - t0,
           1075200.0 / (t1 - t0) / 1000.0);
    if (one)
    {
        printf("one-screen mode - keeping red frame\n");
        bb_close();
        return 0;
    }

    printf("screen 2/4: 8-color band...\n");
    disp_band();

    printf("screen 3/4: horizontal gray ramp...\n");
    disp_gray_hor16();

    printf("screen 4/4: text overlay...\n");
    lcd_fill_rect(0, 240, 240, 40, BLACK);
    lcd_draw_string(16, 248, "RK3506 240x280", YELLOW, BLACK);

    printf("done - screen keeps the last frame\n");
    bb_close();
    return 0;
}
