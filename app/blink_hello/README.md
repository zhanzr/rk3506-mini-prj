# blink_hello

First real project for the rk3506-mini: hello world + LED2 blinker.

## Behavior

1. Prints `hello world from rk3506-mini`.
2. Saves the current trigger of the on-board **LED2** (`/sys/class/leds/run`,
   normally `heartbeat` - the vendor "system alive" indicator), sets it to
   `none` and blinks it at 1 Hz (0.5 s on / 0.5 s off) via `brightness`.
3. Every blink prints the SoC temperature from
   `/sys/class/thermal/thermal_zone0` (auto-detected; the RK3506G has one
   thermal zone, ~45 C idle).
4. On Ctrl-C / SIGTERM it turns the LED off and **restores the saved
   trigger** (heartbeat comes back), prints `bye`.

## Is stealing the heartbeat LED safe?

Yes (verified). LED2 is a `leds-gpio` device with the `heartbeat` trigger -
purely cosmetic, nothing in the system depends on it. Taking it over stops
the heartbeat pattern while the app runs; the app restores it on exit, and
a reboot restores it regardless.

Polarity note: the kernel DT marks this LED **ACTIVE LOW** (`gpio-24 ...
out lo ACTIVE LOW` in `/sys/kernel/debug/gpio`), so the LED class API
inverts the physical pin for you. The vendor doc's "high active" claim
refers to the raw GPIO level, not `brightness`. Use `/sys/class/leds/run`
and ignore polarity.

Sample output (stop with Ctrl-C):

```
hello world from rk3506-mini (LED2 = /sys/class/leds/run)
led trigger (saved): none mmc0 timer [heartbeat] backlight gpio rfkill-any rfkill-none rfkill0
blink 1: led on  soc temp: 46204
blink 2: led off soc temp: 46204
...
^Crestored trigger: heartbeat
bye
```

## Build and run

Fast development loop (cross-build in WSL, push, run):

```
make push run
```

`CROSS` defaults to `~/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-linux-`;
override with `make ... CROSS=/path/to/bin/arm-linux-`.
