/*
  spi_hw.h - hardware SPI transport (/dev/spidev0.0) for the panel,
  rk3506-mini JP4 wiring (same as the st7789 apps):

    SCL  -> JP4.23 (rm_io1 spi0_clk, hw)
    MOSI -> JP4.19 (rm_io3 spi0_mosi, hw)
    CS   -> JP4.24 (rm_io7 spi0_csn0, hw CS via spidev)
    DC   -> JP4.33 (gpio 47 / GPIO1_B7)
    RES  -> JP4.29 (gpio 49 / GPIO1_C1)
    BLK  -> JP4.15 (PWM1 ch0, 200 kHz - main.c drives it via sysfs)
*/
#ifndef SPI_HW_H
#define SPI_HW_H

#include <stdint.h>
#include <stddef.h>

int  spi_open(unsigned speed_hz);   /* opens /dev/spidev1.0, SPI mode 0 (panel wants mode 0) */
void spi_close(void);
void spi_res_open(int gpio);        /* RES gpio (default 49) */
void spi_res_set(int on);
void spi_dc_open(int gpio);         /* DC gpio (default 47) */
void spi_dc_set(int on);            /* DC: 0 = command, 1 = data */
void spi_write_byte(uint8_t b);     /* one byte with current DC */
void spi_write_buf(const uint8_t *b, size_t n); /* DC high, chunked 4096 */
void spi_write_pixels16(const uint16_t *px, size_t count);
                                    /* DC high, swaps words to big-endian */

#endif /* SPI_HW_H */
