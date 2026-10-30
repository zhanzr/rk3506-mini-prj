# st7789_md130_240x240

ST7789V 1.3" 240x240 (MD130) SPI TFT for the rk3506-mini, ported from
`f4-demo/nano-f411/app/st7789_md130_240x240` (STM32F411). Hardware SPI0
version - runs on the stock image with the rebuilt DTB (`/dev/spidev0.0`).

## Driver facts (from the f411 reference)

- ST7789 240x240 glass at **GRAM rows 0..239**: `COL_Pre = ROW_Pre = 0`
  (a nonzero offset shifts every window and leaves a blank band)
- Init: sleep out, MADCTL 0x00, COLMOD 0x55 (16bpp), **INVON 0x21**
  (IPS panel), PORCTRL/VCOMS/VDVVRHEN/VRHS/VDVS/VCOM-offset/FRCTRL2/
  PWCTRL1 + pos/neg gamma, full-window CASET/RASET 0..239, display on
- **SPI MODE 3** (SCL idle high, rising-edge sample): this module renders
  nothing on mode 0 - the exact failure recorded in the reference repo's
  `c542_st7789_md130_possible_issue.md` (dark at 72/36/0.56 MHz on mode
  0; mode 3 works everywhere). NOTE: the MD169 1.69" module works on
  mode 0; do not share transport code blindly.
- **CS is hardwired to GND** on the module - the board's CS0 pin
  (JP4.24) is left unconnected; spidev still toggles it harmlessly.
  The GRAM window auto-increments across spidev's 4096-byte chunk
  boundaries, so bulk rasters just work with CS permanently low.

## Wiring (same as st7789_md169_240x280_spi, minus CS)

| Module | JP4 | gpio (sysfs) | Resource |
|---|---|---|---|
| GND | 6 | - | |
| V33 | 1 | - | |
| SCLK | 23 | - | hw SPI0 (rm_io4, DTB-rebuilt) |
| MOSI | 19 | - | hw SPI0 (rm_io6, DTB-rebuilt) |
| RESET | 29 | 49 (GPIO1_C1) | gpio chardev |
| DC | 33 | 47 (GPIO1_B7) | gpio chardev |
| CS | - | - | **module-internal GND** |
| BLK | 15 | - | PWM1 ch0 (ff170000), 200 kHz |

## Backlight

PWM 200 kHz (period 5000 ns @ clk_pwm1 93.75 MHz), normal polarity,
11% (odd loops) / 55% (even loops). At 500 Hz the LED showed visible
ripple on bright frames.

## Demo sequence (`make run`, loops forever, ~58 s per loop)

banner (2x font) -> info page (16px font) -> inverted info ->
TEST_STAND (frame/gray/band/red/green/blue/white/black, write time +
4 s dwell each) -> HSV gradient (1 s sweep + hold) -> stock boot logo
(2x) + caption -> scaled font (1x/2x/3x). Console logs phases,
per-screen write times and fps.

Usage: `./st7789_demo [-s speed_kHz]` (default 40000).
