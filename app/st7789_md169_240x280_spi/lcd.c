/*
  lcd.c - ST7789V 1.69" 240x280 (MD169) driver, hardware SPI0 port.
  Init sequence: the module vendor's own TFT==169 branch (module_8pin
  stm32f1_ex tftlcd.c) - values differ from the f411/c542 ST7789 inits.
  Geometry: 240x280 glass inside 240x320 GRAM -> ROW_Pre = 20.
  Transport: spi_hw (spidev + DC/RES GPIO), rasters stream with DC held
  high (GRAM window auto-increments across CS rises).
*/
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "ref/lcd_fonts.h"
#include "ref/lcd_font_1608.h"
#include "lcd.h"
#include "spi_hw.h"

/* ---- driver state ---- */
static pFONT    *s_AsciiFont;
static uint32_t  s_Color     = LCD_WHITE;
static uint32_t  s_BackColor = LCD_BLACK;
static uint8_t   s_Transparent;

static void delay_ms(uint32_t ms)
{
    usleep((useconds_t)ms * 1000);
}

uint32_t LCD_GetTick(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void StopDelay(uint16_t ms)
{
    delay_ms(ms);
}

/* ---- low level ---- */
void WriteComm(uint16_t data)
{
    spi_dc_set(0);
    spi_write_byte((uint8_t)data);
    spi_dc_set(1);
}

void WriteData(uint16_t data)
{
    spi_write_byte((uint8_t)data);
}

void LCD_WriteDataFast(uint8_t data)
{
    spi_write_byte(data);
}

void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    WriteComm(0x2A);
    WriteData((uint16_t)((x1 + COL_Pre) >> 8));
    WriteData((uint16_t)(x1 + COL_Pre));
    WriteData((uint16_t)((x2 + COL_Pre) >> 8));
    WriteData((uint16_t)(x2 + COL_Pre));

    WriteComm(0x2B);
    WriteData((uint16_t)((y1 + ROW_Pre) >> 8));
    WriteData((uint16_t)(y1 + ROW_Pre));
    WriteData((uint16_t)((y2 + ROW_Pre) >> 8));
    WriteData((uint16_t)(y2 + ROW_Pre));

    WriteComm(0x2C);
}

/* bulk fill: build one 480-byte row, repeat over spidev (CS rises per
   4096-byte chunk are harmless - the GRAM window auto-increments) */
static void fill_bulk16(uint16_t rgb565, uint32_t pixels)
{
    static uint16_t row[COL];
    uint32_t per_row = pixels < COL ? pixels : COL;

    for (uint32_t i = 0; i < per_row; i++)
        row[i] = rgb565;

    while (pixels)
    {
        uint32_t n = pixels < COL ? pixels : COL;

        spi_write_buf((const uint8_t *)row, n * 2);
        pixels -= n;
    }
}

static uint16_t rgb565(uint32_t rgb888)
{
    return (uint16_t)(((rgb888 >> 8) & 0xF800) |
                      ((rgb888 >> 5) & 0x07E0) |
                      ((rgb888 >> 3) & 0x001F));
}

void LCD_RESET(void)
{
    spi_res_set(1);
    delay_ms(50);
    spi_res_set(0);
    delay_ms(200);
    spi_res_set(1);
    delay_ms(200);
}

/* ---- module vendor init (module_8pin stm32f1_ex, TFT==169) ---- */
void LCD_IC_Init(void)
{
    WriteComm(0x36);
    WriteData(0x00);              /* USE_HORIZONTAL==0 */

    WriteComm(0x01);              /* Software Reset */
    delay_ms(120);

    WriteComm(0x11);              /* Sleep Out */
    delay_ms(120);

    WriteComm(0x3A);              /* 65k mode, 16bpp */
    WriteData(0x05);

    WriteComm(0xC5);              /* VCOM */
    WriteData(0x1A);

    WriteComm(0xB2);              /* Porch Setting */
    WriteData(0x05);
    WriteData(0x05);
    WriteData(0x00);
    WriteData(0x33);
    WriteData(0x33);

    WriteComm(0xB7);              /* Gate Control */
    WriteData(0x05);              /* 12.2v  -10.43v */

    WriteComm(0xBB);              /* VCOM */
    WriteData(0x3F);

    WriteComm(0xC0);              /* Power control */
    WriteData(0x2C);

    WriteComm(0xC2);              /* VDV and VRH Command Enable */
    WriteData(0x01);

    WriteComm(0xC3);              /* VRH Set */
    WriteData(0x0F);              /* 4.3+(vcom+vcom offset+vdv) */

    WriteComm(0xC4);              /* VDV Set */
    WriteData(0x20);              /* 0v */

    WriteComm(0xC6);              /* Frame Rate Control, 111 Hz */
    WriteData(0x01);

    WriteComm(0xD0);              /* Power Control 1 */
    WriteData(0xA4);
    WriteData(0xA1);

    WriteComm(0xE8);              /* Power Control 1 */
    WriteData(0x03);

    WriteComm(0xE9);              /* Equalize time control */
    WriteData(0x09);
    WriteData(0x09);
    WriteData(0x08);

    WriteComm(0xE0);              /* Set Gamma */
    WriteData(0xD0);
    WriteData(0x05);
    WriteData(0x09);
    WriteData(0x09);
    WriteData(0x08);
    WriteData(0x14);
    WriteData(0x28);
    WriteData(0x33);
    WriteData(0x3F);
    WriteData(0x07);
    WriteData(0x13);
    WriteData(0x14);
    WriteData(0x28);
    WriteData(0x30);

    WriteComm(0xE1);              /* Set Gamma */
    WriteData(0xD0);
    WriteData(0x05);
    WriteData(0x09);
    WriteData(0x09);
    WriteData(0x08);
    WriteData(0x03);
    WriteData(0x24);
    WriteData(0x32);
    WriteData(0x32);
    WriteData(0x3B);
    WriteData(0x14);
    WriteData(0x13);
    WriteData(0x28);
    WriteData(0x2F);

    WriteComm(0x21);              /* INVON */
    delay_ms(120);
    WriteComm(0x11);              /* Sleep Out */
    delay_ms(120);
    WriteComm(0x29);              /* Display On */
    delay_ms(120);
}

void LCD_Init(void)
{
    LCD_RESET();
    LCD_IC_Init();
    LCD_Clear();
}

/* ---- colors ---- */
static uint16_t rgb888_to_rgb565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                      ((c >> 3) & 0x001F));
}

void LCD_SetColor(uint32_t rgb888)
{
    s_Color = rgb888;
}

void LCD_SetBackColor(uint32_t rgb888)
{
    s_BackColor = rgb888;
}

void LCD_Clear(void)
{
    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    fill_bulk16(rgb565(s_BackColor), (uint32_t)COL * ROW);
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    for (uint16_t k = 0; k < h; k++)
    {
        LCD_SetAddress(x, (uint16_t)(y + k), (uint16_t)(x + w - 1),
                       (uint16_t)(y + k));
        fill_bulk16(rgb565(s_BackColor), w);
    }
}

/* ---- vendor TEST_STAND screens ---- */
void DispColor(uint32_t rgb888)
{
    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    fill_bulk16(rgb565(rgb888), (uint32_t)COL * ROW);
}

void DispFrame(void)
{
    static uint16_t row[COL];

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (int i = 0; i < ROW; i++)
    {
        for (int j = 0; j < COL; j++)
        {
            if (i == 0 || i == ROW - 1 || j == 0 || j == COL - 1)
                row[j] = 0xF800;
            else if (i == 1 || i == ROW - 2 || j == 1 || j == COL - 2)
                row[j] = 0x001F;
            else
                row[j] = 0xFFFF;
        }
        spi_write_buf((const uint8_t *)row, COL * 2);
    }
}

void DispGrayHor16(void)
{
    static uint16_t row[COL];

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (int j = 0; j < COL; j++)
        row[j] = (uint16_t)((j * 16 / COL) * 2048);
    for (int i = 0; i < ROW; i++)
        spi_write_buf((const uint8_t *)row, COL * 2);
}

void DispBand(void)
{
    static const uint16_t cols[8] = {
        0x0000, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x7FFF, 0xFFE0, 0xFFFF
    };
    static uint16_t row[COL];

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (int j = 0; j < COL; j++)
        row[j] = cols[j * 8 / COL];
    for (int i = 0; i < ROW; i++)
        spi_write_buf((const uint8_t *)row, COL * 2);
}

void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart,
                uint16_t Yend)
{
    LCD_SetAddress(Xstart, Ystart, Xend, Yend);
}

/* ---- drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t rgb888)
{
    uint16_t c = rgb888_to_rgb565(rgb888);

    LCD_SetAddress(x, y, x, y);
    LCD_WriteDataFast((uint8_t)(c >> 8));
    LCD_WriteDataFast((uint8_t)c);
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t h)
{
    LCD_SetAddress(x, y, x, (uint16_t)(y + h - 1));
    fill_bulk16(rgb565(s_Color), h);
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t w)
{
    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), y);
    fill_bulk16(rgb565(s_Color), w);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    for (uint16_t k = 0; k < h; k++)
    {
        LCD_SetAddress(x, (uint16_t)(y + k), (uint16_t)(x + w - 1),
                       (uint16_t)(y + k));
        fill_bulk16(rgb565(s_Color), w);
    }
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int dx = (int)x2 - (int)x1;
    int dy = (int)y2 - (int)y1;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    int err = (dx > 0 ? dx : -dx) - (dy > 0 ? dy : -dy);

    for (;;)
    {
        LCD_DrawPoint(x1, y1, s_Color);
        if (x1 == x2 && y1 == y2)
            break;
        {
            int e2 = 2 * err;

            if (e2 > -dy) { err -= dy; x1 = (uint16_t)(x1 + sx); }
            if (e2 <  dx) { err += dx; y1 = (uint16_t)(y1 + sy); }
        }
    }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    LCD_DrawLine_H(x, y, w);
    LCD_DrawLine_H(x, (uint16_t)(y + h - 1), w);
    LCD_DrawLine_V(x, y, h);
    LCD_DrawLine_V((uint16_t)(x + w - 1), y, h);
}

void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r)
{
    int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;

    while (x < y)
    {
        if (f >= 0) { y--; ddy += 2; f += ddy; }
        x++; ddx += 2; f += ddx;
        LCD_DrawPoint((uint16_t)(x0 + x), (uint16_t)(y0 + y), s_Color);
        LCD_DrawPoint((uint16_t)(x0 - x), (uint16_t)(y0 + y), s_Color);
        LCD_DrawPoint((uint16_t)(x0 + x), (uint16_t)(y0 - y), s_Color);
        LCD_DrawPoint((uint16_t)(x0 - x), (uint16_t)(y0 - y), s_Color);
        LCD_DrawPoint((uint16_t)(x0 + y), (uint16_t)(y0 + x), s_Color);
        LCD_DrawPoint((uint16_t)(x0 - y), (uint16_t)(y0 + x), s_Color);
        LCD_DrawPoint((uint16_t)(x0 + y), (uint16_t)(y0 - x), s_Color);
        LCD_DrawPoint((uint16_t)(x0 - y), (uint16_t)(y0 - x), s_Color);
    }
}

void LCD_FillCircle(uint16_t x0, uint16_t y0, uint16_t r)
{
    for (int dy = -r; dy <= r; dy++)
    {
        int dx2 = r * r - dy * dy;
        int span = dx2 > 0 ? (int)sqrt((double)dx2) : 0;
        int x = (int)x0 - span;

        LCD_SetAddress((uint16_t)x, (uint16_t)(y0 + dy),
                       (uint16_t)(x0 + span), (uint16_t)(y0 + dy));
        fill_bulk16(rgb565(s_Color), (uint32_t)(2 * span + 1));
    }
}

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    uint16_t *data)
{
    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (uint16_t k = 0; k < h; k++)
        spi_write_buf((const uint8_t *)(data + (uint32_t)k * w),
                      (size_t)w * 2);
}

/* ---- text ---- */
void LCD_SetAsciiFont(pFONT *font)
{
    s_AsciiFont = font;
}

void LCD_ShowTransparent(uint8_t mode)
{
    s_Transparent = mode;
}

void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c)
{
    uint16_t index;
    uint8_t  disChar;
    uint16_t Buff[8 * 16];

    if ((s_AsciiFont == NULL) || (c < 0x20U) || (c > 0x7EU))
        return;
    c = (uint8_t)(c - 0x20U);

    index = (uint16_t)c * s_AsciiFont->Sizes;
    {
        uint16_t bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;

        for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
        {
            for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
            {
                disChar = s_AsciiFont->pTable[index + row * bytesPerRow +
                                              (col / 8)];
                Buff[row * s_AsciiFont->Width + col] =
                    (disChar & (uint8_t)(1u << (col % 8)))
                        ? rgb565(s_Color)
                        : (s_Transparent ? 0 : rgb565(s_BackColor));
            }
        }
    }
    LCD_CopyBuffer(x, y, s_AsciiFont->Width, s_AsciiFont->Height, Buff);
}

void LCD_DisplayString(uint16_t x, uint16_t y, char *p)
{
    while (*p)
    {
        LCD_DisplayChar(x, y, (uint8_t)*p);
        x = (uint16_t)(x + s_AsciiFont->Width);
        p++;
    }
}
