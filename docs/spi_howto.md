# Hardware SPI on JP4 - how it was made to work (as-built)

This documents the complete path from a **stock vendor image with no
usable SPI** to **40 MHz hardware SPI0 driving LCD panels from JP4**,
plus everything learned about the userspace side (modes, byte order,
chunking, DC framing, panel quirks). Pin-level wiring analysis and the
alternatives live in `jp4_spi_lcm.md`; this file records what was
actually deployed and why each piece is the way it is.

Current state (verified on hardware):

| What | Status |
|---|---|
| `/dev/spidev0.0` | live, SPI0 @ up to 40 MHz measured, mode 0/3 both work |
| Backlight PWM | `pwm1_8ch_0` on JP4.15, 200 kHz, sysfs-controlled |
| Apps | `app/ili9163c_md144_128x128_spi` (60 fps), `app/st7789_md169_240x280_spi` (22 fps full-frame), `app/st7789_md130_240x240` |

## The three obstacles

Driving a SPI LCD from JP4 required clearing three independent hurdles:

1. **The SoC never routed SPI0 to the pins.** On the RK3506 most JP4
   pins are RM_IO pins (`rm_io0..19` = GPIO0_A0..C3) whose peripheral
   functions are selected by a **separate RM_IO mux** (`grf_pmu`,
   `rockchip_set_rmio()` in the pinctrl driver), not the usual IOMUX
   nibbles. The vendor DTS enables neither SPI0 nor any `pwm1` channel,
   so the pins sit in GPIO mode. This is *not* fixable from userspace -
   it needs a DTB rebuild.
2. **The kernel had no SPI driver at all.** The vendor defconfig
   (`vanxoak_hd_rk3506g_evb_sd_defconfig`) does not set
   `CONFIG_SPI_ROCKCHIP` / `CONFIG_SPI_SPIDEV` - even with a DTB that
   enables spi0 there would be no `/dev/spidev*`. Needs a kernel
   rebuild.
3. **spidev is a byte pipe, not an LCD driver.** The panel protocol
   (DC line, command vs data framing, RGB565 byte order, chunk limits,
   SPI mode, CS behavior) is entirely a userspace problem.

## Step 1 - DTB: route SPI0 + PWM to JP4 pins

Edited `kernel-6.1/arch/arm/boot/dts/vanxoak-hd-rk3506g-evb-sd-v1.dts`
(as-built diff):

```dts
/* rk3506-mini JP4 expansion: SPI0 + LCM backlight PWM (added for LCM work).
 * CAN1 disabled: its default pin (GPIO0_B5) is reused as the PWM. */
&spi0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&rm_io1_spi0_clk &rm_io3_spi0_mosi
	             &rm_io6_spi0_miso &rm_io7_spi0_csn0>;
	spidev@0 {
		compatible = "rockchip,spidev";
		reg = <0>;
		spi-max-frequency = <50000000>;
	};
};

&pwm1_8ch_0 {
	status = "okay";
	pinctrl-names = "active";
	pinctrl-0 = <&rm_io13_pwm1_ch0>;
	assigned-clocks = <&cru CLK_PWM1>;
	assigned-clock-rates = <100000000>;
};

&can1 {
	status = "disabled";
};
```

Resulting JP4 pin map (all pinctrl nodes exist in
`rk3506-pinctrl-rmio.dtsi`):

| JP4 pin | RM_IO | Function | Panel signal |
|---|---|---|---|
| 23 | rm_io1 | `spi0_clk` | SCLK |
| 19 | rm_io3 | `spi0_mosi` | MOSI |
| 21 | rm_io6 | `spi0_miso` | MISO (unused by write-only panels) |
| 24 | rm_io7 | `spi0_csn0` | CS (hardware) |
| 33 | - | GPIO1_B7 (gpio 47), plain GPIO | DC |
| 29 | - | GPIO1_C1 (gpio 49), plain GPIO | RESET |
| 15 | rm_io13 | `pwm1_ch0` | BLK |

Why CAN1 had to go: its TX pin (GPIO0_B5 / rm_io13) is the only free
RM_IO pin left on JP4, and hardware PWM needs an RM_IO pin (bank1 pins
on JP4 have no RM_IO, hence no PWM there at all). If CAN1 is ever
needed again, the fallback is BLK tied to 3.3 V or a bank1 GPIO with
software PWM.

Bank0 GPIO/GRF facts established while diagnosing this (live-verified
with `app/gpio_test`): the GPIO0 IOMUX nibbles in `grf_pmu`
(0xff910000, pins 0-7 -> reg 0x0, 8-15 -> 0x8, 16-23 -> 0x10, 4
bits/pin) are already 0 (GPIO mode) on the stock DTB, and the RM_IO
mux overrides them when `rockchip_set_rmio()` assigns a peripheral.
Writing IOMUX nibbles by hand from /dev/mem is possible but was not
needed once the DTB was rebuilt properly.

## Step 2 - kernel: SPI driver config fragment

Rockchip SDK mechanism: `device/rockchip/.chips/rk3506/rockchip_rk3506_g_evb_sd_defconfig`
lists kernel config fragments that get merged into the defconfig at
`make ..._defconfig` time:

```
RK_KERNEL_CFG="vanxoak_hd_rk3506g_evb_sd_defconfig"
RK_KERNEL_CFG_FRAGMENTS="rk3506-display.config rk3506-usb-otg.config rk3506-spi.config"
```

New file `kernel-6.1/arch/arm/configs/rk3506-spi.config`:

```
CONFIG_SPI=y
CONFIG_SPI_MASTER=y
CONFIG_SPI_ROCKCHIP=y
CONFIG_SPI_SPIDEV=y
```

Gotchas learned here:

- Fragment files must live in `arch/arm/configs/` **and** the names in
  `RK_KERNEL_CFG_FRAGMENTS` must match exactly; `mk-kernel.sh` passes
  the defconfig + fragments straight to kbuild
  (`make <defconfig> <frag1> <frag2>...`), which merges them. A
  mismatched name is silently ignored - verify the merge in the kernel
  build log, not just the file's existence.
- `CONFIG_SPIDEV_COMPAT` naming: the DT node uses
  `compatible = "rockchip,spidev"` because the upstream `"spidev"`
  compatible is rejected by the driver with a warning; the rockchip
  spelling is what vendor kernels expect.

## Step 3 - rebuild boot.img and deploy

The image is a FIT (`RK_USE_FIT_IMG`, `zboot.its`); `boot.img` in
`output/firmware/` is a symlink to `kernel-6.1/zboot.img`. Full
instructions in `docs/spi_sdk_device_tree_guide.pdf` (vendor); the
short version:

```bash
cd ~/sdk
./build.sh kernel                 # rebuilds kernel + packs kernel-6.1/zboot.img
# deploy (board TF card exposed via USB card reader, or scp + dd on target):
dd if=output/firmware/boot.img of=/dev/mmcblk0p3 bs=1M conv=fsync
```

`boot` is partition 3 of the TF layout (`parameter-evb-SD.txt`); the
rootfs and vendor blobs are untouched. Verify on target after reboot:

```bash
ls -l /dev/spidev0.0                       # exists -> SPI driver + DT ok
cat /proc/device-tree/spi@ff120000/status  # "okay"
cat /sys/kernel/debug/pwm                  # ff170000 present, requested
cat /sys/kernel/debug/clk/clk_summary | grep pwm   # clk_pwm1 rate
```

`clk_pwm1` reports **93.75 MHz** on this board despite the
`assigned-clock-rates = <100000000>` (parent/divider rounding) - use
the debugfs value, not the DT value, when computing PWM periods.

## Step 4 - userspace: the transport (`spi_hw.c`)

Reference implementation: `app/st7789_md130_240x240/spi_hw.c`
(4-wire). Key facts, each hit the hard way:

**Device setup** - three ioctls after open: `SPI_IOC_WR_MODE`,
`SPI_IOC_WR_BITS_PER_WORD` (8), `SPI_IOC_WR_MAX_SPEED_HZ`. Requesting
40 MHz works (the rockchip driver clamps to what the clock tree can
do; actual wire rate is lower, see performance numbers below).

**4096-byte write limit** - spidev rejects `write()`s larger than its
`bufsiz` (default 4096 bytes = one transfer). Everything is chunked:

```c
while (n) { size_t chunk = n > 4096 ? 4096 : n; write(fd, b, chunk); ... }
```

**DC is a GPIO, framed per byte-group** - commands (DC low) and data
(DC high) go through the same spidev fd; DC is a gpio chardev line
(uAPI v2: `GPIO_V2_GET_LINE_IOCTL` -> `GPIO_V2_LINE_SET_VALUES_IOCTL`)
routed JP4.33 = gpio 47. RESET likewise (JP4.29 = gpio 49).

**CS does not end a GRAM transaction** - the panel's auto-increment
write (0x2C raster) survives CS deassert/reassert between spidev
chunk writes: set the window once (CASET/RASET/RAMWR via DC-low
commands), then stream the whole frame as data chunks. Hardware CS
(rm_io7) or a module with CS tied to GND both work this way. The only
exception is the 9-bit 3-wire panel below.

**RGB565 byte order** - the ST7789/ILI9163C class wants each 16-bit
pixel **high byte first** on the wire. Host `uint16_t` buffers are
little-endian, so RAM-order streaming swaps every pixel: red 0xF800
shows as green 0x00F8, green 0x07E0 as red 0xE007, blue 0x001F as
green 0x1F00 (a full R->B, G->R, B->G rotation - if your solid colors
rotate like that, this is the bug). `spi_write_pixels16()` swaps into
a staging buffer; fills build hi/lo byte rows directly.

**SPI mode is per-panel, test it first.** Both the bit-bang reference
and the f411 hardware reference idle SCL **high** (mode 3 timing).
Experience with three panels:

| Panel | Mode needed | Wrong-mode symptom |
|---|---|---|
| ST7789 MD169 240x280 | 0 or 3 | works either way |
| ST7789 MD130 240x240 | **3** | **dark at every speed** (0.56-72 MHz tested on the f411/C542 - speed-independent, so framing not signal integrity) |
| ILI9163C MD144 128x128 | 0 | - (but see 9-bit below) |

If a panel stays dark: try the other mode before touching anything
else. `spi_hw.c` has a single `s_mode` constant for this.

## The 3-wire (9-bit) exception: ILI9163C

`app/ili9163c_md144_128x128_spi/spi_hw.c` implements a different
transport: no DC pin - instead every byte is a **9-bit frame**
(D/C bit + 8 data bits) packed MSB-first into a byte stream:

```
frame (9 bits) D7 D6 D5 D4 D3 D2 D1 D0 C  -> stream bits ...running
```

Rules that cost debugging time:

- The 9th bit **is the DC line** - dropping it (padding the wrong
  side) makes every command look like data: panel stays in sleep,
  screen stays white.
- Chunks flushed to spidev must be **9-byte aligned** (72 bits = 8
  frames) so the flush boundary never splits a frame mid-stream.
- CS deassert also must happen at a frame boundary (the shift
  register truncates partial frames).

## Backlight PWM

`pwm1_8ch_0` = `/sys/class/pwm/pwmchip0` (numbering can shift - probe
by readlink for `ff170000`). Sysfs sequence with correct ordering
(duty must be < period at every step, and polarity changes need the
channel disabled):

```
echo 0 > .../export        # if not yet exported
echo 0 > .../pwm0/enable
echo normal > .../pwm0/polarity    # DT default is INVERSED -> duty would mean off-time
echo 0 > .../pwm0/duty_cycle
echo 5000 > .../pwm0/period        # 200 kHz
echo 550 > .../pwm0/duty_cycle     # 11%
echo 1 > .../pwm0/enable
```

- **500 Hz (2 ms period) shows visible ripple** on bright frames on
  these modules; 25 kHz still faintly visible; **200 kHz is clean**.
  `clk_pwm1` is 93.75 MHz -> 5000 ns period quantizes to ~469 ticks.
- **Polarity: the DT enables the channel as INVERSED** by default.
  With inversed polarity duty 11% = 89% on-time. The apps set
  `normal` polarity explicitly so duty = on-time.
- The apps run 11% (odd loops) / 55% (even loops) so brightness
  differences are visible during long demo loops.

## Performance (measured)

| Path | Effective rate | Full frame |
|---|---|---|
| Bit-bang GPIO uAPI v2 (2 ioctls/bit) | ~580 kbit/s | ~3.5 s @ 240x280 |
| hw SPI0, per-pixel 2-byte `write()`s | ~130 kByte/s | ~2.1 s |
| hw SPI0, bulk chunked rasters (4096 B) | ~3 MByte/s @ 40 MHz req. | **45 ms @ 240x280** (22 fps), 16 ms @ 128x128 (60 fps) |
| Gradient sweep (row-colored bulk writes) | - | 29-30 fps @ 240x280 |
| hw SPI0 @ **93.75 MHz** (ST7365P panel) | ~5.9 MByte/s | 52 ms @ 320x480 (19 fps); 15 fps live UVC camera viewer incl. decode |

### Reaching > 50 MHz (the two hidden clamps)

`clk_spi0` runs at 187.5 MHz, so 93.75 MHz (divider /2) is electrically
there - but two independent software clamps must be lifted first:

1. **DTB**: the spidev node's `spi-max-frequency` (stock: 50 MHz).
   The SPI core silently clamps every transfer to it. Set it to
   100000000 in `vanxoak-hd-rk3506g-evb-sd-v1.dts` (deployed).
2. **Driver**: `drivers/spi/spi-rockchip.c` hard-codes
   `#define MAX_SCLK_OUT 50000000U` - the controller cap, applied
   regardless of the DT. Patched to 100000000U (deployed).

Measured before/after (ST7365P 320x480 full-frame bulk write):
requested 60 MHz -> 71 ms (clamped 50 MHz); requested 100 MHz ->
52 ms (93.75 MHz actual) after both patches. Panel verified stable
on JP4 jumper wiring at that rate (ST7365P; the ST7789 modules have
not been pushed past 40 MHz).

Lesson: the per-`write()` syscall overhead dominates; always stream
whole rows/frames through `spi_write_pixels16()`/`fill_bulk16()`-style
paths, never per pixel.

## Troubleshooting quick table

| Symptom | Cause | Fix |
|---|---|---|
| `/dev/spidev0.0` absent | kernel lacks SPI_ROCKCHIP/SPIDEV or DTB stale | fragment + DTB rebuild (steps 1-3) |
| Panel dark at **every** speed | wrong SPI mode (esp. ST7789 MD130) | flip `s_mode` 0 <-> 3 |
| Screen white, console fine | 9-bit frames mangled (3-wire panels) | bit-9 packing + 9-byte-aligned flush (see ili9163c spi_hw.c) |
| Colors rotate R->G->B | RGB565 written little-endian | `spi_write_pixels16()` |
| Mirrored / rotated | MADCTL (0x36) | try 0x00/0x48/0x88/0xC8; ili9163c has a live `-m` picker |
| Blank band top or bottom | glass offset inside GRAM | set `ROW_Pre` (240x280 = 20, 240x240 = 0) |
| Backlight ripple | PWM frequency too low | 200 kHz + normal polarity |
| spidev write EINVAL > 4096 | transfer larger than bufsiz | chunk writes |

## File map

| File | Role |
|---|---|
| `app/st7789_md130_240x240/spi_hw.c` | 4-wire transport, mode 3, pixel swap (cleanest reference) |
| `app/st7789_md169_240x280_spi/spi_hw.c` | same, mode 0 |
| `app/ili9163c_md144_128x128_spi/spi_hw.c` | 3-wire 9-bit transport |
| `app/gpio_test/gpio_test.c` | JP4 pin walker + RM_IO iomux diagnostic |
| `docs/jp4_spi_lcm.md` | pin budget analysis, no-rebuild bit-bang wiring |
| `docs/spi_sdk_device_tree_guide.pdf` | vendor guide for SPI in the SDK/DT |
| `~/sdk/kernel-6.1/arch/arm/configs/rk3506-spi.config` | kernel fragment (not in this repo - SDK is external) |
| `~/sdk/kernel-6.1/arch/arm/boot/dts/vanxoak-hd-rk3506g-evb-sd-v1.dts` | as-built DTS additions (see diff in step 1) |

Reference microcontroller projects the drivers were ported from:
`D:\f4-demo\nano-f411\app\st7789_md169_240x280`,
`D:\f4-demo\nano-f411\app\st7789_md130_240x240` (incl.
`c542_st7789_md130_possible_issue.md` - the mode-3 evidence),
`D:\f4-demo\nano-f411\app\ili9163c_md144_128x128`.
