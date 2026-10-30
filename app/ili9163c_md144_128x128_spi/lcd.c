/*
  lcd.c - ILI9163C 1.44" 128x128 (MD144) driver, Linux port of the f411
  reference (init sequence verbatim). Transport is spi_hw (hardware SPI0
  via /dev/spidev0.0), 9-bit frames: D/C bit first, then the 8 data bits.
*/
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "font_1608.h"
#include "logo64.h"
#include "lcd.h"
#include "spi_hw.h"

static uint8_t s_madctl = 0x08;
static int     g_invon = 1;

void lcd_set_madctl(uint8_t v)
{
    s_madctl = v;
}

void lcd_set_inversion(int on)
{
    g_invon = on;
}

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
    spi_data8((uint8_t)(data >> 8));
    spi_data8((uint8_t)data);
}

void lcd_begin_data(void)
{
}

void lcd_end_data(void)
{
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

/* ST7789-class panel on this module is actually driven by the ILI9163C
 * init from the f411 reference (verified working on the f411/f746). */
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

    write_comm(0x36);             /* MADCTL (settable: mirror etc.) */
    write_data8(s_madctl);

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
    spi_stream_begin();
    while (n--)
        spi_stream_word(color);
    spi_stream_end();
}

void disp_band(void)
{
    static const uint16_t cols[8] = {
        BLACK, BLUE, RED, MAGENTA, GREEN, CYAN, YELLOW, WHITE
    };
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    spi_stream_begin();
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
            spi_stream_word(cols[j * 8 / COL]);
    spi_stream_end();
}

void disp_gray_hor16(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    spi_stream_begin();
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
        {
            uint16_t c = (uint16_t)(j * 16 / COL) * 2048; /* r5<<11 */
            uint16_t g = (uint16_t)(j * 32 / COL) << 5;
            spi_stream_word(c | g);
        }
    spi_stream_end();
}

void disp_frame(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
        {
            uint16_t c;

            if (i < 2 || i > ROW - 3 || j < 2 || j > COL - 3)
                c = WHITE;
            else
                c = BLACK;
            spi_stream_word(c);
        }
}

void disp_checker(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
            spi_stream_word(((i / 8) ^ (j / 8)) & 1 ? WHITE : BLACK);
}

void disp_vstripes(void)
{
    int i, j;

    lcd_set_address(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
            spi_stream_word(j & 1 ? WHITE : BLACK);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color)
{
    lcd_set_address(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    spi_stream_begin();
    for (uint32_t n = (uint32_t)w * h; n--;)
        spi_stream_word(color);
    spi_stream_end();
}

void lcd_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;)
    {
        write_comm(0x2A);
        write_data8(0); write_data8((uint8_t)x0);
        write_data8(0); write_data8((uint8_t)x0);
        write_comm(0x2B);
        write_data8(0); write_data8((uint8_t)y0);
        write_data8(0); write_data8((uint8_t)y0);
        write_comm(0x2C);
        write_data16(color);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = 2 * err;

            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
}

void lcd_draw_circle(int x0, int y0, int r, uint16_t color)
{
    int f = 1 - r;
    int ddx = 1, ddy = -2 * r, x = 0, y = r;

    while (x < y)
    {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
        write_comm(0x2A);
        write_data8(0); write_data8((uint8_t)(x0 + x));
        write_data8(0); write_data8((uint8_t)(x0 + x));
        write_comm(0x2B);
        write_data8(0); write_data8((uint8_t)(y0 + y));
        write_data8(0); write_data8((uint8_t)(y0 + y));
        write_comm(0x2C);
        write_data16(color);
        /* mirror octants via direct window writes */
        {
            int px[4] = { x0 - x, x0 + x, x0 - y, x0 + y };
            int py[4] = { y0 - y, y0 - y, y0 - x, y0 - x };
            int k;

            for (k = 0; k < 4; k++)
            {
                if (px[k] < 0 || px[k] > 127 || py[k] < 0 || py[k] > 127)
                    continue;
                write_comm(0x2A);
                write_data8(0); write_data8((uint8_t)px[k]);
                write_data8(0); write_data8((uint8_t)px[k]);
                write_comm(0x2B);
                write_data8(0); write_data8((uint8_t)py[k]);
                write_data8(0); write_data8((uint8_t)py[k]);
                write_comm(0x2C);
                write_data16(color);
            }
        }
    }
}

void lcd_fill_circle(int x0, int y0, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++)
    {
        int dx = (int)(r * r - dy * dy);
        int span = dx > 0 ? (int)sqrt((double)dx) : 0;

        lcd_fill_rect((uint16_t)(x0 - span), (uint16_t)(y0 + dy),
                      (uint16_t)(2 * span + 1), 1, color);
    }
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
            for (row = 0; row < FONT16_H; row++)
            {
                uint8_t bits = asc2_1608[index * 16 + row];

                for (col = 0; col < FONT16_W; col++)
                    write_data16((bits & (1u << (7 - col))) ? color
                                                            : bgcolor);
            }
        }
        x = (uint16_t)(x + FONT16_W);
        s++;
    }
}

/* ---- stock boot logo, 2x scaled, centered ---- */
void lcd_show_logo(void)
{
    static uint16_t fb[128 * 128]; /* 32 KB */
    int x, y;

    memset(fb, 0, sizeof(fb));

    /* logo64_fb: 64x48, 6 pages x 64 bytes, bit0 = top row; source is a
     * 64x27 logo drawn in the top of that 64x48 frame. Scale 2x -> place
     * at (0, 16) covering 128x96... actual glyph area is the top 27*2=54
     * rows; center: y offset 16 keeps it visually balanced. */
    for (int sy = 0; sy < 48; sy++)
        for (int sx = 0; sx < 64; sx++)
        {
            unsigned bit = (logo64_fb[sy * 8 + sx / 8] >> (sx % 8)) & 1;
            uint16_t c = bit ? WHITE : BLACK; /* lit pixel -> white */

            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                {
                    int px = sx * 2 + dx;
                    int py = 16 + sy * 2 + dy;

                    if (px < 128 && py < 128)
                        fb[py * 128 + px] = c;
                }
        }

    /* caption */
    {
        const char *s = "RK3506-mini";
        int tx = (128 - 11 * 8) / 2 + 4;

        for (int i = 0; s[i] && tx + 8 <= 128; i++, tx += 8)
        {
            int index = s[i] - 0x20;

            for (int row = 0; row < 16; row++)
            {
                uint8_t bits = asc2_1608[index * 16 + row];

                for (int col = 0; col < 8; col++)
                    fb[(112 + row) * 128 + tx + col] =
                        (bits & (1u << (7 - col))) ? YELLOW : BLACK;
            }
        }
    }

    lcd_set_address(0, 0, 127, 127);
    spi_stream_begin();
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            spi_stream_word(fb[y * 128 + x]);
    spi_stream_end();
}
