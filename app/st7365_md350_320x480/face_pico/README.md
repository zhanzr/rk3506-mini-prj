# st7365_md350_320x480/face_pico

UVC camera face detection on the ST7365P 3.5" 320x480 (MD350) SPI
panel, using the **pico** detector (nenadmarkus/pico, MIT): a cascade
of decision trees over pixel-intensity comparisons - the runtime is a
single C file (`vendor/pico/picornt.c`) and the trained facefinder
cascade is **embedded in the binary** (`facefinder_embed.h`).

Companion to `app/uvc_face_detect_...` (CNN): pico is ~30-60x faster
per pass at some accuracy cost.

- same capture/panel stack (RGB565 default, `-6` for RGB666, auto
  camera lifecycle, `UVC NA` + rescan)
- requires exactly **640x480** YUYV or MJPEG; exits if the camera
  cannot negotiate that mode
- 320x240 luma sample -> detector thread; measured **5-25 ms per
  pass** on the H3 (video unaffected at 15-16 fps)
- boxes mapped through the 90-degree rotation/scale/letterbox map,
  drawn into both panel buffers; band OSD `uh-N <fps> fps <n>f <ms>ms`
- pico params: scalefactor 1.15, stridefactor 0.1, size 48-240,
  quality threshold 5.0 (see det_thread)

## Build & deploy (WSL cross)

```bash
make
make push
```

Run on the board: `cd st7365_md350_320x480/face_pico && ./face_pico`.
Pass `-6` to select RGB666; RGB565 is the default. Stop other
panel/camera apps first (single-open resources).
