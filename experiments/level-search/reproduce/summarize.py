from pathlib import Path
import csv
import hashlib
import json
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit('Usage: summarize.py RUN_OUTPUT_DIRECTORY')
HERE = Path(sys.argv[1]).resolve()
status = json.loads((HERE / 'status.json').read_text())
rows = json.loads((HERE / 'runs.json').read_text())
stats = json.loads((HERE / 'summary.json').read_text())
chosen = json.loads((HERE / 'selection.json').read_text())
assert status['status'] == 'completed' and status['guard_exit'] == 0 and status['final_mode'] == 'auto'
assert '600Mhz' in status['final_limits'] and '3000Mhz' in status['final_limits']
assert sum(r['phase'] == 'screen' for r in rows) == 42
assert sum(r['phase'] == 'confirm' for r in rows) == 4 * sum(1+len(v) for v in chosen.values())
clocks = []
for row in rows:
    prefix = f"{row['phase']}-r{row['repeat']}-side-{row['gridSide']}-level-{row['level']}"
    with (HERE / f'{prefix}.clocks.csv').open() as f:
        measured = [float(r['mhz']) for r in csv.DictReader(f) if r['phase'] == 'measure']
    assert len(measured) == row['clock_samples'] and 1990 <= min(measured) <= max(measured) <= 2010
    assert max(measured)-min(measured) <= 10 and row['warmup_seconds'] >= 5
    assert row['samples'] == (36 if row['phase'] == 'screen' and row['gridSide'] == 2560 else 180)
    assert row['extent'] == [1536, 864] and row['power_before']['ac'] == row['power_after']['ac'] == '1'
    output = (HERE / f'{prefix}.stdout').read_text()
    error = (HERE / f'{prefix}.stderr').read_text()
    assert re.search(rf"culling verified: \d+/{row['sphereCount']} visible; all commands match CPU reference", output)
    assert not re.search(r'VUID-|Validation Error|SYNC-HAZARD', output+error)
    clocks.extend(measured)

protocol = json.loads((HERE / 'protocol.json').read_text())
assert hashlib.sha256((HERE / 'benchmark-180').read_bytes()).hexdigest() == protocol['fullBinarySha256']
assert hashlib.sha256((HERE / 'benchmark-36').read_bytes()).hexdigest() == protocol['screen36BinarySha256']
assert (HERE / 'main_screen36.cpp').read_text().replace('measurementFrames = 36;', 'measurementFrames = 180;') == (HERE / 'main_benchmark.cpp').read_text()
protocol.update(observedClockMinMhz=min(clocks), observedClockMaxMhz=max(clocks),
                observedClockSamples=len(clocks), completedRuns=len(rows), restored=True,
                selectedLevels=chosen)
(HERE / 'protocol.json').write_text(json.dumps(protocol, indent=2)+'\n')

for phase in ('screen', 'confirm'):
    with (HERE / f'{phase}-results.csv').open('w') as f:
        fields = ['spheres','level','group_side','runs','cull_median_ms','cull_min_ms','cull_max_ms',
                  'cull_reduction_percent','total_median_ms','total_min_ms','total_max_ms','total_reduction_percent']
        writer = csv.writer(f)
        writer.writerow(fields)
        for s in stats:
            if s['phase'] != phase:
                continue
            writer.writerow([s['sphereCount'],s['level'],s['groupSide'],s['runs'],
                             *[s['cull_ms'][k] for k in ('median','minimum','maximum','reduction_percent')],
                             *[s['total_ms'][k] for k in ('median','minimum','maximum','reduction_percent')]])

best = []
for n in (320,640,1280,2560):
    pool = [s for s in stats if s['phase']=='confirm' and s['gridSide']==n]
    reference = next(s for s in pool if s['level']==0)
    record = dict(spheres=n*n, reference=reference)
    for metric in ('cull_ms','total_ms'):
        record[metric] = dict(best_group=min((s for s in pool if s['level']), key=lambda s:(s[metric]['median'],s['level'])),
                              best_overall=min(pool, key=lambda s:(s[metric]['median'],s['level'])))
    best.append(record)
(HERE / 'best-confirmed.json').write_text(json.dumps(best,indent=2)+'\n')

lines = ['# Fresh group-level tuning at all four object counts', '',
         'Every count was screened independently; no previous level winner was assumed. All distinct grouped inputs were measured. Independent confirmation compares the union of two lowest screening cull levels and two lowest screening total levels, plus a fresh per-object reference.', '',
         'Reported winners are the lowest observed medians among independently confirmed candidates. One screening run per other level does not establish a statistically unique global optimum. Small median differences must be read alongside ranges and paired results.', '']
for metric,title in [('cull_ms','Culling'),('total_ms','Total GPU interval')]:
    lines += [f'## {title}: independent four-round confirmation', '',
              '| Spheres | Normal ms | Fastest confirmed group | Group ms | Reduction vs normal | Fastest including reference |',
              '|---:|---:|---|---:|---:|---|']
    for record in best:
        s=record[metric]['best_group']; overall=record[metric]['best_overall']
        label=f"L{s['level']} ({s['groupSide']}×{s['groupSide']})"
        winner='Per-object' if overall['level']==0 else f"L{overall['level']}"
        lines.append(f"| {record['spheres']:,} | {record['reference'][metric]['median']:.3f} | {label} | {s[metric]['median']:.3f} | {s[metric]['reduction_percent']:+.1f}% | {winner} |")
    lines += ['']
lines += ['## Confirmation details', '',
          '| Spheres | Method | Cull median [min–max] ms | Total median [min–max] ms | Paired cull reductions | Paired total reductions |',
          '|---:|---|---|---|---|---|']
for s in stats:
    if s['phase']!='confirm': continue
    label='Per-object' if s['level']==0 else f"L{s['level']}"
    values=[f"{s[m]['median']:.3f} [{s[m]['minimum']:.3f}–{s[m]['maximum']:.3f}]" for m in ('cull_ms','total_ms')]
    paired=[', '.join(f'{v:+.1f}%' for v in s[m]['paired_reduction_percent']) for m in ('cull_ms','total_ms')]
    lines.append(f"| {s['sphereCount']:,} | {label} | {values[0]} | {values[1]} | {paired[0]} | {paired[1]} |")
lines += ['', '## Screening: single-run observations', '',
          '| Spheres | Method | Block side | Cull ms | Total ms |', '|---:|---|---:|---:|---:|']
for s in stats:
    if s['phase']!='screen': continue
    label='Per-object' if s['level']==0 else f"L{s['level']}"
    lines.append(f"| {s['sphereCount']:,} | {label} | {s['groupSide'] or '—'} | {s['cull_ms']['median']:.3f} | {s['total_ms']['median']:.3f} |")
lines += ['', '## Protocol and limits', '',
          f"Completed {len(rows)} runs: 42 screening runs plus four fresh confirmation rounds for every selected condition. {len(clocks)} measured frequency samples span {min(clocks):.0f}–{max(clocks):.0f} MHz. All final CPU command oracles pass. Original auto/min600/max3000MHz restored, guard exit0 and controller readback verified.", '',
          '- Radeon 860M/RADV, AC power, fixed1536×864 extent, identical renderer/shaders, sphere radius0.25m/spacing1m, camera/far plane and fixed-slot indirect path. Only external benchmark stop/camera control changes; production sources untouched.',
          '- Screening: levels0–8 at320²,0–9 at640²,0–10 at1280²,0–11 at2560². Full-grid CPU tests verified all higher accepted levels have identical shader-read group fields and member IDs to the one-group representative; they are not untested alternatives.',
          '- Screening uses180fixed angles0,2,...358° except2560², which uses36angles0,10,...350° to bound pathological large-group runtime. Every level within a count uses the same screening angles. Screening numbers are not pooled into confirmation or used for cross-count speed claims.',
          '- Confirmation always uses180angles0–358° and four run means per condition, with order/reversed order/rotation/reversed rotation at each count. Counts reverse order every other round. Source orders and selection are saved separately. Percentages use each count’s fresh confirmation reference, not old tables.',
          '- Every run requires>=5s warm-up and>=1s observed stable1990–2010MHz/spread<=10MHz, monitored during measurement. A clock deviation, resize, missing oracle or failed query invalidates the run. Renderer stays unprivileged; the transactional guard has a1800-second fail-safe and restores on controller exit/EOF.',
          '- Median/min/max are computed across run means. GPU elapsed markers may include dependency waits and overlap; cull+draw is not total, and these are not isolated shader costs or FPS. FIFO unchanged; memory clocks, package power and desktop activity remain uncontrolled.',
          '- Counts already exceed the100mfar-plane footprint; increasing count predominantly adds rejected objects and command slots. Static scenes and final-frame oracles do not cover every scene/motion/device.', '',
          'Evidence: protocol.json, screen-order.json, selection.json, confirmation-order.json, runs.json, summary.json, screen/confirm CSVs, best-confirmed.json, equivalent-levels.txt and its CPU check source, raw per-run logs/clock traces, and guard/status restoration logs. No old measurements pooled, source optimization, commit, push or proposal edit.']
(HERE / 'result.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines[:25]))
