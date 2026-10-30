/*
  main.c - ILI9163C 128x128 (MD144) demo for the rk3506-mini over
  hardware SPI0 (/dev/spidev0.0), ported from the f411/f746 references.

  Usage: ./ili9163c_demo [options]
    -c N    CS gpio     (default 7  = JP4.24)
    -r N    RES gpio    (default 49 = JP4.29)
    -s N    SPI speed in kHz (default 8000 = 8 MHz)
    -M hex  MADCTL value (default 0x08; 0x48 = X-mirrored)
    -n      no INVON (panel without IPS inversion)
    -w      SPI speed sweep: checkerboard at 8/12/16/20/26/32 MHz,
            3 s each - the highest clean speed is the max
    -1      exit after the first screen
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpio_bb.h"
#include "lcd.h"
#include "spi_hw.h"

static double now_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void usage(void)
{
    printf("usage: ili9163c_demo [-c cs] [-r res] [-s kHz] [-M hex] "
           "[-n] [-w] [-1]\n");
}

static void screens(void)
{
    printf("  red / green / blue / white / black\n");
    disp_color(RED);   sleep(1);
    disp_color(GREEN); sleep(1);
    disp_color(BLUE);  sleep(1);
    disp_color(WHITE); sleep(1);
    disp_color(BLACK); sleep(1);

    printf("  8-color band\n");
    disp_band();       sleep(2);

    printf("  frame border\n");
    disp_frame();      sleep(2);

    printf("  horizontal gray ramp (16 steps)\n");
    disp_gray_hor16(); sleep(2);

    printf("  checkerboard\n");
    disp_checker();    sleep(2);

    printf("  1-px vertical stripes\n");
    disp_vstripes();   sleep(2);

    printf("  lines + circles\n");
    disp_color(BLACK);
    lcd_draw_line(0, 0, 127, 127, RED);
    lcd_draw_line(127, 0, 0, 127, GREEN);
    lcd_draw_circle(64, 64, 40, CYAN);
    lcd_draw_circle(64, 64, 20, YELLOW);
    lcd_fill_circle(64, 64, 8, MAGENTA);
    sleep(2);

    printf("  stock boot logo (2x) + caption\n");
    lcd_show_logo();   sleep(3);
}

int main(int argc, char **argv)
{
    double t0, t1;
    int cs = 7, res = 49;
    unsigned int speed_hz = 8000000;
    int invon = 1, sweep = 0, one = 0;
    unsigned madctl = 0x08;
    static const unsigned sweep_khz[] = { 8000, 12000, 16000, 20000,
                                          26000, 32000, 40000 };

    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 'c': cs  = atoi(argv[++i]); break;
            case 'r': res = atoi(argv[++i]); break;
            case 's': speed_hz = (unsigned)atoi(argv[++i]) * 1000; break;
            case 'M': madctl = (unsigned)strtoul(argv[++i], NULL, 16); break;
            case 'n': invon = 0; break;
            case 'w': sweep = 1; break;
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

    lcd_set_madctl((uint8_t)madctl);
    lcd_set_inversion(invon);

    if (spi_open(speed_hz) != 0)
        return 1;
    spi_res_open(res);

    printf("ILI9163C MD144 128x128 on hw SPI0: CS=gpio%d RES=gpio%d "
           "speed=%u kHz MADCTL=0x%02X invon=%d\n",
           cs, res, speed_hz / 1000, madctl, invon);

    if (sweep)
    {
        for (unsigned i = 0;
             i < sizeof(sweep_khz) / sizeof(sweep_khz[0]); i++)
        {
            unsigned khz = sweep_khz[i];

            printf("sweep %u kHz: checkerboard + band (watch for "
                   "corruption)\n", khz);
            if (spi_open(khz * 1000) != 0)
                break;
            disp_checker();
            disp_band();
            sleep(3);
            /* re-open at base speed for next iteration setup */
            spi_close();
            if (spi_open(speed_hz) != 0)
                return 1;
        }
        printf("sweep done - report the highest clean speed\n");
        spi_close();
        spi_close();
        return 0;
    }

    t0 = now_s();
    lcd_init(); /* reset + init + black clear */
    t1 = now_s();
    printf("init done in %.2f s\n", t1 - t0);

    printf("screen 1: solid red (%.3f s for full frame)\n", now_s() * 0);
    t0 = now_s();
    disp_color(RED);
    t1 = now_s();
    printf("  %.3f s (%.1f kbit/s effective)\n", t1 - t0,
           262144.0 / (t1 - t0) / 1000.0);
    if (one)
    {
        printf("one-screen mode\n");
        spi_close();
        spi_close();
        return 0;
    }

    screens();

    printf("done - screen keeps the last frame\n");
    spi_close();
    spi_close();
    return 0;
}
