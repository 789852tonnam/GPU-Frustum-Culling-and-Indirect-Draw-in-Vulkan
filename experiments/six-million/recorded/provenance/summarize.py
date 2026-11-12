from pathlib import Path
import csv
import hashlib
import json
import re
import statistics

HERE = Path(__file__).resolve().parent
PREVIOUS = HERE.parent / 'clock-2000-count-ablation'
new_runs = json.loads((HERE / 'runs.json').read_text())
status = json.loads((HERE / 'status.json').read_text())
assert len(new_runs) == 8 and status['status'] == 'completed' and status['guard_exit'] == 0
assert status['final_mode'] == 'auto' and '600Mhz' in status['final_limits'] and '3000Mhz' in status['final_limits']
clocks = []
for row in new_runs:
    label = f"r{row['repeat']}-side-{row['gridSide']}-level-{row['level']}"
    with (HERE / f'{label}.clocks.csv').open() as f:
        samples = [float(x['mhz']) for x in csv.DictReader(f) if x['phase'] == 'measure']
    assert len(samples) == row['clock_samples'] and 1990 <= min(samples) <= max(samples) <= 2010
    assert max(samples) - min(samples) <= 10 and row['warmup_seconds'] >= 5 and row['samples'] == 180
    assert row['power_before']['ac'] == row['power_after']['ac'] == '1'
    log = (HERE / f'{label}.stdout').read_text()
    error = (HERE / f'{label}.stderr').read_text()
    match = re.search(r'culling verified: (\d+)/6553600 visible; all commands match CPU reference', log)
    assert match and int(match[1]) == 11053 and not re.search(r'VUID-|Validation Error|SYNC-HAZARD', log + error)
    clocks.extend(samples)

protocol = json.loads((HERE / 'protocol.json').read_text())
old_protocol = json.loads((PREVIOUS / 'protocol.json').read_text())
assert protocol['binarySha256'] == old_protocol['binarySha256'] == hashlib.sha256((HERE / 'gpu_group_benchmark').read_bytes()).hexdigest()
assert protocol['shaderSha256'] == old_protocol['shaderSha256']
protocol.update(observedClockMinMhz=min(clocks), observedClockMaxMhz=max(clocks),
                observedClockSamples=len(clocks), restored=True, finalVisibleCount=11053)
(HERE / 'protocol.json').write_text(json.dumps(protocol, indent=2) + '\n')

all_runs = json.loads((PREVIOUS / 'runs.json').read_text()) + new_runs
table = []
for count in (102400, 409600, 1638400, 6553600):
    methods = []
    for level in (0, 3):
        runs = [r for r in all_runs if r['sphereCount'] == count and r['level'] == level]
        assert len(runs) == 4 and {r['repeat'] for r in runs} == {1, 2, 3, 4}
        methods.append({metric: statistics.median(r[metric] for r in runs)
                        for metric in ('cull_ms', 'total_ms')})
    normal, grouped = methods
    table.append(dict(spheres=count, normal_cull_ms=normal['cull_ms'], grouped_cull_ms=grouped['cull_ms'],
                      cull_reduction_percent=100*(1-grouped['cull_ms']/normal['cull_ms']),
                      normal_total_ms=normal['total_ms'], grouped_total_ms=grouped['total_ms'],
                      total_reduction_percent=100*(1-grouped['total_ms']/normal['total_ms']),
                      source='clock-2000-six-million' if count == 6553600 else 'clock-2000-count-ablation'))
with (HERE / 'combined-results.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=table[0].keys())
    writer.writeheader()
    writer.writerows(table)

lines = ['# Stable-clock object-count comparison, including 6,553,600 spheres', '',
         'Per-object GPU culling versus fixed 16 × 16 groups (level 3). Each entry is the median of four run means; each run measures 180 fixed yaw angles after calibrated warm-up.', '',
         '| Spheres | Normal cull ms | Grouped cull ms | Cull reduction | Normal total ms | Grouped total ms | Total reduction |',
         '|---:|---:|---:|---:|---:|---:|---:|']
for r in table:
    lines.append(f"| {r['spheres']:,} | {r['normal_cull_ms']:.3f} | {r['grouped_cull_ms']:.3f} | {r['cull_reduction_percent']:+.1f}% | {r['normal_total_ms']:.3f} | {r['grouped_total_ms']:.3f} | {r['total_reduction_percent']:+.1f}% |")
lines += ['', 'Reduction = 100 × (1 − grouped / normal), computed before rounding. Negative means slower. The first three rows retain the previous session; the six-million row is a later extension with identical binary, shader hashes and protocol. Percentages always use the reference measured at the same count in the same session.', '',
          '## Six-million extension: repetitions and limits', '',
          '| Metric | Normal min–max ms | Grouped min–max ms | Paired reductions by round |',
          '|---|---:|---:|---|']
for metric in ('cull_ms', 'total_ms'):
    a = sorted((r for r in new_runs if r['level'] == 0), key=lambda r: r['repeat'])
    b = sorted((r for r in new_runs if r['level'] == 3), key=lambda r: r['repeat'])
    paired = [100*(1-y[metric]/x[metric]) for x, y in zip(a, b)]
    lines.append(f"| {metric} | {min(r[metric] for r in a):.3f}–{max(r[metric] for r in a):.3f} | {min(r[metric] for r in b):.3f}–{max(r[metric] for r in b):.3f} | " + ', '.join(f'{v:+.1f}%' for v in paired) + ' |')
lines += ['', f"Eight new runs, {len(clocks)} measured sensor samples at {min(clocks):.0f}–{max(clocks):.0f} MHz. Every new final-command oracle passes; 11,053 objects are visible at the final angle in all tested counts. Static grids already exceed the camera's 100 m far plane, so increasing count mostly increases rejected objects and command slots while visible geometry stays constant.", '',
          'Same Radeon 860M/RADV, AC power, 1536 × 864 extent, Release binary, radius 0.25 m, spacing 1 m, 180 angles 0–358° by 2°, four paired-order rounds (normal first twice, grouped first twice). Warm-up ≥5 seconds and ≥1-second clock stability window; accept 1990–2010 MHz with ≤10 MHz spread, monitored throughout. Guard restored auto/min 600/max 3000 MHz and exited 0.', '',
          'GPU elapsed intervals can include dependency waits and overlap. Do not sum cull+draw or claim FPS from total. Memory clocks, package power and desktop workloads were not controlled; separate sessions and a small repeat count limit cross-count conclusions. This sweep does not locate an exact crossover or establish that grouping always helps at higher counts. Capture runs and validation overhead are excluded from these timings.', '',
          'Evidence: protocol.json, runs.json, summary.json, combined-results.csv, per-run stdout/stderr/clock CSVs, status.json and guard logs. The previous three-count evidence is preserved in ../clock-2000-count-ablation/. No application code changes, commit, push or proposal edit.']
(HERE / 'result.md').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines[:10]))
print(json.dumps(dict(samples=len(clocks), minimum_mhz=min(clocks), maximum_mhz=max(clocks))))
