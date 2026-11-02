# st7365_md350_320x480/qr_scan

UVC camera **QR/barcode reader** on the ST7365P 3.5" 320x480 (MD350)
SPI panel - Orange Pi Zero, using **zbar** (LGPL-2.1, board package
`libzbar-dev`).

- same capture/panel stack as the other uvc_* apps (RGB565 default,
  `-6` for RGB666, auto camera lifecycle, `uh-N` + fps band)
- the luma plane goes straight to zbar as Y800 every frame (scan takes
  single-digit ms - no detector thread needed)
- on a decode: the text is drawn above the band, the full string goes
  to the log; re-scans continuously and picks up new codes
- verifies QRCODE, EAN/UPC, Code128 etc. (all zbar symbologies enabled)

## Build & run (native on the board)

```bash
sudo apt-get install -y build-essential libzbar-dev
# sources pushed by `make board` from WSL, then:
cd st7365_md350_320x480/qr_scan && make
./qr_scan [-6]
```

Hold a QR in front of the camera at ~10-30 cm; decoded text appears on
the panel and in the log as `qr[N]: qrcode "text"`. Stop other
panel/camera apps first (single-open resources).
