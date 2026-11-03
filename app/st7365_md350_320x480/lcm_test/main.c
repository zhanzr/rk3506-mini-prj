/*
  main.c - ST7365P 320x480 (MD350) demo for the Orange Pi Zero over
  hardware SPI0 (/dev/spidev0.0), ported from the f411/f746 reference
  flow, with the lcm_logo screens from the ssd1317 project.

  4-wire SPI (mode 0): DC = PA1 (CON4.11) selects command/data, CS
  hardware (spidev CS0 = PA13/CON4.24), RES = PA6 (CON4.7), BLK on
  3.3 V (full-on, no PWM pin free on this header).

  Usage: ./lcm_test [-s speed_kHz] (default 100000)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <unistd.h>

#include "ref/lcd_fonts.h"
#include "ref/lcd_font_1608.h"
#include "lcd.h"
#include "logo64.h"
#include "spi_hw.h"

#include "log.h"

/* ---- screen layout ---- */
#define SCREEN_W   320
#define SCREEN_H   480
#define FPS_BAND   24
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK

/* requested SPI speed (kHz) - shared with the info page */
static unsigned g_speed_khz = 100000;
#define INFO_DY    20      /* 16 px Font16 + 4 px leading */
#define DWELL_MS   4000    /* per-pattern hold so colors can be judged */

/* ---- UVC camera probe: host index of the first capture camera ---- */
static int uvc_host_probe(void)
{
    DIR *d = opendir("/sys/class/video4linux");
    struct dirent *e;
    int found = -1;

    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL)
    {
        char dev_path[300], link_target[400], joined[600], resolved[4096];
        char raw[300], sys_path[400];
        int fd, hnum = -1;
        struct v4l2_capability cap;
        ssize_t n;

        if (strncmp(e->d_name, "video", 5) != 0)
            continue;
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", e->d_name);
        fd = open(dev_path, O_RDWR);
        if (fd < 0)
            continue;
        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
            (cap.capabilities & V4L2_CAP_DEVICE_CAPS
                 ? (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)
                 : (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) &&
            strcmp((char *)cap.driver, "uvcvideo") == 0)
        {
            snprintf(link_target, sizeof(link_target),
                     "/sys/class/video4linux/%s/device", e->d_name);
            n = readlink(link_target, raw, sizeof(raw) - 1);
            sys_path[0] = 0;
            if (n > 0)
            {
                raw[n] = 0;
                snprintf(joined, sizeof(joined),
                         "/sys/class/video4linux/%s/%s", e->d_name, raw);
                if (realpath(joined, resolved))
                    strcpy(sys_path, resolved);
                else
                    strcpy(sys_path, joined);
            }
            /* rk3506-mini USB hosts (opz used 1c19000/1c1b400/...):
               ff780000 = uh-0 (the only USB host on the board) */
            found = strstr(sys_path, "ff780000") ? 0 : -1;
            close(fd);
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

/* ---- backlight ----
 * BLK is hardwired to 3.3 V on this port (always full brightness): the
 * only PWM-capable CON4 pin is PA6/PWM1 and it is used as RES. The
 * rk3506 port's PWM dimming (11%/55% loops) collapses to a no-op here;
 * the init log stays so the demo flow is recognizable. Note: the MD169
 * backlight on a plain 3.3 V pin was verified working already on the
 * rk3506-mini ("JP4.17 (V33) also works - verified lit"). */

/* ---- backlight PWM (JP4.15, pwm1_8ch_0 @ ff170000) ----
 * clk_pwm1 = 93.75 MHz; 5000 ns = 200 kHz - smooths the LED driver,
 * lower rates show visible ripple on bright frames. The opz board had
 * BLK hardwired to 3.3 V; the rk3506-mini drives it with real PWM. */
#define BL_PERIOD_NS 5000

static int g_bl_pct;   /* current duty, shown on the info page */

static void backlight_init(void)
{
    char path[128];
    FILE *fp;

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/export");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/enable");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/polarity");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "normal"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/duty_cycle");
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
                log_printf(LOG_LVL_DEBUG, "[fps] %u", fps);
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
        spi_write_pixels16(row, (size_t)rows * SCREEN_W);
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

    log_printf(LOG_LVL_INFO, "  gradient: %u frames in %u ms = %u fps",
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
    spi_write_pixels16(s_fb, SCREEN_W * SCREEN_H);
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
                    s_fb[(180 + y * 2 + dy) * SCREEN_W + 96 + x * 2 + dx] = c;
        }
    fb_string_scaled("OPi-Zero", 96, 300, 1, YELLOW, BLACK);
    fb_blit();
}

/* scaled font screen: 1x / 2x / 3x of the 8x16 font */
static void scaled_font_screen(void)
{
    fb_clear(BLACK);
    fb_string_scaled("OPi-Zero", 96, 8, 1, WHITE, BLACK);
    fb_string_scaled("OPi", 112, 48, 2, CYAN, BLACK);
    fb_string_scaled("Zero", 100, 128, 3, GREEN, BLACK);
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
    log_printf(LOG_LVL_INFO, "  %-6s: %u ms (hold %d s)", name, LCD_GetTick() - t0,
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
    unsigned long mhz = 1200UL;
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

    log_printf(LOG_LVL_INFO, "[LCD] info%s: pixfmt=RGB565 spi=%u kHz uvc=%s",
           invert ? " (inverted)" : "", g_speed_khz,
           uvc_host_probe() >= 0 ? "present" : "none");
    log_printf(LOG_LVL_INFO, "[LCD] info: freq=%lu MHz drive=HW SPI0 (4-wire)", mhz);

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

    int uh = uvc_host_probe();

    snprintf(buf, sizeof buf, "RGB565");
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    snprintf(buf, sizeof buf, "%uM SPI", g_speed_khz / 1000);
    LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    y += INFO_DY;

    if (uh >= 0)
    {
        snprintf(buf, sizeof buf, "uh-%d", uh);
        LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    }
    else
    {
        snprintf(buf, sizeof buf, "uh-x");
        LCD_DisplayString((uint16_t)ix, (uint16_t)y, buf);
    }

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
    log_printf(LOG_LVL_INFO, "[LCD] phase: info");
    info_demo(5000, 0);

    log_printf(LOG_LVL_INFO, "[LCD] phase: info (inverted colors)");
    info_demo(5000, 1);

    log_printf(LOG_LVL_INFO, "[LCD] phase: vendor TEST_STAND (write time + %d s dwell)",
           DWELL_MS / 1000);
    TEST_STAND();

    log_printf(LOG_LVL_INFO, "[LCD] phase: gradient");
    gradient_demo(1000);
    delay_ms(DWELL_MS);

    log_printf(LOG_LVL_INFO, "[LCD] phase: board boot logo (2x) + caption");
    logo_screen();
    delay_ms(DWELL_MS);

    log_printf(LOG_LVL_INFO, "[LCD] phase: scaled font (1x/2x/3x)");
    scaled_font_screen();
    delay_ms(DWELL_MS);
}

int main(int argc, char **argv)
{
    log_init("lcm_test");
    log_config_from_argv(argc, argv);
    unsigned speed_khz = g_speed_khz;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 's': g_speed_khz = (unsigned)atoi(argv[++i]); break;
            default:
                log_printf(LOG_LVL_INFO, "usage: lcm_test [-s speed_kHz]");
                return 1;
            }
        }
        else
        {
            log_printf(LOG_LVL_INFO, "usage: lcm_test [-s speed_kHz]");
            return 1;
        }
    }

    log_printf(LOG_LVL_INFO, "ST7365P 320x480 (MD350), 4-wire SPI mode 0 (hw SPI0 + DC):");
    log_printf(LOG_LVL_INFO, "SCLK=JP4.23 MOSI=JP4.19 CS=JP4.24 DC=JP4.33 RST=JP4.29 "
           "BLK=JP4.15 (PWM 200 kHz)\n");

    backlight_init();

    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    if (spi_open(g_speed_khz * 1000) != 0)
        return 1;

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
    paint_fps_band();

    for (int loop = 1;; loop++)
    {
        int pct = (loop % 2) ? 11 : 55;

        backlight_set(pct);
        log_printf(LOG_LVL_INFO, "[LCD] loop %d: backlight %d%%", loop, pct);

        log_printf(LOG_LVL_INFO, "[LCD] phase: HARDWARE banner");
        banner_page("now will do", "HW SPI0 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        log_printf(LOG_LVL_INFO, "[LCD] running patterns on HARDWARE SPI0 @ %u MHz",
               g_speed_khz / 1000);
        run_patterns();
    }

    return 0;
}
