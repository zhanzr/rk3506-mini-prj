/*
 * SSD1317 "MD066" 0.66" 64x48 mono OLED, I2C - driver logic ported from the
 * AT32F425 reference (f425-start bare/ssd1317_md066_64x48_iic), transport
 * layer ported from the MP157 DK1 reference (i2c-dev + GPIO uAPI) to the
 * PocketBeagle:
 *
 *   SCL  -> P1.28 (I2C2_SCL, /dev/i2c-2)
 *   SDA  -> P1.26 (I2C2_SDA)
 *   RES  -> P2.22 (GPIO1_14, gpiod line name "P2.22", active low)
 *   VCC  -> P1.14 (3.3 V), GND -> P1.16
 *
 * module I2C address: 7-bit 0x3C (0x78 in 8-bit write notation), ~100 kHz
 */
#ifndef __OLED_H
#define __OLED_H

#include <signal.h>
#include <stdint.h>

/* shared stop flag: cleared by the signal handler in main(), polled by the
 * driver so Ctrl-C takes effect immediately (mid-demo, mid-sleep) */
extern volatile sig_atomic_t oled_running;

#define SIZE 8
#define XLevelL   0x00
#define XLevelH   0x10
#define Max_Column 64
#define Max_Row   48
#define Brightness 0xFF
#define X_WIDTH   64
#define Y_WIDTH   48

#define OLED_CMD  0  /* command byte */
#define OLED_DATA 1  /* data byte */

extern const unsigned char F6x8[][6];
extern const unsigned char F8X16[];

void OLED_WR_Byte(unsigned dat, unsigned cmd);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Init(int res_fd);
void OLED_Clear(void);
void OLED_Fill(unsigned char data);
void OLED_Invert(unsigned char on);
void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char chr,
                   unsigned char Char_Size);
void OLED_ShowNum(unsigned char x, unsigned char y, uint32_t num,
                  unsigned char len, unsigned char size2);
void OLED_ShowString(unsigned char x, unsigned char y,
                     const unsigned char *p, unsigned char Char_Size);
void OLED_Set_Pos(unsigned char x, unsigned char y);
void OLED_Demo(void);
void fill_picture(unsigned char fill_Data);
void Write_IIC_Command(unsigned char IIC_Command);
void Write_IIC_Data(unsigned char IIC_Data);
void Write_IIC_Byte_Buf(uint8_t slave_addr, uint8_t *buf, uint16_t size);
void oled_set_i2c_fd(int fd);
void oled_res_set(int res_fd, int on);
void oled_delay_ms(long ms);

#endif
