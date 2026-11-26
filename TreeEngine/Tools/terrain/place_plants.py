"""Plants a terrain scene from its trees file and the PNW species: the trees where LiDAR found them, and understory
scattered around them.

Each species used is copied once from Game/Resources/Scenes/pnw-plants.scene.json (with its materials and leaf
atlases) as a source tree, standing at the first tree of that species; every other plant is an `instanceOf` copy,
turned at random and scaled to its tree's height (trees) or a little either way (understory), on the package's own
ground heights. Plants keep their species' growth speed, and the scene's growth clock starts at GROWN_AT (its
`growth.startTime`), so the scene opens on mature plants and key 0 regrows them in a few seconds. Their
materials are leaner than pnw-plants' (one bark's maps, no roughness maps: see lean()), since a scene holds 32
textures and the terrain, water, roads and props use most of them.

Understory, in the trees file's area:
- under the canopy (within a tree's crown): sword fern most, then salal, Oregon grape, red huckleberry, cedar and
  hemlock saplings, vine maple;
- in openings (no crown overhead): orchard and pasture grass and bracken;
- beside roads and driveways (up to 4 m past their edge, where there's light): Himalayan blackberry and bracken;
- near the beach (ground under the water level within 40 m): oceanspray and salal.
Nothing stands on a road, driveway or trail, and nothing in the water. Everything is seeded, so a re-run gives the
same scene; it replaces the objects it made before (ids starting `pnw-` and `plant-`) and keeps everything else.

    python place_plants.py <Name.terrain> <Name.trees.geojson> <scene.json> [--roads <Name.roads.geojson>]
        [--understory 3000] [--focus X Y RADIUS COUNT] [--seed 7]
"""
import argparse
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_roads  # noqa: E402  (Package, distance_to_line)

ENGINE = os.path.normpath(os.path.join(HERE, '..', '..'))
PNW = os.path.join(ENGINE, 'Game', 'Resources', 'Scenes', 'pnw-plants.scene.json')
TREE_ORIGIN = 0.5            # Trees draw half a metre below their position, times their scale
                             # (Tree::ComputeTransformationsManual).
# Seconds of growth by which every PNW species is grown at its own speed (they all are by 3 s at animationSpeed 10):
# the scene's growth clock starts here.
GROWN_AT = 10

# Each species' height as its rules grow it (m): the trunk's segments up its leader, rounded. Trees are scaled from
# these to their LiDAR heights.
SOURCE_HEIGHT = {
    'douglas-fir-stand': 40.0, 'douglas-fir': 30.0, 'western-red-cedar': 28.0, 'western-hemlock': 26.0,
    'red-alder': 20.0, 'bigleaf-maple': 18.0, 'pacific-madrone': 13.0, 'pacific-madrone-bluff': 11.0,
    'western-red-cedar-sapling': 4.2, 'western-hemlock-sapling': 3.9,
}
# Widths of lines without one, as the engine draws them (Roads::Load in Game/RoadNetwork.cpp).
WIDTHS = {'road': 6.0, 'driveway': 3.0, 'trail': 1.0}
# Plants grown together: the second stands with the first (its flowers or seed heads).
COMPANIONS = {'oceanspray': 'oceanspray-plumes', 'orchard-grass': 'orchard-grass-seed'}

# Understory mixes (species, weight, scale range) by where they grow.
UNDER_CANOPY = [('sword-fern', 10, (0.8, 1.3)), ('salal', 5, (0.7, 1.3)), ('oregon-grape', 2.5, (0.7, 1.2)),
                ('red-huckleberry', 1.5, (0.7, 1.2)), ('western-red-cedar-sapling', 1.2, (0.5, 1.4)),
                ('western-hemlock-sapling', 1.2, (0.5, 1.4)), ('vine-maple', 0.6, (0.7, 1.2))]
OPENING = [('orchard-grass', 6, (0.8, 1.2)), ('pasture-grass', 6, (0.8, 1.3)), ('bracken-fern', 3, (0.8, 1.3)),
           ('sword-fern', 1, (0.8, 1.2))]
ROADSIDE = [('himalayan-blackberry', 4, (0.7, 1.3)), ('bracken-fern', 3, (0.8, 1.3)), ('orchard-grass', 3, (0.8, 1.2)),
            ('sword-fern', 2, (0.8, 1.2))]
SHORE = [('oceanspray', 4, (0.8, 1.2)), ('salal', 3, (0.8, 1.3)), ('sword-fern', 1, (0.8, 1.2))]


def weighted(rng, mix):
    total = sum(w for _, w, _ in mix)
    pick = rng.uniform(0, total)
    for species, weight, scales in mix:
        pick -= weight
        if pick <= 0:
            return species, scales
    return mix[-1][0], mix[-1][2]


def load_trees(path):
    features = json.load(open(path, encoding='utf-8')).get('features', [])
    trees = []
    for f in features:
        p = f.get('properties') or {}
        if p.get('removed') or p.get('kind', 'tree') != 'tree' or not p.get('species'):
            continue
        x, y = f['geometry']['coordinates'][:2]
        trees.append({'x': x, 'y': y, 'species': p['species'], 'height': p.get('height'), 'crown': p.get('crown') or 3,
                      'id': p.get('id')})
    return trees


def load_lines(path):
    """Roads, driveways and trails as (half width, polylines)."""
    if not path:
        return []
    lines = []
    for f in json.load(open(path, encoding='utf-8')).get('features', []):
        p = f.get('properties') or {}
        if p.get('kind') not in ('road', 'driveway', 'trail') or p.get('removed'):
            continue
        g = f.get('geometry') or {}
        parts = g.get('coordinates') or []
        parts = parts if g.get('type') == 'MultiLineString' else [parts]
        default = WIDTHS[p['kind']]
        half = (p.get('width') or default) / 2 + (p.get('shoulder') or 0)
        for part in parts:
            if len(part) >= 2:
                lines.append((half, [tuple(q[:2]) for q in part]))
    return lines


class Grid:
    """A coarse lookup of points or lines by 10 m cell, so each test looks only nearby."""
    def __init__(self, cell=10.0):
        self.cell, self.cells = cell, {}

    def key(self, x, y):
        return int(math.floor(x / self.cell)), int(math.floor(y / self.cell))

    def add(self, x0, y0, x1, y1, item):
        a, b = self.key(x0, y0), self.key(x1, y1)
        for i in range(a[0], b[0] + 1):
            for j in range(a[1], b[1] + 1):
                self.cells.setdefault((i, j), []).append(item)

    def near(self, x, y):
        i, j = self.key(x, y)
        seen = []
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for item in self.cells.get((i + di, j + dj), ()):
                    if item not in seen:
                        seen.append(item)
        return seen


def line_grid(lines):
    grid = Grid(10.0)
    for i, (half, line) in enumerate(lines):
        for (ax, ay), (bx, by) in zip(line, line[1:]):
            grid.add(min(ax, bx) - half, min(ay, by) - half, max(ax, bx) + half, max(ay, by) + half, i)
    return grid


def edge_distance(x, y, lines, grid):
    """How far (x, y) is from the nearest road, driveway or trail edge (negative inside one); inf with none near."""
    best = math.inf
    for i in grid.near(x, y):
        half, line = lines[i]
        best = min(best, import_roads.distance_to_line((x, y), line) - half)
    return best


def plan(trees, lines, ground, extent, understory, seed, water_level=1.9, focus=None):
    """Where everything goes: a list of (species, x, y, scale, yaw) in the package's CRS, trees first. `ground(x, y)`
    gives the ground height (NaN outside the package). `focus` (x, y, radius, count) adds `count` more understory
    plants within `radius` of (x, y), where the camera looks closest."""
    rng = random.Random(seed)
    placed = []
    road_grid = line_grid(lines)
    crowns = Grid(10.0)
    for i, t in enumerate(trees):
        crowns.add(t['x'] - t['crown'], t['y'] - t['crown'], t['x'] + t['crown'], t['y'] + t['crown'], i)
    for t in trees:
        source = SOURCE_HEIGHT.get(t['species'])
        scale = min(1.7, max(0.35, (t['height'] or source) / source)) if source else 1.0
        placed.append((t['species'], t['x'], t['y'], scale, rng.uniform(0, 360)))
    # Beach: cells under the water level, coarse, to find "near the beach".
    # Cells as wide as the 40 m reach, so a point's own and neighbouring cells hold every beach sample within it.
    shore = Grid(40.0)
    # Sampled 40 m past the area too: a beach just outside it is still near the plants inside.
    step, reach = 5.0, 40.0
    y = extent[1] - reach
    while y <= extent[3] + reach:
        x = extent[0] - reach
        while x <= extent[2] + reach:
            h = ground(x, y)
            if h == h and h < water_level:
                shore.add(x, y, x, y, (x, y))
            x += step
        y += step
    trunks = Grid(10.0)
    for i, t in enumerate(trees):
        trunks.add(t['x'], t['y'], t['x'], t['y'], i)
    def anywhere():
        return rng.uniform(extent[0], extent[2]), rng.uniform(extent[1], extent[3])

    def near_focus():
        r, a = focus[2] * math.sqrt(rng.random()), rng.uniform(0, 2 * math.pi)
        return focus[0] + r * math.cos(a), focus[1] + r * math.sin(a)

    batches = [(understory, anywhere)] + ([(focus[3], near_focus)] if focus else [])
    for want, where in batches:
        scatter(want, where, rng, trees, lines, road_grid, crowns, trunks, shore, ground, water_level, placed)
    return placed


def scatter(want, where, rng, trees, lines, road_grid, crowns, trunks, shore, ground, water_level, placed):
    """Adds `want` understory plants at points from `where()` that pass: on the ground above the water, off roads,
    driveways, trails and trunks; the mix by canopy, road edge and beach."""
    made, tries = 0, 0
    while made < want and tries < want * 20:
        tries += 1
        x, y = where()
        h = ground(x, y)
        if not h == h or h < water_level + 0.3:
            continue
        edge = edge_distance(x, y, lines, road_grid)
        if edge < 0.3:
            continue
        # Not inside a trunk.
        if any(math.hypot(x - trees[i]['x'], y - trees[i]['y']) < 0.8 for i in trunks.near(x, y)):
            continue
        covered = any(math.hypot(x - trees[i]['x'], y - trees[i]['y']) < trees[i]['crown'] for i in crowns.near(x, y))
        near_shore = any(math.hypot(x - sx, y - sy) < 40 for sx, sy in shore.near(x, y)) if shore.cells else False
        if near_shore and rng.random() < 0.5:
            mix = SHORE
        elif edge < 4.0 and rng.random() < 0.6:
            mix = ROADSIDE
        elif covered:
            mix = UNDER_CANOPY
        else:
            mix = OPENING
        species, scales = weighted(rng, mix)
        placed.append((species, x, y, rng.uniform(*scales), rng.uniform(0, 360)))
        made += 1


def scene_objects(placed, ground, center, pnw):
    """The scene's objects for the plan: a source tree per species (from pnw-plants, grown at once) at its first
    plant, the rest instances; companions (plumes, seed heads) stand with their plant."""
    sources = {o['id']: o for o in pnw['objects'] if o.get('type') == 'tree' and not o.get('instanceOf')}
    objects, made = [], set()
    counter = 0
    for species, x, y, scale, yaw in placed:
        for sp in [species] + ([COMPANIONS[species]] if species in COMPANIONS else []):
            if sp not in sources:
                raise SystemExit(f'{sp} is not a tree in {PNW}')
            # The half metre below the origin scales with the plant.
            position = [round(x - center[0], 3), round(ground(x, y) + TREE_ORIGIN * scale, 3), round(y - center[1], 3)]
            transform = {'position': position, 'rotationDegrees': [0, round(yaw, 1), 0],
                         'scale': [round(scale, 3)] * 3}
            if sp not in made:
                source = json.loads(json.dumps(sources[sp]))
                source['id'] = f'pnw-{sp}'
                source['transform'] = transform
                objects.append(source)
                made.add(sp)
            else:
                counter += 1
                objects.append({'id': f'plant-{counter}', 'instanceOf': f'pnw-{sp}', 'transform': transform})
    return objects


LEAN_BARK = 'furrowed'


# Bark colours matched to photos of the site (Content/photos/scott-dr), as tints of the lean bark: the Douglas firs and
# hemlocks there are grey, lichen-flecked; the cedars keep their red-brown.
PHOTO_BARK = {'bark-douglas-fir': [1.1, 1.3, 1.9, 1], 'bark-hemlock': [1.0, 1.15, 1.6, 1]}


def lean(material, name=None):
    """A pnw-plants material on fewer textures, for a scene that has its own (a scene holds 32): barks all use one
    bark's maps (tinted as before, or as PHOTO_BARK says), and no material has a roughness map (its roughness value
    stands)."""
    m = json.loads(json.dumps(material))
    if name in PHOTO_BARK:
        m['baseColor'] = PHOTO_BARK[name]
    m.pop('metallicRoughnessTexture', None)
    for key in ('baseColorTexture', 'normalTexture'):
        source = (m.get(key) or {}).get('source', '')
        if source.startswith('Nature/bark_'):
            kind = 'color' if key == 'baseColorTexture' else 'normal'
            m[key]['source'] = f'Nature/bark_{LEAN_BARK}_{kind}.jpg'
    return m


def used_resources(objects, pnw):
    """The pnw-plants materials (lean) and atlases the sources use."""
    materials, atlases = {}, {}
    for o in objects:
        if o.get('instanceOf'):
            continue
        for m in (o.get('materials') or {}).values():
            materials[m] = lean(pnw['materials'][m], m)
        atlas = (o.get('leaves') or {}).get('atlas')
        if atlas:
            atlases[atlas] = pnw['atlases'][atlas]
    return materials, atlases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('package', help='the terrain package directory (Name.terrain): ground heights')
    parser.add_argument('trees', help='the trees file (Name.trees.geojson, from import_trees.py)')
    parser.add_argument('scene', help='the scene to plant (its plants are replaced)')
    parser.add_argument('--roads', help='the roads file: nothing on roads, driveways or trails')
    parser.add_argument('--understory', type=int, default=3000, help='understory plants (default 3000)')
    parser.add_argument('--margin', type=float, default=10, help='understory reaches this far past the trees (m)')
    parser.add_argument('--focus', type=float, nargs=4, metavar=('X', 'Y', 'RADIUS', 'COUNT'),
                        help='COUNT more understory plants within RADIUS (m) of X Y (CRS): denser where the camera '
                             'starts')
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    package = import_roads.Package(args.package)
    trees = load_trees(args.trees)
    if not trees:
        sys.exit(f'{args.trees}: no trees')
    lines = load_lines(args.roads)
    extent = (min(t['x'] for t in trees) - args.margin, min(t['y'] for t in trees) - args.margin,
              max(t['x'] for t in trees) + args.margin, max(t['y'] for t in trees) + args.margin)
    placed = plan(trees, lines, package.height, extent, args.understory, args.seed,
                  focus=tuple(args.focus) if args.focus else None)
    pnw = json.load(open(PNW, encoding='utf-8'))
    objects = scene_objects(placed, package.height, (package.cx, package.cy), pnw)
    materials, atlases = used_resources(objects, pnw)
    scene = json.load(open(args.scene, encoding='utf-8-sig'))
    keep = [o for o in scene['objects'] if not (o.get('id', '').startswith('pnw-') or o.get('id', '').startswith('plant-'))]
    scene['objects'] = keep + objects
    scene['growth'] = dict(scene.get('growth', {}), startTime=GROWN_AT)
    for name, material in materials.items():
        scene.setdefault('materials', {})[name] = material
    scene['atlases'] = dict(scene.get('atlases', {}), **atlases)
    with open(args.scene, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(scene, f, indent=2)
        f.write('\n')
    counts = {}
    for species, *_ in placed:
        counts[species] = counts.get(species, 0) + 1
    print(f'planted {args.scene}: {len(trees)} trees and {len(placed) - len(trees)} understory plants '
          f'({len(objects)} objects, {len(materials)} materials) - '
          + ', '.join(f'{n} {s}' for s, n in sorted(counts.items(), key=lambda kv: -kv[1])))


if __name__ == '__main__':
    main()
