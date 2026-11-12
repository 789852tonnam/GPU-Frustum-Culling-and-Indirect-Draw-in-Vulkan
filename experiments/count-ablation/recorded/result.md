# Object-count ablation at stable 2000 MHz

Per-object GPU frustum culling versus grouped culling with fixed 16 × 16 blocks (level3). Four paired-order repeats per mode/count,180fixed yaw angles per run, identical renderer and draw path. Fresh references from this session; do not pool earlier tables.

| Spheres | Normal cull ms | Grouped cull ms | Cull reduction | Normal total ms | Grouped total ms | Total reduction |
|---:|---:|---:|---:|---:|---:|---:|
| 102,400 | 0.119 | 0.148 | -24.5% | 2.944 | 2.951 | -0.2% |
| 409,600 | 0.392 | 0.415 | -5.9% | 5.656 | 5.829 | -3.1% |
| 1,638,400 | 1.829 | 1.374 | +24.9% | 22.365 | 20.608 | +7.9% |

Entries are medians of four run means. Reduction = 100×(1−grouped/normal), calculated before rounding; negative means slower.

At 102,400 objects grouped culling was slower in all four paired rounds. At 409,600 objects paired culling signs were mixed. At 1,638,400 objects grouped culling was faster in all four pairs (16.6–39.7% paired reductions), median reduction 24.9%. The exact crossover is not identified by these three sizes.

At 1,638,400 objects total median decreased 7.9%, but two paired rounds were slower and two faster; grouped total ranges 17.607–23.346 ms overlap normal 21.875–23.238 ms. This is not an established stable total/FPS improvement. Previous group-size results are a separate session; do not treat their different means as a regression.

## Conditions and correctness

- 24 successful runs; 531 measured sensor samples at 1997–2000 MHz, warm-up >= 5 s plus >= 1 s stable window inside 1990–2010 MHz with spread <= 10 MHz; same checks throughout measurement. AC online 1; 1536 × 864; 180 angles 0–358 degrees by 2 degrees; Release-O3.
-Final commands of every run match CPU reference. All counts end at11,053visible objects: the320/640/1280 grids already extend beyond the 100 m far plane, so increasing N mostly adds rejected work while visible geometry is unchanged. Radius 0.25 m / spacing 1 m; camera/floor/shading fixed. Correctness readback is outside timing. The oracle covers the final frame per run plus the existing boundary/smoke test suite, not every measured frame.
-Timestamp intervals include command writes and GPU dependency waits and can overlap; do not sum cull+draw or call them isolated shader time/FPS. FIFO presentation unchanged. Memory clock/package power/desktop activity remain uncontrolled. No formal significance claim or other-GPU test.
-Guard restored auto/min 600/max 3000 MHz; root and controller readback passed, exit 0. Success/failed-write restoration mocks and clock-gate boundary checks passed before execution. No permanent GPU settings or production-source edits.

## Reproduction and evidence

-protocol.json: parameters, compiler/source/shader hashes and observed clock conditions; binary, main_benchmark.cpp, clock_gate.hpp, run_comparison.py and clock_guard.py reuse the existing external harness. Root guard only adjusts clocks; renderer remains unprivileged.
-runs.json/summary.json/results.csv: unrounded statistics. Per-run stdout/stderr and clock CSVs preserve oracle/clock checks. status.json and guard.stdout verify completed 24 runs and restoration.
-The paired order is recorded in protocol.json; normal/grouped is first twice each for each count. Do not describe this as a fully counterbalanced 6-condition Latin square.
