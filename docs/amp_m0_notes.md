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


## Follow-up investigation (2026-11-03): SDK AMP machinery + build verified

Verified live on the board and in the SDK copy; findings refine the
sections above.

### Current image state (re-confirmed unchanged)

7 partitions (uboot/misc/boot/recovery/rootfs/oem/userdata), no `amp`;
no remoteproc class; no `mcu` region in /proc/iomem; no AMP/RPMsg
dmesg lines. The M0 is still held in reset.

### SDK AMP build machinery (all present in ~/sdk)

| Piece | Location | Notes |
|---|---|---|
| AMP chip defconfigs | `device/rockchip/.chips/rk3506/rockchip_rk3506_g_evb1_amp_defconfig` (Linux+M0) and `..._evb1_mcu_defconfig` | set `RK_AMP=y`, `RK_UBOOT_CFG_FRAGMENTS="rk-amp"`, amp ITS + amp parameter file |
| AMP build hook | `device/rockchip/common/build-hooks/25-amp.sh` | builds per-core HAL firmware (`hal/project/<target>/GCC` -> `TestDemo.bin` -> `mcu.bin`), packs `$RK_FIRMWARE_DIR/amp.img` via `rtos/tools/mkimage -f amp.its` |
| U-Boot AMP config | `u-boot/configs/rk-amp.config` = `CONFIG_AMP` + `CONFIG_ROCKCHIP_AMP`; also `rk3506-amp.config` | the missing piece in our U-Boot |
| Kernel AMP fragment | `kernel-6.1/arch/arm/configs/rockchip_amp.config` | MAILBOX, ROCKCHIP_MBOX, ROCKCHIP_AMP, RPMSG_VIRTIO; **`CONFIG_RPMSG_TTY` is not set** - add it for `/dev/ttyRPMSG0` |
| Kernel AMP dtsi | `kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi` | reserved mem, mailbox/rpmsg nodes, amp-irqs |
| AMP partition example | `parameter-128M-amp.txt`: `0x00002000@0x00039a00(amp)` (8 MB between oem and userdata) | our `parameter-evb-sd.txt` has no amp entry |
| MCU HAL project | `hal/project/rk3506-mcu` (GCC/, src/, `gcc_bus_m0.ld`) | TestDemo demos selectable by #define: TEST_DEMO, GPIO_TEST, RPMSG_LINUX_TEST, SYSTICK/TIMER tests, PERF_TEST (benchmark) |
| MCU toolchain | `prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux` | present |

RT-Thread note: the MCU RT-Thread BSP (`rtos/bsp/rockchip/rk3506-mcu`)
is **absent** from this SDK copy (only `rk3506-32` for the AP side) - so
the **HAL bare-metal stack is the only MCU path available here**.

### MCU firmware build: VERIFIED

With the CoreMark experiment reverted (the coremark middleware variant
overflows the 32 KB M0 RAM - recorded in `app/coremark_cm0/`), the stock
HAL TestDemo builds cleanly:

    export PATH=$SDK/prebuilts/gcc/linux-x86/arm/\
      gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin:$PATH
    cd $SDK/hal/project/rk3506-mcu/GCC && make -j4

`TestDemo.bin` = **19,968 bytes - fits the 32 KB M0 RAM**.

### Blockers on this board (all software, all fixable)

1. **No `amp` partition** - edit `parameter-evb-sd.txt` mtdparts to
   insert an amp region before userdata (shifts only userdata content),
   then rewrite the SD partition table + write `amp.img`.
2. **U-Boot lacks AMP** - add `rk-amp` to `RK_UBOOT_CFG_FRAGMENTS` in
   `device/rockchip/.chips/rk3506/rockchip_rk3506_g_evb_sd_defconfig`,
   `./build.sh uboot`, flash the uboot partition.
3. **Kernel lacks AMP nodes/config** - include `rk3506-amp.dtsi` in the
   Vanxoak DTS (or cherry-pick its nodes), add a kernel fragment with
   the rockchip_amp.config contents + `CONFIG_RPMSG_TTY=y`, rebuild
   boot.img (established flow), flash boot partition.
4. **FIT signature block** - `amp_mcu.its` signs loadables with
   key-name-hint "dev" but no dev.key exists in the tree, and U-Boot
   does not enforce FIT signature verification (no FIT_SIGNATURE in the
   rk3506 uboot configs) - strip the `signature { }` section from a
   local copy of the its before packing.
5. **No M0 console on this EVB** - UART4 (GPIO1_C2/C3, core pins
   100/101) is not routed to any header. M0 output must go through
   **RPMsg** (build TestDemo with `RPMSG_LINUX_TEST`, read
   `/dev/ttyRPMSG0` on Linux) or fly-wire the core-module pins.
   Alternatively adapt the demo to drive a visible GPIO (e.g. LED2 on
   GPIO0_D0) - the M0 can reach the same GRF/GPIO controllers.

### Minimal run recipe (once 1-3 are in place)

1. Build MCU firmware (command above); enable `RPMSG_LINUX_TEST` in
   `src/test_demo.c` for an RPMsg ping demo
2. Pack: copy `amp_mcu.its` (signature section removed) next to
   `mcu.bin`, run `rtos/tools/mkimage -f amp_mcu.its amp.img`
3. Repartition SD (amp before userdata), write amp.img to it
4. Reboot - U-Boot loads/releases the M0; Linux shows
   `/dev/ttyRPMSG0`; `echo hi > /dev/ttyRPMSG0` round-trips to the M0
