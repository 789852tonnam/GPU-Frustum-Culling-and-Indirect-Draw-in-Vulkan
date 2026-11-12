from pathlib import Path
import csv
import json
import os
import re
import selectors
import statistics
import subprocess
import time

HERE = Path(__file__).resolve().parent
DEVICE = Path('/sys/class/drm/card1/device')
TIMING = re.compile(r'GPU timestamps \[(per-object|grouped) (\d+)x(\d+), (\d+) samples\]: cull=([\d.e+-]+) ms draw=([\d.e+-]+) ms total=([\d.e+-]+) ms')
results, lines = [], []
extent = None
load = None
env = os.environ.copy()
for name in ('VK_INSTANCE_LAYERS', 'VK_VALIDATION_VALIDATE_SYNC', 'LD_PRELOAD'):
    env.pop(name, None)
env['CG_GPU_CLOCK_PATH'] = str(next((DEVICE / 'hwmon').glob('hwmon*/freq1_input')))


def power():
    return {key: Path(path).read_text().strip() for key, path in {
        'ac': '/sys/class/power_supply/ACAD/online',
        'battery': '/sys/class/power_supply/BAT0/capacity',
        'policy': str(DEVICE / 'power_dpm_force_performance_level')}.items()}


initial_power = power()
guard = subprocess.Popen(['/usr/bin/pkexec', '--disable-internal-agent', '/usr/bin/python3',
                          str(HERE / 'clock_guard.py')], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
selector = selectors.DefaultSelector()
selector.register(guard.stdout, selectors.EVENT_READ)
status = dict(status='awaiting-authentication', target_mhz=2000, power_before=initial_power)


def run(grid_side, level, repeat):
    global load, extent
    before = power()
    assert guard.poll() is None and before['policy'] == 'manual'
    assert before['ac'] == initial_power['ac'], 'Power source changed.'
    label = f'r{repeat}-side-{grid_side}-level-{level}'
    args = [str(HERE / 'gpu_group_benchmark'), '--grid-side', str(grid_side)]
    if level:
        args += ['--grouped', '--group-level', str(level)]
    child_env = dict(env, CG_CLOCK_LOG=str(HERE / f'{label}.clocks.csv'))
    with (HERE / f'{label}.stdout').open('w') as output, (HERE / f'{label}.stderr').open('w') as error:
        start = time.monotonic()
        load = subprocess.Popen(args, env=child_env, stdout=output, stderr=error)
        while load.poll() is None:
            if guard.poll() is not None or time.monotonic() - start > 45:
                load.terminate()
                raise RuntimeError('Clock guard ended or run exceeded deadline; excluded.')
            time.sleep(0.2)
        assert load.returncode == 0, (label, (HERE / f'{label}.stderr').read_text())
    text = (HERE / f'{label}.stdout').read_text()
    assert 'Swapchain recreations: 0' in text
    assert re.search(rf'culling verified: \d+/{grid_side**2} visible; all commands match CPU reference', text)
    assert not re.search(r'VUID-|Validation Error|SYNC-HAZARD', text + (HERE / f'{label}.stderr').read_text())
    warmup = re.search(r'BENCHMARK_WARMUP_DONE frames=(\d+) seconds=([\d.e+-]+)', text)
    assert warmup and float(warmup[2]) >= 5.0
    clock = re.search(r'CLOCK_MONITOR samples=(\d+) min=([\d.e+-]+) max=([\d.e+-]+)', text)
    assert clock and int(clock[1]) >= 10 and 1990 <= float(clock[2]) <= float(clock[3]) <= 2010
    assert float(clock[3]) - float(clock[2]) <= 10
    with (HERE / f'{label}.clocks.csv').open() as f:
        measured = [float(r['mhz']) for r in csv.DictReader(f) if r['phase'] == 'measure']
    assert len(measured) == int(clock[1]) and min(measured) == float(clock[2]) and max(measured) == float(clock[3])
    matches = TIMING.findall(text)
    assert matches and int(matches[-1][3]) == 180
    mode, w, h, samples, cull, draw, total = matches[-1]
    assert mode == ('grouped' if level else 'per-object')
    current = (int(w), int(h))
    if extent is None:
        extent = current
    assert current == extent
    frames = re.search(r'Frames submitted for presentation: (\d+)', text)
    assert frames and int(frames[1]) == int(warmup[1]) + 180
    side = 4 * 2 ** ((level or 1) - 1)
    groups = ((grid_side + side - 1) // side) ** 2
    if level:
        assert f'group level {level}; {groups} groups of up to {side}x{side}' in text
    after = power()
    assert after['policy'] == 'manual' and after['ac'] == initial_power['ac']
    row = dict(repeat=repeat, gridSide=grid_side, sphereCount=grid_side**2, level=level, mode=mode, groupSide=side if level else None,
               groups=groups if level else None, samples=int(samples), extent=current,
               cull_ms=float(cull), draw_ms=float(draw), total_ms=float(total),
               warmup_seconds=float(warmup[2]), warmup_frames=int(warmup[1]),
               clock_samples=len(measured), clock_min_mhz=min(measured),
               clock_median_mhz=statistics.median(measured), clock_max_mhz=max(measured),
               power_before=before, power_after=after)
    results.append(row)
    (HERE / 'runs.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'r{repeat} side {grid_side} level {level if level else "flat"}: cull={cull} ms total={total} ms; clock={min(measured):.0f}-{max(measured):.0f} MHz; warmup={warmup[2]} s', flush=True)


try:
    deadline = time.monotonic() + 60
    ready = False
    while time.monotonic() < deadline and guard.poll() is None:
        if selector.select(0.5):
            line = guard.stdout.readline()
            lines.append(line)
            if line.startswith('READY '):
                ready = True
                break
    if not ready:
        if guard.poll() is None:
            try:
                guard.terminate()
            except PermissionError:
                pass
        raise RuntimeError('Administrator authentication or clock setting failed.')
    status['status'] = 'measuring'
    print('2000 MHz limits active; starting clock-gated comparison.', flush=True)
    orders = [
        [(2560,0),(2560,3)],
        [(2560,3),(2560,0)],
        [(2560,3),(2560,0)],
        [(2560,0),(2560,3)],
    ]
    for repeat, order in enumerate(orders, 1):
        for grid_side, level in order:
            run(grid_side, level, repeat)
    summary = []
    for grid_side in (2560,):
        baseline = {}
        for level in (0, 3):
            rows = [r for r in results if r['gridSide'] == grid_side and r['level'] == level]
            assert len(rows) == 4
            item = dict(gridSide=grid_side, sphereCount=grid_side**2, level=level, runs=4,
                groupSide=rows[0]['groupSide'],
                **{metric: dict(median=statistics.median(r[metric] for r in rows),
                               minimum=min(r[metric] for r in rows), maximum=max(r[metric] for r in rows))
                   for metric in ('cull_ms','draw_ms','total_ms')})
            if level == 0:
                baseline = item
            for metric in ('cull_ms','total_ms'):
                item[metric]['reduction_percent'] = 100*(1-item[metric]['median']/baseline[metric]['median'])
                item[metric]['paired_reduction_percent'] = [100*(1-next(r for r in rows if r['repeat']==i)[metric]/next(r for r in results if r['gridSide']==grid_side and r['level']==0 and r['repeat']==i)[metric]) for i in range(1,5)]
            summary.append(item)
    (HERE / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    status['status'] = 'completed'
except Exception as exc:
    status['status'] = 'failed'
    status['error'] = str(exc)
    raise
finally:
    if load is not None and load.poll() is None:
        load.terminate()
        load.wait(timeout=5)
    try:
        guard.stdin.write('DONE\n')
        guard.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    tail, error = guard.communicate(timeout=35)
    (HERE / 'guard.stdout').write_text(''.join(lines) + tail)
    (HERE / 'guard.stderr').write_text(error)
    status['completed_runs'] = len(results)
    status['guard_exit'] = guard.returncode
    status['final_mode'] = (DEVICE / 'power_dpm_force_performance_level').read_text().strip()
    status['final_limits'] = (DEVICE / 'pp_od_clk_voltage').read_text().strip()
    (HERE / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
    print(json.dumps(status), flush=True)
    if error:
        print(error, flush=True)
