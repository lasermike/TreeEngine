"""Checks road_props.py's placement rules on a synthetic network (no files):

    python -I test_road_props.py
"""

import importlib.util
import math
import os
import sys

spec = importlib.util.spec_from_file_location('road_props', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'road_props.py'))
props = importlib.util.module_from_spec(spec)
spec.loader.exec_module(props)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


def line(kind, fid, coords, **extra):
    p = {'kind': kind, 'id': fid, 'source': 'county' if kind == 'road' else 'generated', 'edited': False}
    p.update(extra)
    return {'type': 'Feature', 'properties': p, 'geometry': {'type': 'LineString', 'coordinates': coords}}


county = dict(owner='county', surface='paved', width=6.0, shoulder=0.6, speedLimit=25)
private = dict(owner='private', surface='gravel', width=3.6, shoulder=0.0, speedLimit=25)
# A ST runs east along y = 0 in two pieces split at x = 100 (as the county splits roads); B ST ends at it from the
# north (a T); C LN, private, ends at it from the south at x = 200.
network = [
    line('road', 'a1', [[0.0, 0.0], [100.0, 0.0]], name='A ST', **county),
    line('road', 'a2', [[100.0, 0.0], [300.0, 0.0]], name='A ST', **county),
    line('road', 'b', [[100.0, 0.0], [100.0, 120.0]], name='B ST', **county),
    line('road', 'c', [[200.0, 0.0], [200.0, -80.0]], name='C LN', **private),
]


def placed(features, kind):
    return [f for f in features if f['properties'].get('prop') == kind]


out, counts = props.place_props(network)
stops, streets = placed(out, 'stop-sign'), placed(out, 'street-name-sign')
check(counts['stop-sign'] == 1 and counts['street-name-sign'] == 2, f'counts per rule: {counts}')
# The stop sign: on B ST, on the right of traffic heading south (the west side, x < 100), north of A ST's shoulder,
# facing north (toward that traffic).
if stops:
    x, y = stops[0]['geometry']['coordinates']
    check(x < 100 and y > 3.6 and abs(stops[0]['properties']['heading'] - 0) < 1, f'stop sign placement: {stops[0]}')
# Street-name signs: the lower blade names the through road.
check(sorted(s['properties']['text'] for s in streets) == ['A ST|B ST', 'A ST|C LN'], f'sign text: {streets}')

# Where roads meet at a sharp angle the corner post steps back until it's off every road: E ST ends at A ST from the
# south-east, and private F LN leaves 20 degrees to its right.
angle = math.radians(20)
sharp = [line('road', 'a', [[-100.0, 0.0], [100.0, 0.0]], name='A ST', **county),
         line('road', 'e', [[0.0, 0.0], [80.0, -80.0]], name='E ST', **county),
         line('road', 'f', [[0.0, 0.0], [80.0 * math.cos(-math.pi / 4 + angle) * 1.41, 80.0 * math.sin(-math.pi / 4 + angle) * 1.41]],
              name='F LN', **private)]
out, counts = props.place_props(sharp)
roads_here = [(f['properties'], [tuple(p) for p in f['geometry']['coordinates']]) for f in sharp]
for sign in placed(out, 'stop-sign') + placed(out, 'street-name-sign'):
    point = tuple(sign['geometry']['coordinates'])
    clear = all(props.nearest(point, l)[0] >= props.reach(p) + 0.45 for p, l in roads_here)
    check(clear, f"a sign stands on a road at a sharp junction: {sign['properties']['id']} at {point}")
check(counts['stop-sign'] == 1, f'sharp junction counts: {counts}')

# A road piece meeting another piece of itself (a bend split in two) is no intersection.
bend = [line('road', 'd1', [[0.0, 0.0], [50.0, 0.0]], name='D RD', **county),
        line('road', 'd2', [[50.0, 0.0], [50.0, 50.0]], name='D RD', **county)]
out, counts = props.place_props(bend)
check(counts['stop-sign'] == 0 and counts['street-name-sign'] == 0, f'a bend got signs: {counts}')

# Mailboxes: a driveway north of A ST at x = 40 gets one on the north side, just past the mouth in the direction of
# travel there (westbound: x < 40), facing south to the road. Two driveways branching from a lane at x = 250 (south)
# line up at the lane's mouth.
drives = network + [
    line('driveway', 'd-north', [[40.0, 0.0], [40.0, 30.0]], name='40 A ST'),
    line('driveway', 'lane', [[250.0, 0.0], [250.0, -40.0]], name='250 A ST'),
    line('driveway', 'branch', [[250.0, -40.0], [270.0, -60.0]], name='260 A ST'),
    line('driveway', 'nowhere', [[150.0, 50.0], [160.0, 60.0]]),
]
out, counts = props.place_props(drives)
boxes = {f['properties']['id']: f for f in placed(out, 'mailbox')}
check(counts['mailbox'] == 3 and counts['mailbox without a road'] == 1, f'mailbox counts: {counts}')
north = boxes.get('prop-mailbox-d-north')
if north:
    x, y = north['geometry']['coordinates']
    check(x < 40 and 3.6 < y < 4.5 and abs(north['properties']['heading'] - 180) < 1 and north['properties']['text'] == '40 A ST',
          f'the north mailbox: {north}')
lane, branch = boxes.get('prop-mailbox-lane'), boxes.get('prop-mailbox-branch')
if lane and branch:
    (lx, ly), (bx, by) = lane['geometry']['coordinates'], branch['geometry']['coordinates']
    # South side: eastbound traffic, so past the mouth is x > 250; facing north.
    check(ly < -3.6 and by == ly and 250 < lx < bx and abs(bx - lx - props.MAILBOX_SPACING) < 0.02
          and abs(lane['properties']['heading']) < 1, f'shared-lane mailboxes: {lane}, {branch}')

# Re-running replaces unedited generated props; an edited one stays (where it was moved), and isn't placed again.
moved = dict(boxes['prop-mailbox-d-north'])
moved = {'type': 'Feature', 'properties': dict(moved['properties'], edited=True), 'geometry': {'type': 'Point', 'coordinates': [41.0, 9.0]}}
again = [f for f in out if f['properties']['id'] != 'prop-mailbox-d-north'] + [moved]
out2, counts2 = props.place_props(again)
north2 = [f for f in out2 if f['properties']['id'] == 'prop-mailbox-d-north']
check(len(north2) == 1 and north2[0]['geometry']['coordinates'] == [41.0, 9.0], f'the edited mailbox: {north2}')
check(len(placed(out2, 'mailbox')) == 3 and counts2['kept'] == 1, f'mailboxes after a re-run: {counts2}')

print(f'{failures} failures' if failures else 'road_props checks passed')
sys.exit(1 if failures else 0)
