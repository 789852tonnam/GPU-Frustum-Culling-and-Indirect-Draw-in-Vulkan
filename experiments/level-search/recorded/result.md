# Fresh group-level tuning at all four object counts

**Author-selected primary results:** On 12 November 2026, the project author chose this fresh per-count search and independent four-round confirmation as the primary result set for the project. For 1,638,400 spheres, use L5 (64 × 64): median cull reduction 15.7% and median total reduction 2.7%. Keep the per-count/per-metric selections, paired results and uncertainty below; earlier fixed-L3 tables remain historical experiments and are not pooled into these results.

Every count was screened independently; no previous level winner was assumed. All distinct grouped inputs were measured. Independent confirmation compares the union of two lowest screening cull levels and two lowest screening total levels, plus a fresh per-object reference.

Reported winners are the lowest observed medians among independently confirmed candidates. One screening run per other level does not establish a statistically unique global optimum. Small median differences must be read alongside ranges and paired results.

## Culling: independent four-round confirmation

| Spheres | Normal ms | Fastest confirmed group | Group ms | Reduction vs normal | Fastest including reference |
|---:|---:|---|---:|---:|---|
| 102,400 | 0.123 | L2 (8×8) | 0.121 | +1.6% | L2 |
| 409,600 | 0.393 | L2 (8×8) | 0.378 | +3.9% | L2 |
| 1,638,400 | 1.774 | L5 (64×64) | 1.495 | +15.7% | L5 |
| 6,553,600 | 5.708 | L3 (16×16) | 4.915 | +13.9% | L3 |

## Total GPU interval: independent four-round confirmation

| Spheres | Normal ms | Fastest confirmed group | Group ms | Reduction vs normal | Fastest including reference |
|---:|---:|---|---:|---:|---|
| 102,400 | 2.949 | L2 (8×8) | 2.903 | +1.6% | L2 |
| 409,600 | 5.727 | L3 (16×16) | 5.669 | +1.0% | L3 |
| 1,638,400 | 22.739 | L5 (64×64) | 22.133 | +2.7% | L5 |
| 6,553,600 | 101.964 | L2 (8×8) | 96.657 | +5.2% | L2 |

## Confirmation details

| Spheres | Method | Cull median [min–max] ms | Total median [min–max] ms | Paired cull reductions | Paired total reductions |
|---:|---|---|---|---|---|
| 102,400 | Per-object | 0.123 [0.118–0.138] | 2.949 [2.930–3.620] | +0.0%, +0.0%, +0.0%, +0.0% | +0.0%, +0.0%, +0.0%, +0.0% |
| 102,400 | L1 | 0.135 [0.122–0.287] | 2.926 [2.860–3.558] | -3.4%, -16.8%, -2.3%, -108.8% | +2.4%, -1.2%, +2.7%, +1.7% |
| 102,400 | L2 | 0.121 [0.109–0.167] | 2.903 [2.836–3.407] | -9.2%, +7.3%, +12.4%, -21.2% | +1.0%, +1.7%, +3.6%, +5.9% |
| 409,600 | Per-object | 0.393 [0.376–0.444] | 5.727 [5.601–7.473] | +0.0%, +0.0%, +0.0%, +0.0% | +0.0%, +0.0%, +0.0%, +0.0% |
| 409,600 | L1 | 0.473 [0.436–0.700] | 5.989 [5.667–6.395] | -9.0%, -19.3%, -29.0%, -57.5% | -0.7%, -3.6%, -14.2%, +20.5% |
| 409,600 | L2 | 0.378 [0.367–0.448] | 6.358 [5.611–6.962] | +7.6%, -2.7%, +5.0%, -0.8% | +0.3%, -14.2%, -24.3%, +18.9% |
| 409,600 | L3 | 0.407 [0.383–0.460] | 5.669 [5.585–5.809] | +2.3%, -12.7%, +0.9%, -3.6% | -0.4%, +2.4%, +0.3%, +22.3% |
| 409,600 | L4 | 0.463 [0.414–0.532] | 5.772 [5.650–6.083] | -20.0%, -10.2%, -15.2%, -19.7% | -8.1%, +3.1%, -2.0%, +21.9% |
| 1,638,400 | Per-object | 1.774 [1.497–2.004] | 22.739 [20.173–24.397] | +0.0%, +0.0%, +0.0%, +0.0% | +0.0%, +0.0%, +0.0%, +0.0% |
| 1,638,400 | L1 | 1.902 [1.770–1.967] | 23.447 [22.528–24.767] | +2.0%, +6.9%, -31.4%, -12.8% | -1.4%, -14.8%, -6.4%, +2.7% |
| 1,638,400 | L3 | 1.552 [1.341–1.655] | 23.438 [21.785–27.444] | +21.8%, +22.3%, -10.6%, +14.6% | -7.9%, -13.5%, +6.4%, -12.5% |
| 1,638,400 | L5 | 1.495 [1.296–1.637] | 22.133 [21.059–23.154] | +17.2%, +35.3%, -3.7%, +8.4% | -3.1%, -6.0%, +0.5%, +13.7% |
| 6,553,600 | Per-object | 5.708 [5.553–6.285] | 101.964 [88.934–104.016] | +0.0%, +0.0%, +0.0%, +0.0% | +0.0%, +0.0%, +0.0%, +0.0% |
| 6,553,600 | L2 | 5.303 [4.768–5.705] | 96.657 [91.186–98.860] | +2.9%, -2.5%, +14.1%, +21.6% | +5.0%, -9.9%, +10.6%, +6.3% |
| 6,553,600 | L3 | 4.915 [3.613–5.640] | 97.250 [94.176–99.514] | +38.2%, -1.3%, +14.2%, +19.4% | +8.1%, -11.2%, +7.6%, +2.4% |
| 6,553,600 | L6 | 5.259 [5.121–5.705] | 101.413 [93.896–105.769] | +2.4%, +4.8%, +7.8%, +17.0% | +0.4%, -5.6%, +2.7%, -3.7% |

## Screening: single-run observations

| Spheres | Method | Block side | Cull ms | Total ms |
|---:|---|---:|---:|---:|
| 102,400 | Per-object | — | 0.102 | 2.797 |
| 102,400 | L1 | 4 | 0.113 | 2.815 |
| 102,400 | L2 | 8 | 0.115 | 2.885 |
| 102,400 | L3 | 16 | 0.159 | 2.969 |
| 102,400 | L4 | 32 | 0.169 | 2.931 |
| 102,400 | L5 | 64 | 0.426 | 3.198 |
| 102,400 | L6 | 128 | 1.266 | 4.050 |
| 102,400 | L7 | 256 | 3.625 | 6.489 |
| 102,400 | L8 | 512 | 5.227 | 7.931 |
| 409,600 | Per-object | — | 0.419 | 5.805 |
| 409,600 | L1 | 4 | 0.461 | 5.901 |
| 409,600 | L2 | 8 | 0.363 | 6.081 |
| 409,600 | L3 | 16 | 0.406 | 6.271 |
| 409,600 | L4 | 32 | 0.442 | 5.678 |
| 409,600 | L5 | 64 | 0.626 | 6.144 |
| 409,600 | L6 | 128 | 1.271 | 6.987 |
| 409,600 | L7 | 256 | 4.909 | 11.166 |
| 409,600 | L8 | 512 | 14.823 | 20.647 |
| 409,600 | L9 | 1024 | 23.390 | 29.727 |
| 1,638,400 | Per-object | — | 1.856 | 22.908 |
| 1,638,400 | L1 | 4 | 2.045 | 21.162 |
| 1,638,400 | L2 | 8 | 1.543 | 24.672 |
| 1,638,400 | L3 | 16 | 1.530 | 22.678 |
| 1,638,400 | L4 | 32 | 1.643 | 25.573 |
| 1,638,400 | L5 | 64 | 1.490 | 20.784 |
| 1,638,400 | L6 | 128 | 2.280 | 23.634 |
| 1,638,400 | L7 | 256 | 5.031 | 29.284 |
| 1,638,400 | L8 | 512 | 18.184 | 38.194 |
| 1,638,400 | L9 | 1024 | 67.141 | 87.998 |
| 1,638,400 | L10 | 2048 | 94.586 | 119.451 |
| 6,553,600 | Per-object | — | 6.581 | 102.175 |
| 6,553,600 | L1 | 4 | 6.119 | 113.505 |
| 6,553,600 | L2 | 8 | 5.375 | 89.075 |
| 6,553,600 | L3 | 16 | 5.033 | 93.954 |
| 6,553,600 | L4 | 32 | 6.011 | 108.267 |
| 6,553,600 | L5 | 64 | 5.562 | 108.826 |
| 6,553,600 | L6 | 128 | 4.592 | 92.314 |
| 6,553,600 | L7 | 256 | 7.895 | 98.897 |
| 6,553,600 | L8 | 512 | 14.665 | 112.962 |
| 6,553,600 | L9 | 1024 | 69.382 | 172.443 |
| 6,553,600 | L10 | 2048 | 268.161 | 380.170 |
| 6,553,600 | L11 | 4096 | 396.411 | 518.841 |

## Protocol and limits

Completed 106 runs: 42 screening runs plus four fresh confirmation rounds for every selected condition. 4885 measured frequency samples span 1997–2000 MHz. All final CPU command oracles pass. Original auto/min 600/max 3000 MHz restored, guard exit 0 and controller readback verified.

- Radeon 860M/RADV, AC power, fixed 1536 × 864 extent, identical renderer/shaders, sphere radius 0.25 m / spacing 1 m, camera/far plane and fixed-slot indirect path. Only external benchmark stop/camera control changes; production sources untouched.
- Screening: levels 0–8 at 320², 0–9 at 640², 0–10 at 1280², 0–11 at 2560². Full-grid CPU tests verified all higher accepted levels have identical shader-read group fields and member IDs to the one-group representative; they are not untested alternatives.
- Screening uses 180 fixed angles0,2,...358° except 2560², which uses 36 angles0,10,...350° to bound pathological large-group runtime. Every level within a count uses the same screening angles. Screening numbers are not pooled into confirmation or used for cross-count speed claims.
- Confirmation always uses 180 angles0–358° and four run means per condition, with order/reversed order/rotation/reversed rotation at each count. Counts reverse order every other round. Source orders and selection are saved separately. Percentages use each count’s fresh confirmation reference, not old tables.
- Every run requires >=5 s warm-up and >=1 s observed stable 1990–2010 MHz with spread <=10 MHz, monitored during measurement. A clock deviation, resize, missing oracle or failed query invalidates the run. Renderer stays unprivileged; the transactional guard has an 1800-second fail-safe and restores on controller exit/EOF.
- Median/min/max are computed across run means. GPU elapsed markers may include dependency waits and overlap; cull+draw is not total, and these are not isolated shader costs or FPS. FIFO unchanged; memory clocks, package power and desktop activity remain uncontrolled.
- Counts already exceed the 100 m far-plane footprint; increasing count predominantly adds rejected objects and command slots. Static scenes and final-frame oracles do not cover every scene/motion/device.

Evidence: protocol.json, screen-order.json, selection.json, confirmation-order.json, runs.json, summary.json, screen/confirm CSVs, best-confirmed.json, equivalent-levels.txt and its CPU check source, raw per-run logs/clock traces, and guard/status restoration logs. No old measurements pooled, source optimization, commit, push or proposal edit.
