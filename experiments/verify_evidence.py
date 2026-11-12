from pathlib import Path
import csv
import hashlib
import json
import math
import re
import statistics
import struct


def verify_manifest(root):
    entries = json.loads((root / 'manifest.json').read_text())['files']
    for entry in entries:
        path = root / entry['path']
        data = path.read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Evidence checksum mismatch: ' + entry['path'])
    return len(entries)


def verify_runs(folder, expected):
    rows = json.loads((folder / 'runs.json').read_text())
    assert len(rows) == expected
    status = json.loads((folder / 'status.json').read_text())
    assert status['status'] == 'completed' and status['guard_exit'] == 0 and status['final_mode'] == 'auto'
    for r in rows:
        prefix = (r['phase'] + '-') if 'phase' in r else ''
        prefix += f"r{r['repeat']}-side-{r['gridSide']}-level-{r['level']}"
        with (folder / 'raw' / f'{prefix}.clocks.csv').open() as f:
            clocks = [float(x['mhz']) for x in csv.DictReader(f) if x['phase'] == 'measure']
        assert len(clocks) == r['clock_samples'] and 1990 <= min(clocks) <= max(clocks) <= 2010
        assert max(clocks)-min(clocks) <= 10 and r['warmup_seconds'] >= 5
        log = (folder / 'raw' / f'{prefix}.stdout').read_text()
        assert re.search(rf"culling verified: \d+/{r['sphereCount']} visible; all commands match CPU reference", log)
    summary = json.loads((folder / 'summary.json').read_text())
    for s in summary:
        pool = [r for r in rows if r['gridSide']==s['gridSide'] and r['level']==s['level']
                and r.get('phase')==s.get('phase')]
        reference = [r for r in rows if r['gridSide']==s['gridSide'] and r['level']==0
                     and r.get('phase')==s.get('phase')]
        assert len(pool) == s['runs']
        for metric in ('cull_ms','draw_ms','total_ms'):
            median = statistics.median(r[metric] for r in pool)
            assert math.isclose(median, s[metric]['median'], abs_tol=1e-10)
            assert min(r[metric] for r in pool)==s[metric]['minimum']
            assert max(r[metric] for r in pool)==s[metric]['maximum']
            if 'reduction_percent' in s[metric]:
                reduction = 100*(1-median/statistics.median(r[metric] for r in reference))
                assert math.isclose(reduction, s[metric]['reduction_percent'], abs_tol=1e-8)


def verify_captures(folder):
    outputs = [(folder / f'{m}-indirect.bin').read_bytes() for m in ('per-object','grouped')]
    assert outputs[0] == outputs[1] and len(outputs[0]) == 6400*20
    commands = list(struct.iter_unpack('<IIIiI', outputs[0]))
    assert sum(x[1] for x in commands) == 1623
    assert all(c[0]==672 and c[1] in (0,1) and c[2]==c[3]==0 and c[4]==i for i,c in enumerate(commands))
    for r in json.loads((folder / 'replay-results.json').read_text()):
        assert r['commandsSha256']==hashlib.sha256(outputs[0]).hexdigest() and r['messages']==[]


if __name__ == '__main__':
    root = Path(__file__).resolve().parent
    count = verify_manifest(root)
    for name,n in [('level-search',106),('count-ablation',24),('six-million',8)]:
        verify_runs(root / name / 'recorded', n)
    verify_captures(root / 'renderdoc/recorded')
    print(f'Verified {count} immutable files, 138 run records/statistics/clock traces and both capture buffers.')
