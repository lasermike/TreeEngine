"""Finds the trees in an area from LiDAR and writes them as points to a trees file (Name.trees.geojson).

The canopy height model is the survey's highest-hit surface (DSM) minus its bare earth (DTM). Treetops are its local
maxima, searched over a window that grows with the tree's height (tall trees have wide crowns, so one tall crown
isn't split into several tops); each crown is the area that drains to its top (a watershed of the canopy height).
Aerial imagery (USDA NAIP: red, green, blue and near infrared at 0.6 m, public domain) then sorts crowns into conifer
and broadleaf: in summer imagery the conifers are blue-grey green and broadleaf crowns yellow-green. Height, crown size and
position pick a species from Game/Resources/Scenes/pnw-plants.scene.json.

The trees file is GeoJSON in the package's CRS, one point per tree, with `species`, `height` (m), `crown` (radius,
m), `type` (conifer or broadleaf), `source` and `edited`. Edit it in QGIS like the roads file: move, delete or add
points, change a species, and set `edited` to true on what you change. A re-run keeps edited and hand-added (`source`
manual) trees, and `removed` markers that stop a tree being found again, and replaces the rest.

    python import_trees.py <Name.terrain> <Name.trees.geojson> --dsm <dsm folder or VRT> --dtm <dtm folder or VRT>
        --center <x> <y> [--size 300] [--naip fetch|<raster>|none] [--roads <Name.roads.geojson>]
        [--buildings <osm buildings JSON>] [--work <dir>]

Needs GDAL on PATH (gdalbuildvrt, gdalwarp), for example from OSGeo4W or QGIS. `--naip fetch` downloads the newest
NAIP image over the area from Microsoft's Planetary Computer (sending it the area's bounds).
"""
import argparse
import glob
import hashlib
import heapq
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_roads  # noqa: E402  (Package, osm_ways, distance_to_line, write_collection)

FEET = 0.3048006096          # US survey foot: Washington's LiDAR heights are in feet.
CELL = 0.5                   # Canopy grid spacing (m).
MIN_HEIGHT = 4.0             # Lower tops are shrubs, cars, sheds: not trees.
STAC = 'https://planetarycomputer.microsoft.com/api/stac/v1/search'
SIGN = 'https://planetarycomputer.microsoft.com/api/sas/v1/sign?href='
AGENT = 'TreeEngine-import-trees/1.0'


def log(message):
    print(message, file=sys.stderr)


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f'{command[0]} failed:\n{result.stderr}')
    return result.stdout


def require(tool):
    if shutil.which(tool) is None:
        sys.exit(f'{tool} not found on PATH; install GDAL (OSGeo4W, QGIS or conda-forge)')


# ---------------------------------------------------------------------------------------------------------------
# Rasters: cut to the area on a CELL grid in the package's CRS, read with numpy (row 0 is north).

def source_raster(path, work, name):
    """A raster GDAL reads: a file or VRT as given, or a VRT over a folder's GeoTIFFs."""
    if os.path.isdir(path):
        tiles = sorted(glob.glob(os.path.join(path, '*.tif')))
        if not tiles:
            sys.exit(f'{path}: no .tif tiles')
        vrt = os.path.join(work, name + '.vrt')
        run(['gdalbuildvrt', '-overwrite', vrt] + tiles)
        return vrt
    if not os.path.exists(path) and not path.startswith('/vsi'):
        sys.exit(f'{path} not found')
    return path


def cut(source, work, name, crs, extent, bands=1, resampling='bilinear'):
    """The source over `extent` (xmin, ymin, xmax, ymax) at CELL spacing, as a float32 array (bands, rows, cols)
    with NaN where it has no data."""
    raw = os.path.join(work, name + '.raw')
    run(['gdalwarp', '-overwrite', '-t_srs', crs, '-te', *(f'{v:.3f}' for v in extent), '-tr', str(CELL), str(CELL),
         '-r', resampling, '-ot', 'Float32', '-dstnodata', 'nan', '-of', 'ENVI', source, raw])
    cols = round((extent[2] - extent[0]) / CELL)
    rows = round((extent[3] - extent[1]) / CELL)
    data = np.fromfile(raw, dtype='<f4')
    if data.size != bands * rows * cols:
        sys.exit(f'{name}: expected {bands} x {rows} x {cols} values, got {data.size}')
    return data.reshape(bands, rows, cols)


def fetch_naip(work, crs, extent):
    """The newest NAIP image over the area from the Planetary Computer, as a signed URL GDAL can read."""
    corners = subprocess.run(['gdaltransform', '-s_srs', crs, '-t_srs', 'EPSG:4326', '-output_xy'],
                                                 input=f'{extent[0]} {extent[1]}\n{extent[2]} {extent[3]}\n',
                                                 capture_output=True, text=True, check=True).stdout.split()
    west, south, east, north = (float(v) for v in corners)
    query = {'collections': ['naip'], 'bbox': [west, south, east, north], 'limit': 20,
             'sortby': [{'field': 'datetime', 'direction': 'desc'}]}
    request = urllib.request.Request(STAC, data=json.dumps(query).encode(),
                                     headers={'Content-Type': 'application/json', 'User-Agent': AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        items = json.load(response).get('features', [])
    if not items:
        sys.exit('no NAIP image covers the area')
    newest = max(item['properties']['datetime'] for item in items)
    year = newest[:4]
    hrefs = [item['assets']['image']['href'] for item in items if item['properties']['datetime'][:4] == year]
    signed = []
    for href in hrefs:
        with urllib.request.urlopen(urllib.request.Request(SIGN + urllib.parse.quote(href, safe=''),
                                                           headers={'User-Agent': AGENT}), timeout=60) as response:
            signed.append('/vsicurl/' + json.load(response)['href'])
    log(f'NAIP {year}: {len(signed)} image(s)')
    vrt = os.path.join(work, 'naip.vrt')
    run(['gdalbuildvrt', '-overwrite', vrt] + signed)
    return vrt, year


# ---------------------------------------------------------------------------------------------------------------
# Canopy: tops and crowns.

def window_max(chm, radius):
    """The maximum within a square of `radius` cells about each cell."""
    padded = np.pad(chm, radius, constant_values=-np.inf)
    view = np.lib.stride_tricks.sliding_window_view(padded, (2 * radius + 1, 2 * radius + 1))
    return view.max(axis=(2, 3))


def smooth(chm):
    """A 3 x 3 binomial smoothing, so leaders and branch tips don't each make a top."""
    k = np.array([1.0, 2.0, 1.0]) / 4
    padded = np.pad(chm, 1, mode='edge')
    rows = k[0] * padded[:-2] + k[1] * padded[1:-1] + k[2] * padded[2:]
    return k[0] * rows[:, :-2] + k[1] * rows[:, 1:-1] + k[2] * rows[:, 2:]


def search_radius(height):
    """The treetop search radius (m) for a tree this tall: crowns widen with height."""
    return 1.5 + 0.06 * height


def find_tops(chm, min_height=MIN_HEIGHT):
    """Treetops: cells at least min_height tall that are the highest within search_radius of their height. Returns
    (row, col) pairs, tallest first."""
    heights = np.nan_to_num(smooth(np.nan_to_num(chm, nan=0.0)), nan=0.0)
    tops = np.zeros(heights.shape, dtype=bool)
    # Each cell's own search radius in cells; test the cells of each radius against the maximum over that window.
    cell_r = np.maximum(1, np.round(search_radius(heights) / CELL)).astype(int)
    tall = heights >= min_height
    for r in np.unique(cell_r[tall]):
        band = tall & (cell_r == r)
        tops |= band & (heights >= window_max(heights, int(r)))
    # Candidates that touch (a plateau: a flat or level top passes the test at every cell) are one top, at the cell
    # nearest the plateau's middle.
    seen = np.zeros(heights.shape, dtype=bool)
    centers = []
    for r0, c0 in zip(*np.nonzero(tops)):
        if seen[r0, c0]:
            continue
        seen[r0, c0] = True
        group, stack = [], [(r0, c0)]
        while stack:
            r, c = stack.pop()
            group.append((r, c))
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    rr, cc = r + dr, c + dc
                    if (0 <= rr < tops.shape[0] and 0 <= cc < tops.shape[1] and tops[rr, cc]
                            and not seen[rr, cc]):
                        seen[rr, cc] = True
                        stack.append((rr, cc))
        mr = sum(r for r, _ in group) / len(group)
        mc = sum(c for _, c in group) / len(group)
        centers.append(min(group, key=lambda p: (p[0] - mr) ** 2 + (p[1] - mc) ** 2))
    rows = np.array([r for r, _ in centers], dtype=int)
    cols = np.array([c for _, c in centers], dtype=int)
    order = np.argsort(-heights[rows, cols], kind='stable')
    # A flat or level top (a plateau) passes the test at every cell: keep a candidate only when no taller or equal
    # one already kept is within its search radius, so each crown keeps one top.
    kept = []
    for i in order:
        r, c = int(rows[i]), int(cols[i])
        reach = search_radius(heights[r, c]) / CELL
        if any((r - kr) ** 2 + (c - kc) ** 2 <= reach * reach for kr, kc in kept):
            continue
        kept.append((r, c))
    return kept, heights


def crowns(heights, tops, floor=2.0):
    """Each canopy cell (at least `floor` tall) assigned to a top by flooding down from the tops, highest first (a
    marker-controlled watershed). Returns a label array (-1 for none) and each top's cell count."""
    labels = np.full(heights.shape, -1, dtype=np.int32)
    heap = []
    for i, (r, c) in enumerate(tops):
        labels[r, c] = i
        heapq.heappush(heap, (-heights[r, c], r, c, i))
    rows, cols = heights.shape
    while heap:
        _, r, c, i = heapq.heappop(heap)
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            rr, cc = r + dr, c + dc
            if 0 <= rr < rows and 0 <= cc < cols and labels[rr, cc] < 0 and heights[rr, cc] >= floor:
                # Only downhill or level: a crown doesn't climb into a neighbour's.
                if heights[rr, cc] <= heights[r, c] + 0.25:
                    labels[rr, cc] = i
                    heapq.heappush(heap, (-heights[rr, cc], rr, cc, i))
    counts = np.bincount(labels[labels >= 0], minlength=len(tops))
    return labels, counts


# ---------------------------------------------------------------------------------------------------------------
# Types and species.

def crown_means(image, labels, count):
    """Each crown's mean of every band of `image` (bands, rows, cols); NaN where a crown has no image."""
    means = np.full((image.shape[0], count), np.nan)
    flat = labels.ravel()
    inside = flat >= 0
    for b in range(image.shape[0]):
        values = image[b].ravel()
        ok = inside & np.isfinite(values)
        sums = np.bincount(flat[ok], weights=values[ok], minlength=count)
        n = np.bincount(flat[ok], minlength=count)
        with np.errstate(invalid='ignore', divide='ignore'):
            means[b] = np.where(n > 0, sums / n, np.nan)
    return means


def yellowness(red, green, blue):
    """How yellow-green a crown is: red and green against blue, over its brightness, so sunlit and shaded crowns
    compare alike."""
    with np.errstate(invalid='ignore', divide='ignore'):
        return (red + green - 2 * blue) / (red + green + blue)


def classify(yellow, threshold):
    """Conifer or broadleaf per crown. In summer imagery the conifers here are blue-grey green and broadleaf crowns
    (alder, maple, madrone) yellow-green, whatever the light, so crowns yellower than `threshold` are broadleaf.
    Crowns without imagery are conifers (most of the canopy). Near infrared brightness and NDVI don't separate them
    reliably: sunlit conifers on the slopes are as bright as broadleaf crowns."""
    return ['broadleaf' if np.isfinite(y) and y > threshold else 'conifer' for y in yellow]


def pick(seed, choices):
    """A repeatable weighted choice: the same tree gets the same species on every run."""
    total = sum(w for _, w in choices)
    value = int(hashlib.sha1(seed.encode()).hexdigest()[:8], 16) / 0xFFFFFFFF * total
    for name, weight in choices:
        value -= weight
        if value <= 0:
            return name
    return choices[-1][0]


def species_for(kind, height, crown, near_water, seed):
    """A species from pnw-plants for a tree of this type and size. Conifers: mostly stand-grown Douglas fir, with
    cedar and hemlock; wide, low crowns are more often cedar or an open-grown fir. Broadleaf: madrone near the water,
    big wide crowns bigleaf maple, otherwise red alder."""
    if kind == 'conifer':
        if height < 8:
            return pick(seed, [('western-red-cedar-sapling', 1), ('western-hemlock-sapling', 1)])
        open_grown = crown > 0.28 * height
        if open_grown:
            return pick(seed, [('douglas-fir', 4), ('western-red-cedar', 3), ('western-hemlock', 1)])
        return pick(seed, [('douglas-fir-stand', 7), ('western-red-cedar', 2), ('western-hemlock', 1)])
    if near_water:
        return pick(seed, [('pacific-madrone-bluff', 2), ('pacific-madrone', 2), ('red-alder', 1)])
    if crown > 5.5 and height > 14:
        return pick(seed, [('bigleaf-maple', 3), ('red-alder', 1)])
    return pick(seed, [('red-alder', 4), ('pacific-madrone', 1), ('bigleaf-maple', 1)])


# ---------------------------------------------------------------------------------------------------------------
# Exclusions and output.

def road_lines(path):
    """Roads and driveways as (half width plus a metre, points) in the file's coordinates."""
    if not path:
        return []
    lines = []
    for f in json.load(open(path, encoding='utf-8')).get('features', []):
        props = f.get('properties') or {}
        if props.get('kind') in ('road', 'driveway') and not props.get('removed'):
            geometry = f.get('geometry') or {}
            coords = geometry.get('coordinates') or []
            # A MultiLineString (the engine and finish_road_edits.py allow them) is several polylines.
            parts = coords if geometry.get('type') == 'MultiLineString' else [coords]
            # Without a width, the engine draws roads 6 m and driveways 3 m (Roads::Load in Game/RoadNetwork.cpp).
            width = props.get('width') or (3.0 if props.get('kind') == 'driveway' else 6.0)
            for part in parts:
                lines.append((width / 2 + 1.0, [tuple(p[:2]) for p in part]))
    return lines


def inside(point, polygon):
    x, y = point
    hit = False
    for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1]):
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            hit = not hit
    return hit


MAX_SHIFT = 2.5    # How far (m) a trunk may move off a road: a crown overhanging a road has its trunk beside it.
CLEARANCE_TOLERANCE = 1e-3


def place(point, roads, buildings):
    """Where the trunk under a top stands: the top itself, or, when that's on a road or driveway, the nearest point
    at the road's edge if that's within MAX_SHIFT. Returns (point, None) or (None, why) when there's no place."""
    x, y = point
    for _ in range(3):  # Moving off one road can land on another: try a few times.
        moved = False
        for clearance, line in roads:
            if len(line) < 2:
                continue
            distance, nearest = import_roads.nearest_on_line((x, y), line)
            if distance < clearance - CLEARANCE_TOLERANCE:
                # Just past the edge, so rounding can't leave it a hair inside on the next pass.
                shift = clearance - distance + CLEARANCE_TOLERANCE
                if shift > MAX_SHIFT:
                    return None, 'road'
                dx, dy = x - nearest[0], y - nearest[1]
                if distance < 1e-6:
                    return None, 'road'
                x, y = x + dx / distance * shift, y + dy / distance * shift
                # The limit holds for the trunk's whole move, not each step: overlapping roads add up.
                if math.hypot(x - point[0], y - point[1]) > MAX_SHIFT:
                    return None, 'road'
                moved = True
        if not moved:
            break
    else:
        return None, 'road'
    for outline in buildings:
        if inside((x, y), outline):
            return None, 'building'
    return (x, y), None


def tree_feature(tid, x, y, props):
    return {'type': 'Feature', 'properties': dict({'kind': 'tree', 'id': tid}, **props),
            'geometry': {'type': 'Point', 'coordinates': [round(x, 2), round(y, 2)]}}


def kept_trees(path):
    """The existing file's trees a re-run keeps: edited, hand-added (manual) and removed markers."""
    if not os.path.exists(path):
        return []
    features = json.load(open(path, encoding='utf-8')).get('features', [])
    return [f for f in features
            if (f.get('properties') or {}).get('edited') or (f.get('properties') or {}).get('removed')
            or (f.get('properties') or {}).get('source') == 'manual']


def detect(chm, dtm, image, extent, roads=(), buildings=(), water_level=3.0, broadleaf_yellow=0.08):
    """The trees in the area: chm and dtm (rows, cols) and optional image (4 bands: red, green, blue, near infrared)
    on the CELL grid over `extent`. Returns (trees, report) where each tree is a dict with x, y, height, crown,
    type, species."""
    tops, heights = find_tops(chm)
    labels, counts = crowns(heights, tops)
    report = {'tops': len(tops)}
    if image is not None and len(tops):
        means = crown_means(image, labels, len(tops))
        red, nir = means[0], means[3]
        with np.errstate(invalid='ignore', divide='ignore'):
            ndvi = (nir - red) / (nir + red)
        types = classify(yellowness(means[0], means[1], means[2]), broadleaf_yellow)
    else:
        ndvi = np.full(len(tops), np.nan)
        types = ['conifer'] * len(tops)
    # Near the water: within 40 m of ground below the water level (the beach under the bluff).
    low = np.nan_to_num(dtm, nan=1e9) < water_level
    reach = max(1, round(40 / CELL))
    near = window_max(low.astype(np.float32), reach) > 0 if low.any() else np.zeros(low.shape, dtype=bool)
    trees = []
    skipped = {'road': 0, 'building': 0, 'not green': 0}
    for i, (r, c) in enumerate(tops):
        x = extent[0] + (c + 0.5) * CELL
        y = extent[3] - (r + 0.5) * CELL
        if np.isfinite(ndvi[i]) and ndvi[i] < 0.15:
            skipped['not green'] += 1         # a roof, a vehicle: canopy height without leaves
            continue
        where, why = place((x, y), roads, buildings)
        if why:
            skipped[why] += 1
            continue
        x, y = where
        # Its height from the unsmoothed canopy: smoothing finds tops but lowers a pointed apex.
        height = float(np.nanmax(chm[max(0, r - 1):r + 2, max(0, c - 1):c + 2]))
        crown = math.sqrt(counts[i] * CELL * CELL / math.pi)
        seed = f'{x:.1f},{y:.1f}'
        trees.append({'x': x, 'y': y, 'height': height, 'crown': crown, 'type': types[i],
                      'species': species_for(types[i], height, crown, bool(near[r, c]), seed)})
    report.update(skipped)
    return trees, report


def merge(kept, trees):
    """The trees file's features: the kept ones, then each detected tree a kept one doesn't stand for. A kept tree
    stands for its own detection wherever it was moved (they share the id) and for any found within 2 m of it."""
    near_kept = [tuple(f['geometry']['coordinates'][:2]) for f in kept]
    kept_ids = {(f.get('properties') or {}).get('id') for f in kept}
    features = list(kept)
    for t in trees:
        tid = f'lidar-{t["x"]:.0f}-{t["y"]:.0f}'
        if tid in kept_ids or any(math.hypot(t['x'] - kx, t['y'] - ky) < 2.0 for kx, ky in near_kept):
            continue
        features.append(tree_feature(tid, t['x'], t['y'], {
            'species': t['species'], 'height': round(t['height'], 1), 'crown': round(t['crown'], 1),
            'type': t['type'], 'source': 'lidar', 'edited': False}))
    return features


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('package', help='the terrain package directory (Name.terrain): its CRS')
    parser.add_argument('output', help='the trees file to write (Name.trees.geojson)')
    parser.add_argument('--dsm', required=True, help='highest-hit surface: a folder of GeoTIFF tiles, or a raster/VRT')
    parser.add_argument('--dtm', required=True, help='bare earth from the same survey: folder or raster/VRT')
    parser.add_argument('--center', type=float, nargs=2, required=True, metavar=('X', 'Y'),
                        help="the area's center in the package's CRS")
    parser.add_argument('--size', type=float, default=300, help='the area\'s side (m, default 300)')
    parser.add_argument('--height-scale', type=float, default=FEET,
                        help='metres per source height unit (default US survey feet)')
    parser.add_argument('--naip', default='fetch', help='fetch (Planetary Computer), a 4-band raster, or none')
    parser.add_argument('--roads', help='the roads file: no trees on roads or driveways')
    parser.add_argument('--buildings', help='OpenStreetMap buildings JSON (Overpass, as import_roads caches it)')
    parser.add_argument('--water-level', type=float, default=3.0, help='ground below this (m) is beach or water')
    parser.add_argument('--broadleaf-yellow', type=float, default=0.08,
                        help='crowns yellower than this in the imagery are broadleaf (default 0.08; see classify)')
    parser.add_argument('--work', help='folder for intermediate rasters (default: a temporary folder)')
    args = parser.parse_args()
    for tool in ('gdalbuildvrt', 'gdalwarp', 'gdaltransform'):
        require(tool)
    crs = json.load(open(os.path.join(args.package, 'manifest.json')))['source']['crs']
    half = args.size / 2
    extent = (args.center[0] - half, args.center[1] - half, args.center[0] + half, args.center[1] + half)
    work = args.work or tempfile.mkdtemp(prefix='import-trees-')
    os.makedirs(work, exist_ok=True)
    dsm = cut(source_raster(args.dsm, work, 'dsm'), work, 'dsm', crs, extent)[0] * args.height_scale
    dtm = cut(source_raster(args.dtm, work, 'dtm'), work, 'dtm', crs, extent)[0] * args.height_scale
    chm = np.clip(dsm - dtm, 0, 120)
    log(f'canopy: {np.isfinite(chm).mean() * 100:.0f}% covered, tallest {np.nanmax(chm):.1f} m')
    image, year = None, None
    if args.naip == 'fetch':
        source, year = fetch_naip(work, crs, extent)
        image = cut(source, work, 'naip', crs, extent, bands=4, resampling='average')
    elif args.naip != 'none':
        image = cut(args.naip, work, 'naip', crs, extent, bands=4, resampling='average')
    buildings = []
    if args.buildings:
        elements = json.load(open(args.buildings)).get('elements', [])
        points = [[(p['lon'], p['lat']) for p in e.get('geometry', [])] for e in elements if e.get('type') == 'way']
        flat = '\n'.join(f'{lon} {lat}' for line in points for lon, lat in line)
        projected = iter(float(v) for v in subprocess.run(
            ['gdaltransform', '-s_srs', 'EPSG:4326', '-t_srs', crs, '-output_xy'], input=flat + '\n',
            capture_output=True, text=True, check=True).stdout.split())
        buildings = [[(next(projected), next(projected)) for _ in line] for line in points]
    trees, report = detect(chm, dtm, image, extent, road_lines(args.roads), buildings, args.water_level,
                           args.broadleaf_yellow)
    kept = kept_trees(args.output)
    features = merge(kept, trees)
    header = {'type': 'FeatureCollection', 'name': os.path.basename(args.output).split('.')[0] + ' trees',
              'crs': {'type': 'name', 'properties': {'name': 'urn:ogc:def:crs:' + crs.replace(':', '::')}}}
    import_roads.write_collection(args.output, header, features)
    counts = {}
    for f in features:
        s = f['properties'].get('species', '?')
        counts[s] = counts.get(s, 0) + 1
    log(f"tops {report['tops']}; skipped: {report['road']} on roads, {report['building']} on buildings, "
        f"{report['not green']} not green" + (f'; NAIP {year}' if year else ''))
    print(f'wrote {args.output}: {len(features)} trees ({len(kept)} kept) - '
          + ', '.join(f'{n} {s}' for s, n in sorted(counts.items(), key=lambda kv: -kv[1])))


if __name__ == '__main__':
    main()
