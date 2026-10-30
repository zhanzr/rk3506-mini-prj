# Applications

User-space applications for the rk3506-mini board. Each subdirectory is one
self-contained project with its own `Makefile` and `README.md`.

| Project | Description |
|---|---|
| `hello/` | Minimal hello world - proves the cross toolchain + deploy loop |
| `blink_hello/` | Hello world + LED2 (`/sys/class/leds/run`, heartbeat) blinker + SoC temperature sampling |
| `coremark/` | EEMBC CoreMark 1.0, ARM and Thumb-2 variants (results in its README) |
| `coremark_cm0/` | CoreMark for the on-chip Cortex-M0 - SDK patch + status: does not fit 32 KB M0 RAM, and the stock image cannot load M0 code yet (see README) |
| `ssd1317_md066_64x48_iic/` | SSD1317 64x48 I2C OLED demo (i2c-dev + GPIO uAPI), ported from the PocketBeagle reference - runs at ~85 fps |

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
