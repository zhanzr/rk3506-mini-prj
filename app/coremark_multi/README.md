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
| rr L1  | 4593.93     | 4711.82     | 4698.38     | one core at a time |
| rr L2  | 4593.93     | 4705.09     | 4704.30     | one core at a time |
| par L1 | 4453.28     | 4223.55     | 4190.36     | wall 13.37 s |
| par L2 | 4439.16     | 4228.33     | 4183.79     | wall 13.39 s |

Observations:

- Round robin: each core independently lands in the 4594-4712 band,
  consistent with the single-core `coremark-arm` figure (4682.7 CM/s
  at 60000 iterations). An earlier one-off cpu2 reading of 4096 CM/s
  was background-activity interference during that slot; re-runs are
  stable.
- Parallel: all three run CRC-valid simultaneously; per-core scores
  drop ~8-11% from the round-robin level (shared L2/DDR bandwidth),
  cpu0 the least affected. No per-core frequency data exists in this
  image (no cpufreq); the board clock is fixed at 1.2 GHz.

Raw data: `results.csv` on the board (regenerate by running).
