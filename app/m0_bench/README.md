# RK3506 Cortex-M0 runtime and CoreMark

This directory contains the working M0 firmware, Linux loader, host-side GPIO
runner, and the historical vendor-HAL experiment. The code used for the live
CoreMark run is in `firmware/`; [`vendor_hal_experiment/`](vendor_hal_experiment/)
contains a separate SDK patch and notes and is **not** compiled by this build.

## What builds the measured firmware

`firmware/Makefile` builds `m0-coremark.bin` from:

- `firmware/startup.c` and `firmware/lnk.ld` for the bare-metal entry and SRAM
  layout;
- `firmware/coremark_main.c`, `firmware/core_portme.c`, and
  `firmware/core_portme.h` for GPIO reporting and the RK3506 port; and
- the CoreMark algorithm sources in `firmware/coremark/`.

The other `firmware/main.c` target builds the GPIO liveness image `m0.bin`.
The runtime loader is in `m0_loader/`; `board_coremark.py` measures the
benchmark interval from Linux and receives the result over GPIO1_C0.

## Verified live result

On the Vanxoak RK3506G board, CoreMark's 2K performance configuration
completed 10,000 iterations in **32.393 s**, or **308.71 iterations/s**.
The expected seed/list/matrix/state CRCs were `e9f5`, `e714`, `1fd7`, and
`8e3a`; the final CRC was `988c`. The flat image was 5,372 bytes. The full
loader readback, SMC, M0 release, GPIO report, and CRC check succeeded without
rebuilding or reflashing the board.

Timing comes from the Linux A7 monotonic clock around M0 GPIO markers, not an
on-M0 timer. This is a validated, host-timed result, not an EEMBC-certified
submission; the M0 clock is unknown, so no CoreMark/MHz number is claimed.
The measurement and current image limitations are documented in
[`../../docs/amp_m0_notes.md`](../../docs/amp_m0_notes.md).

## Build and run

On the host:

```sh
make -C app/m0_bench/firmware
make -C app/m0_bench/m0_loader
scp app/m0_bench/firmware/m0-coremark.bin \
  app/m0_bench/m0_loader/rk3506_m0_loader.ko \
  app/m0_bench/board_coremark.py <board>:/root/m0bench/
```

On the board, as root:

```sh
cd /root/m0bench
python3 ./board_coremark.py
```

The loader can run only once per boot because the SMC remaps SRAM as M0 TCM.
Reboot before each new firmware load or repeated benchmark run. The current
image has no automatic AMP startup or RPMsg support; neither is required for
this runtime test.
