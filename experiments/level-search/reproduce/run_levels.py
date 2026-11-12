from pathlib import Path
import csv
import json
import os
import re
import selectors
import statistics
import subprocess
import time
import random
import shutil
import sys
import hashlib
from selection import candidates, last_distinct_level, frame_count_matches

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[2]
if len(sys.argv) != 2:
    raise SystemExit('Usage: run_levels.py NEW_OUTPUT_DIRECTORY')
HERE = Path(sys.argv[1]).resolve()
if HERE.exists():
    raise SystemExit('Output directory already exists; recorded evidence must not be overwritten.')
BIN = REPO / 'build/experiments'
for name in ('benchmark-180', 'benchmark-36', 'experiment_clock_gate_test', 'experiment_equivalent_levels'):
    if not (BIN / name).is_file():
        raise SystemExit('Build experiment_tools first; missing ' + str(BIN / name))
HERE.mkdir(parents=True)
for name in ('benchmark-180', 'benchmark-36'):
    shutil.copy2(BIN / name, HERE / name)
for name in ('clock_guard.py', 'main_benchmark.cpp', 'main_screen36.cpp', 'clock_gate.hpp'):
    shutil.copy2(SOURCE / name, HERE / name)
subprocess.run([sys.executable, str(SOURCE / 'selection.py')], check=True)
subprocess.run([sys.executable, str(SOURCE / 'test_guard.py')], check=True)
subprocess.run([str(BIN / 'experiment_clock_gate_test')], check=True)
with (HERE / 'equivalent-levels.txt').open('w') as output:
    subprocess.run([str(BIN / 'experiment_equivalent_levels')], stdout=output, check=True)
protocol = json.loads((SOURCE.parent / 'recorded/protocol.json').read_text())
for key in ('observedClockMinMhz', 'observedClockMaxMhz', 'observedClockSamples', 'completedRuns', 'selectedLevels', 'restored'):
    protocol.pop(key, None)
protocol['fullBinarySha256'] = hashlib.sha256((HERE / 'benchmark-180').read_bytes()).hexdigest()
protocol['screen36BinarySha256'] = hashlib.sha256((HERE / 'benchmark-36').read_bytes()).hexdigest()
protocol['runnerSha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
protocol['sourceSha256'] = hashlib.sha256((HERE / 'main_benchmark.cpp').read_bytes()).hexdigest()
protocol['rendererSourceSha256'] = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in (REPO / 'src').iterdir() if f.suffix in ('.cpp', '.hpp')}
protocol['shaderSha256'] = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in (REPO / 'build/shaders').glob('*.spv')}
protocol['compiler'] = ['cmake', '--build', 'build', '--target', 'experiment_tools']
protocol['environmentNote'] = 'Original recorded hardware fields describe the target machine; verify driver/OS before a rerun. Actual selected device/API is also printed in every raw run log.'
(HERE / 'protocol.json').write_text(json.dumps(protocol, indent=2) + '\n')
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
(HERE / 'status.json').write_text(json.dumps(status, indent=2) + '\n')


def run(grid_side, level, repeat, phase, frames):
    global load, extent
    before = power()
    assert guard.poll() is None and before['policy'] == 'manual'
    assert before['ac'] == initial_power['ac'], 'Power source changed.'
    label = f'{phase}-r{repeat}-side-{grid_side}-level-{level}'
    args = [str(HERE / f'benchmark-{frames}'), '--grid-side', str(grid_side)]
    if level:
        args += ['--grouped', '--group-level', str(level)]
    child_env = dict(env, CG_CLOCK_LOG=str(HERE / f'{label}.clocks.csv'))
    with (HERE / f'{label}.stdout').open('w') as output, (HERE / f'{label}.stderr').open('w') as error:
        start = time.monotonic()
        load = subprocess.Popen(args, env=child_env, stdout=output, stderr=error)
        while load.poll() is None:
            if guard.poll() is not None or time.monotonic() - start > 150:
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
    assert matches and int(matches[-1][3]) == frames
    mode, w, h, samples, cull, draw, total = matches[-1]
    assert mode == ('grouped' if level else 'per-object')
    current = (int(w), int(h))
    if extent is None:
        extent = current
    assert current == extent
    assert frame_count_matches(text, int(warmup[1]), frames)
    side = 4 * 2 ** ((level or 1) - 1)
    groups = ((grid_side + side - 1) // side) ** 2
    if level:
        assert f'group level {level}; {groups} groups of up to {side}x{side}' in text
    after = power()
    assert after['policy'] == 'manual' and after['ac'] == initial_power['ac']
    row = dict(phase=phase, repeat=repeat, gridSide=grid_side, sphereCount=grid_side**2, level=level, mode=mode, groupSide=side if level else None,
               groups=groups if level else None, samples=int(samples), extent=current,
               cull_ms=float(cull), draw_ms=float(draw), total_ms=float(total),
               warmup_seconds=float(warmup[2]), warmup_frames=int(warmup[1]),
               clock_samples=len(measured), clock_min_mhz=min(measured),
               clock_median_mhz=statistics.median(measured), clock_max_mhz=max(measured),
               power_before=before, power_after=after)
    results.append(row)
    (HERE / 'runs.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'{phase} r{repeat} side {grid_side} level {level if level else "flat"}: cull={cull} ms total={total} ms; clock={min(measured):.0f}-{max(measured):.0f} MHz; warmup={warmup[2]} s', flush=True)


try:
    deadline = time.monotonic() + 180
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
    screen_order = [(n, level) for n in (320,640,1280,2560) for level in range(last_distinct_level(n)+1)]
    random.Random(20261112).shuffle(screen_order)
    (HERE / 'screen-order.json').write_text(json.dumps(screen_order, indent=2) + '\n')
    for n, level in screen_order:
        run(n, level, 1, 'screen', 36 if n == 2560 else 180)
    chosen = {n: candidates([r for r in results if r['gridSide']==n]) for n in (320,640,1280,2560)}
    (HERE / 'selection.json').write_text(json.dumps(chosen, indent=2) + '\n')
    orders = {}
    for n, levels in chosen.items():
        order = [0] + levels
        random.Random(n).shuffle(order)
        rotated = order[1:] + order[:1]
        orders[n] = [order, list(reversed(order)), rotated, list(reversed(rotated))]
    (HERE / 'confirmation-order.json').write_text(json.dumps(orders, indent=2) + '\n')
    for repeat in range(1,5):
        sizes = (320,640,1280,2560) if repeat%2 else (2560,1280,640,320)
        for n in sizes:
            for level in orders[n][repeat-1]:
                run(n, level, repeat, 'confirm', 180)
    summary = []
    for phase in ('screen','confirm'):
        for n in (320,640,1280,2560):
            levels = sorted({r['level'] for r in results if r['phase']==phase and r['gridSide']==n})
            for level in levels:
                rows = [r for r in results if r['phase']==phase and r['gridSide']==n and r['level']==level]
                assert len(rows) == (1 if phase=='screen' else 4)
                item = dict(phase=phase, gridSide=n, sphereCount=n*n, level=level, runs=len(rows),
                            groupSide=rows[0]['groupSide'],
                            **{m:dict(median=statistics.median(r[m] for r in rows),
                                      minimum=min(r[m] for r in rows),maximum=max(r[m] for r in rows))
                               for m in ('cull_ms','draw_ms','total_ms')})
                refs = [r for r in results if r['phase']==phase and r['gridSide']==n and r['level']==0]
                for metric in ('cull_ms','total_ms'):
                    item[metric]['reduction_percent'] = 100*(1-item[metric]['median']/statistics.median(r[metric] for r in refs))
                    item[metric]['paired_reduction_percent'] = [100*(1-r[metric]/next(a for a in refs if a['repeat']==r['repeat'])[metric]) for r in rows]
                summary.append(item)
    (HERE / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    status['status'] = 'completed'
except BaseException as exc:
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
