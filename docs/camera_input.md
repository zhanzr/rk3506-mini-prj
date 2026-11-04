# Camera input options for the rk3506-mini

Investigated 2026-11-04. Question: is UVC the only feasible camera
input, and what about the SoC's native camera interface?

## Summary

| Path | Status |
|---|---|
| **UVC USB camera** | **Working and proven** - C920 via the runtime loader apps (`app/m0_bench/`, `app/st7365_md350_320x480/`): video viewer, face/object detection. Any UVC camera works; USB 2.0 limits (YUYV 640x480 = 15-30 fps). |
| **FlexBus CIF** (DVP parallel camera) | The SoC's native camera interface. Driver built into the stock kernel; DT node disabled; sensor drivers present as sources but not enabled. **Not feasible on JP4** (see below). Needs a kernel rebuild + module wiring. |
| **MIPI CSI-2 camera** | Not possible - the RK3506 has no CSI-2 receiver (the DSI block is display-only; the isp/rkisp1 drivers in-tree belong to rk3568/rk3588-class SoCs). Also no hardware ISP. |
| **M0 VICAP capture** | The MCU HAL includes VICAP support (`hal_vicap.o`) - in the vendor AMP design the M0 does deterministic capture into DDR. Same pin problem as FlexBus (below); advanced AMP use. |

## FlexBus CIF - the native camera path

The CIF accepts standard DVP sensor output (PCLK/HREF/VSYNC + 8-bit
bus + I2C control), and stores YUV422 (UYVY/YUYV/VYUY/YVYU),
NV12/NV16/NV21/NV61 or RAW8 into DDR via videobuf2-dma-contig. V4L2
compliant (`v4l2-fwnode` graph + sensor subdev), so standard V4L2
apps/GStreamer read from it.

What using it requires:

1. Kernel rebuild: enable a DVP sensor driver (Rockchip-family
   sources present: gc02m2/gc0311/gc0329/gc2035/gc2053/gc2093/gc1084,
   bf3925, ...) + the `flexbus_cif` DT node (status okay) + i2c bus.
2. Wire a DVP camera module to the FlexBus pins.
3. No ISP on the SoC - raw frames (DVP modules have built-in ISPs; CV
   apps consume the frames directly).

### FlexBus CIF on JP4 - NOT feasible

The FlexBus signals are muxable only on GPIO1_B/C/D bank pins
(pinctrl func 3/4/5), and the CIF needs the flexbus *data bus*. The
complete flexbus pin map (rk3506-pinctrl.dtsi):

| FlexBus signal | SoC pin | On JP4? |
|---|---|---|
| clk | GPIO1_C1 | **yes - JP4.29** (gpio 49) |
| d0 | GPIO1_D3 | no |
| d1 | GPIO1_D2 | no |
| d2 | GPIO1_D1 | no |
| d3 | GPIO1_D0 | no |
| d4 | GPIO1_C7 | no |
| d5 | GPIO1_C6 | no |
| d6 | GPIO1_C5 | no |
| d7 | GPIO1_C4 | no |
| d8 | GPIO1_C3 | no |
| d9 | GPIO1_C2 | no |
| d10 | GPIO1_B7 | **yes - JP4.33** (gpio 47) |
| d11 | GPIO1_B6 | JP4.35/38 |
| d12 | GPIO1_B5 | **yes - JP4.37** (gpio 45) |
| d13 | GPIO1_B4 | no |
| d14 | GPIO1_B3 | no |
| d15 | GPIO1_B2 | JP4.40? |
| csn m0-m5 | GPIO1_PB0/PB2/PB4/PC0/PC2 | no |

An 8-bit DVP camera needs PCLK + VSYNC + HREF + 8 data lines (11+
signals). JP4 can mux at most the flexbus CLK + 3 data bits - the
12-pin data bus (GPIO1_C2-C7, PD0-PD3, PB3-PB4) routes to the LCD FPC
connectors on the core module, not to JP4.

Where the FlexBus CIF *could* work: on the RGB LCD FPC connector,
whose data lines overlap these GPIO1_C/D pins (i.e. trading the RGB
display for a DVP camera) - a custom bring-up with no vendor dtsi
example, and it conflicts with the RGB display use.

The M0 VICAP angle (hal_vicap in the MCU HAL) inherits the same pin
problem - the M0 cannot reach a camera wired to JP4 either.

## Camera performance context

- The A7s (3x 1.2 GHz, no GPU, no ISP) run OpenCV/CV workloads on
  raw frames: the m0_bench/st7365 detection apps prove 15 fps
  640x480 capture + CNN inference pipelines work.
- USB 2.0 limits: YUYV 640x480 tops at ~15-30 fps depending on
  bandwidth sharing; MJPEG 1080p30 streams decode at ~15 fps with the
  vendor libjpeg.
