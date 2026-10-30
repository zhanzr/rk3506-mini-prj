/*
  lcd.c - ILI9163C 1.44" 128x128 (MD144) driver, Linux port of the f411
  reference (init sequence verbatim). Transport is gpio_bb (bit-bang),
  9-bit frames: D/C bit first, then the 8 data bits.
*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "font_1608.h"
#include "spi_hw.h"
#include "lcd.h"

void write_comm(uint8_t data)
{
    spi_cmd(data);
}

void write_data8(uint8_t data)
{
    spi_data8(data);
}

void write_data16(uint16_t data)
{
    write_data8((uint8_t)(data >> 8));
    write_data8((uint8_t)data);
}

void lcd_begin_data(void)
{
    spi_stream_begin();
}

void lcd_end_data(void)
{
    spi_stream_end();
}

/* stream one 16bpp word as two 9-bit frames with CS held low */
static void put_pixel_word(uint16_t v)
{
    spi_stream_word(v);
}

static void delay_ms(int ms)
{
    usleep((useconds_t)ms * 1000);
}

void lcd_reset(void)
{
    spi_res_set(1);
    delay_ms(50);
    spi_res_set(0);
    delay_ms(200);
    spi_res_set(1);
    delay_ms(200);
}

/* ILI9163C (MD144 module): gamma (0x26), frame rate (0xB1), power
 * (0xC0/0xC1), VCOM (0xC5/0xC7), 16bpp COLMOD, full-GRAM CASET
 * (0..127) / RASET (0..159), MADCTL 0x08, source direction (0xB7),
 * gamma enable (0xF2) and the gamma tables. */
void lcd_ic_init(void)
{
    write_comm(0x11);             /* Exit Sleep */
    delay_ms(50);

    write_comm(0x26);             /* Set Default Gamma */
    write_data8(0x04);

    write_comm(0xB1);             /* Set Frame Rate */
    write_data8(0x0C);
    write_data8(0x14);

    write_comm(0xC0);             /* VRH1[4:0] & VC[2:0] */
    write_data8(0x0C);
    write_data8(0x05);

    write_comm(0xC1);             /* BT[2:0] for AVDD & VCL & VGH & VGL */
    write_data8(0x02);

    write_comm(0xC5);             /* VMH[6:0] & VML[6:0] */
    write_data8(0x29);
    write_data8(0x43);

    write_comm(0xC7);             /* VCOM control */
    write_data8(0x40);

    write_comm(0x3A);             /* Color Format: 16bpp */
    write_data8(0x05);

    write_comm(0x2A);             /* Column Address: 0..127 */
    write_data8(0x00);
    write_data8(0x00);
    write_data8(0x00);
    write_data8(0x7F);

    write_comm(0x2B);             /* Page Address: 0..159 */
    write_data8(0x00);
    write_data8(0x00);
    write_data8(0x00);
    write_data8(0x9F);

    write_comm(0x36);             /* Scanning Direction (MADCTL) */
    write_data8(0x08);            /* 180 deg rotated vs 0xC8 default */

    write_comm(0xB7);             /* Source Output Direction */
    write_data8(0x00);

    write_comm(0xF2);             /* Enable Gamma */
    write_data8(0x01);

    write_comm(0xE0);             /* Positive gamma */
    write_data8(0x36); write_data8(0x29); write_data8(0x12);
    write_data8(0x22); write_data8(0x1C); write_data8(0x15);
    write_data8(0x42); write_data8(0xB7); write_data8(0x2F);
    write_data8(0x13); write_data8(0x12); write_data8(0x0A);
    write_data8(0x11); write_data8(0x0B); write_data8(0x06);

    write_comm(0xE1);             /* Negative gamma */
    write_data8(0x09); write_data8(0x16); write_data8(0x2D);
    write_data8(0x0D); write_data8(0x13); write_data8(0x15);
    write_data8(0x40); write_data8(0x48); write_data8(0x53);
    write_data8(0x0C); write_data8(0x1D); write_data8(0x25);
    write_data8(0x2E); write_data8(0x34); write_data8(0x39);

    write_comm(0x29);             /* Display On */
}

void lcd_init(void)
{
    lcd_reset();
    lcd_ic_init();
    disp_color(BLACK);
}

void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    write_comm(0x2A);
    write_data8((uint8_t)((x1 + COL_Pre) >> 8));
    write_data8((uint8_t)(x1 + COL_Pre));
    write_data8((uint8_t)((x2 + COL_Pre) >> 8));
    write_data8((uint8_t)(x2 + COL_Pre));

    write_comm(0x2B);
    write_data8((uint8_t)((y1 + ROW_Pre) >> 8));
    write_data8((uint8_t)(y1 + ROW_Pre));
    write_data8((uint8_t)((y2 + ROW_Pre) >> 8));
    write_data8((uint8_t)(y2 + ROW_Pre));

    write_comm(0x2C);
}

void disp_color(uint16_t color)
{
    uint32_t n = (uint32_t)COL * ROW;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    lcd_begin_data();
    while (n--)
        put_pixel_word(color);
    lcd_end_data();
}

void disp_band(void)
{
    static const uint16_t cols[8] = {
        BLACK, BLUE, RED, MAGENTA, GREEN, CYAN, YELLOW, WHITE
    };
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    lcd_begin_data();
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL; j++)
        {
            put_pixel_word(cols[j * 8 / COL]);
        }
    }
    lcd_end_data();
}

void disp_gray_hor16(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    lcd_begin_data();
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL; j++)
        {
            uint16_t c = (uint16_t)(j * 16 / COL) * 2048; /* r5<<11 */
            uint16_t g = (uint16_t)(j * 32 / COL) << 5;
            uint16_t v = c | g;

            put_pixel_word(v);
        }
    }
    lcd_end_data();
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color)
{
    lcd_set_address(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    lcd_begin_data();
    for (uint32_t n = (uint32_t)w * h; n--;)
        put_pixel_word(color);
    lcd_end_data();
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *s,
                     uint16_t color, uint16_t bgcolor)
{
    while (*s)
    {
        char c = *s;

        if (c < 0x20 || c > 0x7E)
            c = '?';
        {
            int index = c - 0x20;
            int row, col;

            lcd_set_address(x, y, (uint16_t)(x + FONT16_W - 1),
                            (uint16_t)(y + FONT16_H - 1));
            lcd_begin_data();
            for (row = 0; row < FONT16_H; row++)
            {
                uint8_t bits = asc2_1608[index * 16 + row];

                for (col = 0; col < FONT16_W; col++)
                {
                    put_pixel_word((bits & (1u << (7 - col))) ? color
                                                              : bgcolor);
                }
            }
            lcd_end_data();
        }
        x = (uint16_t)(x + FONT16_W);
        s++;
    }
}
