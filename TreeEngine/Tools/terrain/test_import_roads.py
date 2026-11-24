"""Checks import_roads.py's geometry on synthetic data (no downloads, no package):

    python -I test_import_roads.py
"""

import importlib.util
import math
import os
import sys

spec = importlib.util.spec_from_file_location('import_roads', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'import_roads.py'))
roads = importlib.util.module_from_spec(spec)
spec.loader.exec_module(roads)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


def close(a, b, tolerance=1e-6):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tolerance


class Extent:
    def __init__(self, extent):
        self.extent = extent


# A 10 m link between two junctions, shorter than the 20 m taper: both ends land exactly on their targets.
link = roads.resample([(0.0, 0.0), (10.0, 0.0)], 4.0)
roads.bend_to(link, (0.0, 1.0), (10.0, -1.0), 20.0)
check(close(link[0], (0.0, 1.0)) and close(link[-1], (10.0, -1.0)), 'a short link reopened a junction')
check(all(-1.0001 <= p[1] <= 1.0001 for p in link), 'a short link bent beyond its targets')
# One end only: the other stays, and points past the reach don't move.
long = roads.resample([(0.0, 0.0), (100.0, 0.0)], 4.0)
roads.bend_to(long, (0.0, 2.0), None, 20.0)
check(close(long[0], (0.0, 2.0)) and close(long[-1], (100.0, 0.0)) and all(p[1] == 0 for p in long if p[0] >= 20),
      'a one-ended bend moved the wrong points')

# Clipping to the package: a line leaving and re-entering splits in two; one wholly outside vanishes.
parts = roads.clip_line([(-10.0, 5.0), (5.0, 5.0), (5.0, 20.0), (8.0, 20.0), (8.0, 5.0)], (0.0, 0.0, 10.0, 10.0))
check(len(parts) == 2 and close(parts[0][0], (0.0, 5.0)) and close(parts[0][-1], (5.0, 10.0))
      and close(parts[1][0], (8.0, 10.0)) and close(parts[1][-1], (8.0, 5.0)), f'clipping split wrongly: {parts}')
check(roads.clip_line([(20.0, 20.0), (30.0, 30.0)], (0.0, 0.0, 10.0, 10.0)) == [], 'a line outside the package survived')

way = ({'id': 1, 'tags': {'highway': 'service', 'service': 'driveway'}}, [(50.0, 30.0), (50.0, 60.0)])

# No roads at all: the driveway is kept, unattached, instead of failing.
driveways = roads.build_driveways(Extent((0.0, 0.0, 100.0, 100.0)), [way], [], [])
check(len(driveways) == 1 and not driveways[0]['connected'], 'a driveway with no roads failed or was attached')

# Driveways snap to the roads that will be written: the imported road ran along y = 30, where the driveway starts,
# but an edit moved it to y = 20, and the driveway's road end goes there.
edited_road = [[(0.0, 20.0), (100.0, 20.0)]]
driveways = roads.build_driveways(Extent((0.0, 0.0, 100.0, 100.0)), [way], edited_road, [])
check(len(driveways) == 1 and driveways[0]['connected'] and close(driveways[0]['line'][0], (50.0, 20.0)),
      f'the driveway did not reach the edited road: {driveways}')

# A driveway crossing the package's edge is clipped there.
outside = ({'id': 2, 'tags': {'highway': 'service'}}, [(50.0, 90.0), (50.0, 140.0)])
driveways = roads.build_driveways(Extent((0.0, 0.0, 100.0, 100.0)), [outside], edited_road, [])
check(len(driveways) == 1 and all(p[1] <= 100.0 + 1e-6 for p in driveways[0]['line']), 'a driveway ran off the package')

# Generated driveways. A 200 x 200 m package, dry except a pond at y > 150, with "A ST" along y = 0 and "B ST" along
# x = 100 (crossing y from 0 to 200).
class Ground:
    extent = (0.0, 0.0, 200.0, 200.0)

    def contains(self, x, y, margin=0):
        return 0 <= x <= 200 and 0 <= y <= 200

    def height(self, x, y):
        return 0.0 if y > 150 else 10.0


def address(x, y, street, pin='1', number='1'):
    return {'type': 'Feature', 'geometry': {'type': 'Point', 'coordinates': [x, y]},
            'properties': {'STREET': street, 'PIN': pin, 'STREETNUMB': number, 'ADDRESS': f'{number} {street}'}}


roads_ab = [('A ST', [(0.0, 0.0), (200.0, 0.0)]), ('B ST', [(100.0, 0.0), (100.0, 200.0)])]
generate = lambda addresses, buildings=(), served=(): roads.build_generated_driveways(
    Ground(), addresses, list(buildings), roads_ab, list(served), 1.0, [])

# The road the address names, even when another is nearer: 40 m to A ST, 30 m to B ST.
made = generate([address(70, 40, 'A ST')])
check(len(made) == 1 and close(made[0]['line'][0], (70, 0)) and close(made[0]['line'][1], (70, 40)),
      f'the driveway did not run to the named road: {made}')
# Served by an existing driveway whose house end is within 30 m; within 12 m of a road; two units of one address.
check(generate([address(70, 40, 'A ST')], served=[[(70, 0), (75, 30)]]) == [], 'a served address got a driveway')
check(generate([address(70, 8, 'A ST')]) == [], 'an address on a road got a driveway')
units = generate([address(40, 60, 'A ST', '7', '12'), address(45, 64, 'A ST', '7', '12')])
check(len(units) == 1, 'two units of one address got two driveways')
# A house east of B ST whose address names C ST, west of B ST: the path there would cross B ST, so the driveway goes
# to the nearest road it can reach without crossing one (A ST).
crossing = roads.build_generated_driveways(Ground(), [address(150, 40, 'C ST')], [],
                                           [('C ST', [(0.0, 40.0), (60.0, 40.0)])] + roads_ab, [], 1.0, [])
check(len(crossing) == 1 and not close(crossing[0]['line'][0], (60, 40)),
      f'a driveway crossed a road to reach the one its address names: {crossing}')
# Driveways don't cross: the second address's path to A ST would cross the first driveway, so it takes B ST.
pair = generate([address(20, 60, 'A ST', '1'), address(60, 30, 'A ST', '2')], served=[[(70.0, 0.0), (10.0, 80.0)]])
check(all(not roads.segments_cross(d['line'][0], d['line'][1], (70.0, 0.0), (10.0, 80.0)) for d in pair)
      and all(not roads.segments_cross(a['line'][0], a['line'][1], b['line'][0], b['line'][1]) for a in pair for b in pair),
      f'driveways cross: {pair}')
# A crossing exactly through another road's vertex counts: B runs through (50, 50) as a vertex, the house at
# (50, 100) names A (y = 0), so it must take B instead of crossing it.
vertex_roads = [('A ST', [(0.0, 0.0), (100.0, 0.0)]), ('B ST', [(0.0, 50.0), (50.0, 50.0), (100.0, 50.0)])]
through = roads.build_generated_driveways(Ground(), [address(50, 100, 'A ST')], [], vertex_roads, [], 1.0, [])
check(len(through) == 1 and close(through[0]['line'][0], (50, 50)), f'a driveway crossed a road through its vertex: {through}')
# An address whose driveway was edited (kept by id) is left alone, and can't serve or block a neighbor: the kept
# driveway ends 90 m away, so the neighbor 25 m from the address still gets its own.
kept = roads.build_generated_driveways(Ground(), [address(50, 60, 'A ST', '1', '1'), address(75, 60, 'A ST', '2', '2')],
                                       [], roads_ab, [[(50.0, 0.0), (50.0, 150.0)]], 1.0, [], kept_ids={'address-1-1'})
check(len(kept) == 1 and kept[0]['id'] == 'address-2-2', f'an edited address affected generation: {kept}')
# Across water: the only road beyond the pond is skipped.
pond = roads.build_generated_driveways(Ground(), [address(30, 190, 'D ST')], [],
                                       [('D ST', [(0.0, 120.0), (60.0, 120.0)])], [], 1.0, [])
check(pond == [], 'a driveway crossed water')
# A building within 30 m: the driveway ends 5 m short of its center, toward the road.
building = ({'id': 9}, [(60.0, 50.0), (80.0, 50.0), (80.0, 70.0), (60.0, 70.0)])
house = generate([address(72, 45, 'A ST')], buildings=[building])
check(len(house) == 1 and close(house[0]['line'][1], (70, 55), 1e-6), f'the driveway did not end at the building: {house}')

# Served is judged at the house: the address is 35 m from an existing driveway's end, but its building is 6 m from it.
near_house = ({'id': 10}, [(40.0, 59.0), (60.0, 59.0), (60.0, 79.0), (40.0, 79.0)])
check(generate([address(50, 40, 'A ST')], buildings=[near_house], served=[[(30.0, 0.0), (50.0, 75.0)]]) == [],
      'an address whose building an existing driveway reaches got a second driveway')


class Dry(Ground):
    def __init__(self, wet=lambda x, y: False):
        self.wet = wet

    def height(self, x, y):
        if not self.contains(x, y):
            return float('nan')
        return 0.0 if self.wet(x, y) else 10.0


# A building centered off the package (y = 215) is ignored: the driveway ends at the in-bounds address instead.
outside_house = ({'id': 11}, [(40.0, 205.0), (60.0, 205.0), (60.0, 225.0), (40.0, 225.0)])
edge = roads.build_generated_driveways(Dry(), [address(50, 190, 'A ST')], [outside_house], roads_ab, [], 1.0, [])
check(len(edge) == 1 and close(edge[0]['line'][1], (50, 190)), f'a driveway ended off the package: {edge}')
# Water behind the driveway's end doesn't block it: the building's center is 40 m from the road, the driveway ends
# 5 m short at 35 m, and the water starts at 38 m.
behind = ({'id': 12}, [(65.0, 35.0), (75.0, 35.0), (75.0, 45.0), (65.0, 45.0)])
front = roads.build_generated_driveways(Dry(lambda x, y: 38 < y < 50), [address(70, 30, 'A ST')], [behind], roads_ab, [],
                                        1.0, [])
check(len(front) == 1 and close(front[0]['line'][0], (70, 0)) and close(front[0]['line'][1], (70, 35)),
      f'water behind the house blocked its driveway: {front}')
# Water at the very end of the driveway, within the last 2 m sampling interval, blocks it: a 20 m driveway with the
# shore at 19 m. A ST is the only road, so there's no other way in.
shore = roads.build_generated_driveways(Dry(lambda x, y: y > 19), [address(50, 20, 'A ST')], [],
                                        [('A ST', [(0.0, 0.0), (200.0, 0.0)])], [], 1.0, [])
check(shore == [], f'a driveway ended in water: {shore}')
# A building on the road (its address set back 20 m) gets no zero-length driveway.
on_road = ({'id': 13}, [(45.0, -5.0), (55.0, -5.0), (55.0, 5.0), (45.0, 5.0)])
zero = generate([address(50, 20, 'A ST')], buildings=[on_road])
check(zero == [], f'a house on the road got a driveway: {zero}')

# The closing summary copes with a hand-made feature that has only kind and id.
line = roads.summary('out', [{'type': 'Feature', 'properties': {'kind': 'driveway', 'id': 'mine'}},
                             {'type': 'Feature', 'properties': {'kind': 'driveway', 'id': 'g', 'source': 'generated'}}])
check(line == 'wrote out: 0 roads, 2 driveways (1 generated)', f'summary: {line}')

# A re-run keeps features edited by hand and removed markers (edited or not), and nothing else.
existing = [{'properties': {'id': 'a', 'edited': True}}, {'properties': {'id': 'b', 'removed': True}},
            {'properties': {'id': 'c', 'edited': False}}, {'properties': {'id': 'd'}}]
check([f['properties']['id'] for f in roads.kept_features(existing)] == ['a', 'b'], 'a re-run kept the wrong features')

print(f'{failures} failures' if failures else 'import_roads checks passed')
sys.exit(1 if failures else 0)
