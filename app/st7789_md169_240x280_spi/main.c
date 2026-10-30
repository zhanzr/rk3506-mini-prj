/*
  main.c - ST7789 240x280 (MD169) demo for the rk3506-mini over
  hardware SPI0 (/dev/spidev0.0), ported from the f411/f746 reference
  flow, with the lcm_logo screens from the ssd1317 project.

  4-wire SPI: DC (gpio 47) selects command/data, CS hardware (spidev
  CS0), RES gpio 49, BLK on PWM1 ch0 (JP4.15, 25 kHz, 11%/55% by loop).

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
#define INFO_DY    20      /* 16 px Font16 + 4 px leading */
#define DWELL_MS   4000    /* per-pattern hold so colors can be judged */

/* ---- backlight PWM (JP4.15, pwm1_8ch_0 @ ff170000) ----
 * 25 kHz: at 500 Hz the LED shows visible ripple on bright frames */
#define BL_PERIOD_NS 40000

static int g_bl_pct;   /* current duty, shown on the info page */

static void backlight_init(void)
{
    char path[128];
    FILE *fp;

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/export");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/period");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "%d", BL_PERIOD_NS); fclose(fp); }

    g_bl_pct = -1;
}

static void backlight_set(int percent)
{
    char path[128];
    FILE *fp;

    if (percent == g_bl_pct)
        return;
    g_bl_pct = percent;

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/duty_cycle");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "%d", BL_PERIOD_NS * percent / 100); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/enable");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "1"); fclose(fp); }
}

static void delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
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
        frames++;
        g_frames++;
        fps_update();
        t = LCD_GetTick() - start;
    } while (t < ms);

    printf("  gradient: %u frames in %u ms = %u fps\n",
           frames, t, frames * 1000u / (t ? t : 1));
}

/* ---- framebuffer helpers (logo + scaled text), 240x280 RGB565 ---- */
static uint16_t s_fb[SCREEN_W * SCREEN_H];

static uint16_t rgb565(uint32_t rgb888)
{
    return (uint16_t)(((rgb888 >> 8) & 0xF800) |
                      ((rgb888 >> 5) & 0x07E0) |
                      ((rgb888 >> 3) & 0x001F));
}

static void fb_blit(void)
{
    LCD_SetAddress(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    spi_write_buf((const uint8_t *)s_fb, sizeof(s_fb));
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

                        if (px >= 0 && px < SCREEN_W &&
                            py >= 0 && py < SCREEN_H)
                            s_fb[py * SCREEN_W + px] = v;
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

static void fb_clear(uint16_t color)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        s_fb[i] = color;
}

/* stock boot logo (resource.img logo.bmp -> logo64.h), 2x scaled,
 * caption underneath - the lcm_logo screen of the ssd1317 project.
 * logo64 layout: page-major, 64 cols x 48 rows, bit = row in page. */
static void logo_screen(void)
{
    fb_clear(BLACK);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 64; x++)
        {
            uint8_t byte = logo64_fb[(y / 8) * 64 + x];
            uint16_t c = (byte >> (y % 8)) & 1 ? WHITE : BLACK;

            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                    s_fb[(76 + y * 2 + dy) * SCREEN_W + 56 + x * 2 + dx] = c;
        }
    fb_string_scaled("RK3506-mini", 76, 180, 1, YELLOW, BLACK);
    fb_blit();
}

/* scaled font screen: 1x / 2x / 3x of the 8x16 font */
static void scaled_font_screen(void)
{
    fb_clear(BLACK);
    fb_string_scaled("RK3506-mini", 76, 8, 1, WHITE, BLACK);
    fb_string_scaled("RK35", 88, 48, 2, CYAN, BLACK);
    fb_string_scaled("RK", 96, 128, 3, GREEN, BLACK);
    fb_blit();
}

/* ---- timed TEST_STAND (with dwell) ---- */
static void disp_red(void)   { DispColor(LCD_RED); }
static void disp_green(void) { DispColor(LCD_GREEN); }
static void disp_blue(void)  { DispColor(LCD_BLUE); }
static void disp_white(void) { DispColor(LCD_WHITE); }
static void disp_black(void) { DispColor(LCD_BLACK); }

static void timed_screen(const char *name, void (*fn)(void))
{
    uint32_t t0 = LCD_GetTick();

    fn();
    printf("  %-6s: %u ms (hold %d s)\n", name, LCD_GetTick() - t0,
           DWELL_MS / 1000);
    delay_ms(DWELL_MS);
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

    LCD_SetAsciiFont(&ASCII_Font16);
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
    int y = 4;

    snprintf(buf, sizeof buf, "%s", comp);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "Build %s", __DATE__);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "Freq %lu MHz", mhz);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "SPI0 40 MHz (HW)");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "SCL=23 MOSI=19");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "DC=33 RST=29 CS=24");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "BLK=15 PWM %d%%", g_bl_pct);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);

    g_fps_color = LCD_WHITE;

    uint32_t start = LCD_GetTick();

    do
    {
        fps_update();
        usleep(50000);
    } while (LCD_GetTick() - start < ms);
}

/* ---- big-font banner page (2x scaled 8x16, fb path) ---- */
static void banner_page(const char *l1, const char *l2,
                        uint32_t fg, uint32_t bg, uint32_t ms)
{
    int x1 = (SCREEN_W - (int)strlen(l1) * 8 * 2) / 2;
    int x2 = (SCREEN_W - (int)strlen(l2) * 8 * 2) / 2;

    if (x1 < 0) x1 = 0;
    if (x2 < 0) x2 = 0;

    fb_clear(rgb565(bg));
    fb_string_scaled(l1, x1, 100, 2, rgb565(fg), rgb565(bg));
    fb_string_scaled(l2, x2, 148, 2, rgb565(fg), rgb565(bg));
    fb_blit();
    paint_fps_band();

    delay_ms(ms);
}

/* ---- full pattern set ---- */
static void run_patterns(void)
{
    printf("[LCD] phase: info\n");
    info_demo(5000, 0);

    printf("[LCD] phase: info (inverted colors)\n");
    info_demo(5000, 1);

    printf("[LCD] phase: vendor TEST_STAND (write time + %d s dwell)\n",
           DWELL_MS / 1000);
    TEST_STAND();

    printf("[LCD] phase: gradient\n");
    gradient_demo(1000);
    delay_ms(DWELL_MS);

    printf("[LCD] phase: stock boot logo (2x) + caption\n");
    logo_screen();
    delay_ms(DWELL_MS);

    printf("[LCD] phase: scaled font (1x/2x/3x)\n");
    scaled_font_screen();
    delay_ms(DWELL_MS);
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
           "BLK=JP4.15 (PWM 25 kHz)\n");

    backlight_init();

    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    if (spi_open(speed_khz * 1000) != 0)
        return 1;

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
    paint_fps_band();

    for (int loop = 1;; loop++)
    {
        int pct = (loop % 2) ? 11 : 55;

        backlight_set(pct);
        printf("[LCD] loop %d: backlight %d%%\n", loop, pct);

        printf("[LCD] phase: HARDWARE banner\n");
        banner_page("now will do", "HW SPI0 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI0 @ %u MHz\n",
               speed_khz / 1000);
        run_patterns();
    }

    return 0;
}
