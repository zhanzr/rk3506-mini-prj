# st7789_md169_240x280_spi

ST7789V 1.69" 240x280 (MD169) SPI TFT for the rk3506-mini, ported from
`f4-demo/nano-f411/app/st7789_md169_240x280` (STM32F411, itself from the
c542 board_t1 st7789 bsp). This is the **hardware SPI0 version** (4-wire: DC gpio + spidev CS0) -
runs on the stock image with zero DTB changes.

Driver facts carried over from the reference:
- ST7789 240x280 glass on a 240x320 GRAM: **ROW_Pre = 20** (glass covers
  GRAM rows 20..299), MADCTL 0x00, IPS inversion (INVON 0x21)
- SPI mode 3 (clock idle high, sample on rising edge), MSB first

## Wiring ("rebuild-style" JP4 layout)

| Module | JP4 | gpio (sysfs) | Resource |
|---|---|---|---|
| GND | 6 | - | |
| V33 | 1 | - | |
| SCLK | 23 | 1 (GPIO0_A1) | bit-banged (hw SPI0 needs DTB) |
| MOSI | 19 | 3 (GPIO0_A3) | bit-banged |
| (MISO) | 21 | 6 (GPIO0_A6) | unused |
| RESET | 29 | 49 (GPIO1_C1) | |
| DC | 33 | 47 (GPIO1_B7) | |
| CS | 24 | 7 (GPIO0_A7) | bit-banged (hw CSN0 needs DTB) |
| BLK | 15 | 13 (GPIO0_B5) | gpio mode confirmed (iomux nibble 0); JP4.17 (V33) also works - verified lit |

Transport: GPIO char-device uAPI v2, two SET_VALUES ioctls per bit.
Measured: **~3.5 s per full 240x280x16bpp frame (~309 kbit/s effective)**,
init sequence ~4 s. A hardware-spidev version (after the DTB rebuild,
`docs/jp4_spi_lcm.md`) would be in the tens-of-fps range.

## Demo sequence (`make run`, ~25 s total)

1. init (reset + ST7789 power/porch/gamma init + black clear)
2. solid red - the "wiring and init are good" screen
3. 8-color band
4. horizontal gray ramp
5. text overlay: "RK3506 240x280" (8x16 stock font from the reference)

The panel keeps the last frame after exit.

## Build and run

```
make push run
```

## TODO (rebuild path)

After the DTB adds SPI0 (`docs/jp4_spi_lcm.md`), switch `gpio_bb.c` to
`/dev/spidev0.0` (or add a second transport), which turns this into a
real-time-capable driver; BLK becomes a real PWM on JP4.15
(`rm_io13_pwm1_ch0`).
