/*
  spi_hw.h - hardware SPI transport (/dev/spidev0.0) for the ST7789
  4-wire panel: DC/RES/CS as GPIO lines, data over spidev.

  Default wiring ("rebuild-style" JP4 layout):
    SCL  -> JP4.23  gpio 1   (SPI0_CLK, hw)
    MOSI -> JP4.19  gpio 3   (SPI0_MOSI, hw)
    CS   -> JP4.24  gpio 7   (SPI0_CSN0, hw CS via spidev)
    DC   -> JP4.33  gpio 47  (GPIO1_B7)
    RES  -> JP4.29  gpio 49  (GPIO1_C1)
    BLK  -> JP4.15           (PWM1 ch0 - set up via sysfs pwmchip)
*/
#ifndef SPI_HW_H
#define SPI_HW_H

#include <stdint.h>
#include <stddef.h>

int  spi_open(unsigned speed_hz);   /* opens /dev/spidev0.0, SPI mode 0 */
void spi_close(void);
void spi_res_open(int gpio);        /* RES gpio (default 49) */
void spi_res_set(int on);
void spi_dc_open(int gpio);         /* DC gpio (default 47) */
void spi_dc_set(int on);            /* DC: 0 = command, 1 = data */
void spi_write_byte(uint8_t b);     /* one byte with current DC */
void spi_write_buf(const uint8_t *b, size_t n); /* DC high, chunked 4096 */

#endif /* SPI_HW_H */
