# st7365_md350_320x480/object_detect

UVC camera **80-class object detection** (COCO: person, cup, laptop,
cell phone...) on the ST7365P 3.5" 320x480 (MD350) SPI panel, using
**Yolo-Fastest v1.1** through **NCNN** (Tencent's armv7-optimized
inference framework, NEON, no runtime deps).

- model: `vendor/ncnn_model/yf-opt.{param,bin}` - Yolo-Fastest v1.1
  COCO-80, **pre-flattened with ncnnoptimize** (the raw darknet2ncnn
  weights hang ncnn's load_model on armv7; the optimized layout loads
  and runs fine - x86 confirmed the model itself is valid)
- vendored `vendor/ncnn/` - built from ncnn 20240410, gcc-12 cross,
  `-O2 NEON`, with one arm header patch (cast_fp16.h: the FP16-storage
  guard `__ARM_FP & 2` must be neutralized on Cortex-A7 which lacks
  FP16 - see tools/armv7-toolchain-gcc12.cmake)
- `yolo_shim.cpp` wraps the C++ API for the C app
- requires exactly **640x480** YUYV or MJPEG; exits if the camera
  cannot negotiate that mode
- 320x240 BGR sample (BT.601) -> ncnn resize to 320x320 -> boxes
  (already NMS'd by the model graph), score threshold 0.35
- detector pthread: measured **~280 ms per pass** on the H3 while
  video streams at 13-14 fps; boxes + class label + score drawn on the
  panel (both buffers); band OSD `uh-N <fps> fps <n>o <ms>ms`
- panel output defaults to RGB565; pass `-6` for RGB666

## Build & deploy (WSL cross)

```bash
make
make push     # binary + model files
```

Run on the board: `cd st7365_md350_320x480/object_detect &&
./object_detect`. Stop other panel/camera apps first (single-open
resources).
