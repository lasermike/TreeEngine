"""Checks finish_road_edits.py on synthetic features (no git, no files):

    python -I test_finish_road_edits.py
"""

import importlib.util
import os
import sys

spec = importlib.util.spec_from_file_location('finish_road_edits', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                                'finish_road_edits.py'))
finish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(finish)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


def line(props, coords):
    return {'type': 'Feature', 'properties': dict(props), 'geometry': {'type': 'LineString', 'coordinates': coords}}


road = line({'kind': 'road', 'id': 'r', 'source': 'county', 'edited': False, 'surface': 'paved', 'width': 6.0},
            [[0.0, 0.0], [100.0, 0.0]])
drive = line({'kind': 'driveway', 'id': 'd', 'source': 'generated', 'edited': False, 'surface': 'gravel', 'width': 3.0},
             [[50.0, 0.0], [50.0, 40.0]])
base = [road, drive]

# Unchanged, as QGIS saves it (extra digits, empty properties written as null): kept exactly as before.
resaved = [line(dict(road['properties'], name=None), [[0.0000001, 0.0], [100.0, 0.0000002]]), drive]
out, notes = finish.finish(base, resaved)
check(out[0] is road and out[1] is drive and notes == [], f'an unchanged file changed: {notes}')

# A reshaped driveway is marked edited, rounded, and its missed start (3 m off the road) snapped onto it.
moved = line(dict(drive['properties']), [[40.0, 3.0], [45.123456, 20.0], [50.0, 40.0]])
out, notes = finish.finish(base, [road, moved])
check(out[1]['properties']['edited'] is True and out[1]['geometry']['coordinates'] == [[40.0, 0.0], [45.12, 20.0], [50.0, 40.0]],
      f'the edited driveway: {out[1]}')
check(any('start moved 3.0 m' in n for n in notes), f'notes: {notes}')
# A changed property alone counts as an edit.
wider = line(dict(drive['properties'], width=4.0), drive['geometry']['coordinates'])
out, _ = finish.finish(base, [road, wider])
check(out[1]['properties']['edited'] is True and out[1]['properties']['width'] == 4.0, 'a width change was not marked edited')

# A new line with empty properties becomes a gravel driveway with a unique id; a start beyond --snap-start stays.
drawn = line({'kind': None, 'id': None, 'source': None, 'edited': None}, [[80.0, 10.0], [90.0, 40.0]])
out, notes = finish.finish(base, [road, drive, drawn])
p = out[2]['properties']
check(p == {'kind': 'driveway', 'id': 'manual-80-10', 'source': 'manual', 'edited': True, 'surface': 'gravel', 'width': 3.0}
      and out[2]['geometry']['coordinates'][0] == [80.0, 10.0], f'the new driveway: {out[2]}')
# Deleted features and crossings are reported.
across = line(dict(drive['properties']), [[50.0, 10.0], [50.0, -20.0]])
out, notes = finish.finish(base, [road, across], snap_start=0)
check(any('d: crosses r' in n for n in notes), f'a crossing was not reported: {notes}')
# A start 1 m past the road it joins is a junction, not a crossing; nor does a line cross itself.
past = line(dict(drive['properties']), [[50.0, -1.0], [50.123, 40.0]])
out, notes = finish.finish(base, [road, past], snap_start=0)
check(not any('crosses' in n for n in notes), f'a junction was reported as a crossing: {notes}')
out, notes = finish.finish(base, [road])
check(len(out) == 1 and any('d: deleted' in n for n in notes), f'a deletion was not reported: {notes}')

print(f'{failures} failures' if failures else 'finish_road_edits checks passed')
sys.exit(1 if failures else 0)
