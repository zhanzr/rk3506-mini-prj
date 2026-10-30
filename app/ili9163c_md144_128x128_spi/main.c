/*
  main.c - ILI9163C 128x128 (MD144) demo for the rk3506-mini over
  hardware SPI0 (/dev/spidev0.0), ported from the f411/f746 references.
  Rasters stream via the 9-bit frame packer (bulk, CS held low across
  288-byte chunks cut at frame boundaries).

  Usage: ./ili9163c_demo [options]
    -c N    CS gpio     (default 7  = JP4.24)
    -r N    RES gpio    (default 49 = JP4.29)
    -s N    SPI speed in kHz (default 40000; driver clamps)
    -M hex  MADCTL value: 0x08 / 0x48 (X mirror) / 0x0C / 0x4C (MH too)
    -m      mirror picker: cycles 4 MADCTL values showing text, 2.5 s each
    -n      no INVON
    -w      SPI speed sweep with checkerboard (report highest clean speed)
    -1      exit after the first screen
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
           "[-m] [-n] [-w] [-1]\n");
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

    printf("  text overlay\n");
    lcd_fill_rect(0, 112, 128, 16, BLACK);
    lcd_draw_string(8, 112, "RK3506 128x128", YELLOW, BLACK);
    sleep(2);
}

/* mirror picker: cycles the 4 plausible MADCTL values so the user can
 * pick the one where text reads correctly */
static void mirror_pick(void)
{
    static const unsigned vals[4] = { 0x08, 0x48, 0x0C, 0x4C };

    for (int i = 0; i < 4; i++)
    {
        printf("MADCTL 0x%02X - if the text reads correctly, note this "
               "value\n", vals[i]);
        lcd_set_madctl((uint8_t)vals[i]);
        /* MADCTL can be changed on the fly: re-send it + redraw text */
        disp_color(BLACK);
        lcd_draw_string(8, 56, "MADCTL TEST", WHITE, BLACK);
        {
            char buf[16];

            snprintf(buf, sizeof(buf), "0x%02X", vals[i]);
            lcd_draw_string(32, 76, buf, YELLOW, BLACK);
        }
        sleep(3);
    }
}

static void speed_sweep(unsigned base_khz __attribute__((unused)))
{
    static const unsigned sweep_khz[] = { 8000, 12000, 16000, 20000,
                                          26000, 32000, 40000 };

    for (unsigned i = 0; i < sizeof(sweep_khz) / sizeof(sweep_khz[0]); i++)
    {
        printf("sweep %u kHz: checkerboard + band (watch for corruption)\n",
               sweep_khz[i]);
        if (spi_set_speed(sweep_khz[i] * 1000) != 0)
        {
            printf("  speed not accepted, stopping sweep\n");
            break;
        }
        disp_checker();
        sleep(2);
        disp_band();
        sleep(2);
    }
    printf("sweep done - report the highest clean speed\n");
}

int main(int argc, char **argv)
{
    double t0, t1;
    int cs = 7, res = 49;
    unsigned speed_khz = 40000;
    int invon = 1, sweep = 0, one = 0, mpick = 0;
    unsigned madctl = 0x48;

    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 'c': cs  = atoi(argv[++i]); break;
            case 'r': res = atoi(argv[++i]); break;
            case 's': speed_khz = (unsigned)atoi(argv[++i]); break;
            case 'M': madctl = (unsigned)strtoul(argv[++i], NULL, 16); break;
            case 'n': invon = 0; break;
            case 'w': sweep = 1; break;
            case 'm': mpick = 1; break;
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
    if (spi_open(speed_khz * 1000) != 0)
        return 1;
    spi_res_open(res);

    printf("ILI9163C MD144 128x128 on hw SPI0: CS=gpio%d RES=gpio%d "
           "speed=%u kHz MADCTL=0x%02X invon=%d\n",
           cs, res, speed_khz, madctl, invon);

    if (mpick)
    {
        mirror_pick();
        spi_close();
        return 0;
    }

    t0 = now_s();
    lcd_init();
    t1 = now_s();
    printf("init done in %.2f s\n", t1 - t0);

    printf("screen 1: solid red\n");
    t0 = now_s();
    disp_color(RED);
    t1 = now_s();
    printf("  %.3f s/frame (%.2f fps, %.0f kbit/s effective)\n",
           t1 - t0, 1.0 / (t1 - t0),
           294912.0 / (t1 - t0) / 1000.0);
    if (one)
    {
        printf("one-screen mode\n");
        spi_close();
        return 0;
    }

    screens();

    if (sweep)
        speed_sweep(speed_khz);

    printf("done - screen keeps the last frame\n");
    spi_close();
    return 0;
}
