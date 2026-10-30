/*
  font_1608.h - 8x16 ASCII font (asc2_1608 grid), Linux port of the f411
  reference lcd_font_1608 (table data copied verbatim).

  Characters 0x20 (' ') .. 0x7E ('~'), 95 glyphs x 16 bytes = 1520 bytes.
  Layout: one byte per row, bit 0 = leftmost pixel (LSB first).
*/
#ifndef FONT_1608_H
#define FONT_1608_H

#include <stdint.h>

#define FONT16_W 8
#define FONT16_H 16

extern const uint8_t asc2_1608[1520]; /* 95 glyphs x 16 bytes */

#endif /* FONT_1608_H */
