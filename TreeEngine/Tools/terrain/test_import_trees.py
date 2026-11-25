"""Checks import_trees.py on a synthetic canopy (no downloads, no GDAL):

    python -I test_import_trees.py
"""

import importlib.util
import json
import math
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location('import_trees', os.path.join(HERE, 'import_trees.py'))
trees = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trees)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


CELL = trees.CELL
SIZE = 60.0
N = round(SIZE / CELL)
EXTENT = (1000.0, 2000.0, 1000.0 + SIZE, 2000.0 + SIZE)


def cell_center(x, y):
    """Grid cell (row, col) holding world (x, y)."""
    return int((EXTENT[3] - y) / CELL), int((x - EXTENT[0]) / CELL)


def world(row, col):
    return EXTENT[0] + (col + 0.5) * CELL, EXTENT[3] - (row + 0.5) * CELL


# A canopy of cone-shaped crowns: (x, y, height, crown radius).
CONES = [(1015.0, 2045.0, 40.0, 5.0), (1030.0, 2045.0, 30.0, 4.0), (1045.0, 2045.0, 20.0, 3.0),
         (1015.0, 2020.0, 35.0, 4.5), (1035.0, 2020.0, 12.0, 2.5), (1050.0, 2015.0, 3.0, 1.5)]
rows, cols = np.mgrid[0:N, 0:N]
xs, ys = EXTENT[0] + (cols + 0.5) * CELL, EXTENT[3] - (rows + 0.5) * CELL
chm = np.zeros((N, N), dtype=np.float32)
for x, y, h, r in CONES:
    d = np.hypot(xs - x, ys - y)
    chm = np.maximum(chm, np.where(d < r, h - (h * 0.5) * d / r, 0))

# Tops: one per cone at least MIN_HEIGHT tall, at its apex, tallest first; the 3 m shrub isn't a tree.
tops, heights = trees.find_tops(chm)
check(len(tops) == 5, f'found {len(tops)} tops, expected 5 (the 3 m cone is a shrub)')
found = [world(r, c) for r, c in tops]
for x, y, h, r in CONES[:5]:
    near = [p for p in found if math.hypot(p[0] - x, p[1] - y) <= 1.0]
    check(len(near) == 1, f'the {h:.0f} m tree at ({x}, {y}) has {len(near)} tops within 1 m')
check([round(heights[r, c]) for r, c in tops] == sorted([round(heights[r, c]) for r, c in tops], reverse=True),
      'tops are not tallest first')

# A tall crown with a second leader 1.5 m from its apex stays one tree (the search window grows with height).
twin = chm.copy()
r0, c0 = cell_center(1015.0, 2045.0)
twin[r0, c0 + 3] = 39.5
twin_tops, _ = trees.find_tops(twin)
check(sum(math.hypot(world(r, c)[0] - 1015.0, world(r, c)[1] - 2045.0) < 3 for r, c in twin_tops) == 1,
      'a second leader on a 40 m crown made a second tree')

# Crowns: each cone's cells drain to its own top, so its area gives back its radius (within a cell or so).
labels, counts = trees.crowns(heights, tops)
for i, (r, c) in enumerate(tops):
    x, y = world(r, c)
    cone = min(CONES, key=lambda k: math.hypot(k[0] - x, k[1] - y))
    radius = math.sqrt(counts[i] * CELL * CELL / math.pi)
    # The crown is the part of the cone above the 2 m floor.
    expected = cone[3] if cone[2] / 2 >= 2.0 else cone[3] * (cone[2] - 2.0) / (cone[2] / 2)
    check(abs(radius - expected) < 0.8, f'crown of the {cone[2]:.0f} m tree: radius {radius:.1f}, expected {expected:.1f}')

# Types: yellow-green crowns are broadleaf, blue-grey green ones conifers, whatever their brightness.
check(trees.classify([trees.yellowness(80, 100, 40), trees.yellowness(40, 50, 20)], 0.08) == ['broadleaf', 'broadleaf'],
      'yellow-green crowns, bright or shaded, are not broadleaf')
check(trees.classify([trees.yellowness(60, 80, 75), trees.yellowness(120, 160, 150), float('nan')], 0.08)
      == ['conifer'] * 3, 'blue-grey crowns, bright or shaded, or without imagery, are not conifers')

# Species: repeatable per place; broadleaf by water is madrone or alder; small conifers are saplings.
a = trees.species_for('conifer', 40, 5, False, '1015.0,2045.0')
check(a == trees.species_for('conifer', 40, 5, False, '1015.0,2045.0'), 'the same tree got a different species')
check(a in ('douglas-fir-stand', 'western-red-cedar', 'western-hemlock'), f'a 40 m stand conifer became {a}')
check(trees.species_for('conifer', 6, 2, False, 'x') in ('western-red-cedar-sapling', 'western-hemlock-sapling'),
      'a 6 m conifer is not a sapling')
seen = {trees.species_for('broadleaf', 12, 4, True, f'{i}') for i in range(40)}
check(seen <= {'pacific-madrone', 'pacific-madrone-bluff', 'red-alder'} and 'pacific-madrone-bluff' in seen,
      f'broadleaf trees by the water: {sorted(seen)}')
scene = json.load(open(os.path.join(HERE, '..', '..', 'Game', 'Resources', 'Scenes', 'pnw-plants.scene.json')))
ids = {o['id'] for o in scene['objects']}
species = {trees.species_for(k, h, c, w, f'{i}') for k in ('conifer', 'broadleaf') for h in (6, 20, 40)
           for c in (2, 6, 12) for w in (False, True) for i in range(30)}
check(species <= ids, f'species not in pnw-plants: {sorted(species - ids)}')

# Trunks: a top over a road moves to its edge when that's close, and is dropped when it's far.
road = [(5.0, (0.0, 0.0), (100.0, 0.0))]
roads_list = [(clearance, [a_, b_]) for clearance, a_, b_ in road]
moved, why = trees.place((50.0, 3.5), roads_list, [])
check(why is None and abs(moved[1] - 5.0) < 1e-2 and abs(moved[0] - 50.0) < 1e-6,
      f'a top 1.5 m inside the road edge went to {moved}, {why}')
_, why = trees.place((50.0, 0.5), roads_list, [])
check(why == 'road', 'a top over the middle of the road kept a trunk')
_, why = trees.place((50.0, 20.0), [], [[(40.0, 10.0), (60.0, 10.0), (60.0, 30.0), (40.0, 30.0)]])
check(why == 'building', 'a top inside a building kept a trunk')

# The whole pass: imagery makes the 30 m tree broadleaf; a road over the 20 m tree moves it; ground below the water
# level near the 12 m tree makes it a by-the-water species.
image = np.zeros((4, N, N), dtype=np.float32)
image[0], image[1], image[2], image[3] = 60, 80, 75, 170       # blue-grey conifer green everywhere
r1, c1 = cell_center(1030.0, 2045.0)
image[0, r1 - 8:r1 + 9, c1 - 8:c1 + 9] = 90                    # yellow-green over the 30 m tree
image[1, r1 - 8:r1 + 9, c1 - 8:c1 + 9] = 110
image[2, r1 - 8:r1 + 9, c1 - 8:c1 + 9] = 50
dtm = np.full((N, N), 30.0, dtype=np.float32)
dtm[cell_center(1035.0, 2001.0)[0]:, :] = 1.0                    # a beach along the south edge
road_line = [(2.6, [(1040.0, 2040.0), (1050.0, 2046.0)])]
found_trees, report = trees.detect(chm, dtm, image, EXTENT, road_line, [])
# Each tree by the cone it stands for (cone apexes fall between cells, so heights come out a little under).
by_height = {min((40, 30, 20, 35, 12), key=lambda h: abs(h - t['height'])): t for t in found_trees}
check(len(found_trees) == 5, f'{len(found_trees)} trees from the synthetic canopy, expected 5')
check(by_height.get(30, {}).get('type') == 'broadleaf' and by_height.get(40, {}).get('type') == 'conifer',
      'imagery types: ' + str({h: t['type'] for h, t in by_height.items()}))
t20 = by_height.get(20)
check(t20 is not None and trees.import_roads.distance_to_line((t20['x'], t20['y']), road_line[0][1]) >= 2.6 - 1e-6,
      'the 20 m tree under the road edge was not moved off it')
check(by_height.get(12, {}).get('species') in ('western-red-cedar', 'douglas-fir', 'western-hemlock',
                                                 'douglas-fir-stand', 'western-red-cedar-sapling',
                                                 'western-hemlock-sapling'), 'the 12 m conifer by the water')

# Re-runs keep edited, hand-added and removed trees.
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, 'Test.trees.geojson')
    features = [trees.tree_feature('lidar-1', 1, 2, {'species': 'red-alder', 'source': 'lidar', 'edited': True}),
                trees.tree_feature('lidar-2', 3, 4, {'species': 'red-alder', 'source': 'lidar', 'edited': False}),
                trees.tree_feature('mine', 5, 6, {'species': 'salal', 'source': 'manual'}),
                trees.tree_feature('lidar-3', 7, 8, {'removed': True})]
    trees.import_roads.write_collection(path, {'type': 'FeatureCollection'}, features)
    kept = [f['properties']['id'] for f in trees.kept_trees(path)]
    check(kept == ['lidar-1', 'mine', 'lidar-3'], f'kept {kept}')

# A LiDAR tree moved more than 2 m and marked edited isn't found again at its old place (Codex review of #22).
detected = [{'x': 1015.2, 'y': 2045.3, 'height': 40.0, 'crown': 5.0, 'type': 'conifer', 'species': 'douglas-fir-stand'},
            {'x': 1030.0, 'y': 2045.0, 'height': 30.0, 'crown': 4.0, 'type': 'conifer', 'species': 'western-hemlock'}]
moved = trees.tree_feature('lidar-1015-2045', 1020.0, 2045.0, {'species': 'western-red-cedar', 'source': 'lidar',
                                                               'edited': True})
merged = trees.merge([moved], detected)
ids = [f['properties']['id'] for f in merged]
check(ids == ['lidar-1015-2045', 'lidar-1030-2045'], f'a moved tree came back at its old place: {ids}')
check(merged[0]['geometry']['coordinates'] == [1020.0, 2045.0], 'the moved tree lost its new place')
# A hand-added tree suppresses a detection within 2 m of it.
mine = trees.tree_feature('mine', 1029.0, 2045.5, {'species': 'bigleaf-maple', 'source': 'manual'})
check([f['properties']['id'] for f in trees.merge([mine], detected)] == ['mine', 'lidar-1015-2045'],
      'a detection beside a hand-added tree was kept')

# Multipart roads are several polylines (Codex review of #22): each keeps trunks off it.
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, 'Test.roads.geojson')
    json.dump({'type': 'FeatureCollection', 'features': [
        {'type': 'Feature', 'properties': {'kind': 'road', 'id': 'r', 'width': 5.0},
         'geometry': {'type': 'MultiLineString', 'coordinates': [[[0, 0], [100, 0]], [[0, 50], [100, 50]]]}}]},
        open(path, 'w'))
    multi = trees.road_lines(path)
    check(len(multi) == 2 and all(len(line) == 2 for _, line in multi), f'a two-part road gave {multi}')
    moved, why = trees.place((50.0, 48.5), multi, [])
    check(why is None and moved[1] <= 46.5 + 1e-6, f'a top on the second part went to {moved}, {why}')

# A trunk moved to an oblique road's edge stays there (Codex review of #22: rounding left it a hair inside).
where, why = trees.place((70.23419074566571, 44.10747966498315), [(3.5, [(0.0, 0.0), (100.0, 60.0)])], [])
check(why is None, 'a top 1.8 m inside an oblique road was dropped')
for t in np.linspace(0.05, 0.95, 50):
    px, py = 100 * t, 60 * t
    for side in (-1, 1):
        ox, oy = px - side * 0.514 * 2.0, py + side * 0.857 * 2.0
        where, why = trees.place((ox, oy), [(3.5, [(0.0, 0.0), (100.0, 60.0)])], [])
        check(why is None and trees.import_roads.distance_to_line(where, [(0.0, 0.0), (100.0, 60.0)]) >= 3.5,
              f'a top 2 m from an oblique road at {ox:.2f}, {oy:.2f}: {where}, {why}')

print('import_trees checks passed' if not failures else f'{failures} import_trees checks failed')
sys.exit(1 if failures else 0)
