"""Tidies a roads file after editing it in QGIS, ready to re-grade and commit:

    python finish_road_edits.py ../../Game/Resources/Terrain/Hungerford.roads.geojson

It compares the file with its last committed version (--base REF, default HEAD; or --base-file FILE) and:
- marks every changed or new feature `edited: true`, so a later import_roads.py run keeps it;
- fills in a new feature's missing properties: a unique id and source manual; a new line is a driveway (gravel,
  3 m wide; a new road needs its surface and width set by hand), a new point a prop (a post facing north unless
  its prop and heading are set);
- moves the first point of a changed or new line onto the nearest other line when it is within --snap-start meters
  (default 6) but not on it: a road end the snapping in QGIS missed;
- drops empty (null) properties, rounds changed coordinates to centimeters, and writes one feature per line in the
  file's order, unchanged features exactly as before, so the diff shows only the edits;
- keeps a deleted imported or generated feature as a removed marker (`removed: true`, `edited: true`): the engine
  skips it, and imports and prop placement don't bring it back;
- places the generated props again (road_props.py; --no-props skips it), so mailboxes and signs follow the edited
  lines; props you moved or added stay.

It lists what it changed, deleted features, and changed lines that cross another more than 3 m from both lines'
ends (closer is a junction: a line starting on another, a little past it). Then re-grade the terrain with
import_heightmap.py --roads and rebuild.
"""

import argparse
import importlib.util
import json
import math
import os
import subprocess
import sys

_spec = importlib.util.spec_from_file_location('import_roads', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                            'import_roads.py'))
roads = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(roads)
_spec = importlib.util.spec_from_file_location('road_props', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                          'road_props.py'))
road_props = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(road_props)

TOLERANCE = 0.005  # coordinates closer than this (m) are unchanged: QGIS rewrites them with more digits
JUNCTION = 3.0  # a crossing this close (m) to either line's end is a junction, not reported


def parts(f):
    """A line feature's parts (lists of [x, y]); none for other geometry."""
    geometry = f.get('geometry') or {}
    coordinates = geometry.get('coordinates') or []
    if geometry.get('type') == 'LineString':
        return [coordinates]
    if geometry.get('type') == 'MultiLineString':
        return list(coordinates)
    return []


def properties(f):
    """A feature's properties without empty values or the edited flag, for comparing."""
    return {k: v for k, v in (f.get('properties') or {}).items() if v is not None and k != 'edited'}


def close(a, b):
    """Whether two GeoJSON coordinate trees match to TOLERANCE."""
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(a - b) <= TOLERANCE
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(close(x, y) for x, y in zip(a, b))
    return a == b


def rounded(c):
    return round(c, 2) if isinstance(c, float) else [rounded(x) for x in c] if isinstance(c, list) else c


def same(a, b):
    ga, gb = a.get('geometry') or {}, b.get('geometry') or {}
    return (ga.get('type') == gb.get('type') and close(ga.get('coordinates'), gb.get('coordinates'))
            and properties(a) == properties(b))


def finish(base, edited, snap_start=6.0):
    """Returns (features, notes): `edited`'s features tidied against `base`'s, and what was done."""
    before = {(f.get('properties') or {}).get('id'): f for f in base}
    ids = {(f.get('properties') or {}).get('id') for f in edited} - {None, ''}
    notes, result, changed = [], [], []
    for f in edited:
        props = properties(f)
        old = before.get(props.get('id'))
        if old is not None and same(old, f):
            result.append(old)
            continue
        # A copy of the geometry, rounded to centimeters: snapping below moves points in it.
        geometry = {'type': (f.get('geometry') or {}).get('type'),
                    'coordinates': rounded(json.loads(json.dumps((f.get('geometry') or {}).get('coordinates'))))}
        lines = parts({'geometry': geometry})
        if old is None:
            point = geometry['type'] == 'Point'
            props.setdefault('kind', 'prop' if point else 'driveway')
            if not props.get('id'):
                start = (geometry['coordinates'] if point else lines[0][0] if lines and lines[0] else (0, 0))
                props['id'] = f'manual-{start[0]:.0f}-{start[1]:.0f}'
                while props['id'] in ids:
                    props['id'] += '+'
                ids.add(props['id'])
            props.setdefault('source', 'manual')
            if props['kind'] == 'driveway':
                props.setdefault('surface', 'gravel')
                props.setdefault('width', roads.DRIVEWAY_WIDTH)
            elif props['kind'] == 'prop':
                if 'prop' not in props:
                    notes.append(f"{props['id']}: a new prop without a model is a post: set its prop and heading")
                props.setdefault('prop', 'post')
                props.setdefault('heading', 0)
            elif 'surface' not in props or 'width' not in props:
                notes.append(f"{props['id']}: a new {props['kind']} needs its surface and width set")
        props['edited'] = True
        # Key order as import_roads writes it.
        order = ['kind', 'id', 'source', 'edited']
        props = {k: props[k] for k in order + [k for k in props if k not in order] if k in props}
        feature = {'type': 'Feature', 'properties': props, 'geometry': geometry}
        changed.append((feature, old is None))
        result.append(feature)

    # Snap missed road ends onto the lines as they will be written: every part is the written feature's own list,
    # so a start moved here is where the next snap looks.
    every = [(f['properties'].get('id'), part) for f in result if not f['properties'].get('removed')
             for part in parts(f) if len(part) >= 2]
    for feature, _ in changed:
        own = parts(feature)
        if snap_start <= 0 or not own or len(own[0]) < 2:
            continue
        others = [part for _, part in every if part is not own[0]]
        if not others:
            continue
        # In plan (x, y): a file may carry heights or other ordinates, kept as they are when a start moves.
        distance, at = min((roads.nearest_on_line(tuple(own[0][0][:2]), [tuple(p[:2]) for p in part]) for part in others),
                           key=lambda r: r[0])
        if 0.05 < distance <= snap_start:
            own[0][0] = [round(at[0], 2), round(at[1], 2)] + list(own[0][0][2:])
            notes.append(f"{feature['properties']['id']}: start moved {distance:.1f} m onto the nearest line")
    for feature, new in changed:
        props = feature['properties']
        notes.append(f"{props['id']}: {'new' if new else 'changed'}" + (f" ({props.get('name')})" if props.get('name') else ''))
        if not parts(feature) and props.get('kind') != 'prop':
            notes.append(f"{props['id']}: not a line ({feature['geometry'].get('type')}); kept as drawn")

    # Deleted features an import or prop placement would make again stay as removed markers; others just go.
    markers = []
    for i in sorted(set(before) - {(f.get('properties') or {}).get('id') for f in edited} - {None}):
        old = before[i]
        if (old.get('properties') or {}).get('source') in ('county', 'osm', 'generated'):
            marker = json.loads(json.dumps(old))
            marker['properties']['edited'] = True
            marker['properties']['removed'] = True
            markers.append(marker)
            notes.append(f'{i}: removed (kept as a removed marker, so imports leave it out)')
        else:
            notes.append(f'{i}: deleted')
    # Crossings away from both lines' ends (a line starting on another, a little past it, is a junction), including
    # those through a vertex of either line.
    for feature, _ in changed:
        own_id, own = feature['properties']['id'], [part for part in parts(feature) if len(part) >= 2]
        own_ends = [part[i] for part in own for i in (0, -1)]
        reported = set()
        for other_id, other in every:
            if other_id == own_id or other_id in reported:
                continue
            ends = own_ends + [other[0], other[-1]]
            for part in own:
                hit = next((x for x in crossings(part, other)
                            if min(math.hypot(x[0] - e[0], x[1] - e[1]) for e in ends) > JUNCTION), None)
                if hit:
                    notes.append(f'{own_id}: crosses {other_id} at ({hit[0]:.0f}, {hit[1]:.0f})')
                    reported.add(other_id)
                    break
    return result + markers, notes


def crossings(a, b):
    """Where polylines a and b meet: through segments or at a vertex of either."""
    found = []
    for p, q in zip(a, a[1:]):
        for s, t in zip(b, b[1:]):
            d = (q[0] - p[0]) * (t[1] - s[1]) - (q[1] - p[1]) * (t[0] - s[0])
            if abs(d) < 1e-12:
                continue  # parallel
            u = ((s[0] - p[0]) * (t[1] - s[1]) - (s[1] - p[1]) * (t[0] - s[0])) / d
            v = ((s[0] - p[0]) * (q[1] - p[1]) - (s[1] - p[1]) * (q[0] - p[0])) / d
            if -1e-9 <= u <= 1 + 1e-9 and -1e-9 <= v <= 1 + 1e-9:
                found.append((p[0] + u * (q[0] - p[0]), p[1] + u * (q[1] - p[1])))
    return found


def read_base(path, ref, base_file):
    if base_file:
        return json.load(open(base_file))
    folder, name = os.path.split(os.path.abspath(path))
    text = subprocess.run(['git', 'show', f'{ref}:./{name}'], cwd=folder, capture_output=True, text=True, check=True).stdout
    return json.loads(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('roads', help='the roads file saved from QGIS (Name.roads.geojson)')
    parser.add_argument('--base', default='HEAD', help='the git revision to compare with (default HEAD)')
    parser.add_argument('--base-file', help='compare with this file instead of a git revision')
    parser.add_argument('--snap-start', type=float, default=6.0,
                        help="move a changed line's first point onto the nearest line within this many meters (0: off)")
    parser.add_argument('--no-props', dest='props', action='store_false',
                        help="leave the props as they are (by default they're placed again for the edited lines)")
    args = parser.parse_args()

    base = read_base(args.roads, args.base, args.base_file)
    current = json.load(open(args.roads))
    features, notes = finish(base.get('features', []), current.get('features', []), args.snap_start)
    header = {k: base[k] for k in ('type', 'name', 'crs', 'treeRoads') if k in base}
    header.update({k: current[k] for k in ('crs',) if k in current})
    if args.props:
        features, prop_counts = road_props.place_props(features)
        notes.append(f'props: {road_props.describe(prop_counts)}')
    roads.write_collection(args.roads, header, features)
    for note in notes:
        print(note)
    edits = sum(1 for n in notes if n.split(': ', 1)[-1].startswith(('new', 'changed')))
    print(f'{args.roads}: {edits} new or changed features of {len(features)}')


if __name__ == '__main__':
    sys.exit(main())
