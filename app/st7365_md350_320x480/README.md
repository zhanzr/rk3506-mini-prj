# st7365_md350_320x480 - ST7365P 3.5" 320x480 SPI panel (MD350) on rk3506-mini

Ported from the opz-dev group (`/home/user1/opz-dev/app/st7365_md350_320x480`,
Orange Pi Zero SPI1) to the rk3506-mini: **hardware SPI0** on JP4, same
wiring as the st7789 apps, and the opz "BLK hardwired to 3.3 V"
limitation replaced by the board's **PWM1 ch0 backlight** (JP4.15,
200 kHz, `docs/spi_howto.md`).

## Wiring (identical to st7789_md169_240x280_spi)

| Module | JP4 | Resource |
|---|---|---|
| SCLK | 23 | rm_io1 spi0_clk (hw) |
| MOSI | 19 | rm_io3 spi0_mosi (hw) |
| CS | 24 | rm_io7 spi0_csn0 (hw, spidev CS0) |
| DC | 33 | gpio 47 (GPIO1_B7) |
| RESET | 29 | gpio 49 (GPIO1_C1) |
| BLK | 15 | PWM1 ch0 (200 kHz; 11%/55% alternating in lcm_test, 55% in uvc_to_lcd) |

## Panel notes (from the proven f411/opz references)

- ST7365P is ST7796-register-compatible; init enables the vendor
  command set (0xF0 0xC3/0x96/0x3C/0x69), MADCTL 0x48 (portrait, BGR),
  COLMOD 0x05 (16bpp RGB565; `-6` switches to 18bpp RGB666 in uvc_to_lcd)
- windows at COL_Pre = 0 / ROW_Pre = 0 (full 320x480 GRAM)
- SPI mode 0; both apps request **100 MHz** by default (`-s` overrides).
  clk_spi0 = 187.5 MHz -> the driver reaches 93.75 MHz (divider /2).
  Getting there required raising the DTB spidev node
  `spi-max-frequency` 50 -> 100 MHz (stock value clamps everything to
  ~50 MHz: 74 ms/frame; at 93.75 MHz: ~37 ms/frame, measured).
- the glass has an inactive border outside the 320x480 active area
  (renders as a light strip, no content)

## Apps

- `lcm_test/` - bring-up demo: banner, info pages, TEST_STAND (write
  time + 4 s dwell), HSV gradient, stock RK3506 boot logo, scaled
  fonts, fps counter; 11%/55% alternating backlight loops
- `uvc_to_lcd/` - UVC camera viewer: crop-fill center, OSD line
  (format/SPI/host) + fps on the frame, camera auto-detect + lifecycle
  ("UVC NA" + rescan when absent). RGB565 default, `-6` for RGB666.
  Ships a vendored libjpeg 8.2.2 (the stock image has no libjpeg;
  runtime finds it via `LD_LIBRARY_PATH=vendor`).
