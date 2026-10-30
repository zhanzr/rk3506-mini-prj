/*
  lcd.h - ST7789V 1.69" 240x280 (MD169) driver, Linux port of the f411
  reference (nano-f411 app/st7789_md169_240x280), bit-bang via gpio_bb.

  Panel geometry (from the c542 board_t1 bsp, verified there):
  240x280 glass inside the ST7789's 240x320 GRAM -> ROW_Pre = 20
  (glass covers GRAM rows 20..299), MADCTL 0x00, IPS inversion (INVON).
*/
#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#define LCD_Width    240
#define LCD_Height   280
#define COL       240
#define ROW       280
#define COL_Pre   0
#define ROW_Pre   20

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
void write_comm(uint16_t data);
void write_data(uint16_t data);
void write_data_fast(uint8_t b);    /* raw byte, caller manages CS/DC */
void lcd_begin_data(void);          /* DC high, CS low */
void lcd_end_data(void);            /* CS high */

void disp_color(uint16_t color);
void disp_frame(void);
void disp_gray_hor16(void);
void disp_band(void);
void lcd_draw_string(uint16_t x, uint16_t y, const char *s,
                     uint16_t color, uint16_t bgcolor);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color);

#endif /* LCD_H */
