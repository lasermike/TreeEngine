"""Builds a terrain package's roads file from public data, snapped to the package's ground.

    python import_roads.py Hungerford.terrain Hungerford.roads.geojson --cache E:/Source/Tree/Content/roads

Sources (downloaded once into --cache, reused after):
  - Mason County Street Centerlines and Site Address (ArcGIS Online shapefiles; Washington State Plane South, US feet).
  - OpenStreetMap driveways and service ways in the package's area (Overpass API).

Each county centerline is snapped sideways onto the road bed the package's heights show (the flattest strip of the
road's width within --snap metres, smoothed along the road), keeping intersections joined. OpenStreetMap ways
that duplicate a county road are dropped; the rest become driveways, their road end snapped onto the nearest road.

The output is GeoJSON in the package's CRS (its manifest's source.crs, UTM metres), so QGIS shows it over imagery
(the "crs" member is GeoJSON 2008's, which GDAL and QGIS read). Feature properties are in Tools/terrain/README.md.
A feature whose `edited` property is true in an existing output is kept as it is, and its id isn't imported again.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile

import numpy as np

COUNTY_ITEMS = {
    'centerlines': ('1e20c1df4e7a4ae9bc3271f86d98e531', 'Street_Centerlines.shp'),
    'addresses': ('577c68581a09429098c021c1e7ad2621', 'Site_Address.shp'),
}
OVERPASS = 'https://overpass-api.de/api/interpreter'
AGENT = 'TreeEngine terrain tools'

# Cross-section defaults by surface and owner: width (m), shoulder each side (m), ditch depth and width (m).
PROFILES = {
    ('paved', 'county'): (6.0, 0.6, 0.6, 2.4),
    ('gravel', 'county'): (5.0, 0.3, 0.5, 2.0),
    ('paved', 'private'): (4.0, 0.0, 0.0, 0.0),
    ('gravel', 'private'): (3.6, 0.0, 0.0, 0.0),
}
DRIVEWAY_WIDTH = 3.0


def log(message):
    print(message, file=sys.stderr)


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f'{command[0]} failed:\n{result.stderr}')
    return result.stdout


# ---------------------------------------------------------------------------------------------------------------
# The package: its CRS and bounds (manifest source) and its finest heights.

class Package:
    def __init__(self, directory):
        manifest = json.load(open(os.path.join(directory, 'manifest.json')))
        source = manifest.get('source') or {}
        if not source.get('crs') or not source.get('bounds'):
            sys.exit(f'{directory}: the manifest has no source.crs and source.bounds (import it with import_heightmap.py)')
        self.crs = source['crs']
        self.bounds = source['bounds']                  # xmin, ymin, xmax, ymax of the source raster, in the CRS
        self.world = manifest['worldSize']
        self.spacing = manifest['sampleSpacing']
        # TerrainTool pack puts the raster's southwest pixel center at the package's -x/-z corner and pads north and
        # east to a whole quadtree, so the package's origin (its center) is that corner plus half the world.
        size = source.get('size') or [round((self.bounds[2] - self.bounds[0]) / self.spacing),
                                      round((self.bounds[3] - self.bounds[1]) / self.spacing)]
        self.cx = self.bounds[0] + 0.5 * (self.bounds[2] - self.bounds[0]) / size[0] + self.world / 2
        self.cy = self.bounds[1] + 0.5 * (self.bounds[3] - self.bounds[1]) / size[1] + self.world / 2
        # What the heights cover, in the CRS: the clip for every imported line.
        self.extent = (self.cx - self.world / 2, self.cy - self.world / 2, self.cx + self.world / 2, self.cy + self.world / 2)
        levels = manifest['levels']
        quads, block, stride = manifest['nodeQuads'], manifest['blockSamples'], manifest['blockStride']
        finest = levels - 1
        nodes = 1 << finest
        first = ((1 << (2 * finest)) - 1) // 3
        samples = quads * nodes + 1
        raw = np.memmap(os.path.join(directory, manifest['nodesFile']), dtype=np.uint8, mode='r')
        grid = np.zeros((samples, samples), dtype=np.float32)
        for z in range(nodes):
            for x in range(nodes):
                offset = (first + z * nodes + x) * stride
                values = np.frombuffer(raw[offset:offset + block * block * 2].tobytes(), dtype='<u2').reshape(block, block)
                grid[z * quads:(z + 1) * quads + 1, x * quads:(x + 1) * quads + 1] = values[1:quads + 2, 1:quads + 2]
        self.heights = manifest['heightOffset'] + grid * manifest['heightScale']   # Row 0 = south (-z), column 0 = west.

    def height(self, x, y):
        """Bilinear height at CRS (x, y); nan outside."""
        gx = (x - self.cx + self.world / 2) / self.spacing
        gz = (y - self.cy + self.world / 2) / self.spacing
        n = self.heights.shape[0] - 1
        if not (0 <= gx < n and 0 <= gz < n):
            return math.nan
        i, j = int(gz), int(gx)
        fz, fx = gz - i, gx - j
        h = self.heights
        return float((h[i, j] * (1 - fx) + h[i, j + 1] * fx) * (1 - fz) + (h[i + 1, j] * (1 - fx) + h[i + 1, j + 1] * fx) * fz)

    def contains(self, x, y, margin=0):
        e = self.extent
        return (e[0] + margin <= x <= e[2] - margin) and (e[1] + margin <= y <= e[3] - margin)


# ---------------------------------------------------------------------------------------------------------------
# Sources, cached.

def county_layer(cache, name, package):
    """The county layer clipped to the package and reprojected to its CRS, as GeoJSON features."""
    item, shapefile = COUNTY_ITEMS[name]
    folder = os.path.join(cache, name)
    path = os.path.join(folder, shapefile)
    if not os.path.exists(path):
        os.makedirs(folder, exist_ok=True)
        archive = os.path.join(folder, name + '.zip')
        log(f'downloading Mason County {name}')
        request = urllib.request.Request(f'https://www.arcgis.com/sharing/rest/content/items/{item}/data', headers={'User-Agent': AGENT})
        with urllib.request.urlopen(request, timeout=300) as response, open(archive, 'wb') as out:
            shutil.copyfileobj(response, out)
        zipfile.ZipFile(archive).extractall(folder)
    with tempfile.TemporaryDirectory() as work:
        out = os.path.join(work, name + '.geojson')
        b = package.extent
        run(['ogr2ogr', '-f', 'GeoJSON', '-t_srs', package.crs, '-spat', str(b[0]), str(b[1]), str(b[2]), str(b[3]),
             '-spat_srs', package.crs, '-clipdst', str(b[0]), str(b[1]), str(b[2]), str(b[3]), out, path])
        return json.load(open(out))['features']


def osm_ways(cache, package, selector='["highway"="service"]', name='osm'):
    """OpenStreetMap ways matching `selector` in the package's area, as lists of CRS points with their tags."""
    path = os.path.join(cache, name + '-' + '-'.join(f'{v:.0f}' for v in package.bounds) + '.json')
    if not os.path.exists(path):
        lonlat = subprocess.run(['gdaltransform', '-s_srs', package.crs, '-t_srs', 'EPSG:4326', '-output_xy'],
                                input=f'{package.bounds[0]} {package.bounds[1]}\n{package.bounds[2]} {package.bounds[3]}\n',
                                capture_output=True, text=True, check=True).stdout.split()
        west, south, east, north = float(lonlat[0]), float(lonlat[1]), float(lonlat[2]), float(lonlat[3])
        box = f'{south:.6f},{west:.6f},{north:.6f},{east:.6f}'
        query = f'[out:json][timeout:90];way{selector}({box});out tags geom;'
        log('querying OpenStreetMap')
        request = urllib.request.Request(OVERPASS, data=urllib.parse.urlencode({'data': query}).encode(), headers={'User-Agent': AGENT})
        with urllib.request.urlopen(request, timeout=180) as response:
            open(path, 'wb').write(response.read())
    elements = json.load(open(path)).get('elements', [])
    points = [(e, [(p['lon'], p['lat']) for p in e.get('geometry', [])]) for e in elements if e.get('type') == 'way']
    flat = '\n'.join(f'{lon} {lat}' for _, line in points for lon, lat in line)
    projected = subprocess.run(['gdaltransform', '-s_srs', 'EPSG:4326', '-t_srs', package.crs, '-output_xy'],
                               input=flat + '\n', capture_output=True, text=True, check=True).stdout.split()
    values = iter(float(v) for v in projected)
    ways = []
    for element, line in points:
        ways.append((element, [(next(values), next(values)) for _ in line]))
    return ways


# ---------------------------------------------------------------------------------------------------------------
# Geometry.

def clip_line(line, extent):
    """The parts of a polyline inside a rectangle (xmin, ymin, xmax, ymax), each a list of two or more points."""
    xmin, ymin, xmax, ymax = extent
    parts, current = [], []
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        t0, t1, dx, dy = 0.0, 1.0, bx - ax, by - ay
        inside = True
        for p, q in ((-dx, ax - xmin), (dx, xmax - ax), (-dy, ay - ymin), (dy, ymax - ay)):
            if p == 0:
                if q < 0:
                    inside = False
                    break
            else:
                t = q / p
                if p < 0:
                    t0 = max(t0, t)
                else:
                    t1 = min(t1, t)
        if not inside or t0 > t1:
            if len(current) >= 2:
                parts.append(current)
            current = []
            continue
        start = (ax + dx * t0, ay + dy * t0)
        end = (ax + dx * t1, ay + dy * t1)
        if not current or math.hypot(current[-1][0] - start[0], current[-1][1] - start[1]) > 1e-6:
            if len(current) >= 2:
                parts.append(current)
            current = [start]
        current.append(end)
        if t1 < 1:
            parts.append(current)
            current = []
    if len(current) >= 2:
        parts.append(current)
    return [p for p in parts if sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(p, p[1:])) > 0.5]


def resample(line, step):
    """Points every `step` metres along a polyline, ends included."""
    out = [line[0]]
    carry = 0.0
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        length = math.hypot(bx - ax, by - ay)
        s = step - carry
        while s < length:
            out.append((ax + (bx - ax) * s / length, ay + (by - ay) * s / length))
            s += step
        carry = (carry + length) % step
    if math.hypot(out[-1][0] - line[-1][0], out[-1][1] - line[-1][1]) > 0.01:
        out.append(line[-1])
    return out


def normals(points):
    result = []
    for i in range(len(points)):
        a = points[max(i - 1, 0)]
        b = points[min(i + 1, len(points) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        result.append((-dy / length, dx / length))
    return result


def simplify(points, tolerance):
    """Douglas-Peucker."""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        ax, ay = points[first]
        bx, by = points[last]
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy) or 1e-9
        worst, index = -1, -1
        for i in range(first + 1, last):
            px, py = points[i]
            d = abs((px - ax) * dy - (py - ay) * dx) / length
            if d > worst:
                worst, index = d, i
        if worst > tolerance:
            keep[index] = True
            stack += [(first, index), (index, last)]
    return [p for p, k in zip(points, keep) if k]


def distance_to_line(point, line):
    best = math.inf
    px, py = point
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1e-9)))
        best = min(best, math.hypot(px - ax - t * dx, py - ay - t * dy))
    return best


def nearest_on_line(point, line):
    best, where = math.inf, None
    px, py = point
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1e-9)))
        qx, qy = ax + t * dx, ay + t * dy
        d = math.hypot(px - qx, py - qy)
        if d < best:
            best, where = d, (qx, qy)
    return best, where


def snap_offsets(package, points, width, reach):
    """Sideways offsets that put each point on the flattest strip of `width` within +-reach, or nan where no
    strip is flat enough to be a road bed."""
    half = max(1, int(round(width / 2)))
    offsets = []
    for (px, py), (nx, ny) in zip(points, normals(points)):
        section = np.array([package.height(px + nx * o, py + ny * o) for o in range(-reach - half, reach + half + 1)])
        if np.isnan(section).any():
            offsets.append(math.nan)
            continue
        best, where = math.inf, 0
        for o in range(-reach, reach + 1):
            window = section[o + reach:o + reach + 2 * half + 1]
            spread = float(window.max() - window.min()) + 0.02 * abs(o)    # Prefer the nearer of two flat strips.
            if spread < best:
                best, where = spread, o
        offsets.append(where if best < 0.35 + 0.02 * abs(where) else math.nan)
    return offsets


def smooth(offsets, radius):
    """Fills gaps by interpolation, then a running median and mean along the road."""
    values = np.array(offsets, dtype=float)
    good = ~np.isnan(values)
    if not good.any():
        return np.zeros(len(values))
    index = np.arange(len(values))
    values = np.interp(index, index[good], values[good])
    median = np.array([np.median(values[max(0, i - radius):i + radius + 1]) for i in index])
    return np.array([median[max(0, i - radius):i + radius + 1].mean() for i in index])


# ---------------------------------------------------------------------------------------------------------------
# Building the features.

def bend_to(points, start, end, reach):
    """Moves a sampled line's ends to `start` and `end` (None: stays) and the points near them by shares that fall
    linearly to nothing within `reach` metres, or within the line's length when it's shorter, so each end lands
    exactly on its target and joined ends don't kink."""
    if start is None and end is None:
        return
    along = [0.0]
    for a, b in zip(points, points[1:]):
        along.append(along[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    length = along[-1]
    span = min(reach, length) or 1.0
    d0 = (start[0] - points[0][0], start[1] - points[0][1]) if start else (0.0, 0.0)
    d1 = (end[0] - points[-1][0], end[1] - points[-1][1]) if end else (0.0, 0.0)
    for i, s in enumerate(along):
        w0 = max(0.0, 1 - s / span)
        w1 = max(0.0, 1 - (length - s) / span)
        points[i] = (points[i][0] + d0[0] * w0 + d1[0] * w1, points[i][1] + d0[1] * w0 + d1[1] * w1)
    if start:
        points[0] = start
    if end:
        points[-1] = end


def county_profile(props):
    surface = 'gravel' if (props.get('SURF') or '').upper() == 'GRAVEL' else 'paved'
    owner = 'county' if (props.get('RD_OWNER') or '').upper() == 'CO' else 'private'
    if (props.get('SURF') or '').upper() in ('', 'N/A') and owner == 'private':
        surface = 'gravel'
    width, shoulder, ditch_depth, ditch_width = PROFILES[(surface, owner)]
    speed = props.get('SPEEDLMT') or 0
    if owner == 'county' and speed >= 40:
        width, shoulder = 6.6, 1.2
    return surface, owner, width, shoulder, ditch_depth, ditch_width


def build_roads(package, features, reach, report):
    """County centerlines as road features, snapped, with intersections joined."""
    roads = []
    for index, feature in enumerate(features):
        props = feature['properties']
        geometry = feature['geometry']
        if not geometry:
            continue
        lines = geometry['coordinates'] if geometry['type'] == 'MultiLineString' else [geometry['coordinates']]
        surface, owner, width, shoulder, ditch_depth, ditch_width = county_profile(props)
        for part, line in enumerate(lines):
            line = [(p[0], p[1]) for p in line]
            if len(line) < 2:
                continue
            # Stable across county updates: the road number (or name) and where the line starts, to the metre.
            label = props.get('RD_NO') or '-'.join((props.get('RDNAME') or 'unnamed').lower().split())
            road_id = f'county-{label}-{line[0][0]:.0f}-{line[0][1]:.0f}'
            roads.append({'id': road_id, 'line': line, 'name': (props.get('RDNAME') or '').strip(),
                          'surface': surface, 'owner': owner, 'width': width, 'shoulder': shoulder,
                          'ditchDepth': ditch_depth, 'ditchWidth': ditch_width,
                          'speedLimit': props.get('SPEEDLMT') or 0, 'class': props.get('FUNCT_CLAS') or ''})
    # Snap every road, sampled every 4 m.
    moved = []
    for road in roads:
        points = resample(road['line'], 4.0)
        raw = snap_offsets(package, points, road['width'], reach)
        found = [o for o in raw if not math.isnan(o)]
        offsets = smooth(raw, 4)
        road['snapped'] = [(px + nx * o, py + ny * o) for (px, py), (nx, ny), o in zip(points, normals(points), offsets)]
        road['found'] = len(found) / max(len(raw), 1)
        moved += list(np.abs(offsets))
    # Join ends that met before snapping: each meeting point goes to the mean of its roads' snapped ends. All the
    # targets are found first, then each road is bent to both of its targets at once, so a short link can't reopen
    # one junction while closing the other.
    ends = {}
    for road in roads:
        for which in (0, -1):
            key = (round(road['line'][which][0], 1), round(road['line'][which][1], 1))
            ends.setdefault(key, []).append((road, which))
    targets = {}
    for key, members in ends.items():
        if len(members) < 2:
            continue
        mx = sum(r['snapped'][w][0] for r, w in members) / len(members)
        my = sum(r['snapped'][w][1] for r, w in members) / len(members)
        for road, which in members:
            targets[(id(road), which)] = (mx, my)
    for road in roads:
        bend_to(road['snapped'], targets.get((id(road), 0)), targets.get((id(road), -1)), 20.0)
    # Snapping can nudge a line's end past the package's edge: clip again (a rare second part gets its own id).
    clipped = []
    for road in roads:
        parts = clip_line(simplify(road['snapped'], 0.25), package.extent)
        for n, part in enumerate(parts):
            piece = dict(road, line=part)
            if len(parts) > 1:
                piece['id'] = f"{road['id']}-{n + 1}"
            clipped.append(piece)
    roads[:] = clipped
    if moved:
        moved = np.array(moved)
        report.append(f'{len(roads)} road lines snapped: moved a median {np.median(moved):.1f} m, 90% under '
                      f'{np.percentile(moved, 90):.1f} m; a road bed was found at '
                      f'{100 * np.mean([r["found"] for r in roads]):.0f}% of samples (the rest interpolated)')
    return roads


def nearest_road(point, road_lines):
    """(distance, nearest point) over every road line; infinite distance when there are none."""
    return min((nearest_on_line(point, l) for l in road_lines), key=lambda r: r[0], default=(math.inf, None))


def build_driveways(package, ways, road_lines, report):
    """OpenStreetMap service ways that aren't roads already, clipped to the package, as driveways with their road
    end on a road. `road_lines` are the roads that will be written (edited ones as edited)."""
    driveways, duplicates = [], 0
    for element, way in ways:
        if len(way) < 2:
            continue
        tags = element.get('tags', {})
        service = tags.get('service', '')
        if service in ('parking_aisle', 'slipway', 'drive-through', 'emergency_access'):
            continue
        parts = clip_line(way, package.extent)
        for part_index, line in enumerate(parts):
            samples = resample(line, 5.0)
            near = sum(1 for p in samples if nearest_road(p, road_lines)[0] < 8)
            if near / len(samples) > 0.7:
                duplicates += 1
                continue
            # Orient from the road: the end nearer a road first, and move it onto that road if it's close.
            d0, at0 = nearest_road(line[0], road_lines)
            d1, at1 = nearest_road(line[-1], road_lines)
            if d1 < d0:
                line, d0, at0 = line[::-1], d1, at1
            if d0 < 15:
                line = [at0] + line[1:] if d0 < 1 else [at0] + line
            suffix = f'-{part_index + 1}' if len(parts) > 1 else ''
            driveways.append({'id': f"osm-way-{element['id']}{suffix}", 'line': simplify(line, 0.25), 'name': tags.get('name', ''),
                              'surface': 'paved' if tags.get('surface') in ('asphalt', 'concrete', 'paved') else 'gravel',
                              'width': DRIVEWAY_WIDTH, 'connected': d0 < 15})
    report.append(f'{len(driveways)} driveways from OpenStreetMap ({sum(d["connected"] for d in driveways)} joined to a road); '
                  f'{duplicates} OpenStreetMap ways dropped as duplicates of county roads')
    return driveways


def segments_cross(a, b, c, d):
    """Whether segments ab and cd cross (touching ends don't count)."""
    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    d1, d2, d3, d4 = side(c, d, a), side(c, d, b), side(a, b, c), side(a, b, d)
    return d1 * d2 < 0 and d3 * d4 < 0


def touches_interior(a, b, point, tolerance=0.01):
    """Whether `point` lies on segment ab away from its ends."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    if length2 == 0:
        return False
    t = ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length2
    margin = tolerance / math.sqrt(length2)
    if t <= margin or t >= 1 - margin:
        return False
    return math.hypot(point[0] - a[0] - t * dx, point[1] - a[1] - t * dy) <= tolerance


def crosses_any(a, b, lines, skip=None):
    """Whether segment ab crosses any of the lines (but `skip`): through a segment, or through one of a line's
    vertices on ab's interior (which a strict crossing test misses). ab's own ends may touch them."""
    for line in lines:
        if line is skip:
            continue
        if any(segments_cross(a, b, p, q) for p, q in zip(line, line[1:])):
            return True
        if any(touches_interior(a, b, vertex) for vertex in line):
            return True
    return False


def build_generated_driveways(package, addresses, buildings, named_roads, served, water_level, report, kept_ids=()):
    """Driveways for site addresses no driveway serves yet: from the road the address names (else the nearest road)
    straight to the house (the nearest building within 30 m, else the address point). `named_roads` are
    (name, line) pairs as they will be written, `served` the driveway lines already there. Skips an address
    within 12 m of a road, within 8 m of one already handled, within 30 m of a served driveway's house end, or whose
    straight path would cross another road or driveway or pass through water (heights at or below `water_level`).
    An address whose driveway id is in `kept_ids` (edited by hand, already among `served`) is left alone before it
    can affect any other."""
    lines = [line for _, line in named_roads]
    driveway_lines = [list(line) for line in served]
    house_ends = [line[-1] for line in served]
    centers = []
    for _, outline in buildings:
        if len(outline) >= 3:
            centers.append((sum(p[0] for p in outline) / len(outline), sum(p[1] for p in outline) / len(outline)))
    driveways, handled, keys, ids = [], [], set(), set()
    counts = {'edited': 0, 'served': 0, 'on a road': 0, 'duplicate': 0, 'crossing a road, a driveway or water': 0,
              'no road': 0}
    for address in addresses:
        props = address.get('properties') or {}
        geometry = address.get('geometry') or {}
        if geometry.get('type') != 'Point':
            continue
        point = tuple(geometry['coordinates'][:2])
        if not package.contains(*point):
            continue
        label = props.get('PIN') or f'{point[0]:.0f}-{point[1]:.0f}'
        number = props.get('STREETNUMB') or ''
        base_id = f'address-{label}' + (f'-{number}' if number else '')
        if base_id in kept_ids:
            counts['edited'] += 1
            handled.append(point)
            keys.add((props.get('PIN'), props.get('STREETNUMB')))
            continue
        # Units of one address (same parcel and number) share its driveway, as do addresses within 8 m.
        unit_key = (props.get('PIN'), props.get('STREETNUMB'))
        if any(math.hypot(point[0] - h[0], point[1] - h[1]) < 8 for h in handled) or (unit_key[0] and unit_key in keys):
            counts['duplicate'] += 1
            continue
        handled.append(point)
        keys.add(unit_key)
        if any(math.hypot(point[0] - h[0], point[1] - h[1]) < 30 for h in house_ends):
            counts['served'] += 1
            continue
        near, _ = nearest_road(point, lines)
        if near < 12:
            counts['on a road'] += 1
            continue
        # The house: the nearest building within 30 m of the address, else the address itself.
        house = point
        best = min(centers, key=lambda c: math.hypot(c[0] - point[0], c[1] - point[1]), default=None)
        if best and math.hypot(best[0] - point[0], best[1] - point[1]) < 30:
            house = best
        # The road: the one the address names, if a straight path to it is clear; else the nearest clear one.
        street = (props.get('STREET') or '').strip().upper()
        candidates = sorted(named_roads, key=lambda r: (r[0].strip().upper() != street, nearest_on_line(house, r[1])[0]))
        chosen = None
        for name, line in candidates:
            distance, at = nearest_on_line(house, line)
            if distance > 500:
                continue
            if crosses_any(at, house, lines, skip=line) or crosses_any(at, house, driveway_lines):
                continue
            samples = resample([at, house], 2.0)
            if any(package.height(x, y) <= water_level for x, y in samples[1:-1]):
                continue
            chosen = (at, distance)
            break
        if not chosen:
            counts['crossing a road, a driveway or water' if lines else 'no road'] += 1
            continue
        at, distance = chosen
        # End short of the building's center, toward the road: its front.
        if house is not point and distance > 10:
            house = (house[0] + (at[0] - house[0]) * 5 / distance, house[1] + (at[1] - house[1]) * 5 / distance)
        driveway_id = base_id
        while driveway_id in ids or driveway_id in kept_ids:
            driveway_id += '+'
        ids.add(driveway_id)
        driveway_lines.append([at, house])
        driveways.append({'id': driveway_id, 'line': [at, house],
                          'name': (props.get('ADDRESS') or '').strip(), 'surface': 'gravel', 'width': DRIVEWAY_WIDTH})
        house_ends.append(house)
    report.append(f'{len(driveways)} driveways generated from {len(addresses)} site addresses (skipped: '
                  + ', '.join(f'{v} {k}' for k, v in counts.items() if v) + ')')
    return driveways


def feature(kind, item, source):
    props = {'kind': kind, 'id': item['id'], 'source': source, 'edited': False}
    for key in ('name', 'surface', 'owner', 'width', 'shoulder', 'ditchDepth', 'ditchWidth', 'speedLimit', 'class'):
        if key in item and item[key] not in ('', None):
            props[key] = round(item[key], 2) if isinstance(item[key], float) else item[key]
    return {'type': 'Feature', 'properties': props,
            'geometry': {'type': 'LineString', 'coordinates': [[round(x, 2), round(y, 2)] for x, y in item['line']]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('package', help='the terrain package directory (Name.terrain)')
    parser.add_argument('output', help='the roads file to write (Name.roads.geojson)')
    parser.add_argument('--cache', required=True, help='directory for the downloaded sources (kept between runs)')
    parser.add_argument('--snap', type=int, default=8, help='how far sideways (m) a road may move onto its bed')
    parser.add_argument('--snap-heights', metavar='PACKAGE',
                        help="snap to this package's heights instead: the same area imported without --roads, since "
                             "a graded package's beds already follow the last import's roads")
    parser.add_argument('--water-level', type=float, default=1.9,
                        help='heights at or below this are water: generated driveways never cross them (default 1.9 m)')
    args = parser.parse_args()

    os.makedirs(args.cache, exist_ok=True)
    package = Package(args.package)
    if args.snap_heights:
        surveyed = Package(args.snap_heights)
        if surveyed.crs != package.crs or surveyed.extent != package.extent or surveyed.heights.shape != package.heights.shape:
            sys.exit(f'{args.snap_heights} does not cover the same grid as {args.package}')
        package.heights = surveyed.heights
    report = []
    centerlines = county_layer(args.cache, 'centerlines', package)
    addresses = county_layer(args.cache, 'addresses', package)
    roads = build_roads(package, centerlines, args.snap, report)

    # Features edited by hand replace their imported versions, so driveways snap to and dedupe against the roads
    # as they will be written.
    kept, kept_ids = [], set()
    if os.path.exists(args.output):
        for f in json.load(open(args.output)).get('features', []):
            if f.get('properties', {}).get('edited'):
                kept.append(f)
                kept_ids.add(f['properties'].get('id'))
    named_roads = [(r['name'], r['line']) for r in roads if r['id'] not in kept_ids]
    served = []
    for f in kept:
        geometry = f.get('geometry') or {}
        if geometry.get('type') not in ('LineString', 'MultiLineString'):
            continue
        lines = geometry['coordinates'] if geometry['type'] == 'MultiLineString' else [geometry['coordinates']]
        lines = [[(p[0], p[1]) for p in line] for line in lines if len(line) >= 2]
        if f['properties'].get('kind') == 'road':
            named_roads += [(f['properties'].get('name') or '', line) for line in lines]
        elif f['properties'].get('kind') == 'driveway':
            served += lines
    road_lines = [line for _, line in named_roads]
    driveways = build_driveways(package, osm_ways(args.cache, package), road_lines, report)
    served += [d['line'] for d in driveways if d['id'] not in kept_ids]
    buildings = osm_ways(args.cache, package, '["building"]', 'osm-buildings')
    generated = build_generated_driveways(package, addresses, buildings, named_roads, served, args.water_level, report,
                                          kept_ids)
    features = list(kept)
    features += [feature('road', r, 'county') for r in roads if r['id'] not in kept_ids]
    features += [feature('driveway', d, 'osm') for d in driveways if d['id'] not in kept_ids]
    features += [feature('driveway', d, 'generated') for d in generated if d['id'] not in kept_ids]
    if kept:
        report.append(f'{len(kept)} edited features kept from the existing file')

    crs = package.crs.split(':')
    collection = {
        'type': 'FeatureCollection',
        'name': os.path.splitext(os.path.basename(args.output))[0],
        'crs': {'type': 'name', 'properties': {'name': f'urn:ogc:def:crs:{crs[0]}::{crs[1]}'}},
        'treeRoads': {'version': 1, 'package': os.path.basename(os.path.normpath(args.package)),
                      'sources': ['Mason County Street Centerlines and Site Address (masoncountywa.gov GIS)',
                                  'OpenStreetMap contributors (ODbL)']},
        'features': features,
    }
    with open(args.output, 'w', newline='\n') as out:
        out.write('{\n')
        for key in ('type', 'name', 'crs', 'treeRoads'):
            out.write(f'"{key}": {json.dumps(collection[key])},\n')
        out.write('"features": [\n')
        out.write(',\n'.join(json.dumps(f) for f in features))
        out.write('\n]\n}\n')
    for line in report:
        log(line)
    log(f'wrote {args.output}: {sum(f["properties"]["kind"] == "road" for f in features)} roads, '
        f'{sum(f["properties"]["kind"] == "driveway" for f in features)} driveways '
        f'({sum(f["properties"]["source"] == "generated" for f in features)} generated)')


if __name__ == '__main__':
    main()
