/*
  lcd.h - ILI9163C 1.44" 128x128 (MD144), faithful port of the f411
  reference LCD_* API. Hardware SPI0 transport (spi_hw, 9-bit frames).
*/
#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "ref/lcd_fonts.h"

/* ---- panel geometry ---- */
#define LCD_Width    128
#define LCD_Height   128
#define COL       128
#define ROW       128
#define COL_Pre   0
#define ROW_Pre   0
#define Delay_Time 500

/* ---- 24-bit colors (RGB888, converted by LCD_SetColor) ---- */
#define LCD_WHITE       0xFFFFFF
#define LCD_BLACK       0x000000
#define LCD_BLUE        0x0000FF
#define LCD_GREEN       0x00FF00
#define LCD_RED         0xFF0000
#define LCD_CYAN        0x00FFFF
#define LCD_MAGENTA     0xFF00FF
#define LCD_YELLOW      0xFFFF00

/* ---- raw RGB565 ---- */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0

uint32_t LCD_GetTick(void);
void StopDelay(uint16_t ms);

void LCD_Init(void);
void LCD_RESET(void);
void LCD_IC_Init(void);
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetColor(uint32_t rgb888);
void LCD_SetBackColor(uint32_t rgb888);
void LCD_Clear(void);
void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart,
                uint16_t Yend);
void DispColor(uint32_t color);
void DispFrame(void);
void DispGrayHor16(void);
void DispBand(void);

void LCD_SetAsciiFont(pFONT *font);
void LCD_ShowTransparent(uint8_t mode);
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c);
void LCD_DisplayString(uint16_t x, uint16_t y, char *p);

void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color);
void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height);
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data);

#endif /* LCD_H */
