/*
  spi_hw.h - hardware SPI transport (/dev/spidev0.0) for the ILI9163C
  3-wire panel: 9-bit frames (D/C bit + 8 data bits) packed into a
  continuous byte bitstream, matching the f411 reference HW path.
*/
#ifndef SPI_HW_H
#define SPI_HW_H

#include <stdint.h>

int  spi_open(unsigned speed_hz);   /* opens /dev/spidev0.0, SPI mode 0 */
void spi_res_open(int gpio);        /* RES stays a GPIO (default 49) */
void spi_res_set(int on);
void spi_close(void);
void spi_cmd(uint8_t b);            /* one 9-bit frame, D/C = 0 */
void spi_data8(uint8_t b);          /* one 9-bit frame, D/C = 1 */

/* raster burst: CS held by spidev per write; frames packed continuously,
 * incomplete trailing frame at each chunk boundary is discarded by the
 * panel (fewer than 9 bits) and carried over transparently */
void spi_stream_begin(void);
void spi_stream_word(uint16_t v);   /* two data frames (16bpp word) */
void spi_stream_end(void);

#endif /* SPI_HW_H */
