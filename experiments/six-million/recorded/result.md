# Stable-clock object-count comparison, including 6,553,600 spheres

Per-object GPU culling versus fixed 16 × 16 groups (level 3). Each entry is the median of four run means; each run measures 180 fixed yaw angles after calibrated warm-up.

| Spheres | Normal cull ms | Grouped cull ms | Cull reduction | Normal total ms | Grouped total ms | Total reduction |
|---:|---:|---:|---:|---:|---:|---:|
| 102,400 | 0.119 | 0.148 | -24.5% | 2.944 | 2.951 | -0.2% |
| 409,600 | 0.392 | 0.415 | -5.9% | 5.656 | 5.829 | -3.1% |
| 1,638,400 | 1.829 | 1.374 | +24.9% | 22.365 | 20.608 | +7.9% |
| 6,553,600 | 5.387 | 4.833 | +10.3% | 102.660 | 106.776 | -4.0% |

Reduction = 100 × (1 − grouped / normal), computed before rounding. Negative means slower. The first three rows retain the previous session; the six-million row is a later extension with identical binary, shader hashes and protocol. Percentages always use the reference measured at the same count in the same session.

## Six-million extension: repetitions and limits

| Metric | Normal min–max ms | Grouped min–max ms | Paired reductions by round |
|---|---:|---:|---|
| cull_ms | 5.163–5.598 | 4.775–4.951 | +9.6%, +11.0%, +7.5%, +11.6% |
| total_ms | 95.651–113.237 | 100.507–113.435 | -5.1%, -0.2%, -7.8%, -0.3% |

Eight new runs, 1061 measured sensor samples at 1997–2000 MHz. Every new final-command oracle passes; 11,053 objects are visible at the final angle in all tested counts. Static grids already exceed the camera's 100 m far plane, so increasing count mostly increases rejected objects and command slots while visible geometry stays constant.

Same Radeon 860M/RADV, AC power, 1536 × 864 extent, Release binary, radius 0.25 m, spacing 1 m, 180 angles 0–358° by 2°, four paired-order rounds (normal first twice, grouped first twice). Warm-up ≥5 seconds and ≥1-second clock stability window; accept 1990–2010 MHz with ≤10 MHz spread, monitored throughout. Guard restored auto/min 600/max 3000 MHz and exited 0.

GPU elapsed intervals can include dependency waits and overlap. Do not sum cull+draw or claim FPS from total. Memory clocks, package power and desktop workloads were not controlled; separate sessions and a small repeat count limit cross-count conclusions. This sweep does not locate an exact crossover or establish that grouping always helps at higher counts. Capture runs and validation overhead are excluded from these timings.

Evidence: protocol.json, runs.json, summary.json, combined-results.csv, per-run stdout/stderr/clock CSVs, status.json and guard logs. The previous three-count evidence is preserved in ../clock-2000-count-ablation/. No application code changes, commit, push or proposal edit.
