# ili9163c_md144_128x128_spi

ILI9163C 1.44" 128x128 LCD (MD144 connector) for the rk3506-mini, ported
from `f4-demo/nano-f411/app/ili9163c_md144_128x128`. Software (bit-bang)
version, runs on the stock image.

**This module is 3-wire serial**: the connector has no D/C pin and no
backlight pin - every byte is a 9-bit frame (D/C bit clocked first, then
the 8 data bits, MSB first), and the backlight is hardwired on-module.

Driver facts carried over from the reference:
- gamma 0x26, frame rate 0xB1, power 0xC0/0xC1, VCOM 0xC5/0xC7,
  COLMOD 0x3A = 0x05 (16bpp), MADCTL 0x08 (180 deg vs the 0xC8 default),
  source direction 0xB7, gamma enable 0xF2, gamma tables 0xE0/0xE1
- geometry 128x128, COL_Pre = 0, ROW_Pre = 0 (init addresses columns
  0..127 and pages 0..159)

## Wiring

| LCD pin | JP4 | gpio (sysfs) |
|---|---|---|
| GND | 6 | - |
| VCC | 1 | 3.3 V |
| SCL | 23 | 1 (GPIO0_A1) |
| SDA | 19 | 3 (GPIO0_A3) |
| RES | 29 | 49 (GPIO1_C1) |
| CS | 24 | 7 (GPIO0_A7) |

## Demo

`make push run` - init (reset + ILI9163C init + black clear), then:
solid red -> 8-color band -> gray ramp -> text overlay
"RK3506 128x128". ~0.5 s per full frame (511 kbit/s effective).

Options: `./ili9163c_demo [-c cs] [-k sclk] [-m mosi] [-r res] [-s us]`.
