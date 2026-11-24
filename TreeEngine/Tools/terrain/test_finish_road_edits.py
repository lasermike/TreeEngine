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
# Deleting a generated driveway keeps it as a removed marker (so imports don't make it again); a hand-drawn one just
# goes. Markers are no snapping target.
out, notes = finish.finish(base, [road])
check(len(out) == 2 and out[1]['properties']['id'] == 'd' and out[1]['properties']['removed'] is True
      and out[1]['properties']['edited'] is True and out[1]['geometry'] == drive['geometry']
      and any('d: removed' in n for n in notes), f'a deleted generated driveway left no marker: {out}, {notes}')
manual = line({'kind': 'driveway', 'id': 'mine', 'source': 'manual', 'edited': True}, [[10.0, 0.0], [10.0, 30.0]])
out, notes = finish.finish(base + [manual], [road, drive])
check(len(out) == 2 and any('mine: deleted' in n for n in notes), f'a deleted hand-drawn driveway left a marker: {notes}')
near_marker = line({'kind': 'driveway', 'id': 'n'}, [[50.0, 3.0], [70.0, 40.0]])
out, notes = finish.finish(base, [road, near_marker], snap_start=6)
check(out[1]['geometry']['coordinates'][0] == [50.0, 0.0], f'a line snapped to a removed marker: {out[1]}')
# Deleting a removed marker deletes it (so the next import may bring the feature back), not another marker.
osm_marker = line({'kind': 'driveway', 'id': 'osm-way-9', 'source': 'osm', 'edited': True, 'removed': True},
                  [[20.0, 0.0], [20.0, 30.0]])
out, notes = finish.finish(base + [osm_marker], [road, drive])
check(len(out) == 2 and not any(f['properties']['id'] == 'osm-way-9' for f in out)
      and any('osm-way-9: removed marker deleted' in n for n in notes), f'a deleted marker came back: {out}, {notes}')

# A crossing through an interior vertex of either line is still a crossing.
vertex = line(dict(drive['properties']), [[50.0, -10.0], [50.0, 0.0], [50.0, 10.0]])
out, notes = finish.finish(base, [road, vertex], snap_start=0)
check(any('d: crosses r' in n for n in notes), f'a crossing through a vertex was not reported: {notes}')

# Snaps see earlier snaps. A's start moves from (0, 5) onto the road, so A becomes (0, 0)-(100, 5). B, starting at
# (2, 11), was 6 m from A as drawn but is 10.9 m from A as written: it stays put rather than snapping to where A was.
# C, starting at (70, 8.5), snaps onto A as written (y = 3.5 at x = 70, 5 m away), not onto A as drawn (y = 5).
a = line({'kind': 'driveway', 'id': 'a'}, [[0.0, 5.0], [100.0, 5.0]])
b = line({'kind': 'driveway', 'id': 'b'}, [[2.0, 11.0], [2.0, 100.0]])
c = line({'kind': 'driveway', 'id': 'c'}, [[70.0, 8.5], [70.0, 100.0]])
out, notes = finish.finish(base, [road, drive, a, b, c])
final_a, final_b, final_c = (out[i]['geometry']['coordinates'] for i in (2, 3, 4))
gap = finish.roads.nearest_on_line(tuple(final_c[0]), [tuple(p) for p in final_a])[0]
check(final_a[0] == [0.0, 0.0] and final_b[0] == [2.0, 11.0] and gap < 0.05,
      f'snapped to where a line was drawn, not where it is: a {final_a}, b {final_b}, c {final_c}')

# A new point is a prop: a post facing north unless its prop is set, with a note.
point = {'type': 'Feature', 'properties': {'kind': None, 'prop': None}, 'geometry': {'type': 'Point', 'coordinates': [12.0, 7.0]}}
out, notes = finish.finish(base, [road, drive, point])
p = out[2]['properties']
check(p.get('kind') == 'prop' and p.get('prop') == 'post' and p.get('heading') == 0 and p.get('id') == 'manual-12-7'
      and any('without a model' in n for n in notes), f'the new point: {out[2]}, {notes}')

# XYZ coordinates: snapping works in plan and keeps the start's z.
road3 = line(dict(road['properties']), [[0.0, 0.0, 12.0], [100.0, 0.0, 13.0]])
drive3 = line({'kind': 'driveway', 'id': 'z'}, [[30.0, 3.0, 14.0], [30.0, 40.0, 15.0]])
out, notes = finish.finish([road3], [road3, drive3])
check(out[1]['geometry']['coordinates'][0] == [30.0, 0.0, 14.0], f'an XYZ start did not snap: {out[1]}')

# Multi-part lines: an unchanged re-save is kept, a reshape is kept (not restored), a property change keeps the parts.
multi = {'type': 'Feature', 'properties': {'kind': 'road', 'id': 'm', 'source': 'county', 'edited': False,
                                           'surface': 'gravel', 'width': 5.0},
         'geometry': {'type': 'MultiLineString', 'coordinates': [[[0.0, 50.0], [40.0, 50.0]], [[60.0, 50.0], [100.0, 50.0]]]}}
same_multi = {**multi, 'geometry': {'type': 'MultiLineString',
                                    'coordinates': [[[0.0000001, 50.0], [40.0, 50.0]], [[60.0, 50.0], [100.0, 50.0]]]}}
out, notes = finish.finish(base + [multi], [road, drive, same_multi])
check(out[2] is multi and notes == [], f'an unchanged multi-part line changed: {notes}')
bent = {**multi, 'geometry': {'type': 'MultiLineString', 'coordinates': [[[0.0, 50.0], [40.0, 60.0]], [[60.0, 50.0], [100.0, 50.0]]]}}
out, _ = finish.finish(base + [multi], [road, drive, bent], snap_start=0)
check(out[2]['properties']['edited'] is True and out[2]['geometry'] == bent['geometry'], f'a multi-part reshape was lost: {out[2]}')
renamed = {**multi, 'properties': dict(multi['properties'], name='X RD')}
out, _ = finish.finish(base + [multi], [road, drive, renamed], snap_start=0)
check(out[2]['geometry'] == multi['geometry'] and out[2]['properties']['name'] == 'X RD', f'a renamed multi-part line: {out[2]}')

print(f'{failures} failures' if failures else 'finish_road_edits checks passed')
sys.exit(1 if failures else 0)
