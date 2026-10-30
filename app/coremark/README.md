# coremark

EEMBC CoreMark 1.0 benchmark, ported from `pbeagle_prj/app/coremark`
(PocketBeagle, itself from an STM32MP157 reference). Upstream CoreMark
sources + the `linux` port (`linux/core_portme.*`, POSIX timers, heap
memory) are used unchanged; the port work is the build configuration:
Cortex-A7 target and this repository's board/transfer workflow. See
`LICENSE.md` (Apache 2.0).

Two instruction-set variants are built:

- `coremark-arm`   : compiled with `-marm`  (A32 instruction set)
- `coremark-thumb` : compiled with `-mthumb` (T32/Thumb-2 instruction set)

Optimization: `-O3 -funroll-loops -flto` on top of
`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard` (matching the board's
Buildroot config). Runs use `PERFORMANCE_RUN=1`, `SEED_METHOD=SEED_VOLATILE`,
`MEM_METHOD=MEM_MALLOC` (heap), compile-time pinned `ITERATIONS`
(default 60000, > 10 s runtime for a valid score; the `linux` port's default
`SEED_ARG` would ignore `-DITERATIONS` and fall back to auto-calibration,
hence `SEED_VOLATILE`).

## Build and run

```
make                 # or: make ITERATIONS=60000
make push run        # scp both binaries to rk3506-buildroot:~/coremark and run both
make size            # text/data/bss sizes
```

Runs are pinned with `taskset -c 0`. Board clock is fixed at 1.2 GHz (no
cpufreq), see `docs/dts_adc_notes.md` - no governor interference.

## Results - rk3506-mini (3x Cortex-A7 @ 1.2 GHz, single core, fixed clock)

Validated runs (CRC checked correct), 60000 iterations each, GCC 12.4.0
(Buildroot arm-buildroot-linux-gnueabihf), Buildroot linux-6.1-stan-rkr4.3:

| Binary         | ISA (our units) | CoreMark/s   | CoreMark/MHz | text   | data | bss |
|----------------|-----------------|--------------|--------------|--------|------|-----|
| coremark-arm   | ARM (A32)       | 4682.744088  | 3.90         | 25 100 | 436  | 32  |
| coremark-thumb | Thumb-2 (T32)   | 4618.937644  | 3.85         | 19 400 | 436  | 32  |

Observations:

- ~3.9 CoreMark/MHz, in line with published Cortex-A7 figures and clearly
  above the PocketBeagle's Cortex-A8 (3.07-3.11 CM/MHz @ 1 GHz,
  same 40000-iteration methodology).
- On this board ARM (A32) edged out Thumb-2 by ~1.4 % - opposite of the
  PocketBeagle A8 (Thumb-2 +1.3 % there) and the MP157 A7 reference
  (Thumb-2 +0.5 %). The difference is small; both directions have been
  seen on A7, likely LTO/code-layout luck. Thumb-2 remains ~19 % denser.
- SoC temperature stayed at ~45.6 -> 46.8 C during the runs (one core of
  three busy, no thermal throttling exists on this image - clock is fixed).
- Pure single-ISA binaries are not feasible: glibc/crt objects carry their
  own ISA attributes (mixed `Tag_ARM_ISA_use` + `Tag_THUMB_ISA_use` in both
  final ELFs), same as on the other boards. The build flags guarantee the
  ISA of all project compilation units, which dominate the hot path.
- 3 cores available but CoreMark here is single-core (standard rules);
  with 3 parallel instances the board should scale ~3x if ever needed.
