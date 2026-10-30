/*
  gpio_bb.h - Linux GPIO char-device (uAPI v2) bit-bang bus for the
  ST7789 4-wire SPI panel.

  Default wiring ("rebuild-style" JP4 layout, gpio numbers = bank*32+pin):

    SCL  -> JP4.23  gpio 1   (GPIO0_A1, rm_io1)
    SDA  -> JP4.19  gpio 3   (GPIO0_A3, rm_io3)
    CS   -> JP4.24  gpio 7   (GPIO0_A7, rm_io7)
    BL   -> JP4.15  gpio 13  (GPIO0_B5)
    DC   -> JP4.33  gpio 47  (GPIO1_B7)
    RES  -> JP4.29  gpio 49  (GPIO1_C1)

  Every pin can be overridden at runtime via bb_configure(gpio numbers)
  before bb_open() - chip assignment is derived from the gpio number.
*/
#ifndef GPIO_BB_H
#define GPIO_BB_H

#include <stdint.h>

/* override defaults before bb_open(); pass -1 to keep a default */
void bb_configure(int scl, int sda, int cs, int bl, int dc, int res);
void bb_get_pins(int *scl, int *sda, int *cs, int *bl, int *dc, int *res);
/* alternate SPI edge convention: data changes while clock high,
 * latched on the falling edge (vs the default rising-edge latch) */
void bb_edge_falling(void);
int  bb_edge_falling_active(void);

int  bb_open(void);                 /* request all lines, set idle levels */
void bb_close(void);
void bb_slow(int us_per_bit);       /* 1 = default; >1 stretches the bus   */
void bb_res_set(int on);            /* RES */
void bb_dc_set(int on);             /* DC: 0 = command, 1 = data */
void bb_cs_set(int on);             /* CS: 0 = selected */
void bb_bl_set(int on);             /* BL */
void bb_write_byte(uint8_t b);      /* SPI mode 3, MSB first, CS managed by caller */
void bb_write_bytes(const uint8_t *b, int n);

#endif /* GPIO_BB_H */
