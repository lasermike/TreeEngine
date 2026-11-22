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

print(f'{failures} failures' if failures else 'import_roads checks passed')
sys.exit(1 if failures else 0)
