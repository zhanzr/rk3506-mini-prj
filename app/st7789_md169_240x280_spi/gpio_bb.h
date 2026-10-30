/*
  gpio_bb.h - Linux GPIO char-device (uAPI v2) bit-bang bus for the
  ST7789 4-wire SPI panel, rk3506-mini JP4 "rebuild-style" wiring:

    SCL  -> JP4.23  GPIO0_A1  = gpiochip0 line 1  (gpio 1)
    SDA  -> JP4.19  GPIO0_A3  = gpiochip0 line 3  (gpio 3)
    CS   -> JP4.24  GPIO0_A7  = gpiochip0 line 7  (gpio 7)
    BL   -> JP4.15  GPIO0_B5  = gpiochip0 line 13 (gpio 13) *
    DC   -> JP4.33  GPIO1_B7  = gpiochip1 line 15 (gpio 47)
    RES  -> JP4.29  GPIO1_C1  = gpiochip1 line 17 (gpio 49)

  * BL is muxed to CAN1_TX in the stock image - driving it has no pin
    effect until the DTB disables CAN1; jumper BLK to V33 if the panel
    stays dark.
*/
#ifndef GPIO_BB_H
#define GPIO_BB_H

#include <stdint.h>

int  bb_open(void);                 /* request all lines, set idle levels */
void bb_close(void);
void bb_res_set(int on);            /* RES */
void bb_dc_set(int on);             /* DC: 0 = command, 1 = data */
void bb_cs_set(int on);             /* CS: 0 = selected */
void bb_bl_set(int on);             /* BL (see caveat above) */
void bb_write_byte(uint8_t b);      /* SPI mode 3, MSB first, CS managed by caller */
void bb_write_bytes(const uint8_t *b, int n);

#endif /* GPIO_BB_H */
