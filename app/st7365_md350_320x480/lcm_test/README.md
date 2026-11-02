# lcm_test - ST7365P 320x480 bring-up demo (rk3506-mini)

Loop: 2x-font banner -> info page (RGB565, SPI rate, UVC host probe) ->
inverted info -> TEST_STAND (frame/gray/band/red/green/blue/white/black,
per-screen write time + 4 s dwell) -> HSV gradient -> stock RK3506 boot
logo (2x) -> scaled fonts (1x/2x/3x). Backlight alternates 11% / 55%
per loop (PWM 200 kHz, JP4.15).

Usage: `./lcm_test [-s speed_kHz]` (default 100000 - 93.75 MHz actual).
Build/deploy from this dir: `make push run`.
