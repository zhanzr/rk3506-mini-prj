/*
  whiteband_test - isolates the top-strip write path (rows 0..27) on the
  ST7365P 3.5" 320x480 (MD350).

  Cycles three phases over the strip only, 3 s each:
    0: black strip + white text "RGB565 100M uh-1"
    1: solid WHITE strip
    2: solid BLACK strip

  If all three phases show on the panel, the strip write path works and
  the problem is elsewhere (camera app flow). If the strip never
  changes, the row addressing / window setup is at fault.

  The rest of the panel (rows 28..479) is filled dark gray once at
  init and stays untouched.
*/
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "lcd.h"
#include "spi_hw.h"
#include "ref/lcd_fonts.h"
#include "ref/lcd_font_1608.h"

#include "log.h"

static void delay_ms(uint32_t ms) { usleep(ms * 1000); }

/* ---- backlight PWM (JP4.15, pwm1_8ch_0 @ ff170000, rk3506-mini) ----
 * 200 kHz normal polarity, fixed 55% (bright enough to judge detection
 * results). The opz board had BLK hardwired to 3.3 V - no PWM there. */
#define BL_PERIOD_NS 5000

static int g_bl_pct = -1;

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

int main(int argc, char **argv)
{
    log_init("whiteband_test");
    log_config_from_argv(argc, argv);
    setvbuf(stdout, NULL, _IONBF, 0);

    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    backlight_init();
    backlight_set(55);
    if (spi_open(100 * 1000 * 1000) != 0)
        return 1;
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);

    /* rest of the panel: dark gray, untouched afterwards */
    LCD_SetColor(0x2104);
    LCD_FillRect(0, 28, LCD_Width, LCD_Height - 28);

    for (int loop = 0;; loop++)
    {
        switch (loop % 3)
        {
        case 0:
            LCD_SetColor(LCD_BLACK);
            LCD_SetBackColor(LCD_BLACK);
            LCD_FillRect(0, 0, LCD_Width, 28);
            LCD_SetColor(LCD_WHITE);
            LCD_DisplayString(4, 4, "RGB565 100M uh-1");
            log_printf(LOG_LVL_INFO, "phase 0: black strip + text");
            break;
        case 1:
            LCD_SetColor(LCD_WHITE);
            LCD_FillRect(0, 0, LCD_Width, 28);
            log_printf(LOG_LVL_INFO, "phase 1: WHITE strip");
            break;
        case 2:
            LCD_SetColor(LCD_BLACK);
            LCD_FillRect(0, 0, LCD_Width, 28);
            log_printf(LOG_LVL_INFO, "phase 2: BLACK strip");
            break;
        }
        delay_ms(3000);
    }
}
