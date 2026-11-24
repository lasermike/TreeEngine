"""Tidies a roads file after editing it in QGIS, ready to re-grade and commit:

    python finish_road_edits.py ../../Game/Resources/Terrain/Hungerford.roads.geojson

It compares the file with its last committed version (--base REF, default HEAD; or --base-file FILE) and:
- marks every changed or new feature `edited: true`, so a later import_roads.py run keeps it;
- fills in a new feature's missing properties: kind driveway, a unique id, source manual, and for a driveway
  gravel, 3 m wide (a new road needs its surface and width set by hand);
- moves the first point of a changed or new line onto the nearest other line when it is within --snap-start meters
  (default 6) but not on it: a road end the snapping in QGIS missed;
- drops empty (null) properties, rounds changed coordinates to centimeters, and writes one feature per line in the
  file's order, unchanged features exactly as before, so the diff shows only the edits.

It lists what it changed, deleted features, and changed lines that cross another more than 3 m from both lines'
ends (closer is a junction: a line starting on another, a little past it). Then re-grade the terrain with import_heightmap.py --roads and rebuild.
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

TOLERANCE = 0.005  # coordinates closer than this (m) are unchanged: QGIS rewrites them with more digits
JUNCTION = 3.0  # a crossing this close (m) to either line's end is a junction, not reported


def points(f):
    geometry = f.get('geometry') or {}
    if geometry.get('type') != 'LineString':
        return []
    return [(p[0], p[1]) for p in geometry.get('coordinates') or []]


def properties(f):
    """A feature's properties without empty values or the edited flag, for comparing."""
    return {k: v for k, v in (f.get('properties') or {}).items() if v is not None and k != 'edited'}


def same(a, b):
    pa, pb = points(a), points(b)
    return (len(pa) == len(pb) and all(math.hypot(p[0] - q[0], p[1] - q[1]) <= TOLERANCE for p, q in zip(pa, pb))
            and properties(a) == properties(b))


def finish(base, edited, snap_start=6.0):
    """Returns (features, notes): `edited`'s features tidied against `base`'s, and what was done."""
    before = {(f.get('properties') or {}).get('id'): f for f in base}
    ids = {(f.get('properties') or {}).get('id') for f in edited} - {None, ''}
    notes, out, changed = [], [], []
    for f in edited:
        props = properties(f)
        old = before.get(props.get('id'))
        if old is not None and same(old, f):
            out.append(old)
            continue
        line = points(f)
        if old is None:
            props.setdefault('kind', 'driveway')
            if not props.get('id'):
                start = line[0] if line else (0, 0)
                props['id'] = f'manual-{start[0]:.0f}-{start[1]:.0f}'
                while props['id'] in ids:
                    props['id'] += '+'
                ids.add(props['id'])
            props.setdefault('source', 'manual')
            if props['kind'] == 'driveway':
                props.setdefault('surface', 'gravel')
                props.setdefault('width', roads.DRIVEWAY_WIDTH)
            elif 'surface' not in props or 'width' not in props:
                notes.append(f"{props['id']}: a new {props['kind']} needs its surface and width set")
        props['edited'] = True
        # Key order as import_roads writes it.
        order = ['kind', 'id', 'source', 'edited']
        props = {k: props[k] for k in order + [k for k in props if k not in order] if k in props}
        changed.append((props, [list(p) for p in line], old is None))
        out.append(None)

    # Snap missed road ends, against every line as it will be written.
    lines = [points(f) for f in out if f is not None] + [[tuple(p) for p in c] for _, c, _ in changed]
    for props, coords, _ in changed:
        if len(coords) < 2 or snap_start <= 0:
            continue
        own = [tuple(p) for p in coords]
        others = [l for l in lines if l != own and len(l) >= 2]
        if not others:
            continue
        distance, at = min((roads.nearest_on_line(own[0], l) for l in others), key=lambda r: r[0])
        if 0.05 < distance <= snap_start:
            coords[0] = [at[0], at[1]]
            notes.append(f"{props['id']}: start moved {distance:.1f} m onto the nearest line")
    written = iter(changed)
    result = []
    for f in out:
        if f is not None:
            result.append(f)
            continue
        props, coords, new = next(written)
        notes.append(f"{props['id']}: {'new' if new else 'changed'}" + (f" ({props.get('name')})" if props.get('name') else ''))
        result.append({'type': 'Feature', 'properties': props,
                       'geometry': {'type': 'LineString', 'coordinates': [[round(x, 2), round(y, 2)] for x, y in coords]}})

    for i in sorted(set(before) - {(f.get('properties') or {}).get('id') for f in edited} - {None}):
        notes.append(f'{i}: deleted (a later import_roads.py run would bring it back)')
    # Crossings away from both lines' ends (a line starting on another, a little past it, is a junction).
    final = {(f.get('properties') or {}).get('id'): points(f) for f in result}
    for props, _, _ in changed:
        own = final[props['id']]
        for other_id, other in final.items():
            if other_id == props['id']:
                continue
            for x in crossings(own, other):
                if min(math.hypot(x[0] - e[0], x[1] - e[1]) for e in (own[0], own[-1], other[0], other[-1])) > JUNCTION:
                    notes.append(f"{props['id']}: crosses {other_id} at ({x[0]:.0f}, {x[1]:.0f})")
                    break
    return result, notes


def crossings(a, b):
    """Where polylines a and b cross."""
    found = []
    for p, q in zip(a, a[1:]):
        for s, t in zip(b, b[1:]):
            if not roads.segments_cross(p, q, s, t):
                continue
            d = (q[0] - p[0]) * (t[1] - s[1]) - (q[1] - p[1]) * (t[0] - s[0])
            u = ((s[0] - p[0]) * (t[1] - s[1]) - (s[1] - p[1]) * (t[0] - s[0])) / d
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
    args = parser.parse_args()

    base = read_base(args.roads, args.base, args.base_file)
    current = json.load(open(args.roads))
    features, notes = finish(base.get('features', []), current.get('features', []), args.snap_start)
    header = {k: base[k] for k in ('type', 'name', 'crs', 'treeRoads') if k in base}
    header.update({k: current[k] for k in ('crs',) if k in current})
    roads.write_collection(args.roads, header, features)
    for note in notes:
        print(note)
    edits = sum(1 for n in notes if n.split(': ', 1)[-1].startswith(('new', 'changed')))
    print(f'{args.roads}: {edits} new or changed features of {len(features)}')


if __name__ == '__main__':
    sys.exit(main())
