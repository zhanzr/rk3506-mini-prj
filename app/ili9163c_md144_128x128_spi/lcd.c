/*
  lcd.c - ILI9163C 1.44" 128x128 (MD144), faithful Linux port of the
  f411 reference lcd.c: same API, same drawing behavior, same fonts
  (6x12 default + 8x16 banner). Transport: hardware SPI0 via spi_hw
  (3-wire 9-bit frames - D/C bit clocked first; no D/C pin on module).

  Init: ILI9163C registers - gamma (0x26), frame rate (0xB1), power
  (0xC0/0xC1), VCOM (0xC5/0xC7), 16bpp COLMOD, MADCTL 0x08, source
  direction (0xB7), gamma enable (0xF2), gamma tables (0xE0/0xE1).
  Geometry: 128x128, COL_Pre = 0, ROW_Pre = 0.
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

/* ---- driver state (mirrors the reference) ---- */
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

/* ---- low level: 9-bit frames over spi0 ---- */
void WriteComm(uint16_t data)
{
    spi_cmd((uint8_t)data);
}

void WriteData(uint16_t data)
{
    spi_data8((uint8_t)data);
}

void LCD_WriteDataFast(uint8_t data)
{
    spi_data8(data);
}

void LCD_BeginData(void)
{
    /* 3-wire: no DC line; CS framing is per spi write */
}

void LCD_EndData(void)
{
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

void LCD_RESET(void)
{
    spi_res_set(1);
    delay_ms(50);
    spi_res_set(0);
    delay_ms(200);
    spi_res_set(1);
    delay_ms(200);
}

/* ILI9163C init, verbatim from the reference */
void LCD_IC_Init(void)
{
    WriteComm(0x11);              /* Exit Sleep */
    delay_ms(50);

    WriteComm(0x26);              /* Set Default Gamma */
    WriteData(0x04);

    WriteComm(0xB1);              /* Set Frame Rate */
    WriteData(0x0C);
    WriteData(0x14);

    WriteComm(0xC0);              /* VRH1[4:0] & VC[2:0] */
    WriteData(0x0C);
    WriteData(0x05);

    WriteComm(0xC1);              /* BT[2:0] */
    WriteData(0x02);

    WriteComm(0xC5);              /* VMH[6:0] & VML[6:0] */
    WriteData(0x29);
    WriteData(0x43);

    WriteComm(0xC7);              /* VCOM control */
    WriteData(0x40);

    WriteComm(0x3A);              /* 16bpp */
    WriteData(0x05);

    WriteComm(0x2A);              /* Column 0..127 */
    WriteData(0x00); WriteData(0x00);
    WriteData(0x00); WriteData(0x7F);

    WriteComm(0x2B);              /* Page 0..159 */
    WriteData(0x00); WriteData(0x00);
    WriteData(0x00); WriteData(0x9F);

    WriteComm(0x36);              /* MADCTL 0x08 */
    WriteData(0x08);

    WriteComm(0xB7);              /* Source Output Direction */
    WriteData(0x00);

    WriteComm(0xF2);              /* Enable Gamma */
    WriteData(0x01);

    WriteComm(0xE0);
    WriteData(0x36); WriteData(0x29); WriteData(0x12); WriteData(0x22);
    WriteData(0x1C); WriteData(0x15); WriteData(0x42); WriteData(0xB7);
    WriteData(0x2F); WriteData(0x13); WriteData(0x12); WriteData(0x0A);
    WriteData(0x11); WriteData(0x0B); WriteData(0x06);

    WriteComm(0xE1);
    WriteData(0x09); WriteData(0x16); WriteData(0x2D); WriteData(0x0D);
    WriteData(0x13); WriteData(0x15); WriteData(0x40); WriteData(0x48);
    WriteData(0x53); WriteData(0x0C); WriteData(0x1D); WriteData(0x25);
    WriteData(0x2E); WriteData(0x34); WriteData(0x39);

    WriteComm(0x29);              /* Display On */
}

void LCD_Init(void)
{
    LCD_RESET();
    LCD_IC_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
}

/* ---- colors ---- */
static uint16_t rgb888_to_rgb565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                      ((c >> 3) & 0x001F));
}

void LCD_SetColor(uint32_t rgb888)
{
    s_Color = rgb888_to_rgb565(rgb888);
}

void LCD_SetBackColor(uint32_t rgb888)
{
    s_BackColor = rgb888_to_rgb565(rgb888);
}

void LCD_Clear(void)
{
    uint32_t n = (uint32_t)COL * ROW;

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (uint32_t n = (uint32_t)w * h; n--;)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
}

/* ---- vendor TEST_STAND screens ---- */
void DispColor(uint32_t rgb888)
{
    uint16_t c = rgb888_to_rgb565(rgb888);
    uint32_t n = (uint32_t)COL * ROW;

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(c >> 8));
        LCD_WriteDataFast((uint8_t)c);
    }
}

void DispFrame(void)
{
    int i, j;

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
        {
            uint16_t c;

            if (i == 0 || i == ROW - 1 || j == 0 || j == COL - 1)
                c = 0xF800;
            else if (i == 1 || i == ROW - 2 || j == 1 || j == COL - 2)
                c = 0x001F;
            else
                c = 0xFFFF;
            LCD_WriteDataFast((uint8_t)(c >> 8));
            LCD_WriteDataFast((uint8_t)c);
        }
}

void DispGrayHor16(void)
{
    int i, j;

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
        {
            uint16_t v = (uint16_t)((j * 16 / COL) * 2048);
            LCD_WriteDataFast((uint8_t)(v >> 8));
            LCD_WriteDataFast((uint8_t)v);
        }
}

void DispBand(void)
{
    static const uint16_t cols[8] = {
        0x0000, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x7FFF, 0xFFE0, 0xFFFF
    };
    int i, j;

    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    for (i = 0; i < ROW; i++)
        for (j = 0; j < COL; j++)
        {
            uint16_t c = cols[j * 8 / COL];
            LCD_WriteDataFast((uint8_t)(c >> 8));
            LCD_WriteDataFast((uint8_t)c);
        }
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
    for (uint16_t k = 0; k < h; k++)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t w)
{
    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), y);
    for (uint16_t k = 0; k < w; k++)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (uint32_t n = (uint32_t)w * h; n--;)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
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

        LCD_SetAddress((uint16_t)(x0 - span), (uint16_t)(y0 + dy),
                       (uint16_t)(x0 + span), (uint16_t)(y0 + dy));
        for (int k = 0; k < 2 * span + 1; k++)
        {
            LCD_WriteDataFast((uint8_t)(s_Color >> 8));
            LCD_WriteDataFast((uint8_t)s_Color);
        }
    }
}

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    uint16_t *data)
{
    uint32_t n = (uint32_t)w * h;

    LCD_SetAddress(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(*data >> 8));
        LCD_WriteDataFast((uint8_t)*data);
        data++;
    }
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
                        ? s_Color
                        : (s_Transparent ? 0 : s_BackColor);
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
