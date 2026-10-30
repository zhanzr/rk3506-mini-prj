# ssd1317_md066_64x48_iic

SSD1317 "MD066" 0.66" 64x48 mono OLED demo for the rk3506-mini, ported
from `pbeagle_prj/app/ssd1317_md066_64x48_iic` (PocketBeagle), which came
from the MP157 DK1 / AT32F425 references (Zhongjingyuan vendor example).

No library dependencies - only kernel UAPI headers (`linux/i2c-dev.h`,
`linux/gpio.h`): the module is driven via i2c-dev, RES via the GPIO
char-device (fixed chip/offset - this image does not name GPIO lines,
unlike the PocketBeagle's).

## Wiring (3.3 V module, JP4)

| Module | JP4 pin | Net / resource |
|---|---|---|
| GND | 9 | GND |
| V33 | 1 | 3.3 V |
| SCL | 28 | `EXP_I2C0_SCL` (LCD_D17) -> `/dev/i2c-0` |
| SDA | 27 | `EXP_I2C0_SDA` (LCD_D18) |
| RES | 29 | GPIO1_C1 = gpiochip1 line 17 (gpio 49), active low |

Module 7-bit I2C address 0x3C. The bus is shared with the onboard RTC
(0x68) and ES8388 codec (0x11); 10k pull-ups are on the baseboard.

**Gotcha (same as on the PocketBeagle):** the module does not ACK while
RES is floating. The demo requests RES output-high before probing. If a
manual `i2cdetect` shows nothing, drive RES high first:

```
echo 49 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio49/direction
echo 1  > /sys/class/gpio/gpio49/value
```

Leave a sysfs export holding the line and the demo fails with
"RES busy" - `echo 49 > /sys/class/gpio/unexport` first.

## Behavior

Demo loop (repeats until Ctrl-C, capped at 30 s per run):
banner/info -> inverted info -> all-on -> all-off -> 8-px stripes ->
4-px stripes -> checkerboard -> vertical bars -> border -> gradient ->
animated bar sweep with console FPS report -> `bye` on exit.

## Build and run

```
make push run       # cross-build in WSL, push, run
```

`CROSS` defaults to the Buildroot SDK in `~/toolchains/...` (see the
repository root `README.md`).
