# blink_hello

First project for the rk3506-mini: reads the SoC temperature and prints
it in degrees Celsius.

## Behavior

1. Prints `rk3506-mini SoC temperature (TS-ADC via /sys/class/thermal)`.
2. Every second reads the SoC temperature from the kernel thermal zone
   (`thermal_zone0`, type `soc-thermal`) and prints it in **°C** with
   one decimal (the sysfs value is millidegrees; the app divides by
   1000).
3. Stops on Ctrl-C / SIGTERM with `bye`.

The temperature source is the **TS-ADC** (`tsadc@ff650000`,
compatible `rockchip,rk3506-tsadc`) - the datasheet's "Temperature
Sensor (TS-ADC), one sensor, -40..125 C, +/-5 C accuracy". The 50 KS/s
sampling rate is the hardware capability; through the kernel thermal
framework the readable rate is ~1 Hz (the driver sweeps the sensor
automatically and the framework exposes the latest conversion). The
device tree sets a 120 C hardware-shutdown trip (`hw-tshut-temp`).

No LED handling: the app never touches `/sys/class/leds/run`, so the
heartbeat indicator keeps running untouched.

Sample output (stop with Ctrl-C):

```
rk3506-mini SoC temperature (TS-ADC via /sys/class/thermal)
soc temp: 42.8 C
soc temp: 42.8 C
...
^Cbye
```

## Build and run

Fast development loop (cross-build in WSL, push, run):

```
make push run
```

`CROSS` defaults to
`~/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-linux-`;
override with `make ... CROSS=/path/to/bin/arm-linux-`.
