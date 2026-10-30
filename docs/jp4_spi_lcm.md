# 4-line SPI LCM on JP4 - pin resources (verified against schematic + live DT)

Mapping for a typical ST7789/ST7735-class 4-line SPI TFT module.
All signals are 3.3 V domain. Reset (JP4.29 / gpio 49) is already proven
by `app/ssd1317_md066_64x48_iic`.

## Recommended wiring

| LCM signal | JP4 pin | Net / SoC resource | Linux | Notes |
|---|---|---|---|---|
| GND | 6 | GND | - | |
| V33 | 1 | 3.3 V | - | |
| SCLK | 23 | `SPI0_CLK` = GPIO0_A1, **RM_IO1** | gpio 1 | hardware SPI0 via DTB (below) or bit-bang |
| MOSI | 19 | `SPI0_MOSI` = GPIO0_A3, **RM_IO3** | gpio 3 | same |
| (MISO) | 21 | `SPI0_MISO` = GPIO0_A6, **RM_IO6** | gpio 6 | unused by write-only LCMs, keep for spidev |
| RESET | 29 | GPIO1_C1 | gpio 49 | proven in ssd1317 project |
| DC | 33 | GPIO1_B7 | gpio 47 | plain GPIO, LCD_D12 alternate unused - free |
| CS | 31 | GPIO1_C0 | gpio 48 | plain GPIO, free - **preferred over HW CS** (frees RM_IO7, see BLK) |
| BLK | 24 | `SPI0_CSN0` net = GPIO0_A7, **RM_IO7** | - | mux as **hardware PWM** (11 % duty default) |

Why CS goes to a plain GPIO: hardware PWM for BLK requires an RM_IO pin
(`rm_io0..19` = GPIO0_A0..C3), and on JP4 the only *free* RM_IO pins are
exactly the four SPI-net pins 23/19/21/24. Giving CS to GPIO1_C0 leaves
GPIO0_A7 (JP4.24) for the backlight PWM. (Alternative: HW CS on JP4.24
via `rm_io7_spi0_csn0`, then BLK = plain GPIO full-on or software-PWM.)

## Why this is the pin budget

Every other RM_IO-capable JP4 pin is claimed by an enabled peripheral:

| JP4 pin | RM_IO | Claimed by |
|---|---|---|
| 7 | rm_io8 (GPIO0_B0) | SAI1_MCLK - es8388 codec (enabled) |
| 11 | rm_io9 (GPIO0_B1) | SAI1_SCLK - codec |
| 13 | rm_io10 (GPIO0_B2) | SAI1_LRCK - codec |
| 15 | rm_io13 (GPIO0_B5) | CAN1_TX (can1 enabled) |
| 18 | rm_io14 (GPIO0_B6) | UART3_TX (uart3) |
| 22 | rm_io15 (GPIO0_B7) | UART3_RX |
| 10 / 8 | rm_io16/17 (GPIO0_C0/C1) | UART1_RX/TX |
| 26 | rm_io18 (GPIO0_C2) | CAN1_RX |
| 32 | rm_io19 (GPIO0_C3) | CAN0_TX |
| 36 | (GPIO0_C4) | CAN0_RX |

Bank1 pins (JP4 29/31/33/35/37/38/40...) have no RM_IO - plain GPIO +
LCD alternate only, so no hardware PWM there.

## DTB changes required (stock image has them disabled)

Verified on the running board: `spi@ff120000` (SPI0) = `disabled`
(`/dev/spidev*` absent), `pwm@ff170000` (pwm1_8ch_0) = disabled; the only
live PWM is ff932000 (MIPI backlight, claimed). DTS additions:

```dts
&spi0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&rm_io1_spi0_clk &rm_io3_spi0_mosi &rm_io6_spi0_miso>;
    spidev@0 {
        compatible = "rockchip,spidev";
        reg = <0>;
        spi-max-frequency = <50000000>;
    };
};

/* BLK backlight PWM on JP4.24 (GPIO0_A7 / rm_io7) */
&pwm1_8ch_0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&rm_io7_pwm1_ch0>;
    assigned-clocks = <&cru CLK_PWM1>;
    assigned-clock-rates = <100000000>;
};
```

All referenced pinctrl nodes exist in `rk3506-pinctrl-rmio.dtsi`
(`rm_io1/3/6_spi0_*`, `rm_io7_pwm1_ch0` verified). PWM use via sysfs
(e.g. 20 kHz period, 11 % duty = 220000 ns):

```
echo 0 > /sys/class/pwm/pwmchipN/export     # chip numbering may shift
echo 2000000 > .../pwm0/period
echo 220000  > .../pwm0/duty_cycle
echo 1       > .../pwm0/enable
```

## If SAI/CAN0/CAN1/UART1/UART3 are not needed (DTB rebuild happens anyway)

Disabling them in the DT frees every claimed RM_IO pin, so the wiring can
follow the module silkscreen 1:1 with **hardware CS**:

| LCM signal | JP4 pin | Resource |
|---|---|---|
| SCLK | 23 | spi0 via `rm_io1_spi0_clk` |
| MOSI | 19 | `rm_io3_spi0_mosi` |
| (MISO) | 21 | `rm_io6_spi0_miso` |
| CS | 24 | **hardware CSN0** via `rm_io7_spi0_csn0` |
| DC | 33 | GPIO1_B7 = gpio 47 (or 31 = gpio 48) |
| RESET | 29 | gpio 49 |
| BLK | **15** | GPIO0_B5 / rm_io13 -> `rm_io13_pwm1_ch0`, 11 % duty |

DT additions in this variant (replaces the snippet above):

```dts
&spi0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&rm_io1_spi0_clk &rm_io3_spi0_mosi
                 &rm_io6_spi0_miso &rm_io7_spi0_csn0>;
    spidev@0 {
        compatible = "rockchip,spidev";
        reg = <0>;
        spi-max-frequency = <50000000>;
    };
};

&pwm1_8ch_0 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&rm_io13_pwm1_ch0>;
    assigned-clocks = <&cru CLK_PWM1>;
    assigned-clock-rates = <100000000>;
};

&can0 { status = "disabled"; };
&can1 { status = "disabled"; };
&uart1 { status = "disabled"; };
&uart3 { status = "disabled"; };

/* SAI1 feeds the onboard es8388 codec; disabling the codec also
   requires removing/disabling the es8388_sound card node that
   references it (vanxoak-hd-rk3506-evb-v1.dtsi). */
&sai1 { status = "disabled"; };
```

Note: a DTB rebuild is required in **both** variants (the stock image has
spi0 and all pwm1 channels disabled); disabling unused peripherals does
not avoid the rebuild, it only widens the pin choice while you are
rebuilding anyway.

## Last resort: no-rebuild wiring (stock image, as-is)

Stock image: no spidev, no usable PWM chip (the only live one is claimed
by the MIPI backlight). Everything runs as plain GPIO + bit-banged SPI.
All pins verified unclaimed (`/sys/kernel/debug/gpio`).

| LCM signal | JP4 pin | gpio number (sysfs) |
|---|---|---|
| GND | 6 | - |
| V33 | 1 | - |
| SCLK | 23 | **1** (GPIO0_A1) |
| MOSI | 19 | **3** (GPIO0_A3) |
| (MISO) | 21 | 6 (GPIO0_A6, unused by write-only panels) |
| RESET | 29 | **49** (GPIO1_C1) |
| DC | 33 | **47** (GPIO1_B7) |
| CS | 31 | **48** (GPIO1_C0) |
| BLK | 1 (V33) | tie high = full brightness; or JP4.37 = gpio 45 (GPIO1_B5) with userspace soft-PWM (jitter-prone) |

Setup (GPIO uAPI or sysfs):

```
for g in 1 3 47 48 49; do
  echo $g > /sys/class/gpio/export
  echo out > /sys/class/gpio/gpio$g/direction
done
echo 1 > /sys/class/gpio/gpio49/value   # RES inactive
```

Bit-bang SPI mode 0, MSB first, on gpio 1 (clock) / 3 (data) with CS 48
framing each transaction: sysfs writes give roughly 10-50 kHz - fine for
occasional full-frame updates, too slow for animation. DC must toggle
low before command bytes and high before data bytes.

