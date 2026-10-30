/*
  main.c - ILI9163C 128x128 (MD144) demo for the rk3506-mini, faithful
  port of the f411 reference main flow (hardware SPI0 path only).

  The module is 3-wire serial: every byte is a 9-bit frame (D/C bit
  clocked first); no D/C pin; backlight hardwired on-module.

  Wiring (JP4): SCL=23 SDA=19 RES=29 CS=24; power on 1/6.
  Reference MADCTL: 0x08. If text appears mirrored on this module batch,
  note it - MADCTL lives in lcd.c LCD_IC_Init.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ref/lcd_fonts.h"
#include "ref/lcd_font_1608.h"
#include "lcd.h"
#include "spi_hw.h"

/* ---- screen layout (f411 reference) ---- */
#define SCREEN_W   128
#define SCREEN_H   128
#define FPS_BAND   20
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK
#define INFO_DY    13

/* ---- FPS counter ---- */
static volatile uint32_t g_frames;
static uint32_t g_last_frames;
static uint32_t g_fps_last_tick;
static uint32_t g_fps_color = LCD_WHITE;

static void fps_frame(void)
{
    g_frames++;
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
        }
    }
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, (uint16_t)ANIM_H, SCREEN_W, FPS_BAND);
}

static void delay_with_fps(uint32_t ms)
{
    uint32_t start = LCD_GetTick();

    do
    {
        fps_update();
        usleep(50000);
    } while (LCD_GetTick() - start < ms);
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

static void draw_gradient(int hue_a, int hue_b, uint16_t *row)
{
    for (int y = 0; y < ANIM_H; y++)
    {
        int frac = y * 1000 / ANIM_H;
        int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
        uint32_t c = hsv_to_rgb(hue, 255, 255);
        uint16_t rgb565 = (uint16_t)(((c >> 8) & 0xF800) |
                                     ((c >> 5) & 0x07E0) |
                                     ((c >> 3) & 0x001F));

        for (int x = 0; x < SCREEN_W; x++)
            row[x] = rgb565;
        LCD_CopyBuffer(0, (uint16_t)y, SCREEN_W, 1, row);
    }
}

static void gradient_demo(uint32_t ms)
{
    static uint16_t row[SCREEN_W];

    LCD_SetBackColor(BACK_COLOR);
    paint_fps_band();

    uint32_t start = LCD_GetTick();
    uint32_t t = 0;

    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;

        if (hue_b >= 3600)
            hue_b -= 3600;
        draw_gradient(hue_a, hue_b, row);
        fps_frame();
        fps_update();
        t = LCD_GetTick() - start;
    } while (t < ms);
}

/* ---- vendor TEST_STAND screens ---- */
static void TEST_STAND(void)
{
    DispFrame();
    StopDelay(Delay_Time);

    DispGrayHor16();
    StopDelay(Delay_Time);

    DispBand();
    StopDelay(Delay_Time);

    DispColor(LCD_RED);   StopDelay(Delay_Time);
    DispColor(LCD_GREEN); StopDelay(Delay_Time);
    DispColor(LCD_BLUE);  StopDelay(Delay_Time);
    DispColor(LCD_WHITE); StopDelay(Delay_Time);
    DispColor(LCD_BLACK); StopDelay(Delay_Time);
}

/* ---- info page ---- */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[24];
    char comp[24];
    unsigned long mhz = 1200UL; /* fixed armclk, see docs/cpu_clock_dvfs.md */
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
    printf("[LCD] info: freq=%lu MHz drive=HW SPI0 (packed 9-bit)\n", mhz);

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
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "Build %s", __DATE__);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "Freq %lu MHz", mhz);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "SPI0 40 MHz (HW)");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "SCL=23 SDA=19");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "RST=29 CS=24");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf); y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "UID 79a8bf52");
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

    usleep(ms * 1000);

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

    printf("[LCD] phase: vendor TEST_STAND\n");
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
            default:  printf("usage: ili9163c_demo [-s speed_kHz]\n");
                      return 1;
            }
        }
        else
        {
            printf("usage: ili9163c_demo [-s speed_kHz]\n");
            return 1;
        }
    }

    printf("ILI9163C 1.44\" 128x128, 3-wire serial (9-bit frames: D/C bit "
           "+ 8 data):\n");
    printf("SCL=JP4.23 SDA=JP4.19 RES=JP4.29 CS=JP4.24; no DC pin "
           "(in-protocol), backlight hardwired on-module\n");

    if (spi_open(speed_khz * 1000) != 0)
        return 1;
    spi_res_open(29);

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    /* the f411 reference loops forever between soft and HW buses; here
     * there is only the HW SPI0 path */
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
