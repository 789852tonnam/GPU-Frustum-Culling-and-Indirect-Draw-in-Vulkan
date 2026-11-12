import re


def frame_count_matches(output, warmup_frames, measured_frames):
    match = re.search(r'Frames submitted for presentation: (\d+)', output)
    return bool(match and int(match[1]) == warmup_frames + measured_frames)


def last_distinct_level(grid_side):
    level = 1
    while (4 << (level - 1)) < grid_side:
        level += 1
    return level


def candidates(rows):
    groups = [r for r in rows if r['level'] != 0]
    selected = set()
    for metric in ('cull_ms', 'total_ms'):
        selected.update(r['level'] for r in sorted(groups, key=lambda r: (r[metric], r['level']))[:2])
    return sorted(selected)


if __name__ == '__main__':
    assert [last_distinct_level(n) for n in (320, 640, 1280, 2560)] == [8, 9, 10, 11]
    fixture = [dict(level=0, cull_ms=0, total_ms=0),
               dict(level=1, cull_ms=1, total_ms=9),
               dict(level=2, cull_ms=2, total_ms=2),
               dict(level=3, cull_ms=8, total_ms=1),
               dict(level=4, cull_ms=9, total_ms=9)]
    assert candidates(fixture) == [1, 2, 3]
    assert candidates([dict(level=i, cull_ms=1, total_ms=1) for i in (3, 2, 1)]) == [1, 2]
    assert frame_count_matches('Frames submitted for presentation: 106', 70, 36)
    assert frame_count_matches('Frames submitted for presentation: 250', 70, 180)
    assert not frame_count_matches('Frames submitted for presentation: 249', 70, 180)
    assert not frame_count_matches('', 70, 180)
    print('Candidate union, reference exclusion, ties and equivalent-level boundaries passed.')
