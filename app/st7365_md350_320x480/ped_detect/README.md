# st7365_md350_320x480/ped_detect

UVC camera **pedestrian detection** on the ST7365P 3.5" 320x480 (MD350)
SPI panel - Orange Pi Zero, using OpenCV's **HOG people detector**
(Dalal-Triggs SVM, `getDefaultPeopleDetector` - pre-trained, ships with
OpenCV, no model download).

- same capture/panel stack as the other uvc_* apps (RGB565 default,
  `-6` for RGB666, auto camera lifecycle, `uh-N` + fps band)
- requires exactly YUYV 640x480; exits if the camera cannot negotiate
  that mode
- 320x240 luma sample -> detector pthread (video keeps streaming at
  15 fps); green 2 px boxes mapped through the panel rotation
- band OSD: `uh-N <fps> fps <n>p <det>ms`
- HOG wants upright full-body silhouettes: best at 2-6 m from the
  camera; expect some false positives (classic HOG behavior)

## Build & run (native on the board)

```bash
sudo apt-get install -y build-essential libopencv-dev pkg-config
# sources pushed by `make board` from WSL, then:
cd st7365_md350_320x480/ped_detect && make
./ped_detect [-6]
```

Stop other panel/camera apps first (single-open resources). Expect
~300-700 ms per detection pass on the H3.
Pass `-6` to select RGB666; RGB565 is the default.
