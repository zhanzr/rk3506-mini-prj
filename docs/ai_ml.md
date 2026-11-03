# AI/ML on the rk3506-mini - runtimes, models, Python

Everything measured on the board (RK3506G: 3x Cortex-A7 @ 1.2 GHz,
NEON-VFPv4, ~97 MB RAM, **no NPU** - all inference is CPU/NEON only,
unlike RK3566/8/3588).

## What is already proven working

| Runtime | App | Model | Measured |
|---|---|---|---|
| **ncnn** (Tencent, NEON) | `object_detect` | Yolo-Fastest v1.1, COCO-80 (`.param`/`.bin`) | ~290 ms/pass, 10-11 fps video |
| **TensorFlow Lite Micro** (static lib) | `person_vis` | `person_detect.tflite` (MLPerf-Tiny int8) | live verdicts over video |
| **TensorFlow Lite Micro** | `wake_word` | micro_speech (preprocessor + speech int8) | yes/no spotting via UVC mic |
| **libfacedetection** (NEON C++) | `face_detect` | CNN, 57.8 KB weights compiled in | 420 ms/pass @ 320x240 |
| **pico** (plain C cascade) | `face_pico` | facefinder blob, 234 KB compiled in | 13 ms/pass @ 320x240 |

Details for the face detectors: `app/st7365_md350_320x480/face_detection_comparison.md`.

None of these use ONNX at runtime - ONNX is only ever an **exchange
format consumed on the PC**. The on-board model formats are:
- ncnn `.param`/`.bin` (converted from ONNX offline with
  `onnx2ncnn`/`ncnnoptimize`, then scp'd)
- TensorFlow Lite flatbuffers (`.tflite`; converted from TF/PyTorch ->
  tflite -> quantized int8 on the PC)
- compiled-in C arrays (libfacedetection, pico, any small net)

This offline-conversion pattern is the recommended workflow: convert on
the PC, deploy the converted artifact, run with the proven runtimes.

## Vendorable runtimes (if needed later)

- **ONNX Runtime** - official prebuilt **armv7l** C++ tarballs exist
  (`onnxruntime-linux-armv7l-*.tgz` on GitHub releases); vendor like we
  did libgomp. Full ONNX support on-board without conversion, at the
  cost of a bigger runtime.
- **ncnn** - already here; covers most CNN architectures after offline
  conversion.
- **MNN** (Alibaba) - also supports armv7.
- Not practical: full TensorFlow / PyTorch C++ or Python frameworks
  (size + RAM far beyond the 97 MB budget; no armv7 builds anyway).

## Performance envelope (rough, CPU-only)

- Cortex-A7 NEON fp32: a few GFLOPS across 3 cores at 1.2 GHz
- int8 (TFLM/ncnn quantized) is the sweet spot - person_detect and
  micro_speech run comfortably; CNN face detection at 320x240 costs
  420 ms/pass; pico cascade 13 ms/pass; Yolo-Fastest COCO-80 ~290 ms
- Practical guidance: small quantized single-purpose models work well;
  anything heavy should run on the PC

## Python on the board

Stock image: **Python 3.11.8 + pip 23.3.2**, no third-party packages.

### numpy - INSTALLED and working (2.4.6)

piwheels (Raspbian) publishes cp311 `linux_armv7l` wheels; numpy needs
OpenBLAS + libgfortran at runtime, neither present on the stock image.
What was done (persisted on the board rootfs):

1. `libopenblas.so.0` + `libgfortran.so.5` vendored from Debian armhf
   debs into `/usr/lib` (libopenblas0-pthread 0.3.29 armhf ->
   `libopenblas_armv6p-r0.3.29.so`; libgfortran5 12.2.0 armhf)
2. wheel fetched on the PC from piwheels (downloading it *on the board*
   from piwheels failed with hash mismatch - flaky transfer), scp'd to
   `/root`, installed with `pip3 install <wheel file>`

Measured: `import numpy` **1.85 s**, 240x320 float32 matmul **10 ms**,
~52 MB RAM still available after import.

Gotchas:
- `pip3 install numpy` fails on the board ("no matching distribution"):
  PyPI has no armv7 wheels, and the piwheels download is unreliable
  from this network - fetch the wheel on the PC, scp, install from file
- the wheel filename must keep its exact platform tag when passed to
  pip (a renamed copy is rejected as "not a valid wheel filename")

### TensorFlow / PyTorch / tflite-runtime - NOT installable

- TensorFlow, PyTorch: no armv7 Python wheels at all (aarch64/x86 only)
  and far beyond the RAM budget
- `tflite-runtime`: armv7 wheels exist only for older Pythons (<=3.9),
  not this image's 3.11
- Practical split: **inference in C/C++** (ncnn/TFLM apps above - much
  lower overhead than any Python layer), Python + numpy for glue,
  analysis and offline verification

### On-board ML-audio side note

The es8388 codec has unconnected mic inputs on this EVB; audio ML
(wake_word) captures from the UVC webcam's USB microphone instead
(`CONFIG_SND_USB_AUDIO` kernel fragment + ALSA plughw auto-probe).
