# /rockchip-test folder - inventory and runnability on this board

The stock Rockchip test suite (V2.4, 20240403) ships in the image at
`/rockchip-test`. The suite is written for full-featured Rockchip
products (GPU/NPU/PCIe/WiFi/BT/Chromium...) - this RK3506G board has a
much smaller peripheral set, so less than half the modules apply.

Checked live on 2026-11-04 (binaries, devices, busses).

## Runnable now

| # | Test | Depends on | Verified |
|---|---|---|---|
| 1 | **DDR test** (`ddr/`) | `memtester` + `stressapptest` - both in the image ✓ | memtester 1 M pass ✓, stressapptest v1.0.9 present ✓. Script runs 24 h stress by default (`/userdata/rockchip-test/*.log`). RAM is only ~97 MB, so the auto-sized test uses ~33 MB |
| 2 | **Camera USB/v4l2 test** (`camera/camera_stresstest_v4l2.sh`) | `v4l2-ctl` ✓ + USB camera ✓ (C920) | capture 640x480 YUYV x 5 frames, size-checked (3,072,000 B) ✓ loops forever |
| 3 | **Audio capture** (`audio/`) | `sox`/`amixer`/`arecord` ✓ | C920 mic = sound card 0 ✓; capture produced an empty wav in a 1 s probe (needs a longer/correct-device run). es8388 codec sound card is NOT registered (no `es8388-sound` card; codec inputs unconnected on this EVB) |
| 4 | **Flash stress** (`flash_test/`) | `/userdata` (TF card) ✓ | works, but wears the TF card - use with care |
| 5 | **CPU test** (`cpu/`) | needs `stress` or `stress-ng` - **both missing** | the script exits; substitute any busy-loop (or app/coremark) for the same effect. `cpu_freq_scaling.sh` is N/A: no cpufreq on this board |
| 6 | **Auto reboot** (`auto_reboot/`) | reboot ✓ (counter in /userdata) | works, but disruptive |

## Present but broken / not applicable on this board

| # | Test | Why it cannot run |
|---|---|---|
| - | **GPU test** (`gpu/`, glmark2) | RK3506 has **no GPU**; glmark2 binary also missing |
| - | **NPU2 test** (`npu2/`) | no NPU on the RK3506 |
| - | **Chromium test** | chromium not in the image; no GPU |
| - | **Benchmark/unixbench** | `/opt/unixbench` missing (not in image); glmark2 missing |
| - | **PCIE test** | no PCIe controller/bus on the RK3506 (lspci binary exists, no bus) |
| - | **Suspend/resume** | `pm-suspend` present, **but no RTC wakealarm** (`/sys/class/rtc/rtc0/wakealarm` missing) - the script exits; without RTC auto-wake the board never resumes remotely |
| - | **Recovery test** | no recovery partition on this TF image |
| - | **Power-lost test** | needs a controllable power-cut harness (physical) |
| - | **Video (gst) test** | `gst-launch-1.0` present, but the script renders via **waylandsink** - no Wayland in the image; no MPP gst plugin either (software decode -> filesink would work as a modified test) |
| - | **WiFi/BT tests** | `rkwifibt_app_test` + aic8800 firmware ship in the image, **but this EVB has no WiFi/BT module** (no wlan0, no SDIO device, no aic8800 in dmesg) |
| - | **Audio playback** | the C920 is capture-only; the es8388 sound card never registered - `aplay` has no valid output device |

## Notes

- Tests that pass `fw=`-style parameters must use busybox-insmod-safe
  syntax; `insmod` on this image accepts no module parameters.
- The camera USB test script renders with `waylandsink` + `mppjpegdec`
  (missing); use `camera_stresstest_v4l2.sh` (pure v4l2-ctl, works) or
  our own UVC pipeline instead.
- The CPU stress requirement is trivially met by alternatives; DDR and
  flash stress are the genuinely useful burn-in tests that run as-is.
- Suspend/resume could become feasible only with a working RTC wake
  source (the EVB v1 does not wire one to this kernel's RTC class).
