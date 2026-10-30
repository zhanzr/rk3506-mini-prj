/*
  lcd.c - ST7789V 1.69" 240x280 (MD169) driver, Linux port of the f411
  reference (init sequence from the c542 board_t1 st7789 bsp, verbatim).
  Transport is gpio_bb (bit-bang SPI mode 3).
*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "font_1608.h"
#include "gpio_bb.h"
#include "lcd.h"

void write_comm(uint16_t data)
{
    bb_dc_set(0);
    bb_cs_set(0);
    bb_write_byte((uint8_t)data);
    bb_cs_set(1);
}

void write_data(uint16_t data)
{
    bb_dc_set(1);
    bb_cs_set(0);
    bb_write_byte((uint8_t)data);
    bb_cs_set(1);
}

void write_data_fast(uint8_t b)
{
    bb_write_byte(b);
}

void lcd_begin_data(void)
{
    bb_dc_set(1);
    bb_cs_set(0);
}

void lcd_end_data(void)
{
    bb_cs_set(1);
}

static void delay_ms(int ms)
{
    usleep((useconds_t)ms * 1000);
}

void lcd_reset(void)
{
    bb_res_set(1);
    delay_ms(50);
    bb_res_set(0);
    delay_ms(200);
    bb_res_set(1);
    delay_ms(200);
}

/* ST7789V/T3 (MD169), from the c542 board_t1 st7789 bsp */
void lcd_ic_init(void)
{
    write_comm(0x11);             /* Sleep out */
    delay_ms(120);

    write_comm(0x36);             /* MADCTL */
    write_data(0x00);
    write_comm(0x3A);             /* COLMOD 16bpp */
    write_data(0x55);
    write_comm(0x21);             /* INVON - IPS panel, inverted colors */

    write_comm(0xB2);             /* PORCTRL */
    write_data(0x0C); write_data(0x0C); write_data(0x00);
    write_data(0x33); write_data(0x33);

    write_comm(0xBB);             /* VCOMS */
    write_data(0x1F);

    write_comm(0xC2);             /* VDVVRHEN */
    write_data(0x01);

    write_comm(0xC3);             /* VRHS */
    write_data(0x13);

    write_comm(0xC4);             /* VDVS */
    write_data(0x20);

    write_comm(0xC5);             /* VCOM offset */
    write_data(0x00);

    write_comm(0xC6);             /* FRCTRL2 */
    write_data(0x0F);

    write_comm(0xD0);             /* PWCTRL1 */
    write_data(0xA4); write_data(0xA1);

    write_comm(0xE0);             /* PVGAMCTRL */
    write_data(0xD0); write_data(0x0A); write_data(0x10); write_data(0x0A);
    write_data(0x0A); write_data(0x26); write_data(0x36); write_data(0x34);
    write_data(0x4D); write_data(0x18); write_data(0x13); write_data(0x14);
    write_data(0x2F); write_data(0x34);

    write_comm(0xE1);             /* NVGAMCTRL */
    write_data(0xD0); write_data(0x0A); write_data(0x10); write_data(0x0A);
    write_data(0x09); write_data(0x26); write_data(0x36); write_data(0x53);
    write_data(0x4C); write_data(0x18); write_data(0x14); write_data(0x14);
    write_data(0x2F); write_data(0x34);

    write_comm(0x2A);             /* CASET 0..239 */
    write_data(0x00); write_data(0x00); write_data(0x00); write_data(0xEF);

    write_comm(0x2B);             /* RASET (vendor default) */
    write_data(0x00); write_data(0x00); write_data(0x00); write_data(0xEF);

    write_comm(0x29);             /* Display on */
    delay_ms(120);
}

void lcd_init(void)
{
    bb_bl_set(1);      /* attempt backlight (needs DTB fix on stock image) */
    lcd_reset();
    lcd_ic_init();
    disp_color(BLACK);
}

/* Set the pixel window using the panel's pre-offsets, then leave
 * CS/DC ready for a raster dump. */
void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    write_comm(0x2A);
    write_data((uint16_t)((x1 + COL_Pre) >> 8));
    write_data((uint16_t)(x1 + COL_Pre));
    write_data((uint16_t)((x2 + COL_Pre) >> 8));
    write_data((uint16_t)(x2 + COL_Pre));

    write_comm(0x2B);
    write_data((uint16_t)((y1 + ROW_Pre) >> 8));
    write_data((uint16_t)(y1 + ROW_Pre));
    write_data((uint16_t)((y2 + ROW_Pre) >> 8));
    write_data((uint16_t)(y2 + ROW_Pre));

    write_comm(0x2C);
}

void disp_color(uint16_t color)
{
    uint32_t n = (uint32_t)COL * ROW;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    lcd_begin_data();
    while (n--)
    {
        write_data_fast((uint8_t)(color >> 8));
        write_data_fast((uint8_t)color);
    }
    lcd_end_data();
}

void disp_frame(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    lcd_begin_data();
    /* first pixel row */
    write_data_fast(0xF8); write_data_fast(0x00);
        for (i = 0; i < COL - 2; i++)
        {
            write_data_fast(0xFF); write_data_fast(0xFF);
        }
        write_data_fast(0x00); write_data_fast(0x1F);
        for (j = 0; j < ROW - 2; j++)
        {
            write_data_fast(0xFF); write_data_fast(0xFF);
            for (i = 0; i < COL - 2; i++)
            {
                write_data_fast(0x00); write_data_fast(0x00);
            }
            write_data_fast(0xFF); write_data_fast(0xFF);
        }
        /* last pixel row */
        write_data_fast(0x00); write_data_fast(0x1F);
        for (i = 0; i < COL - 2; i++)
        {
            write_data_fast(0xFF); write_data_fast(0xFF);
        }
        write_data_fast(0xF8); write_data_fast(0x00);
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

            write_data_fast((uint8_t)(v >> 8));
            write_data_fast((uint8_t)v);
        }
    }
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
            uint16_t c = cols[j * 8 / COL];

            write_data_fast((uint8_t)(c >> 8));
            write_data_fast((uint8_t)c);
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
    {
        write_data_fast((uint8_t)(color >> 8));
        write_data_fast((uint8_t)color);
    }
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
                    uint16_t v = (bits & (1u << (7 - col))) ? color
                                                            : bgcolor;

                    write_data_fast((uint8_t)(v >> 8));
                    write_data_fast((uint8_t)v);
                }
            }
            lcd_end_data();
        }
        x = (uint16_t)(x + FONT16_W);
        s++;
    }
}
