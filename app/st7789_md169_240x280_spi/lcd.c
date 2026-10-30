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

static int g_invon = 1;

void lcd_set_inversion(int on)
{
    g_invon = on;
}

/* ST7789 (MD169 v3) init - VERBATIM from the module vendor's own driver
 * (module_db module-st7789-v3-240x280-1_69, vendor_doc/module_8pin/
 * stm32f1_ex/APP/tftlcd/tftlcd.c, TFT==169 branch). Values differ
 * significantly from the f411/c542 sequences. */
void lcd_ic_init(void)
{
    write_comm(0x36);             /* MADCTL */
    if (1)
        write_data(0x00);
    write_comm(0x01);             /* Software Reset */
    delay_ms(120);

    write_comm(0x11);             /* Sleep Out */
    delay_ms(120);
    /*------------------- ST7789V frame rate setting ------------------*/
    write_comm(0x3A);             /* 65k mode, 16bpp */
    write_data(0x05);
    write_comm(0xC5);             /* VCOM */
    write_data(0x1A);

    write_comm(0xB2);             /* Porch Setting */
    write_data(0x05);
    write_data(0x05);
    write_data(0x00);
    write_data(0x33);
    write_data(0x33);

    write_comm(0xB7);             /* Gate Control */
    write_data(0x05);             /* 12.2v  -10.43v */
    /*------------------- ST7789V power setting -----------------------*/
    write_comm(0xBB);             /* VCOM */
    write_data(0x3F);

    write_comm(0xC0);             /* Power control */
    write_data(0x2C);

    write_comm(0xC2);             /* VDV and VRH Command Enable */
    write_data(0x01);

    write_comm(0xC3);             /* VRH Set */
    write_data(0x0F);             /* 4.3+(vcom+vcom offset+vdv) */

    write_comm(0xC4);             /* VDV Set */
    write_data(0x20);             /* 0v */

    write_comm(0xC6);             /* Frame Rate Control, Normal Mode */
    write_data(0x01);             /* 111 Hz */

    write_comm(0xD0);             /* Power Control 1 */
    write_data(0xA4);
    write_data(0xA1);

    write_comm(0xE8);             /* Power Control 1 */
    write_data(0x03);

    write_comm(0xE9);             /* Equalize time control */
    write_data(0x09);
    write_data(0x09);
    write_data(0x08);
    /*------------------- ST7789V gamma setting -----------------------*/
    write_comm(0xE0);             /* Set Gamma */
    write_data(0xD0);
    write_data(0x05);
    write_data(0x09);
    write_data(0x09);
    write_data(0x08);
    write_data(0x14);
    write_data(0x28);
    write_data(0x33);
    write_data(0x3F);
    write_data(0x07);
    write_data(0x13);
    write_data(0x14);
    write_data(0x28);
    write_data(0x30);

    write_comm(0xE1);             /* Set Gamma */
    write_data(0xD0);
    write_data(0x05);
    write_data(0x09);
    write_data(0x09);
    write_data(0x08);
    write_data(0x03);
    write_data(0x24);
    write_data(0x32);
    write_data(0x32);
    write_data(0x3B);
    write_data(0x14);
    write_data(0x13);
    write_data(0x28);
    write_data(0x2F);

    if (g_invon)
        write_comm(0x21);         /* INVON */

    delay_ms(120);
    write_comm(0x11);             /* Sleep Out */
    delay_ms(120);
    write_comm(0x29);             /* Display on */
    delay_ms(120);
}

void lcd_init(void)
{
    bb_bl_set(1);      /* attempt backlight */
    lcd_reset();
    lcd_ic_init();
    disp_color(BLACK);
}

void lcd_init_slow(int slow_us_per_bit)
{
    bb_slow(slow_us_per_bit);
    lcd_init();
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
