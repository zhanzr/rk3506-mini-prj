#!/usr/bin/env python3
"""Load the M0 CoreMark image, time it on the A7, and decode its GPIO report."""
import argparse
import os
import struct
import subprocess
import sys
import time

GPIO_NUMBER = "48"
GPIO_VALUE = "/sys/class/gpio/gpio48/value"
DEFAULT_FIRMWARE = "/root/m0bench/m0-coremark.bin"
DEFAULT_MODULE = "./rk3506_m0_loader.ko"
REPORT_LENGTH = 19
SYNC_UNITS = 16
POLL_SECONDS = 0.002
EXPECTED_CRCS = {
    "seed": 0xE9F5,
    "list": 0xE714,
    "matrix": 0x1FD7,
    "state": 0x8E3A,
}


class GpioValue:
    def __init__(self):
        self.file = open(GPIO_VALUE, encoding="ascii")

    def read(self):
        self.file.seek(0)
        value = self.file.read().strip()
        if value not in ("0", "1"):
            raise RuntimeError(f"unexpected GPIO value: {value!r}")
        return int(value)

    def close(self):
        self.file.close()


def ensure_gpio_exported():
    if not os.path.exists(GPIO_VALUE):
        try:
            with open("/sys/class/gpio/export", "w", encoding="ascii") as export:
                export.write(GPIO_NUMBER)
        except OSError as exc:
            if not os.path.exists(GPIO_VALUE):
                sys.exit(f"Cannot export GPIO1_C0: {exc}")


def wait_for_level(gpio, target, deadline, description):
    while time.monotonic() < deadline:
        if gpio.read() == target:
            return time.monotonic()
        time.sleep(POLL_SECONDS)
    raise TimeoutError(f"timed out waiting for GPIO1_C0 {description}")


def sample_at(gpio, deadline):
    remaining = deadline - time.monotonic()
    if remaining > 0:
        time.sleep(remaining)
    return gpio.read()


def read_uart_byte(gpio, start, bit_seconds):
    if sample_at(gpio, start + 0.5 * bit_seconds) != 1:
        raise RuntimeError("invalid GPIO report start bit")

    value = 0
    for bit in range(8):
        if sample_at(gpio, start + (1.5 + bit) * bit_seconds):
            value |= 1 << bit

    if sample_at(gpio, start + 9.5 * bit_seconds) != 0:
        raise RuntimeError("invalid GPIO report stop bit")
    return value


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def receive_report(gpio, timeout):
    deadline = time.monotonic() + timeout
    sync_start = wait_for_level(gpio, 1, deadline, "result sync")
    sync_end = wait_for_level(gpio, 0, deadline, "end of result sync")
    bit_seconds = (sync_end - sync_start) / SYNC_UNITS
    if not 0.005 <= bit_seconds <= 1.0:
        raise RuntimeError(f"implausible report bit period: {bit_seconds:.4f}s")

    packet = bytearray()
    for index in range(REPORT_LENGTH):
        byte_start = wait_for_level(
            gpio, 1, time.monotonic() + 12 * bit_seconds, "report byte"
        )
        packet.append(read_uart_byte(gpio, byte_start, bit_seconds))
        if index + 1 < REPORT_LENGTH:
            wait_for_level(
                gpio, 0, time.monotonic() + 2 * bit_seconds, "UART stop bit"
            )
    return bytes(packet), bit_seconds


def decode_report(packet):
    if len(packet) != REPORT_LENGTH:
        raise RuntimeError(f"wrong report length: {len(packet)}")
    if packet[:3] != b"M0\x01":
        raise RuntimeError(f"unknown M0 report header: {packet[:3].hex()}")
    if packet[3] not in (0, 1):
        raise RuntimeError(f"invalid M0 validation status: {packet[3]}")
    if crc8(packet[:-1]) != packet[-1]:
        raise RuntimeError("M0 report checksum mismatch")

    iterations = struct.unpack_from("<I", packet, 4)[0]
    seed_crc, list_crc, matrix_crc, state_crc, final_crc = struct.unpack_from(
        "<HHHHH", packet, 8
    )
    return {
        "valid": bool(packet[3]),
        "iterations": iterations,
        "seed_crc": seed_crc,
        "list_crc": list_crc,
        "matrix_crc": matrix_crc,
        "state_crc": state_crc,
        "final_crc": final_crc,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", default=DEFAULT_FIRMWARE)
    parser.add_argument("--module", default=DEFAULT_MODULE)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--report-timeout", type=float, default=120.0)
    parser.add_argument("--minimum-seconds", type=float, default=10.0)
    args = parser.parse_args()

    ensure_gpio_exported()
    try:
        gpio = GpioValue()
    except OSError as exc:
        sys.exit(f"GPIO1_C0 is unavailable: {exc}")

    try:
        initial = gpio.read()
        print(f"GPIO1_C0 initial level: {initial}")
        subprocess.run(["insmod", args.module, f"fw={args.firmware}"], check=True)
        start = wait_for_level(
            gpio, 1, time.monotonic() + 10.0, "CoreMark start marker"
        )
        end = wait_for_level(
            gpio, 0, start + args.timeout, "CoreMark completion marker"
        )
        elapsed = end - start

        packet, bit_seconds = receive_report(gpio, args.report_timeout)
        report = decode_report(packet)

        print(f"CoreMark elapsed (A7 wall clock): {elapsed:.3f} s")
        print(f"CoreMark iterations: {report['iterations']}")
        print(
            "CoreMark 1.0 score (A7-timed): "
            f"{report['iterations'] / elapsed:.2f} iterations/s"
        )
        print(f"GPIO report bit period: {bit_seconds * 1000:.1f} ms")
        print(
            "CRCs: seed=0x{seed_crc:04x} list=0x{list_crc:04x} "
            "matrix=0x{matrix_crc:04x} state=0x{state_crc:04x} "
            "final=0x{final_crc:04x}".format(**report)
        )

        failures = []
        if not report["valid"]:
            failures.append("M0 CoreMark validation failed")
        for name, expected in EXPECTED_CRCS.items():
            if report[f"{name}_crc"] != expected:
                failures.append(
                    f"{name} CRC 0x{report[f'{name}_crc']:04x}, expected 0x{expected:04x}"
                )
        if report["iterations"] == 0:
            failures.append("firmware reported zero iterations")
        if elapsed < args.minimum_seconds:
            failures.append(
                f"run was shorter than the {args.minimum_seconds:.1f}s minimum"
            )
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            return 1

        print(
            "PASS: CoreMark CRCs validated and the timed run meets the "
            "10-second minimum"
        )
        return 0
    except (OSError, RuntimeError, TimeoutError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        gpio.close()


if __name__ == "__main__":
    sys.exit(main())
