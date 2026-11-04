# coremark_multi

Multi-core CoreMark verification for the RK3506-mini (3x Cortex-A7).
The purpose is **not** a tailored parallel CoreMark: every run is a
standard, unmodified single-core CoreMark process. Three independent
processes are pinned to the three cores with `sched_setaffinity`
(raw syscall, no glibc cpu_set_t), verifying the multi-core features
of the chip:

- **round robin** (`--round-robin`): run one instance per core, one
  core at a time; each core gets its own score
- **parallel** (`--parallel`): run one instance on every core
  simultaneously; each core gets its own score + the combined
  throughput
- `--loops N`: repeat the phase(s) N times (default 2)
- every result is appended to `results.csv` (unix_time, mode, loop,
  cpu, iterations, elapsed_s, coremark_s, crc_valid)

Only the **coremark-arm** configuration is used (A32), the variant
proven fastest in `../coremark/` (4682.7 CoreMark/s single-core,
ARM (A32), 60000 iterations, CRC-validated).

Worker isolation: each CoreMark instance is an independent OS process
(fork + exec of this same binary in `--worker` mode), so every
instance has its own address space and globals - the benchmark core
is shared code compiled with `-Dmain=coremark_entry`.

## Build and run

```
make                 # COREMARK_ITERATIONS=46000 default (~10 s/run)
make push
ssh rk3506-buildroot 'cd /root/coremark_multi && ./coremark_multi \
    --round-robin --parallel --loops 2 --secs 10'
```

Timing is the A7 monotonic clock around each pinned process; per-core
scores are parsed from each worker's own CoreMark report (score line +
CRC validation lines), and results are recorded to `results.csv`.

Board clock is fixed at 1.2 GHz (no cpufreq), see
`../../docs/dts_adc_notes.md`.

## Expected numbers

- round robin: each core ~4680 CoreMark/s (matches the single-core
  `coremark-arm` result at 46000 iterations)
- parallel: each core a bit lower (shared L2/DDR bandwidth), the sum
  clearly above any single core - the multi-core proof

## Results - rk3506-mini (3x Cortex-A7 @ 1.2 GHz, fixed clock)

Verified run (56,160 iterations/run, 2 loops, CRC validated on every
run, SoC 45-48 C):

| Phase  | cpu0 (CM/s) | cpu1 (CM/s) | cpu2 (CM/s) | notes |
|--------|-------------|-------------|-------------|-------|
| rr L1  | 4588.28     | 4712.61     | 4095.66     | one core at a time |
| rr L2  | 4594.31     | 4713.41     | 4705.88     | cpu2 L1 slot overlapped with background scp |
| par L1 | 4475.35     | 4153.69     | 4261.80     | wall 13.49 s, combined 12,890.8 CM/s |
| par L2 | 4243.39     | 4425.48     | 4173.50     | wall 13.43 s, combined 12,842.4 CM/s |

Observations:

- Round robin: each core independently matches the single-core
  `coremark-arm` figure (4590-4716 CM/s; cpu2's first slot overlapped
  with the deploy scp traffic - an honest illustration of background
  sensitivity on a live board).
- Parallel: all three run CRC-valid simultaneously; combined
  throughput ~12,842-12,891 CoreMark/s (~2.75x the single-core score).
  Per-core scores drop ~5-13% (shared L2/DDR bandwidth), and the
  fastest core rotates between runs - scheduler placement effects.
- Per-core frequencies cannot be read from this image (no cpufreq);
  the board clock is fixed at 1.2 GHz.

Raw data: `results.csv` on the board (regenerate by running).
