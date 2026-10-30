# gpio_test

JP4 GPIO pin walker for the rk3506-mini: blinks each pin one by one
(~3 s each, labelled on the console) so a LED module wired in the LCM's
place can verify every signal.

```
make push run        # one pass over the free JP4 pins, then exit
make run-loop        # cycle forever (probe-friendly)
make run ... --all   # from the board: also walks CAN/UART/SAI-owned pins
                     # (those are peripheral-muxed and will NOT toggle)
```

Also performs a diagnostic that matters for the SPI LCM port: reads the
bank0 (RM_IO) IOMUX nibbles in `grf_pmu` @ 0xff910000 and forces them to
0 (GPIO function) for the free pins - on this image they were already
0x0 (verified 2026-10-30), so the earlier "RM_IO trap" theory for the
blank LCM was disproven; the failure is elsewhere (wiring/panel).

Pins (all verified unclaimed in `/sys/kernel/debug/gpio`):

| JP4 | gpio | net |
|---|---|---|
| 23 | 1 | SPI0_CLK |
| 19 | 3 | SPI0_MOSI |
| 21 | 6 | SPI0_MISO |
| 24 | 7 | SPI0_CSN0 |
| 15 | 13 | GPIO0_B5 (LCM BLK) |
| 29 | 49 | GPIO1_C1 |
| 33 | 47 | GPIO1_B7 |
| 31 | 48 | GPIO1_C0 |
| 37 | 45 | GPIO1_B5 |
| 35 | 46 | GPIO1_B6 |

`--all` additionally walks the peripheral-claimed pins (JP4 7/8/10/11/12/
13/15/16/18/22/26/32/36 - SAI1/CAN/UART) which are expected not to toggle.
