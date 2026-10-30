/*
  main.c - ST7789 240x280 (MD169) demo for the rk3506-mini over
  hardware SPI0 (/dev/spidev0.0), ported from the f411/f746 reference
  flow, with the lcm_logo screens from the ssd1317 project.

  4-wire SPI: DC (gpio 47) selects command/data, CS hardware (spidev
  CS0), RES gpio 49, BLK on PWM1 ch0 (JP4.15, 11% default).

  Usage: ./st7789_demo [-s speed_kHz] (default 40000)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ref/lcd_fonts.h"
#include "ref/lcd_font_1608.h"
#include "lcd.h"
#include "logo64.h"
#include "spi_hw.h"

/* ---- screen layout ---- */
#define SCREEN_W   240
#define SCREEN_H   280
#define FPS_BAND   24
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK
#define INFO_DY    13

static void delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

/* ---- backlight PWM (JP4.15, pwm1_8ch_0) ---- */
static void backlight_init(int percent)
{
    /* pwmchip0 = ff170000 (pwm1_8ch_0), probed from the rebuilt DTB */
    const char *base = "/sys/class/pwm/pwmchip0";
    char path[128];
    FILE *fp;
    int duty = 2000000 * percent / 100;

    snprintf(path, sizeof(path), "%s/export", base);
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "%s/pwm0/period", base);
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "2000000"); fclose(fp); }

    snprintf(path, sizeof(path), "%s/pwm0/duty_cycle", base);
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "%d", duty); fclose(fp); }

    snprintf(path, sizeof(path), "%s/pwm0/enable", base);
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "1"); fclose(fp); }

    printf("backlight PWM: %d%%\n", percent);
}

/* ---- FPS counter ---- */
static volatile uint32_t g_frames;
static uint32_t g_last_frames;
static uint32_t g_fps_last_tick;
static uint32_t g_fps_color = LCD_WHITE;

static void fps_reset(void)
{
    g_frames = 0;
    g_last_frames = 0;
    g_fps_last_tick = LCD_GetTick();
}

static void fps_update(void)
{
    uint32_t now = LCD_GetTick();

    if (now - g_fps_last_tick >= 1000)
    {
        uint32_t fps = g_frames - g_last_frames;

        g_last_frames = g_frames;
        g_fps_last_tick = now;
        {
            char buf[8];

            buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S'; buf[3] = ':';
            buf[4] = (char)('0' + (fps / 100) % 10);
            buf[5] = (char)('0' + (fps / 10) % 10);
            buf[6] = (char)('0' + fps % 10);
            buf[7] = '\0';
            LCD_SetColor(g_fps_color);
            LCD_ShowTransparent(1);
            LCD_DisplayString(1, (uint16_t)(ANIM_H + 4), buf);
            LCD_ShowTransparent(0);
            if (fps)
                printf("[fps] %u\n", fps);
        }
    }
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, (uint16_t)ANIM_H, SCREEN_W, FPS_BAND);
}

/* ---- HSV gradient sweep ---- */
static uint32_t hsv_to_rgb(int h, int s, int v)
{
    int region = (h / 600) % 6;
    int fpart  = h % 600;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * fpart) / 600) / 255;
    int t = v * (255 - (s * (600 - fpart)) / 600) / 255;
    int r, g, b;

    switch (region)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#define G_ROWS 8   /* 8 rows x 480 B = 3840 B < spidev 4096 chunk limit */

static void draw_gradient(int hue_a, int hue_b, uint16_t *row)
{
    LCD_SetAddress(0, 0, (uint16_t)(SCREEN_W - 1), (uint16_t)(ANIM_H - 1));
    for (int y0 = 0; y0 < ANIM_H; y0 += G_ROWS)
    {
        int rows = (ANIM_H - y0) < G_ROWS ? (ANIM_H - y0) : G_ROWS;

        for (int k = 0; k < rows; k++)
        {
            int frac = (y0 + k) * 1000 / ANIM_H;
            int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
            uint32_t c = hsv_to_rgb(hue, 255, 255);
            uint16_t px = (uint16_t)(((c >> 8) & 0xF800) |
                                     ((c >> 5) & 0x07E0) |
                                     ((c >> 3) & 0x001F));

            for (int x = 0; x < SCREEN_W; x++)
                row[(uint32_t)k * SCREEN_W + x] = px;
        }
        spi_write_buf((const uint8_t *)row,
                      (size_t)rows * SCREEN_W * 2);
    }
}

static void gradient_demo(uint32_t ms)
{
    static uint16_t row[SCREEN_W * G_ROWS];
    uint32_t frames = 0;

    LCD_SetBackColor(BACK_COLOR);
    paint_fps_band();
    fps_reset();

    uint32_t start = LCD_GetTick();
    uint32_t t = 0;

    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;

        if (hue_b >= 3600)
            hue_b -= 3600;
        draw_gradient(hue_a, hue_b, row);
        frames++; g_frames++;
        fps_update();
        t = LCD_GetTick() - start;
    } while (t < ms);

    printf("  gradient: %u frames in %u ms = %u fps\n",
           frames, t, frames * 1000u / (t ? t : 1));
}

/* ---- timed TEST_STAND ---- */
static void disp_red(void)   { DispColor(LCD_RED); }
static void disp_green(void) { DispColor(LCD_GREEN); }
static void disp_blue(void)  { DispColor(LCD_BLUE); }
static void disp_white(void) { DispColor(LCD_WHITE); }
static void disp_black(void) { DispColor(LCD_BLACK); }

static void timed_screen(const char *name, void (*fn)(void))
{
    uint32_t t0 = LCD_GetTick();

    fn();
    printf("  %-6s: %u ms\n", name, LCD_GetTick() - t0);
}

static void TEST_STAND(void)
{
    timed_screen("frame", DispFrame);
    timed_screen("gray",  DispGrayHor16);
    timed_screen("band",  DispBand);
    timed_screen("red",   disp_red);
    timed_screen("green", disp_green);
    timed_screen("blue",  disp_blue);
    timed_screen("white", disp_white);
    timed_screen("black", disp_black);
}

/* ---- info page ---- */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[24];
    char comp[24];
    unsigned long mhz = 1200UL;
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

#if defined(__clang__)
    snprintf(comp, sizeof comp, "Clang %d.%d", __clang_major__,
             __clang_minor__);
#elif defined(__GNUC__)
    snprintf(comp, sizeof comp, "GCC %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    snprintf(comp, sizeof comp, "unknown");
#endif

    printf("[LCD] info%s: compiler=%s build=%s %s\n",
           invert ? " (inverted)" : "", comp, __DATE__, __TIME__);
    printf("[LCD] info: freq=%lu MHz drive=HW SPI0 (ST7789 4-wire)\n", mhz);

    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();

    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, (uint16_t)ANIM_H, SCREEN_W, FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    g_fps_color = fg;

    const int ix = 2;
    int y = 2;

    snprintf(buf, sizeof buf, "%s", comp);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "Build %s", __DATE__);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "Freq %lu MHz", mhz);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "SPI0 40 MHz (HW)");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "SCL=23 MOSI=19");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "DC=33 RST=29 CS=24");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "BLK=15 PWM 11%%");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);

    g_fps_color = LCD_WHITE;

    uint32_t start = LCD_GetTick();

    do
    {
        fps_update();
        usleep(50000);
    } while (LCD_GetTick() - start < ms);
}

/* ---- big-font banner page ---- */
static void banner_page(const char *l1, const char *l2,
                        uint32_t fg, uint32_t bg, uint32_t ms)
{
    g_fps_color = fg;

    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, (uint16_t)ANIM_H, SCREEN_W, FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);

    LCD_DisplayString(12, 40, (char *)l1);
    LCD_DisplayString(12, 64, (char *)l2);

    delay_ms(ms);

    LCD_SetAsciiFont(&ASCII_Font12);
    g_fps_color = LCD_WHITE;
}

/* ---- full pattern set ---- */
static void run_patterns(void)
{
    printf("[LCD] phase: info\n");
    info_demo(5000, 0);

    printf("[LCD] phase: info (inverted colors)\n");
    info_demo(5000, 1);

    printf("[LCD] phase: vendor TEST_STAND (per-screen write time)\n");
    TEST_STAND();

    printf("[LCD] phase: gradient\n");
    gradient_demo(1000);
}

int main(int argc, char **argv)
{
    unsigned speed_khz = 40000;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 's': speed_khz = (unsigned)atoi(argv[++i]); break;
            default:
                printf("usage: st7789_demo [-s speed_kHz]\n");
                return 1;
            }
        }
        else
        {
            printf("usage: st7789_demo [-s speed_kHz]\n");
            return 1;
        }
    }

    printf("ST7789V 1.69\" 240x280, 4-wire SPI (hw SPI0 + DC gpio):\n");
    printf("SCLK=JP4.23 MOSI=JP4.19 CS=JP4.24 DC=JP4.33 RST=JP4.29 "
           "BLK=JP4.15 (PWM 11%%)\n");

    backlight_init(11);

    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    if (spi_open(speed_khz * 1000) != 0)
        return 1;

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    for (;;)
    {
        printf("[LCD] phase: HARDWARE banner\n");
        banner_page("now will do", "HW SPI0 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI0 @ %u MHz\n",
               speed_khz / 1000);
        run_patterns();
    }

    return 0;
}
