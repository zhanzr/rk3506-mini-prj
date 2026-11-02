# st7365_md350_320x480/wake_word

**"yes/no" keyword spotting** on the ST7365P 3.5" 320x480 (MD350)
SPI panel, orange-pi-zero (upstream reference; rk3506-mini port) — TensorFlow Lite Micro + the H3 codec mic.

## Models + runtime

Two chained models, both Apache-2.0 and shipped in the tflite-micro
tree (`tensorflow/lite/micro/examples/micro_speech/models/`):

1. `audio_preprocessor_int8.tflite` (8.7 KB) — the TF Signal audio
   frontend as a model: 30 ms of int16 audio -> 40 int8 filterbank
   features. Run 49 times with a 20 ms stride over the 1 s window
   (TFLM signal custom ops: Window / FftAutoScale / Rfft / Energy /
   FilterBank* / PCAN — all registered in the app's op resolver).
2. `micro_speech_quantized.tflite` (18.8 KB, DS-CNN) — features ->
   silence / unknown / **yes** / **no** probabilities (softmax int8,
   dequantized in the app).

Runtime: TensorFlow Lite Micro (`vendor/tflm/lib/libtensorflow-microlite.a`,
gcc-12 cross build), `AllOps`-style explicit `MicroMutableOpResolver<24>`;
two interpreters, two arenas (pre 8.5 KB, speech 6.8 KB).

## Board features used

- **H3 audio codec mic** (CON5 pins 10/11/12) via ALSA, 16 kHz mono
  S16, `default` device; links a vendored copy of the board's
  `libasound.so.2` (same method as `app/rec_wav`)
- enables the codec capture route at startup (`amixer ... cap`) —
  the route ships disabled, which silently yields all-zero audio
- MD350 panel over SPI1 (RGB565 default; pass `-6` for RGB666)
- 1 s ring buffer (capture pthread) -> main loop re-runs feature
  extraction + speech model ~2x per second
- startup waits for ALSA initialization and fails explicitly if capture
  cannot provide exactly 16 kHz; inference starts only after the ring
  contains a full second of audio
- nonblocking capture lets shutdown complete promptly; recoverable ALSA
  read errors are recovered, and unrecoverable ones stop the app clearly

## Panel output

- big 2x verdict: **yes** / **no** / unknown / silence (color-coded)
- `yes <p>%  no <p>%` line + a 200 px yes-score meter
- log: per-round probabilities

## Microphone choice

`-d <alsa_dev>` selects the capture device (see `rec_wav -l`); `-6`
selects RGB666 panel output.
Measured: the **USB webcam's mic** (`-d plughw:1,0`) is cleaner than
the board codec mic (fewer clipped samples, no board-rail whine) —
recommended when the camera is attached.

## Build & deploy (WSL cross)

```bash
make
make push     # binary + both model files
```

Stop other panel apps first (single-open resources).
