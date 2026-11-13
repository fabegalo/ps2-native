"""Compare bounded device traces from a real-content control/candidate run.

Usage: python tests/pcm_trace_tests.py control.err candidate.err
This checks device counters at matched IOP cycles, not audible playback or pixels.
"""
from pathlib import Path
import sys


def records(path):
    rows = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        if not line.startswith('[SPU2] cycle='):
            continue
        fields = dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)
        cycle = int(fields['cycle'])
        assert cycle not in rows, 'Duplicate IOP trace cycle'
        rows[cycle] = fields
    assert rows, 'No SPU2 device evidence'
    return rows


control, candidate = map(records, sys.argv[1:3])
common = control.keys() & candidate.keys()
assert common, 'No matched IOP boundaries'
assert any(int(row['dropped']) > 0 for row in control.values()), 'Control did not reproduce loss'
assert all(int(row['dropped']) == 0 for row in candidate.values()), 'Candidate lost PCM'
assert any(any(int(p) > 0 for p in row['peak'].split(',')) for row in candidate.values()), 'Only silence observed'
for cycle in common:
    for field in ('frames', 'dma', 'voices', 'attr', 'peak'):
        assert control[cycle][field] == candidate[cycle][field], (cycle, field)
print(f'PASS {len(common)} matched SPU2 boundaries, non-silent PCM, zero candidate drops')
