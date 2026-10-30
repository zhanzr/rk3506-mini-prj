/*
  lcd.h - ILI9163C 1.44" 128x128 (MD144) driver, Linux port of the f411
  reference. 3-wire serial: no D/C pin - each byte is a 9-bit frame
  (D/C bit clocked first); backlight is hardwired on-module.

  Geometry: 128x128, COL_Pre = 0, ROW_Pre = 0 (init addresses the full
  column range 0..127 and page range 0..159).
*/
#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#define COL 128
#define ROW 128
#define COL_Pre 0
#define ROW_Pre 0

/* raw RGB565 */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0

void lcd_init(void);
void lcd_reset(void);
void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void write_comm(uint8_t data);
void write_data8(uint8_t data);
void write_data16(uint16_t data);
void lcd_begin_data(void);          /* CS low */
void lcd_end_data(void);            /* CS high */

void disp_color(uint16_t color);
void disp_band(void);
void disp_gray_hor16(void);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color);
void lcd_draw_string(uint16_t x, uint16_t y, const char *s,
                     uint16_t color, uint16_t bgcolor);

#endif /* LCD_H */
