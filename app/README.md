# Applications

User-space applications for the rk3506-mini board. Each subdirectory is one
self-contained project with its own `Makefile` and `README.md`.

| Project | Description |
|---|---|
| `hello/` | Minimal hello world - proves the cross toolchain + deploy loop |
| `blink_hello/` | Hello world + LED2 (`/sys/class/leds/run`, heartbeat) blinker + SoC temperature sampling |
| `coremark/` | EEMBC CoreMark 1.0, ARM and Thumb-2 variants (results in its README) |
| `m0_bench/` | Standalone Cortex-M0 firmware, Linux runtime loader, GPIO CoreMark runner, and historical vendor-HAL attempt; validated on the live board (see README) |
| `coremark_multi/` | Multi-core CoreMark verification: per-core round-robin + all-cores-parallel runs, per-core scores + CSV recording (see README) |
| `ssd1317_md066_64x48_iic/` | SSD1317 64x48 I2C OLED demo (i2c-dev + GPIO uAPI), ported from the PocketBeagle reference - ~85 fps; `lcm_logo` shows the board's stock boot logo + scaled stock font |
| `st7789_md169_240x280_spi/` | ST7789 1.69" 240x280 SPI TFT (MD169), ported from the f411 reference - software (bit-bang) version, ~3.5 s/full frame; hw-SPI + PWM backlight after the DTB rebuild |
| `ili9163c_md144_128x128_spi/` | ILI9163C 1.44" 128x128 SPI LCD (MD144), 3-wire 9-bit frames (no D/C pin), ported from the f411 reference - ~0.5 s/frame bit-banged |
| `gpio_test/` | JP4 pin walker: blinks every free JP4 GPIO one by one for LED-probe verification; also forces bank0 RM_IO IOMUX nibbles to GPIO and reports their state |

## Common workflow (per project, from the project directory)

Fast loop during development - cross-build on the host (WSL) and push only
the binary:

```
make push run
```

Transfers use `scp` over the `rk3506-buildroot` ssh alias (see the root
`README.md` for the connection setup). `CROSS` defaults to the Buildroot
SDK in `~/toolchains/...` (absolute path, no env needed) and can be
overridden:

```
make push run CROSS=/path/to/toolchain/bin/arm-linux-
```

Cross flags: `-O2 -Wall -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard`
(matching the board's Buildroot config). See the repository root `README.md`
for toolchain details, board connection, LED notes, and vendor SDK info.
