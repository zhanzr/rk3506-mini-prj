# SPI panel (ST7365P class) - FPS limits and GUI options

Investigated 2026-11-04. Question: can the SPI-panel FPS be improved
(clock, DMA, other)? And could a touch-equipped panel of this class run
a normal GUI (window manager / Qt / GTK)?

## Measured baseline (320x480 RGB565 full frame = 307,200 bytes)

| Path | Time | FPS |
|---|---|---|
| SPI0 @ 93.75 MHz, 4096-byte chunked writes (current) | ~52 ms | ~19 |
| Wire floor at 93.75 MHz (307 KB x 8 / 93.75e6) | ~26 ms | ~38 |
| UVC camera viewer (C920) | - | 15 (camera-bound, not display-bound) |

## 1. Where the time goes / what can improve it

### SPI clock - already at the hardware ceiling

- `clk_spi0` = 187.5 MHz; the Rockchip SSI divider minimum is /2, so
  **93.75 MHz is the maximum SCLK this hardware can generate**.
- Raising the divider ratio would need a higher `clk_spi0` parent rate
  (CRU/CCF work, uncertain parents) for maybe +7% at 100 MHz - the
  ST7365P already validates at 93.75 MHz (beyond its own datasheet
  spec), so the marginal gain/risk trade is poor.
- Further clock increases are NOT the lever.

### DMA - already in use

- The live DT spi0 node carries `dmas`/`dma-names` (dmac0 channels),
  and `spi-rockchip.c` selects `ROCKCHIP_SPI_DMA` for any transfer
  larger than the FIFO (our 4096-byte chunks qualify).
- So the CPU is NOT the bottleneck: the remaining overhead per frame is
  the spidev chunk path (75 write() syscalls of 4096 B, each with
  controller setup) and the wire itself.

### The real lever 1: spidev `bufsiz` (needs a boot-args change)

`/sys/module/spidev/parameters/bufsiz` = 4096 and is not writable at
runtime (permission denied even as root). Raising it moves the limit:

| bufsiz | chunks/frame | est. frame time | FPS |
|---|---|---|---|
| 4096 (today) | 75 | ~52 ms | ~19 |
| 32768 | 10 | ~29 ms | ~34 |
| 262144+ | 2 | ~27 ms | ~37 (wire floor 26 ms) |

Set via kernel command line `spidev.bufsiz=32768` (spidev is built-in),
i.e. a bootargs/DT-chosen change + reboot - no kernel code rebuild.

### The real lever 2: partial updates (no changes needed at all)

A full-frame rewrite is the worst case. GUI usage redraws dirty
rectangles only: a 320x40 status band = 25.6 KB = ~4.3 ms per update;
typical widget changes are a few % of the screen. Any toolkit that
tracks dirty regions (LVGL does by design) turns the same hardware into
an interactive display today. This is why the M0/uvc apps feel live
already.

## 2. Touch-equipped panel as a normal GUI display

Feasible, with a clear best path. A "similar LCM with touch" typically
pairs the ST7365P-class controller with an I2C touch IC (GT911 /
FT5x06 / Goodix families).

- **Touch input: no kernel rebuild needed.** The running kernel has
  `CONFIG_INPUT_TOUCHSCREEN=y` + `TOUCHSCREEN_GOODIX=y`,
  `TOUCHSCREEN_GT9XX=y`, `TOUCHSCREEN_EDT_FT5X06=y`,
  `TOUCHSCREEN_HYN_CST128A=y` built in - the common capacitive ICs
  bind as evdev devices automatically once the I2C address/irq is
  described.
- **GUI stack options, ranked by fit:**
  1. **LVGL 9 (best fit)**: `liblvgl.so` (v9.1) is already on the
     board; LVGL's original domain is exactly this class of SPI panel.
     Write a small display driver (flush callback -> our existing
     bulk-SPI path), register the touch evdev as an indev, and you get
     widgets, animations and partial updates at interactive rates on a
     320x480 panel.
  2. **Qt (possible, heavier)**: no Qt in the image; would need a Qt
     build and a LinuxFB-style backend targeting our SPI path.
     RAM (~97 MB) makes desktop-style Qt stacks tight but a minimal
     Qt build is not impossible.
  3. **GTK / X11 / desktop window managers: not realistic** - RAM,
     X11 stack, and the absence of a DRM/KMS driver for an SPI panel.
- **Kernel framebuffer route (bigger effort, broader result)**: a
  kernel fbdev driver for the panel (`drivers/staging/fbtft/` exists
  in-tree with close cousins like `fb_ili9486.c`; the modern
  `panel-mipi-dbi` DRM driver is not enabled) would expose `/dev/fb0`
  with kernel-side DMA and deferred I/O - then Qt LinuxFB, SDL, and
  any fbdev toolkit work unmodified. Needs a kernel rebuild + a driver
  port, plus the DT bits for the panel and its touch controller.

## 3. GUI programming potential of this board / SDK

- The vendor SDK's GUI story is **LVGL-on-DRM via RKADK** for the
  RGB/MIPI display outputs, and `liblvgl.so` (v9.1) ships in the image.
- For the SPI panel: userspace GUI is fully in reach - the existing
  uvc/m0 memcpy path is effectively an LVGL flush function; add the
  touch IC's evdev as an LVGL input device. Expect a responsive
  embedded GUI (not a desktop).
- No window-manager class stack (X11/Wayland) is realistic at 97 MB
  RAM without a GPU-accelerated KMS driver for the panel.
- Firmware-side alternative: the M0 could drive the panel or a touch
  controller for latency-critical scanning while the A7 does rendering
  (the AMP shmem/mailbox channel exists in the vendor AMP design).

Bottom line: with a touch LCM, the practical stack is
**LVGL 9 + evdev touch over the existing SPI loader path** - usable GUI
today at partial-update rates; a kernel fbdev/DRM driver is the path to
standard Linux GUI frameworks.
