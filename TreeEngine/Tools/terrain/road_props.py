"""Places roadside props in a roads file from its roads and driveways (import_roads.py and finish_road_edits.py run it;
it also runs alone):

    python road_props.py ../../Game/Resources/Terrain/Hungerford.roads.geojson

Props are Point features, `kind` "prop", with `prop` the model (mailbox, post, stop-sign, street-name-sign),
`heading` the direction its front faces in degrees clockwise from north, and `text` (an address, or the street names
on a sign's blades, the lower blade's first, separated by |). Every run replaces the generated props that weren't
edited; edited and hand-placed ones stay, and suppress the generated prop with the same id.

- Street-name signs: one at each intersection of differently named roads (private roads included, as long as they
  have a name), on the corner to the right of the lowest-ranked road's approach. Its lower blade runs along the
  highest-ranked road and names it (the sign faces square to that road); the upper blade names the other.
- Stop signs: on every approach of a county road that ends at, or crosses, a higher-ranked road (rank: county over
  private, then functional class, speed limit, paved over gravel), or that ends (a T) at a road of its rank or
  higher carrying on through; on the right, facing the traffic that stops. Private roads and driveways get none.
- Mailboxes: one per driveway, at the mouth on the road where it (or the lane it branches from) starts, on the
  driveway's side just past it in the direction of travel, the door to the road; driveways sharing a mouth line up.
"""

import json
import math
import os
import sys

JOIN = 1.5          # meters: a line end this close to another line meets it
CLUSTER = 4.0       # meters: junction points this close are one intersection (where pieces of roads meet, their
                    # points coincide; T-junctions staggered a few meters apart stay two)
STUB = 7.5          # meters: a road reaching less than this past an intersection has no approach from that side
MEETS = 4.0         # meters: a road this close to an intersection's center is one of its roads
APPROACH = 15.0     # meters back along a road to find which way it approaches
SIGN_SETBACK = 1.5  # meters beyond a road's shoulder for a sign's post
STOP_BEHIND = 2.5   # meters further back along the approach for a stop sign than for a street-name sign
CLEARANCE = 0.5     # meters a sign post stands off the edge of any road's shoulder
MAILBOX_ALONG = 2.5     # meters along the road from the driveway's center to the first mailbox
MAILBOX_SPACING = 0.45  # meters between mailboxes at one mouth
MAILBOX_SETBACK = 0.35  # meters beyond the shoulder's edge to the mailbox post


def parts(f):
    geometry = f.get('geometry') or {}
    coordinates = geometry.get('coordinates') or []
    if geometry.get('type') == 'LineString':
        coordinates = [coordinates]
    elif geometry.get('type') != 'MultiLineString':
        return []
    return [[(p[0], p[1]) for p in part] for part in coordinates if len(part) >= 2]


def nearest(point, line):
    """(distance, point on the line, unit direction of the line there)."""
    best = (math.inf, None, (1.0, 0.0))
    px, py = point
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (length * length)))
        qx, qy = ax + t * dx, ay + t * dy
        d = math.hypot(px - qx, py - qy)
        if d < best[0]:
            best = (d, (qx, qy), (dx / length, dy / length))
    return best


def point_along(line, distance):
    """The point `distance` meters along a line from its start (its end if shorter)."""
    for a, b in zip(line, line[1:]):
        step = math.hypot(b[0] - a[0], b[1] - a[1])
        if distance <= step and step > 0:
            t = distance / step
            return (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
        distance -= step
    return line[-1]


def heading(dx, dy):
    """Degrees clockwise from north (+y) of the direction (dx east, dy north)."""
    return round(math.degrees(math.atan2(dx, dy)) % 360, 1)


def unit(dx, dy):
    length = math.hypot(dx, dy) or 1.0
    return dx / length, dy / length


def rank(props):
    """Higher is the more important road."""
    road_class = str(props.get('class') or '').strip()
    class_rank = 100 - int(road_class) if road_class.isdigit() else 0
    return (props.get('owner') == 'county', class_rank, props.get('speedLimit') or 0, props.get('surface') == 'paved')


def reach(props):
    """Meters from a road's centerline to its shoulder's edge."""
    return (props.get('width') or 6.0) / 2 + (props.get('shoulder') or 0.0)


class Road:
    def __init__(self, props, line):
        self.props, self.line = props, line
        self.name = (props.get('name') or '').strip()
        self.rank = rank(props)


def intersections(roads):
    """Groups of (center, [(road, approach direction toward the center, distance of the center along the road)]):
    each road meeting at the center comes in from one side (it ends there) or two (it passes through)."""
    points = []
    for road in roads:
        for end in (road.line[0], road.line[-1]):
            for other in roads:
                if other is road:
                    continue
                d, at, _ = nearest(end, other.line)
                if d <= JOIN:
                    points.append(at)
    # Roads crossing where neither ends (an unsplit four-way).
    boxes = [bounds(road.line) for road in roads]
    for a in range(len(roads)):
        for b in range(a + 1, len(roads)):
            if overlaps(boxes[a], boxes[b]):
                points += crossings(roads[a].line, roads[b].line)
    clusters = []
    for p in points:
        for c in clusters:
            if math.hypot(p[0] - c[0][0], p[1] - c[0][1]) <= CLUSTER:
                c[1].append(p)
                break
        else:
            clusters.append([p, [p]])
    result = []
    for _, members in clusters:
        center = (sum(p[0] for p in members) / len(members), sum(p[1] for p in members) / len(members))
        approaches = []
        for road in roads:
            d, at, _ = nearest(center, road.line)
            if d > MEETS:
                continue
            length = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(road.line, road.line[1:]))
            along = along_line(road.line, at)
            # From each side with road left beyond the intersection, the direction traffic comes in.
            for sign in (-1, 1):
                remaining = along if sign < 0 else length - along
                if remaining < STUB:
                    continue
                back = point_along(road.line, along + sign * min(APPROACH, remaining))
                approaches.append((road, unit(at[0] - back[0], at[1] - back[1])))
        roads_here = {id(r) for r, _ in approaches}
        if len(roads_here) >= 2:
            result.append((center, approaches))
    return result


def bounds(line, margin=JOIN):
    xs, ys = [p[0] for p in line], [p[1] for p in line]
    return (min(xs) - margin, min(ys) - margin, max(xs) + margin, max(ys) + margin)


def overlaps(a, b):
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]


def crossings(a, b):
    """Where polylines a and b cross or touch, vertices included."""
    found = []
    for p, q in zip(a, a[1:]):
        for s, t in zip(b, b[1:]):
            d = (q[0] - p[0]) * (t[1] - s[1]) - (q[1] - p[1]) * (t[0] - s[0])
            if abs(d) < 1e-12:
                continue
            u = ((s[0] - p[0]) * (t[1] - s[1]) - (s[1] - p[1]) * (t[0] - s[0])) / d
            v = ((s[0] - p[0]) * (q[1] - p[1]) - (s[1] - p[1]) * (q[0] - p[0])) / d
            if -1e-9 <= u <= 1 + 1e-9 and -1e-9 <= v <= 1 + 1e-9:
                found.append((p[0] + u * (q[0] - p[0]), p[1] + u * (q[1] - p[1])))
    return found


def along_line(line, point):
    best, total, where = math.inf, 0.0, 0.0
    for a, b in zip(line, line[1:]):
        step = math.hypot(b[0] - a[0], b[1] - a[1])
        if step > 0:
            t = max(0.0, min(1.0, ((point[0] - a[0]) * (b[0] - a[0]) + (point[1] - a[1]) * (b[1] - a[1])) / (step * step)))
            d = math.hypot(point[0] - a[0] - t * (b[0] - a[0]), point[1] - a[1] - t * (b[1] - a[1]))
            if d < best:
                best, where = d, total + t * step
        total += step
    return where


def clear(point, roads):
    """Whether a post at the point stands off every road's traveled way and shoulder."""
    return all(nearest(point, other.line)[0] >= reach(other.props) + CLEARANCE for other in roads)


def corner(center, approach, road, major, roads, back_extra=0.0):
    """A post on the right of an approach (direction of travel toward the center), back from the major road's edge:
    where roads meet at a sharp angle, the nearest spot further back (a meter at a time, up to APPROACH) or further
    out from the road (up to 3 m) that's clear of every road; None when there's none."""
    dx, dy = approach
    right = (dy, -dx)
    side = reach(road.props) + SIGN_SETBACK
    back = reach(major.props) + SIGN_SETBACK + back_extra
    for extra in sorted(((b, o) for b in range(int(APPROACH)) for o in range(4)), key=lambda e: e[0] + e[1]):
        point = (center[0] - dx * (back + extra[0]) + right[0] * (side + extra[1]),
                 center[1] - dy * (back + extra[0]) + right[1] * (side + extra[1]))
        if clear(point, roads):
            return point
    return None


def prop(kind, pid, point, facing, text=''):
    props = {'kind': 'prop', 'id': pid, 'source': 'generated', 'edited': False, 'prop': kind, 'heading': facing}
    if text:
        props['text'] = text
    return {'type': 'Feature', 'properties': props,
            'geometry': {'type': 'Point', 'coordinates': [round(point[0], 2), round(point[1], 2)]}}


def signs(roads, counts):
    out = []
    for center, approaches in intersections(roads):
        # The county splits roads where they meet, so a road carrying on through arrives as two pieces with one
        # name: group approaches by name (unnamed roads each on their own).
        groups = {}
        for road, approach in approaches:
            groups.setdefault(road.name or f'#{id(road)}', []).append((road, approach))
        rank_of = {name: max(r.rank for r, _ in arms) for name, arms in groups.items()}
        major = max(groups, key=lambda name: (rank_of[name], len(groups[name])))
        major_road = max((r for r, _ in groups[major]), key=lambda r: r.rank)
        named = [name for name in groups if not name.startswith('#')]
        key = f'{center[0]:.0f}-{center[1]:.0f}'
        others = [name for name in named if name != major]
        if not major.startswith('#') and others:
            # On the right corner of the lowest-ranked road's approach (one that ends here, if any).
            minor = min(others, key=lambda name: (len(groups[name]) > 1, rank_of[name]))
            road, approach = groups[minor][0]
            # The lower blade (the model's x axis) runs along the major road: the sign faces square to it, toward the
            # minor road's oncoming traffic.
            _, _, (mx, my) = nearest(center, major_road.line)
            facing = (my, -mx) if my * -approach[0] - mx * -approach[1] >= 0 else (-my, mx)
            point = corner(center, approach, road, major_road, roads)
            if point is None:
                counts['sign without room'] += 1
            else:
                out.append(prop('street-name-sign', f'prop-street-{key}', point, heading(*facing), f'{major}|{minor}'))
                counts['street-name-sign'] += 1
        for name, arms in groups.items():
            if name == major:
                continue
            # It yields to a higher-ranked road, or ends (a T) at one of its rank or higher that carries on.
            ends_at_through = len(arms) == 1 and len(groups[major]) >= 2 and rank_of[major] >= rank_of[name]
            if rank_of[name] >= rank_of[major] and not ends_at_through:
                continue
            for road, approach in arms:
                if road.props.get('owner') != 'county':
                    continue
                point = corner(center, approach, road, major_road, roads, STOP_BEHIND)
                if point is None:
                    counts['sign without room'] += 1
                    continue
                out.append(prop('stop-sign', f'prop-stop-{key}-{heading(*approach):.0f}', point,
                                heading(-approach[0], -approach[1])))
                counts['stop-sign'] += 1
    return out


def mailboxes(roads, driveways, counts):
    """One per driveway, at its mouth on a road (following lanes it branches from)."""
    mouths = {}     # (road index, rounded point) -> [(driveway props, mouth, road direction, side)]

    # Each driveway's road end, if it has one, and which driveways meet it (an end of either on the other).
    road_end = []
    for props, line in driveways:
        found = None
        for end in (line[0], line[-1]):
            best = min(((nearest(end, r.line), i) for i, r in enumerate(roads)), key=lambda x: x[0][0], default=None)
            if best and best[0][0] <= JOIN:
                (_, at, direction), i = best
                found = (i, at, direction, line[1] if end is line[0] else line[-2])
                break
        road_end.append(found)
    boxes = [bounds(line) for _, line in driveways]
    meets = [[] for _ in driveways]
    for a, (_, la) in enumerate(driveways):
        for b in range(a + 1, len(driveways)):
            lb = driveways[b][1]
            if overlaps(boxes[a], boxes[b]) and (any(nearest(e, lb)[0] <= JOIN for e in (la[0], la[-1]))
                                                 or any(nearest(e, la)[0] <= JOIN for e in (lb[0], lb[-1]))):
                meets[a].append(b)
                meets[b].append(a)

    def mouth_of(start):
        """The road end nearest in hops along the driveways joined to this one, visiting each once."""
        queue, visited = [start], {start}
        while queue:
            k = queue.pop(0)
            if road_end[k]:
                return road_end[k]
            for m in meets[k]:
                if m not in visited:
                    visited.add(m)
                    queue.append(m)
        return None

    for j, (props, line) in enumerate(driveways):
        found = mouth_of(j)
        if not found:
            counts['mailbox without a road'] += 1
            continue
        i, at, direction, inward = found
        cross = direction[0] * (inward[1] - at[1]) - direction[1] * (inward[0] - at[0])
        side = 1 if cross > 0 else -1      # +1: the driveway leaves to the road's left
        mouths.setdefault((i, round(at[0]), round(at[1]), side), []).append((props, at, direction, side))
    out = []
    for (i, *_), served in mouths.items():
        road = roads[i]
        for k, (props, at, direction, side) in enumerate(served):
            dx, dy = direction
            left = (-dy, dx)
            # Right-hand traffic: on the road's left side it travels against the line's direction.
            travel = (dx, dy) if side < 0 else (-dx, -dy)
            out_by = reach(road.props) + MAILBOX_SETBACK
            along = MAILBOX_ALONG + k * MAILBOX_SPACING
            point = (at[0] + left[0] * side * out_by + travel[0] * along, at[1] + left[1] * side * out_by + travel[1] * along)
            facing = heading(-left[0] * side, -left[1] * side)
            out.append(prop('mailbox', f"prop-mailbox-{props.get('id')}", point, facing, (props.get('name') or '').strip()))
            counts['mailbox'] += 1
    return out


def place_props(features):
    """Returns (features, counts): the features with generated props that weren't edited replaced."""
    kept = [f for f in features if (f.get('properties') or {}).get('kind') != 'prop'
            or (f.get('properties') or {}).get('edited') or (f.get('properties') or {}).get('source') != 'generated']
    kept_ids = {(f.get('properties') or {}).get('id') for f in kept}
    roads, driveways = [], []
    for f in features:
        props = f.get('properties') or {}
        for line in parts(f):
            if props.get('kind') == 'road':
                roads.append(Road(props, line))
            elif props.get('kind') == 'driveway':
                driveways.append((props, line))
    counts = {'street-name-sign': 0, 'stop-sign': 0, 'sign without room': 0, 'mailbox': 0, 'mailbox without a road': 0}
    placed = signs(roads, counts) + mailboxes(roads, driveways, counts)
    seen = set()
    new = []
    for p in placed:
        pid = p['properties']['id']
        while pid in seen:
            pid += '+'
        seen.add(pid)
        p['properties']['id'] = pid
        if pid not in kept_ids:
            new.append(p)
    counts['kept'] = sum(1 for f in kept if (f.get('properties') or {}).get('kind') == 'prop')
    return kept + new, counts


def describe(counts):
    return ', '.join(f'{v} {k}' for k, v in counts.items() if v)


def main():
    import importlib.util
    spec = importlib.util.spec_from_file_location('import_roads', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                               'import_roads.py'))
    roads_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(roads_module)
    path = sys.argv[1]
    collection = json.load(open(path))
    features, counts = place_props(collection.get('features', []))
    roads_module.write_collection(path, collection, features)
    print(f'{path}: props {describe(counts)}')


if __name__ == '__main__':
    main()
