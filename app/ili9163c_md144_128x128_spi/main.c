/*
  main.c - ILI9163C 128x128 (MD144) demo for the rk3506-mini, faithful
  port of the f411 reference main flow (hardware SPI0 path only), plus
  the lcm_logo screens (stock boot logo 2x + scaled font) from the
  ssd1317 project.

  The module is 3-wire serial: every byte is a 9-bit frame (D/C bit
  clocked first); no D/C pin; backlight hardwired on-module.

  Wiring (JP4): SCL=23 SDA=19 RES=29 CS=24; power on 1/6.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logo64.h"
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
    fps_reset();
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
        fps_frame();
        frames++;
        fps_update();
        t = LCD_GetTick() - start;
    } while (t < ms);

    printf("  gradient: %u frames in %u ms = %u fps\n",
           frames, t, frames * 1000u / (t ? t : 1));
}

/* ---- timed screen helper ---- */
static void timed_screen(const char *name, void (*fn)(void))
{
    uint32_t t0 = LCD_GetTick();

    fn();
    printf("  %-6s: %u ms\n", name, LCD_GetTick() - t0);
}

static void disp_red(void)   { DispColor(LCD_RED); }
static void disp_green(void) { DispColor(LCD_GREEN); }
static void disp_blue(void)  { DispColor(LCD_BLUE); }
static void disp_white(void) { DispColor(LCD_WHITE); }
static void disp_black(void) { DispColor(LCD_BLACK); }

static void TEST_STAND(void)
{
    fps_reset();
    timed_screen("frame", DispFrame);
    timed_screen("gray",  DispGrayHor16);
    timed_screen("band",  DispBand);
    timed_screen("red",   disp_red);
    timed_screen("green", disp_green);
    timed_screen("blue",  disp_blue);
    timed_screen("white", disp_white);
    timed_screen("black", disp_black);
}

/* ---- fb helpers (128x128 RGB565) ---- */
static uint16_t s_fb[128 * 128];

static void fb_blit(void)
{
    LCD_SetAddress(0, 0, 127, 127);
    spi_stream_begin();
    for (int i = 0; i < 128 * 128; i++)
        spi_stream_word(s_fb[i]);
    spi_stream_end();
}

static void fb_char_scaled(uint8_t c, int x0, int y0, int scale,
                           uint16_t fg, uint16_t bg)
{
    if (c < 0x20 || c > 0x7E)
        return;
    {
        int index = c - 0x20;

        for (int row = 0; row < 16; row++)
        {
            uint8_t bits = asc2_1608[index * 16 + row];

            for (int col = 0; col < 8; col++)
            {
                /* font glyphs are LSB-first: bit 0 = leftmost pixel */
                uint16_t v = (bits >> col) & 1u ? fg : bg;

                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                    {
                        int px = x0 + col * scale + sx;
                        int py = y0 + row * scale + sy;

                        if (px >= 0 && px < 128 && py >= 0 && py < 128)
                            s_fb[py * 128 + px] = v;
                    }
            }
        }
    }
}

static void fb_string_scaled(const char *s, int x0, int y0, int scale,
                             uint16_t fg, uint16_t bg)
{
    int x = x0;

    while (*s)
    {
        fb_char_scaled((uint8_t)*s, x, y0, scale, fg, bg);
        x += 8 * scale;
        s++;
    }
}

/* stock boot logo (resource.img logo.bmp -> logo64.h), 2x scaled,
 * caption underneath - the lcm_logo screen of the ssd1317 project.
 * logo64 layout: page-major, 64 cols x 48 rows, bit = row in page. */
static void logo_screen(void)
{
    fps_reset();
    memset(s_fb, 0, sizeof(s_fb));
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++)
        {
            uint8_t byte = logo64_fb[(y / 8) * 64 + x];
            uint16_t c = (byte >> (y % 8)) & 1 ? WHITE : BLACK;

            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                    s_fb[(16 + y * 2 + dy) * 128 + x * 2 + dx] = c;
        }
    fb_string_scaled("RK3506-mini", 22, 112, 1, YELLOW, BLACK);
    fb_blit();
}

/* scaled font screen: 1x / 2x / 3x of the 8x16 font */
static void scaled_font_screen(void)
{
    fps_reset();
    memset(s_fb, 0, sizeof(s_fb));
    fb_string_scaled("RK3506-mini", 22, 0, 1, WHITE, BLACK);
    fb_string_scaled("RK35", 32, 16, 2, CYAN, BLACK);
    fb_string_scaled("RK", 40, 64, 3, GREEN, BLACK);
    fb_blit();
}

/* ---- info page (f411 reference) ---- */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[24];
    char comp[24];
    unsigned long mhz = 1200UL; /* fixed armclk, see docs/cpu_clock_dvfs.md */
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

    fps_reset();
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

    snprintf(buf, sizeof buf, "SCL=23 SDA=19");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

    snprintf(buf, sizeof buf, "RST=29 CS=24");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y = (uint16_t)(y + INFO_DY);

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

    printf("[LCD] phase: vendor TEST_STAND (per-screen write time)\n");
    TEST_STAND();

    printf("[LCD] phase: gradient\n");
    gradient_demo(1000);

    printf("[LCD] phase: stock boot logo (2x) + caption\n");
    logo_screen();
    sleep(3);

    printf("[LCD] phase: scaled font (1x/2x/3x)\n");
    scaled_font_screen();
    sleep(3);
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
                printf("usage: ili9163c_demo [-s speed_kHz]\n");
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
