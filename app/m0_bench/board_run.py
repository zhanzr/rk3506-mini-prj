#!/usr/bin/env python3
"""Load the M0 test image and require GPIO1_C0 to toggle."""
import os
import subprocess
import sys
import time

GPIO_NUMBER = "48"
GPIO_VALUE = "/sys/class/gpio/gpio48/value"
FIRMWARE = sys.argv[1] if len(sys.argv) > 1 else "/root/m0bench/m0.bin"
MODULE = sys.argv[2] if len(sys.argv) > 2 else "./rk3506_m0_loader.ko"


if not os.path.exists(GPIO_VALUE):
    try:
        with open("/sys/class/gpio/export", "w", encoding="ascii") as export:
            export.write(GPIO_NUMBER)
    except OSError as exc:
        if not os.path.exists(GPIO_VALUE):
            sys.exit(f"Cannot export GPIO1_C0: {exc}")


def read_gpio():
    with open(GPIO_VALUE, encoding="ascii") as gpio:
        value = gpio.read().strip()
    if value not in ("0", "1"):
        raise RuntimeError(f"unexpected GPIO value: {value!r}")
    return value


try:
    initial = read_gpio()
except OSError as exc:
    sys.exit(f"GPIO test line {GPIO_VALUE} is unavailable: {exc}")

print(f"GPIO1_C0 initial level: {initial}")
subprocess.run(["insmod", MODULE, f"fw={FIRMWARE}"], check=True)
print("Loader returned; waiting for at least two GPIO transitions")

last = initial
transitions = 0
deadline = time.monotonic() + 15
while time.monotonic() < deadline and transitions < 2:
    current = read_gpio()
    if current != last:
        transitions += 1
        last = current
        print(f"GPIO1_C0 transition {transitions}: {current}")
    time.sleep(0.02)

if transitions < 2:
    sys.exit("FAIL: M0 GPIO test did not produce two transitions")

print("PASS: observed GPIO1_C0 toggling; M0 firmware is executing")
