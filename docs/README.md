# Docs

Setup and design notes for the rk3506-mini project.

| File | Content |
|---|---|
| `dts_adc_notes.md` | Verified device-tree structure (rk3506.dtsi / rk3502.dtsi, vanxoak board dts), TSADC die sensor, SARADC wiring (jumpers only, no external channels), CPU clock facts (3x A7 fixed 1.2 GHz, no cpufreq) |
| `cpu_clock_dvfs.md` | Why the board runs at 1.2 GHz although the datasheet claims 1.5 GHz: vendor DTS never enables the PWM behind vdd_arm (CPU regulator), rockchip-cpufreq fails at boot and gives up; OPP table / chip grade details; fix sketch + risks |
| `amp_m0_notes.md` | How to program the Cortex-M0 (AMP): HAL / RT-Thread projects, FIT packaging, U-Boot bring-up, kernel resources; current SD image has no AMP support (verified) |
| `graphics_stack_notes.md` | Verified graphics stack: no desktop/WM, LVGL 9.1-on-DRM via RKADK, stock fonts/icons inventory (LVGL built-ins, SmileySans TTF, boot logos), what the 7" MIPI screen shows on stock boot (static logo only - all boot hooks verified) |
| `jp4_spi_lcm.md` | 4-line SPI LCM pin mapping on JP4 (SPI0 via rm_io1/3/6, DC/CS on free bank1 GPIOs, BLK hardware PWM on rm_io7), RM_IO pin budget vs enabled peripherals, required DTB additions + interim options |

Vendor manuals (PDFs, schematics, pin lists) are kept in
`D:\board_database\rk3506-mini` and are not part of this repository.

| Vendor file | Content |
|---|---|
| `READMD.md` (sic) | Vendor board README: GPIO number calc (pin = bank*32 + n), login, display (DRM/modetest), jumpers, connector pin tables (this repo started as a copy of it) |
| `gpio_programming_manual.pdf` | GPIO programming (sysfs API) |
| `led_programming_guide.pdf` | On-board LED programming |
| `lvgl_guide_1.pdf`, `lvgl_guide_2.pdf` | LVGL app development on this board (vendor liblvgl + rkadk/DRM) |
| `RJGT102_programming_guide.pdf` | RJGT102 temperature/humidity sensor guide |
| `software_watchdog_guide.pdf` | Software watchdog guide |
| `adb_login.pdf` | adb access over USB1 |
| `SCH_HD-RK3506-EVB-V1.1.pdf` | Baseboard schematic |
| `【数据手册】HD-RK3506G-CORE V1.0.pdf` | Core module datasheet |
| `【引脚列表】*.xlsx/csv` | Core module pin lists |
| `rk3506_linux6.1_sdk_v1.2.0_evb/` | Full vendor SDK (system image builds; `docs/cn|en` inside has Rockchip manuals; `app/lvgl_demo` is the LVGL reference) |
| `sdcard_image_20260831/` | Bootable SD card images (MIPI 1024x600 and RGB 800x480 variants) |
| `DriverAssitant_v5.13.zip`, `RKDevTool_Release_v3.32.zip`, `SDDiskTool_v1.78.zip` | Flashing tools |

## Notes

- The SDK toolchain export came from the `rockchip_hd_rk3506g_evb_nand`
  buildroot output but matches the SD image ABI (glibc, armv7-a hard-float,
  GCC 12.4) - binaries verified running on the board.
- The board is `HD-RK3506-EVB` baseboard + `HD-RK3506G-CORE` core module,
  model string on the running system: `Vanxoak HD-RK3506-EVM V10 Board`.
- No eMMC/NAND on the core module: the system runs from the TF card.
  Application deployment is over the network (scp), not flashing.
