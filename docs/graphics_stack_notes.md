# Graphics stack, boot display chain, and stock fonts/icons

Verified on the running SD image and the SDK/buildroot configs (2026-10-30).
Corrects an earlier repo note that said "no stock fonts" - that was true
only for *standalone programs* (no `/usr/share/fonts` TTFs exist); the
graphics stack itself ships plenty.

## Stack: no desktop, no window manager

| Layer | Present | Notes |
|---|---|---|
| X11 / X.org | **no** | no `BR2_PACKAGE_XSERVER*` |
| Wayland / Weston / cage / sway | **no** | appear in buildroot only as disabled-dependency notes |
| Qt / GTK | **no** runtime | only a leftover `QT5_JSCORE_AVAILABLE` symbol string |
| **LVGL 9.1 on DRM** | **yes** | `BR2_PACKAGE_LVGL=y`, `BR2_PACKAGE_LVGL_DEMO=y`, backend `RKADK` (`BR2_LVGL_DEMO_BACKEND_RKADK=y`), official widgets demo enabled |

The model is **one fullscreen LVGL application on `/dev/dri/card0`** (via
the vendor RKADK layer), touch over evdev (Goodix on the MIPI FPC,
`ads7846` for the RGB FPC resistive touch). Typical embedded-GUI stack,
not a desktop.

## Stock fonts/icons that DO exist

| Asset | Location |
|---|---|
| LVGL 9.1 built-in fonts (Montserrat family) + FreeType integration | compiled into `/usr/lib/liblvgl.so` (`lv_font_*`, `lv_binfont_create`, `freetype_on_font_create`) |
| Vendor demo TTF (`SmileySans-Oblique.ttf`) | SDK `app/lvgl_demo/rk_demo/resource/`, `motor_demo/resource/` |
| Boot logos `logo.bmp`, `logo_kernel.bmp` (654x270 8bpp RLE, Vanxoak) | boot partition `resource.img` (see `app/ssd1317_md066_64x48_iic/logo64.h` for the extracted version) |
| Project raster fonts F6x8 / F8X16 | `app/ssd1317_md066_64x48_iic/oledfont.c` |

## What the 7" MIPI screen shows on a stock boot

1. **U-Boot paints `logo.bmp`** into the `drm-logo@400000` reserved-memory
   region (`rockchip,drm-logo` DT node); the kernel DRM driver keeps it as
   the splash (`logo_kernel.bmp` for the kernel-stage repaint).
2. **Nothing else starts.** All boot hooks verified:
   - `/oem/RkLunch.sh`: literally `echo "The user's boot script is
     executed here"` - an empty placeholder left for the customer.
   - `S99appinit` only calls `RkLunch.sh`.
   - `S99lvgl-test` runs a GUI **only** if the vendor factory USB stick
     (`/mnt/udisk/hd-rk3506-evb-test/lv_rk3506_evb_test`) is plugged in -
     a production-test rig.
3. So the stock experience is a **static boot logo, forever**, with the
   console on DEBUG-TTL. (`/oem` also contains a `720x512.mp4`, clearly
   meant for a customer-supplied video demo.)

To actually see a GUI on the 7" panel, start `/usr/bin/lv_demo` (LVGL
widgets demo with touch) - manually, or by wiring it into
`/oem/RkLunch.sh`, which is exactly the hook the vendor left empty.

## Misc

- `/oem` is a separate partition (p6, 128 MB, `RK_EXTRA_PARTITION_1_SRC`
  = `rk3506_oem` in the SDK defconfig).
- MIPI backlight PWM is `pwm0_4ch_2` (ff932000, probed as `pwmchip0`),
  routed to the `LCD_PWM` net (core pin LCD_D19) - claimed by the
  backlight driver even with the panel unconnected.
