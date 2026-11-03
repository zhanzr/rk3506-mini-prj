# face_detect vs face_pico - deep comparison on the rk3506-mini

Both apps detect faces from the same UVC camera (Logitech C920, 640x480)
and draw boxes on the same ST7365P 3.5" 320x480 SPI panel. They differ
almost entirely in **the detector**: model type, model size, inference
runtime, and input pixel format. Everything else (V4L2 MMAP capture,
half-res sampler, detector thread, 90-degree box mapping, 2 px boxes,
RGB565/RGB666 panel pipeline, 200 kHz PWM backlight) is shared.

## Headline numbers (measured on this board, Nov 2026)

Board: RK3506G, 3x Cortex-A7 @ 1.2 GHz (fixed clock), NEON-VFPv4.
Camera: C920 YUYV 640x480 @ 15 fps (camera-negotiated rate).

| Metric | face_detect (CNN) | face_pico (cascade) |
|---|---|---|
| Video fps | 14-16 fps | 14-16 fps |
| **Detection time / pass** | **~420 ms** (415-423) | **~13 ms** (11-16) |
| Detector : frame budget | 420 ms >> 66 ms (skips frames) | 13 ms << 66 ms (every frame) |
| Faces found (1 person, frontal) | 1, steady | 1, with brief dropouts |
| Model size (embedded) | **57.8 KB** float weights | **234 KB** cascade blob |
| Runtime RAM extra | ~36 KB result buffer + CNN work buffers | none beyond the blob |

Both hit the same fps ceiling because the **camera delivers 15 fps**;
detection runs on a separate thread either way, so video playback is
unaffected by detector cost. The difference is CPU budget consumed and
latency of box updates: pico updates boxes every frame (66 ms); CNN
updates roughly every 7th frame (~420 ms cadence).

## face_detect - libfacedetection CNN

- **Model**: libfacedetection (Shiqi Yu, MIT license), the CNN variant
  (`facedetectcnn`). Weights are compiled into the binary
  (`facedetectcnn-data.cpp`, 445 KB of C source) = **57.8 KB** of float
  arrays in RAM. Architecture: multi-stage single-shot CNN backbone with
  per-scale detection heads (trained frontal-face model).
- **Runtime**: no external framework - the inference engine is the
  library's own hand-written NEON code (`facedetectcnn.cpp` compiled with
  `-D_ENABLE_NEON`, ARMv7 NEON intrinsics). The app itself stays C via a
  small C++ shim (`det_shim.cpp`) exposing `det_run()`.
- **Input**: 320x240 **BGR888** (3 bytes/px), built by half-sampling the
  640x480 YUYV frame with a full YCbCr->BGR conversion per pixel (the
  CNN consumes color channels and grayscales them internally).
- **Output**: confidence 1-100 + x/y/w/h per face, up to 8 faces.
- **Trade-offs**: strongest detection quality (small/off-angle faces,
  partial occlusion), but 420 ms/pass burns ~60% of one A7 core
  continuously and makes box updates visibly slower than the video.

## face_pico - pico pixel-comparison cascade

- **Model**: the official pico facefinder (nenadmarkus/pico, MIT),
  embedded as a 234 KB binary blob (`facefinder_embed.h`). It is a
  **cascade of binary decision trees over pixel-intensity comparison
  tests** (Markus et al., "pico: object detection with pixel intensity
  comparisons"), trained on frontal faces.
- **Runtime**: `picornt.c` - ~70 lines of plain C, scalar (no SIMD),
  no heap use during scanning. Reads the image as **bytes**.
- **Input**: 320x240 **uint8 grayscale** - literally the YUYV luma bytes
  sampled 2:1 (zero conversion cost; luma IS the grayscale). The cascade
  compares pairs of pixels (`px[a] <= px[b]`), which is invariant to the
  0..255 vs 0..1 scaling, so the byte image works directly against the
  float-trained model. Parameters: scale step 1.15, stride 0.1, window
  48-240 px, detection quality >= 5, clustered into merged boxes.
- **Output**: quality score + x/y/size per face, up to 8 faces.
- **Trade-offs**: ~32x faster than the CNN on this CPU (13 ms vs 420 ms)
  with essentially zero marginal CPU load; quality is lower - brief
  dropouts (faces 0) were observed when the subject's pose shifted, and
  the frontal-only training makes profile faces harder.

## Shared pipeline (identical in both)

1. V4L2 MMAP capture, YUYV preferred, MJPEG fallback (libjpeg decode)
2. `det_publish()`: 2:1 half-sample into the detector input - only if
   the previous detection finished (latch), so the detector never
   back-pressures the video thread
3. Detector pthread: converts + runs the detector, publishes boxes
4. Boxes mapped back through the camera->panel transform (90-degree
   rotation + scale + letterbox for the 320x480 portrait panel) and
   drawn 2 px thick into both panel buffers
5. Status band OSD: `uh-N <fps> fps <n>f <det>ms` (debug log level)

## When to use which

| Use case | Pick |
|---|---|
| Smooth boxes, lowest CPU, battery/thermal headroom | `face_pico` |
| Highest detection quality, small or off-angle faces | `face_detect` |
| Foundation for tracking/landmarks on top | `face_detect` (CNN confidence is steadier) |
| Bench demo of "how fast is a detector" | `face_pico` (13 ms/pass on a 1.2 GHz A7) |

## Reproducing the numbers

```sh
# on the board, per-app dir; switch the app's config.json to debug to
# see the per-second "fps: N det D ms faces F" lines, then restore:
cd st7365_md350_320x480/face_detect   # or face_pico
./face_detect                          # ./face_pico
```
