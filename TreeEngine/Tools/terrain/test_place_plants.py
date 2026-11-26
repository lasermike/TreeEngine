"""Checks place_plants.py on synthetic trees and ground (no package):

    python -I test_place_plants.py
"""

import importlib.util
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location('place_plants', os.path.join(HERE, 'place_plants.py'))
plants = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plants)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


# Ground: a slope rising east, under water (below 1.9 m) along the west edge.
def ground(x, y):
    return 0.5 + 0.1 * (x - 1000.0)


trees = [{'x': 1050.0 + 10 * i, 'y': 2050.0 + 7 * (i % 3), 'species': s, 'height': h, 'crown': 4.0, 'id': f't{i}'}
         for i, (s, h) in enumerate([('douglas-fir-stand', 40.0), ('douglas-fir-stand', 20.0), ('western-red-cedar', 28.0),
                                     ('red-alder', 30.0), ('pacific-madrone-bluff', 11.0)])]
road = [(2.5, [(1000.0, 2040.0), (1120.0, 2040.0)])]
trail = [(0.5, [(1080.0, 2000.0), (1080.0, 2100.0)])]
extent = (1000.0, 2000.0, 1120.0, 2100.0)
placed = plants.plan(trees, road + trail, ground, extent, 1500, 7, water_level=1.9, focus=(1060.0, 2060.0, 15.0, 300))

# Trees come first, where they were found, scaled to their height from their species' own.
check([p[0] for p in placed[:5]] == [t['species'] for t in trees], 'trees are not first, in order')
check(all((p[1], p[2]) == (t['x'], t['y']) for p, t in zip(placed, trees)), 'a tree moved')
check(abs(placed[0][3] - 1.0) < 1e-6 and abs(placed[1][3] - 0.5) < 1e-6, 'trees are not scaled to their height')
check(abs(placed[3][3] - 1.5) < 1e-6, 'a 30 m alder (20 m source) is not scaled 1.5')
# Understory: as many as asked, none on the road or trail, in the water or in a trunk.
under = placed[5:]
check(len(under) == 1800, f'{len(under)} understory plants, expected 1500 + 300')
on_lines = [p for p in under if any(plants.import_roads.distance_to_line((p[1], p[2]), line) < half + 0.3
                                    for half, line in road + trail)]
check(not on_lines, f'{len(on_lines)} plants on the road or trail')
check(all(ground(p[1], p[2]) >= 2.2 for p in under), 'a plant stands in the water')
check(all(math.hypot(p[1] - t['x'], p[2] - t['y']) >= 0.8 for p in under for t in trees), 'a plant in a trunk')
# The focus: its 300 extra plants within 15 m of it.
near = sum(math.hypot(p[1] - 1060.0, p[2] - 2060.0) <= 15.0 for p in under)
check(near >= 300, f'only {near} plants within the focus')
# Mixes by place: under crowns the canopy mix; by the road the roadside mix shows up; near the beach shore plants.
canopy = {s for s, *_ in plants.UNDER_CANOPY}
covered = [p for p in under if any(math.hypot(p[1] - t['x'], p[2] - t['y']) < t['crown'] for t in trees)
           and plants.edge_distance(p[1], p[2], road + trail, plants.line_grid(road + trail)) >= 4.0
           and p[1] > 1060.0]                      # past the beach's 40 m reach (water below x = 1014)
check(covered and all(p[0] in canopy for p in covered), 'a plant under a crown is not from the canopy mix')
roadside = {p[0] for p in under if plants.edge_distance(p[1], p[2], road, plants.line_grid(road)) < 4.0}
check('himalayan-blackberry' in roadside, 'no blackberry along the road')
shore = {p[0] for p in under if p[1] < 1030.0}
check('oceanspray' in shore, 'no oceanspray near the beach')
# The beach reaches 40 m whatever the grid (Codex review of #23): beach only along x < 15, a plant at x = 45 is
# 30 m from it and gets the shore mix about half the time.
def beach(x, y):
    return 0.5 if x < 15.0 else 10.0


shore_plan = plants.plan([], [], beach, (40.0, 0.0, 50.0, 100.0), 400, 3, water_level=1.9)
shore_share = sum(p[0] in ('oceanspray',) for p in shore_plan) / len(shore_plan)
check(shore_share > 0.15, f'plants 25-35 m from the beach got oceanspray {shore_share:.0%} of the time')

# Lines without a width are as wide as the engine draws them (Codex review of #23): a road 6 m, a driveway 3 m.
import tempfile
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, 'Test.roads.geojson')
    json.dump({'type': 'FeatureCollection', 'features': [
        {'type': 'Feature', 'properties': {'kind': k, 'id': k},
         'geometry': {'type': 'LineString', 'coordinates': [[0, y], [100, y]]}}
        for k, y in (('road', 0), ('driveway', 50), ('trail', 80))]}, open(path, 'w'))
    halves = sorted(round(h, 3) for h, _ in plants.load_lines(path))
    check(halves == [0.5, 1.5, 3.0], f'default half widths {halves}, expected trail 0.5, driveway 1.5, road 3')
    lines = plants.load_lines(path)
    check(plants.edge_distance(50.0, 2.9, lines, plants.line_grid(lines)) < 0, 'a point 2.9 m from a 6 m road is off it')

# A clearing along a line: within 10 m of it only a tenth of the understory stays, and only low ground cover.
lot = [(1050.0, 2000.0), (1050.0, 2100.0)]
cleared_plan = plants.plan(trees, road + trail, ground, extent, 1500, 7, water_level=1.9, clearings=[(10.0, lot, 0.1)])
open_plan = plants.plan(trees, road + trail, ground, extent, 1500, 7, water_level=1.9)
inside = lambda p: plants.import_roads.distance_to_line((p[1], p[2]), lot) < 10.0
in_cleared = [p for p in cleared_plan[5:] if inside(p)]
in_open = [p for p in open_plan[5:] if inside(p)]
check(len(in_open) > 100 and len(in_cleared) < 0.2 * len(in_open),
      f'the clearing kept {len(in_cleared)} of about {len(in_open)} plants')
low = {s for s, *_ in plants.CLEARING}
check(all(p[0] in low for p in in_cleared), f'a tall plant in the clearing: {sorted({p[0] for p in in_cleared} - low)}')
outside_cleared = [p for p in cleared_plan[5:] if not inside(p)]
outside_open = [p for p in open_plan[5:] if not inside(p)]
check(abs(len(outside_cleared) - len(outside_open)) < 0.15 * len(outside_open),
      'the clearing made the understory around it denser')
check(cleared_plan[:5] == open_plan[:5], 'the clearing moved trees')
# A clearing that keeps everything changes nothing but the mix: as many plants as without it (Codex review of #29).
kept_all = plants.plan(trees, road + trail, ground, extent, 400, 7, water_level=1.9, clearings=[(1000.0, lot, 1.0)])
check(len(kept_all) - 5 == 400, f'a clearing keeping everything gave {len(kept_all) - 5} of 400 plants')
# A two-part line clears along each part, not across the gap between them.
with tempfile.TemporaryDirectory() as tmp:
    roads_path = os.path.join(tmp, 'Test.roads.geojson')
    json.dump({'type': 'FeatureCollection', 'features': [
        {'type': 'Feature', 'properties': {'kind': 'driveway', 'id': 'lot'},
         'geometry': {'type': 'MultiLineString', 'coordinates': [[[0, 10], [0, 20]], [[100, 10], [100, 20]]]}}]},
        open(roads_path, 'w'))
    parts = plants.clearing_lines(roads_path, 'lot')
    check(len(parts) == 2 and all(len(p) == 2 for p in parts), f'a two-part clearing line gave {parts}')

# Seeded: the same plan twice.
again = plants.plan(trees, road + trail, ground, extent, 1500, 7, water_level=1.9, focus=(1060.0, 2060.0, 15.0, 300))
check(again == placed, 'the plan changed between runs')

# Scene objects: a source per species from pnw-plants (grown at once), instances of it, companions with their plants.
pnw = json.load(open(plants.PNW, encoding='utf-8'))
objects = plants.scene_objects(placed, ground, (1060.0, 2050.0), pnw)
sources = [o for o in objects if not o.get('instanceOf')]
ids = [o['id'] for o in sources]
check(len(ids) == len(set(ids)) and all(i.startswith('pnw-') for i in ids), 'sources are not one per species')
pnw_speed = {o['id']: o.get('animationSpeed', 1) for o in pnw['objects']}
check(all(o['animationSpeed'] == pnw_speed[o['id'][len('pnw-'):]] for o in sources),
      "a source doesn't grow at its species' speed (key 0 would regrow it too fast)")
check(all(o['instanceOf'] in ids for o in objects if o.get('instanceOf')), 'an instance of a missing source')
fir = objects[0]
check(fir['id'] == 'pnw-douglas-fir-stand' and fir['transform']['position'] == [-10.0, round(ground(1050, 2050) + 0.5, 3), 0.0],
      f'the first fir stands at {fir["transform"]["position"]}, not on the ground at its place (+0.5)')
# The half metre below the origin scales with the plant (Codex review of #23): the 20 m fir at scale 0.5.
small = next(o for o in objects if o.get('instanceOf') == 'pnw-douglas-fir-stand')
check(small['transform']['position'][1] == round(ground(1060, 2057) + 0.5 * 0.5, 3),
      f'a half-size fir stands at {small["transform"]["position"][1]}, not 0.25 above the ground')
oceanspray = [o for o in objects if 'oceanspray' in (o.get('instanceOf') or o['id'])]
plumes = [o for o in objects if 'oceanspray-plumes' in (o.get('instanceOf') or o['id'])]
check(len(oceanspray) == 2 * len(plumes) and plumes, 'oceanspray without its plumes')
# Materials: lean (one bark's maps, no roughness maps), all the sources need, few textures.
materials, atlases = plants.used_resources(objects, pnw)
textures = {v['source'] for m in materials.values() for v in m.values() if isinstance(v, dict) and 'source' in v}
check(all('metallicRoughnessTexture' not in m for m in materials.values()), 'a material kept its roughness map')
barks = {plants.LEAN_BARK} | {bark for bark, _ in plants.PHOTO_BARK.values()}
check(all(not t.startswith('Nature/bark_') or any(t.startswith(f'Nature/bark_{b}_') for b in barks) for t in textures),
      'a bark texture other than the lean one and the photo-matched one')
check(len(textures) <= 10, f'{len(textures)} textures for the plants: {sorted(textures)}')
# Photo-matched barks: the firs, hemlocks and alders take the lichen bark.
check(materials['bark-douglas-fir']['baseColorTexture']['source'] == 'Nature/bark_lichen_color.jpg'
      and materials['bark-cedar']['baseColorTexture']['source'] == f'Nature/bark_{plants.LEAN_BARK}_color.jpg',
      'the fir or cedar bark is not the photo-matched one')
# Photo-matched foliage: warmer than pnw-plants' (more red against green), the lichen untouched.
fir_needles = pnw['materials']['needles-fir']['baseColor']
check(materials['needles-fir']['baseColor'][0] / materials['needles-fir']['baseColor'][1] > 2 * fir_needles[0] / fir_needles[1],
      'the fir needles are not warmed')
check('lichen' not in plants.PHOTO_FOLIAGE and 'plumes-oceanspray' not in plants.PHOTO_FOLIAGE,
      'the lichen or the oceanspray flowers are warmed')
check(all(o['materials'][k] in materials for o in sources for k in o['materials']), 'a source material is missing')

# Hungerford's committed scene: planted, and within the 32 textures a scene holds.
scene = json.load(open(os.path.join(HERE, '..', '..', 'Game', 'Resources', 'Scenes', 'hungerford.scene.json'),
                       encoding='utf-8-sig'))
all_textures = {v['source'] for m in scene['materials'].values() for v in m.values() if isinstance(v, dict) and 'source' in v}
check(len(all_textures) <= 24, f'Hungerford names {len(all_textures)} textures; roads, props and signs need the rest of 32')
check(any(o['id'] == 'pnw-douglas-fir-stand' for o in scene['objects']), 'Hungerford is not planted')
check(scene.get('growth', {}).get('startTime') == plants.GROWN_AT and
      {o.get('animationSpeed') for o in scene['objects'] if o.get('type') == 'tree' and o['id'].startswith('pnw-')} == {10},
      "Hungerford doesn't open grown with its plants at their species' speed")

print('place_plants checks passed' if not failures else f'{failures} place_plants checks failed')
sys.exit(1 if failures else 0)
