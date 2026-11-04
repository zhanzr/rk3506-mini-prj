# Device tree and ADC notes (verified)

How the vendor kernel describes this board, and what the ADC hardware is
wired to. Verified against: the running system (`/proc/device-tree`,
sysfs), the vendor SDK sources
(`rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/arch/arm/boot/dts/`), the
baseboard schematic `SCH_HD-RK3506-EVB-V1.1.pdf`, and live IIO readings.

## DTS file structure (vendor SDK, kernel 6.1)

The SD image is built from
`vanxoak-hd-rk3506g-evb-sd-v1.dts`:

```
vanxoak-hd-rk3506g-evb-sd-v1.dts
├── #include "rk3506.dtsi"                        (SoC: CPU, memory map)
├── #include "vanxoak-hd-rk3506-evb-v1.dtsi"      (baseboard: ETH, keys, etc.)
├── #include "vanxoak-hd-rk3506-evb-mipi-1024x600.dtsi"   (active display)
│      (alternatives commented out: rgb-800x480, rgb-1024x600)
├── model = "Vanxoak HD-RK3506-EVM V10 Board"
├── leds: heart_led: label="run", gpios=<&gpio0 RK_PD0 GPIO_ACTIVE_LOW>,
│         linux,default-trigger="heartbeat"        <- our LED2
├── &fspi, &tsadc, &saradc: status="okay"          (saradc vref = vcc_1v8)
└── uarts 1/3/4/... pinctrl via the rm_io matrix
```

Notes:

- `rk3506.dtsi` itself does not define the peripherals - the SoC base is
  shared with RK3502: **`rk3502.dtsi`** holds the actual nodes (SARADC,
  TSADC, OTP, ...). RK3506-specific compatibles are added per node, e.g.
  `saradc: adc@ff4e8000 { compatible = "rockchip,rk3506-saradc",
  "rockchip,rk3562-saradc"; }`.
- RM_IO matrix: RK3506 has a 32-pin IO matrix (`RM_IOn`), each mappable to
  any of 99 "function IDs"; the pinctrl dtsi files
  (`rk3506-pinctrl.dtsi`, `rk3506-pinctrl-rmio.dtsi`) express this. The
  UART pinctrl in the board dts references `rm_io16_uart1_rx` etc.
  (core-module pin list CSV documents the function-ID table).
- RAM: only ~98 MB visible (small DDR); the `reserved-memory` node carves
  a 1 MB `drm-logo@400000` framebuffer logo area.
- Cortex-M0/AMP: separate `-amp.dts` variants exist
  (`rk3506g-evb1-v10-amp.dts`, `rk3506-amp.dtsi`) for the RT-Thread side -
  not used by the stock SD image.

## Temperature sensor: TSADC (die only)

- Node: `tsadc: tsadc@ff650000`,
  `compatible = "rockchip,rk3506-tsadc"`,
  `#thermal-sensor-cells = <1>`,
  trim data from OTP (`cpu_tsadc_trim_l/h`),
  `rockchip,hw-tshut-temp = <120000>` (120 C hardware shutdown).

- Kernel usage (verified 2026-11-04): `/sys/class/thermal/thermal_zone0`
  (type `soc-thermal`) is driven by this TSADC - it is the temperature
  source used by `app/blink_hello`. The datasheet's 50 KS/s is the
  hardware sampling capability; through the thermal framework the
  readable rate is ~1 Hz (the driver sweeps the sensor automatically
  and exposes the latest conversion; `blink_hello` reads it every
  second). Datasheet accuracy: +/-5 C over -40..125 C.
- Userspace: `/sys/class/thermal/thermal_zone0`, `type = soc-thermal`,
  `temp` in millidegrees C (~45000 idle). This is what `blink_hello`
  reads. Die sensor only - not usable for external signals.

## SARADC (the "real" ADC)

- Node: `saradc: adc@ff4e8000` (rk3502.dtsi), board enables it with
  `&saradc { vref-supply = <&vcc_1v8>; status = "okay"; }`.
- Userspace: `/sys/bus/iio/devices/iio:device0`, `name = ff4e8000.adc`,
  channels `in_voltage0_raw` .. `in_voltage7_raw`,
  `in_voltage_scale = 1.7578125` mV/LSB -> 10-bit, full scale 1.8 V.

### Channel wiring on HD-RK3506-EVB V1.1 (schematic page 02-BUS, 11-EXPAN)

| Channel | Core-module pin | Net | Baseboard wiring | Idle raw |
|---|---|---|---|---|
| IN0 | 48 | `SARADC_IN0_BOOT` | MASKROM jumper **H1** (short -> GND = maskrom boot) | ~1018 (~1.79 V) |
| IN1 | 47 | `SARADC_IN1_RECOVER` | RECOVER jumper **H2** (short -> GND = loader mode) | ~1019 (~1.79 V) |
| IN2 | 49 | `SARADC_IN2` | **unconnected** (no connector, no test point) | ~500-544 (float) |
| IN3 | 50 | `SARADC_IN3` | **unconnected** | ~500-544 (float) |
| IN4-7 | not bonded | - | - | 0 |

- The high idle readings on IN0/IN1 are the released-key state (internal
  pull-up / divider), pressing the jumper pulls the channel to GND - that
  is how the ROM/bootloader detects MASKROM/Loader mode.
- **JP4 (40-pin expansion) has no ADC signals**; neither does FPC1/FPC2.
- The internal audio codec (`ACODEC_ADC_INP/INN`, core pins 52/53) is
  also unconnected on this baseboard.

### Practical consequence

No external analog input is available on this board. For analog sensing,
attach an I2C/SPI ADC to JP4 (it exposes I2C0, I2C1, SPI0, CAN0/1, UART1-3,
SPI... see `README.md` pin tables). The SARADC channels can still be read
from userspace for experiments (e.g. watching IN0/IN1 while toggling the
jumpers), with root:

```
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw
```

## CPU clocks (verified, relevant for benchmarks)

- 3x Cortex-A7 (`CPU part 0xc07`), `armclk` fixed at **1.2 GHz**
  (`/sys/kernel/debug/clk/clk_summary`), **no cpufreq** driver - no
  governor, frequency does not change under load. Good benchmark
  repeatability.
- All three cores share the single `armclk` (each `cpu@f00/f01/f02` node
  references `<&cru ARMCLK>`); empirically confirmed: identical fixed
  busy-loop timings per core (30.97 / 32.12 / 32.90 s for 400k shell
  iterations on cpu0/1/2, ~6 % shell-jitter spread, same clock).
- The datasheet's "up to 1.5 GHz" is not reachable on this firmware: the
  vendor DTS never enables the PWM behind `vdd_arm` (the CPU regulator),
  so the kernel's DVFS stack fails at boot and the clock stays at the
  U-Boot value. Full analysis and fix sketch: `cpu_clock_dvfs.md`.
- BogoMIPS 48.00 (typical for A7 loop calibration, not a perf metric).
- `taskset` available on the image for pinning.
