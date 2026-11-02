# st7365_md350_320x480/face_detect

UVC camera **face detection** with live boxes on the ST7365P 3.5" 320x480 (MD350) SPI panel - orange-pi-zero (upstream reference; rk3506-mini port) (H2+/H3, Armbian).

Same capture + panel stack as `app/st7365_md350_320x480/uvc_to_lcd`
(RGB565 default, `-6` for RGB666, auto camera lifecycle,
`UVC NA` + rescan) **plus face detection**:

## Detector

- **libfacedetection** CNN, vendored in `vendor/facedetect/`
  (3-clause BSD, Shiqi Yu). The trained weights are embedded in
  `facedetectcnn-model.cpp` - **nothing to train, nothing to download
  at runtime**. Multi-view (frontal + tilted/side faces), up to
  confidence-scored box + 5 landmarks per face (we use the box).
- The public header is C++-only (`<vector>`), so `det_shim.cpp`
  exposes a plain-C `det_run()`; the app proper stays C.
- Compiled with `-D_ENABLE_NEON -O3` for the H3's NEON pipeline.
- Input: the 640x480 camera frame half-sampled to **320x240 BGR**
  (BT.601 limited-range, chroma centered - same math as the display
  path), built in the capture thread.
- Requires the camera to negotiate exactly **640x480** in YUYV or
  MJPEG; startup exits with an error if that mode is unavailable.

## Threading

Detection runs on its own pthread (~430 ms per pass on the H3) while
the capture/blit loop keeps streaming at 15-16 fps. A mutex +
condvar handshake (`det_pending`) guarantees the detector always
works on a stable sample; V4L2 buffers stay dequeued until the private
detector sample is copied. Boxes and telemetry are published under the same lock
and drawn every frame (2 px green frame, mapped back through the
90-degree rotation/scale/letterbox map), so tracking stays smooth
between detections.

## Telemetry

```
fps: 15  det 441 ms  faces 1  fb avg R=22 G=22 B=22 (6-bit/6-bit/6-bit)
```

Band OSD on the panel: `uh-3 15 fps 1f 441ms`.

## Tunables

Same flags as uvc_to_lcd: `[-G gamma%] [-S sat%] [-R lim|full] [-6]`.
RGB565 is the default; `-6` selects RGB666.
Detection input size is a `#define` (DET_W/DET_H): 320x240 detects
faces down to roughly arm's length from this camera; shrink to 240x180
for more speed at the cost of range.

## Build & deploy

```bash
make            # needs arm-linux-gnueabihf-g++ (C++ for the CNN)
make push
make run        # or on the board: cd uvc_face_detect_... && ./face_detect
```

Exclusive use: the camera + panel are single-open resources - stop
e_server (or uvc_to_lcd) first.
