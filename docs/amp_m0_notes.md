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

The local SDK includes the bare-metal HAL project. The MCU RT-Thread BSP
documented by Rockchip is not present in this SDK copy.

| Stack | Project | Toolchain | Build |
|---|---|---|---|
| Bare-metal RK HAL | `hal/project/rk3506-mcu` | `prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4` | `make` in `GCC/` -> `TestDemo.bin/elf` |
| RT-Thread MCU BSP | Not present in this SDK copy | - | Not available here |

The HAL project already includes the benchmark middleware
(`middleware/benchmark`, CoreMark/Linpack/tinymembench); the earlier vendor-HAL
CoreMark integration attempt and its patch are archived in
`app/m0_bench/vendor_hal_experiment/`.

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

## Initial board status (verified 2026-10-30)

At that time the shipped SD image had **no boot-time AMP support**:

- partition table `parameter-evb-sd.txt`: no `amp` partition
  (uboot/misc/boot/recovery/rootfs/oem/userdata only);
- U-Boot built with fragments `rk3506b rk-emmc`, i.e. **without** `rk-amp`;
- running kernel DT: no `rockchip-amp`, no RPMsg/mailbox nodes, no
  `mcu_reserved`.

At that point the M0 was not running, and the image had no normal
boot-time AMP support. This established that the vendor AMP boot path
was unavailable; it did not rule out loading firmware later from Linux.
The AMP image/partition changes below are still needed for vendor-style
boot-time startup, but not for the runtime loader verified later in this
document. `app/m0_bench/vendor_hal_experiment/` records the earlier
vendor-HAL firmware-side work.


## Follow-up investigation (2026-11-03): SDK AMP machinery + build verified

Verified live on the board and in the SDK copy; findings refine the
sections above.

### Image state before the runtime-loader test (re-confirmed 2026-11-03)

7 partitions (uboot/misc/boot/recovery/rootfs/oem/userdata), no `amp`;
no remoteproc class; no `mcu` region in /proc/iomem; no AMP/RPMsg
dmesg lines. The M0 was held in reset at that time; it was subsequently
released by the runtime loader, as recorded below.

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

### MCU firmware build: VERIFIED

With the earlier vendor-HAL CoreMark experiment reverted (that middleware
variant exceeds the 32 KB M0 RAM - recorded in
`app/m0_bench/vendor_hal_experiment/`), the stock
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


## Community project found: nvitya/rk3506-mcu (2026-11-03)

https://github.com/nvitya/rk3506-mcu - a third-party RK3506 CM0 project
with (a) a **remoteproc kernel driver** (`rk3506_rproc.c`) that
loads/starts/stops the M0 on a running Linux and (b) VIHAL-based MCU
firmware examples. Verified against our setup:

- The M0 start sequence it uses matches the vendor flow exactly and is
  the authoritative recipe: load firmware into the SRAM window
  (0xFFF84000), then `arm_smccc_smc(SIP_MCU_CFG, BUSMCU_0,
  CODE_START_ADDR, entry)` - this ATF call re-maps the SRAM as M0 code
  TCM and sets the entry point (without it the M0 cannot fetch code);
  then `CRU_GATE_CON5 (0xff9a0814) <- 0x0c000000` (hclk/swclktck) and
  `PMU_INT_MASK_CON (0xff90000c) <- 0x00060004` (release). Our board's
  ATF supports the call - the vendor U-Boot `fit_standalone_release()`
  (rk3506.c) issues the identical SMC.
- The driver as published needs `CONFIG_REMOTEPROC` (rproc-core ELF
  helpers) which our stock kernel lacks (`# CONFIG_REMOTEPROC is not
  set`). Adaptation for our constraints: make the module self-contained
  - accept a flat `.bin` (userspace runs objcopy), ioremap-copy it into
  the SRAM window, issue the SMC + release itself; instantiate the
  device from module init instead of DT (or a DT node with a boot.img
  rebuild later). Compiled as `.ko`, loaded with insmod - **no U-Boot or
  kernel image rebuild/reflash needed**.
- An earlier proposal assumed `0x03C00000` was an unallocated shared-memory
  bus hole. Live `/proc/iomem` and `/dev/mem` checks showed it is Linux System
  RAM on this image, not reserved for the M0. Do not use the old
  `app/m0_bench/reader/m0_read` DDR poller; the current result path uses
  GPIO1_C0 instead.
- Author-noted limitation: once the M0 is started, the code TCM SRAM is
  no longer A7-writable (no hot-swap) - irrelevant for a
  start-once-benchmark workload.
- Kernel facts checked: `CONFIG_MODULES=y`,
  `CONFIG_HAVE_ARM_SMCCC=y` (module can issue the SMC), 3x A7 visible.

## Re-verified status (2026-11-04; corrects the earlier final verdict)

**A simple bare-metal M0 image can be loaded and run from the current
Linux image without rebuilding or reflashing the board.** This was
verified on the live RK3506 board using the runtime module and GPIO
liveness test in `app/m0_bench/`.

The board still boots an image without AMP integration: the live device
has no `amp` partition, AMP/RPMsg device-tree nodes, or RPMsg devices.
That remains a limitation for automatic boot-time startup and Linux/M0
RPMsg communication, but it does not prevent an administrator from
loading a flat M0 image from Linux at runtime.

### Direct runtime test

After a reboot restored the pre-SMC address map, the test did the
following without writing any boot partition or changing the partition
table:

1. Copied a 124-byte bare-metal GPIO test image to the SRAM window at
   `0xFFF84000`; the loader compared the complete image readback before
   proceeding.
2. Issued `SIP_MCU_CFG` successfully, then applied the vendor U-Boot
   clock-gate sequence and PMU release.
3. Observed GPIO1_C0 (Linux GPIO line 48) transition `0 -> 1 -> 0`.
   Linux reported the line as output, and it continued toggling after
   the loader module returned. This verifies M0 instruction execution,
   not merely successful register writes.

The module log reported `SRAM image readback verified (124 bytes)`,
`SMC ok`, and `M0 released`. The clock-gate register read back as
`0x0000f300` and the PMU register as `0x00000004`. The SD card still had
the original seven partitions (`uboot`, `misc`, `boot`, `recovery`,
`rootfs`, `oem`, `userdata`) and no `amp` partition.

To repeat the test from the repository root, build the image and module,
copy the three files to the board, then run the liveness script as root:

```sh
make -C app/m0_bench/firmware
make -C app/m0_bench/m0_loader
scp app/m0_bench/firmware/m0.bin \
  app/m0_bench/m0_loader/rk3506_m0_loader.ko \
  app/m0_bench/board_run.py <board>:/root/m0bench/
ssh <board> 'cd /root/m0bench && python3 ./board_run.py'
```

The script exports GPIO line 48 for observation if needed. Run the loader
once per boot: after its SMC remaps SRAM, a reboot is required before
loading a different image.

The test firmware is only a GPIO liveness test; **this does not establish
a CoreMark score or an RPMsg channel**. `board_coremark.py`'s old mailbox
protocol was not implemented by the firmware and must not be used to
claim a benchmark result.

### What the earlier write-protection observations mean

The previous conclusion that the SRAM was universally read-only from
Linux was incorrect. The live kernel log contains a loader readback of
the expected image words before the first SMC. Later attempts, in the
same boot after `SIP_MCU_CFG` had remapped SRAM as M0 code TCM, read back
zeros. The SMC changes the A7-visible mapping; those later reads do not
prove that pre-SMC writes were blocked. Likewise, an error from the
separate secure-register-access SMC does not prove that ordinary
`memcpy_toio()` writes to SRAM fail.

The old loader also called `clk_get()` for clocks with no consumer
device/DT links. The live log showed `-ENOENT`, but it ignored those
failures and still printed a success-shaped “firmware running” message.
The corrected loader verifies SRAM contents before the SMC, uses the
vendor U-Boot clock-gate write, and the GPIO test confirms execution.

An earlier DDR result probe at `0x03c00000` was invalid: in the running
non-AMP image that address is inside Linux System RAM, not reserved
shared memory. That does not affect the GPIO liveness result above.

### Practical limits

- The loader must run before the SMC TCM remap. Once the SMC has been
  issued in a boot, the A7 cannot use this same loading sequence to
  replace the M0 image; reboot first to restore the initial mapping.
- The test requires root privileges, a module built for the running
  kernel, and an available GPIO1_C0 test pin. It does not require an
  AMP partition or a kernel/U-Boot image rebuild.
- For automatic startup after every boot, RPMsg integration, or a
  production firmware update path, use the vendor AMP image flow
  described above. Those features are not enabled in the current image.

### Standalone CoreMark runtime test (2026-11-04)

A stripped bare-metal CoreMark build now fits in the M0's 32 KB SRAM and can
use the same Linux runtime loader; the earlier vendor-HAL integration attempt
in `app/m0_bench/vendor_hal_experiment/` did not fit. The standalone CoreMark
ELF has 5,298 bytes of text, 72 bytes of data, and 44 bytes of BSS; its flat
image is 5,372 bytes, leaving ample room in the 32 KB M0 SRAM for the benchmark
stack and working set.

The firmware uses GPIO1_C0 (Linux GPIO line 48) for both the benchmark
start/end markers and a low-speed serial report. `board_coremark.py` measures
the timed section with the A7's monotonic clock, decodes the iteration count
and CRCs from the GPIO stream, checks the known performance-run CRCs, and
rejects results shorter than 10 seconds. This avoids the unreserved
`0x03c00000` DDR address and does not require RPMsg or a routed M0 UART.
It uses CoreMark's 2K performance-run working set. The reported throughput
is iterations/second; no CoreMark/MHz figure is reported because the M0 clock
has not been measured independently. The A7 polls the legacy sysfs GPIO
interface at 2 ms intervals to time GPIO1_C0's benchmark markers; the result
packet is transmitted on the same GPIO at about 42.7 ms per bit.

### Live CoreMark result (2026-11-04)

Ran the standalone image on the Vanxoak RK3506G board after rebooting to
restore the pre-SMC SRAM mapping. No U-Boot, kernel, partition, or board-image
changes were made. The board was reachable at `192.168.5.57` for this run;
that is its DHCP address, not a configured static address.

| Measurement | Result |
|---|---:|
| Board / kernel | Vanxoak RK3506G / Linux `6.1.118` (`#4`) |
| CoreMark performance configuration | 2K, 10,000 iterations |
| A7-measured timed interval | 32.393 s |
| Throughput | 308.71 iterations/s |
| Seed/list/matrix/state CRCs | `e9f5` / `e714` / `1fd7` / `8e3a` |
| Final CRC | `988c` |
| Firmware flat image | 5,372 bytes |
| GPIO report bit period | 42.7 ms |

The runner reported `PASS`, including the expected CRC checks and the
10-second minimum. The loader log confirmed readback of all 5,372 image bytes,
successful `SIP_MCU_CFG`, and M0 release. This is a validated 2K performance
run; it does not provide a CoreMark/MHz figure because the M0 clock is still
unknown. The measured result is from the GPIO serial report and A7 monotonic
clock, not from the unreserved DDR address or a simulated run. The report
packet used version 1 and passed its CRC-8 as well as CoreMark's known
seed/list/matrix/state CRC checks.

The successful invocation was:

```sh
ssh root@192.168.5.57 \
  'cd /root/m0bench/coremark && python3 ./board_coremark.py \
    --firmware /root/m0bench/coremark/m0-coremark.bin \
    --timeout 420 --report-timeout 180'
```

The image had first been staged under `/root/m0bench/coremark/`, while the
runner's default firmware path is `/root/m0bench/m0-coremark.bin`. The first
attempt using that default failed to open the image and did not issue the
SMC; the corrected invocation above passed. The same verified image, loader,
and runner were then copied into `/root/m0bench/` so the documented default
command works after the next reboot. The board was not rebooted after the
successful run; the M0 completed the report and entered its idle loop.

Firmware SHA-256:
`a90133805e8e8e3d67fc25e92e75b4b43f68a9a8c0e0b053996786ffe3e42202`.
The loader module SHA-256 was
`07f78b147dd89b115a8bb9862fe84ba68ee7cc5f4bbe67d90f3595f77dc785da`.
The runner SHA-256 was
`e14a88dd6c058df9986e715700acaf47add705f8c15f17f163c08840431c2a74`.

Build and run:

```sh
make -C app/m0_bench/firmware
ssh <board> 'mkdir -p /root/m0bench'
scp app/m0_bench/firmware/m0-coremark.bin \
  app/m0_bench/m0_loader/rk3506_m0_loader.ko \
  app/m0_bench/board_coremark.py <board>:/root/m0bench/
ssh <board> 'cd /root/m0bench && python3 ./board_coremark.py'
```

This needs no board image rebuild or reflash. The loader can issue the
SRAM-remapping SMC only once per boot; reboot before each new firmware load,
including repeat CoreMark runs. On a fresh boot, the command loads the
firmware and prints the elapsed time, throughput, and validation CRCs. The
CoreMark GPIO report takes several additional seconds after the timed run.
If the run is shorter than 10 seconds, increase the workload and rebuild:
`make -C app/m0_bench/firmware clean && make -C app/m0_bench/firmware COREMARK_ITERATIONS=20000`.
