# uvc_to_lcd - UVC camera viewer on ST7365P 320x480 (rk3506-mini)

Streams the first UVC camera to the panel, center crop-fill (no
letterbox), OSD line (pixel format / SPI rate / host id) and fps over
the video. Camera lifecycle: absent -> "UVC NA" + 2 s rescan; gone
mid-stream -> release + rescan. MJPEG frames decode through the
vendored libjpeg 8.2.2 (`vendor/libjpeg.so.8.2.2`, shipped to the
board - the stock image has no libjpeg; `make run` sets
`LD_LIBRARY_PATH`). YUYV handled natively.

Usage: `./uvc_to_lcd [-5|-6]` (RGB565 default, `-6` = RGB666;
`-G/-S/-R` gamma/saturation/range from the opz original).
Build/deploy: `make push run`.
