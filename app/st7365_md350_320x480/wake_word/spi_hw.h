/*
  spi_hw.h - hardware SPI transport (/dev/spidev1.0) for the panel
  4-wire panel: DC/RES/CS as GPIO lines, data over spidev.

  Default wiring (CON4 layout, Orange Pi Zero):

    SCL  -> CON4.23 (SPI1_CLK / PA14, hw)
    MOSI -> CON4.19 (SPI1_MOSI / PA15, hw)
    CS   -> CON4.24 (SPI1_CE0 / PA13, hw CS via spidev)
    DC   -> CON4.11 (PA1)
    RES  -> CON4.7  (PA6; shared with the other module demos)
    BLK  -> 3.3 V (hardwired full-on - no PWM pin free)
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
