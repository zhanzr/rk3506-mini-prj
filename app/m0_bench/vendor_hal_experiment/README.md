# Vendor-HAL CoreMark experiment (historical)

This is the earlier attempt to integrate CoreMark into the vendor HAL MCU
project. It is not the source code used to build the successful M0 benchmark
in the parent `firmware/` directory. The working standalone firmware, loader,
runner, and the measured result are documented in
[`../README.md`](../README.md) and [`../../../docs/amp_m0_notes.md`](../../../docs/amp_m0_notes.md).

The vendor middleware configuration below does **not fit** the M0's 32 KB
RAM. This does not imply that CoreMark itself cannot fit: the standalone
firmware in the parent directory builds the same CoreMark algorithms with a
minimal startup and a GPIO-based report path.

## What was attempted (local SDK copy, v1.2.0)

`rk3506-mcu-coremark.patch` modifies:

| File | Change |
|---|---|
| `hal/project/rk3506-mcu/src/main.c` | enable `TEST_DEMO` (calls `test_demo()`) |
| `hal/project/rk3506-mcu/src/test_demo.c` | add `PERF_TEST` + `perf_test()` calling `benchmark_main()` (mirrors `rk3562-mcu`) |
| `hal/project/rk3506-mcu/GCC/Makefile` | drop rpmsg-lite/simple_console middleware (RAM), add `-Os`, `-flto`, `nano.specs` |
| `hal/middleware/benchmark/coremark/barebones/core_portme.h` | `ee_f32`: double -> float (M0 has no FPU; soft-double does not fit) |

Build (in WSL, from the SDK):

```
cd <SDK>/hal/project/rk3506-mcu/GCC
make -j4          # uses prebuilts gcc-arm-none-eabi 10.3 (auto-detected)
```

## Size battle (RK3506 M0 RAM is 32 KB, hard limit)

| Configuration | text+data vs 0x7c00 budget | Result |
|---|---|---|
| vendor defaults (-O2, full newlib) | 0x10ab0 (68 KB) | link error |
| -Os | 0xf7a0 (63 KB) | link error |
| + newlib-nano | 0x95c0 (38 KB) | link error |
| + LTO | 0x7da0 (32.2 KB) | link error - **416 bytes over** |
| + float instead of soft-double | 0x7da0 | still over (binding constraint is `.data/.bss` end vs stack base 0x7c00) |

The last 416 bytes could theoretically be shaved (custom minimal printf
instead of newlib-nano, deeper HAL slimming) - not attempted yet. Note
CoreMark's barebones port uses `MEM_STACK` (its data blocks live on the
stack), and the vendor linker script reserves only 1 KB of stack, so even a
successfully linked image would need its memory model re-checked before
trusting any score.

## Vendor boot-path status

- The shipped image has no `amp` partition, U-Boot `rk-amp` support, or
  kernel AMP/RPMsg nodes. These omissions block automatic AMP startup and
  RPMsg, but **do not block** the runtime loader in the parent project.
- The M0-firmware flow instead uses `rockchip_rk3506_g_evb1_mcu_defconfig`:
  `RK_AMP=y`, `RK_AMP_FIT_ITS="amp_mcu.its"`, U-Boot fragment `rk-amp`,
  kernel `rk3506g-evb1-v10-amp` dts, `parameter-128M-amp.txt`.
- Firmware packaging: `mcu.bin` -> FIT (`amp_mcu.its`, `type="standalone"`,
  `load = <0xfff84000>` = M0 bus alias) -> `amp.img` -> `amp` partition ->
  U-Boot releases the M0 (`fit_standalone_release` opens M0 clocks, maps
  shared memory uncacheable, writes the entry address).

## What would be required to continue this vendor-HAL variant

1. Reduce its size enough to link, then re-check `MEM_STACK` sizing vs the
   vendor's 1 KB stack.
2. If using the vendor boot/RPMsg path, build the AMP system image (full SDK
   `./build.sh` with an MCU-AMP defconfig adapted to the Vanxoak board - the
   stock MCU defconfig targets the Rockchip EVB1 dts) and reflash the SD card.
3. Console: the M0 prints to **UART4 @ 1.5 Mbaud** - not routed on the
   HD-RK3506-EVB baseboard (UART4 pins = core-module pins 100/101 =
   LCD_D0/D1, unused here). Either fly-wire them to a USB-TTL adapter, or
   retarget the output to RPMsg (Linux master exposes it as
   `/dev/ttyRPMSG*`, part of the AMP image work anyway).
4. If this path is pursued, independently validate and record its result.

Vendor reference points (from the AMP guide, chapter 10): CoreMark/MHz
4.0 (RK3568 A55 @816 MHz), 2.92 (RK3562 A53 @816 MHz) - M0 columns were
empty in the vendor table; published Cortex-M0 figures are ~2.3 CoreMark/MHz.
