# Programming the RK3506 Cortex-M0 (AMP) - investigation notes

How the M0 fits into this SoC and what it takes to run code on it.
Verified against the local SDK (v1.2.0), the AMP developer guide
(`docs/cn/Common/AMP/Rockchip_Developer_Guide_AMP_CN.pdf` in the SDK) and
the running board.

## Architecture

- RK3506 = AP (3x Cortex-A7) + MCU (1x Cortex-M0). Rockchip "AMP" =
  asymmetric multi-processing: each core runs an independent system
  (Linux / RT-Thread RTOS / bare-metal HAL).
- M0 memory: 32 KB dedicated RAM, addressed by the M0 from 0x00000000
  (`gcc_bus_m0.ld`), loaded by U-Boot at bus alias `0xfff84000`
  (`amp_mcu.its`: `load = <0xfff84000>`, kernel DT `mcu_reserved@fff80000`,
  48 KB).
- M0 console in the vendor bare-metal project: **UART4 @ 1.5 Mbaud**
  (RMIO-mapped GPIO1_C2/C3). On the HD-RK3506-EVB baseboard UART4 is **not
  routed anywhere** (core-module pins 100/101 = LCD_D0/D1, unused) - an M0
  app needs RPMsg output or fly-wired pins on this board.
- Inter-core comms: mailbox IRQ + shared memory, standard RPMsg /
  RPMsg-Lite on top. AMP shared/RPMsg memory for RK3506: 0x03b00000 /
  0x03c00000 (kernel amp dtsi reserved nodes).

## Firmware build paths (vendor SDK)

Two supported stacks for the M0, both in the local SDK:

| Stack | Project | Toolchain | Build |
|---|---|---|---|
| Bare-metal RK HAL | `hal/project/rk3506-mcu` | `prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4` | `make` in `GCC/` -> `TestDemo.bin/elf` |
| RT-Thread | `rtos/bsp/rockchip/rk3506-mcu` | same | `make` (SCons for AP targets only) |

The HAL project already includes the benchmark middleware
(`middleware/benchmark`, CoreMark/Linpack/tinymembench; see
`app/coremark_cm0/` for the CoreMark port attempt).

Packaging: `mcu.bin` is wrapped into a FIT image (`device/rockchip/rk3506/
amp_mcu.its`, `type = "standalone"`, `load = <0xfff84000>`, sha256) ->
`amp.img`, placed in the `amp` partition. At boot, U-Boot (with
`CONFIG_ROCKCHIP_AMP`) loads it, opens the M0 clocks, marks the shared
memory uncacheable and releases the M0 via
`fit_standalone_release()` (`u-boot/arch/arm/mach-rockchip/rk3506/...`).
There is also an SPL flow (MCU firmware inside `uboot.img`, released by
SPL) for fast-boot products.

## Kernel-side resources (AMP image only)

`kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi`:
`rockchip-amp` node (M0 clocks, UART4 pinctrl, IRQ routing via
`amp-irqs`), `rpmsg@3c00000` (mailbox0/mailbox2, link-id 0x02), reserved
memory (`amp-shmem@3b00000`, `rpmsg@3c00000`, `rpmsg-dma@3d00000`,
`mcu@fff80000`). The stock AP-AMP variant additionally deletes
`cpu@f02` from Linux (CPU2 runs the RTOS) - for pure AP+MCU setups that
deletion is not required.

Userspace on the Linux side then sees RPMsg devices (e.g.
`/dev/ttyRPMSG0` with `CONFIG_RPMSG_TTY`), and the SDK ships an
`amp_monitor` LVGL app (`app/lvgl_demo/amp_monitor`) that displays the
remote-core state.

## Current status on this board (verified 2026-10-30)

The shipped SD image has **no M0 support enabled**:

- partition table `parameter-evb-sd.txt`: no `amp` partition
  (uboot/misc/boot/recovery/rootfs/oem/userdata only);
- U-Boot built with fragments `rk3506b rk-emmc`, i.e. **without** `rk-amp`;
- running kernel DT: no `rockchip-amp`, no RPMsg/mailbox nodes, no
  `mcu_reserved`.

The M0 is held in reset. To run anything on it, a full AMP system image
must be built (SDK `./build.sh`, MCU defconfig e.g.
`rockchip_rk3506_g_evb1_mcu_defconfig` - which targets the Rockchip EVB1
dts and would need adaptation to the Vanxoak board files) and the SD card
reflashed. `app/coremark_cm0/` records the firmware-side work done so far.
