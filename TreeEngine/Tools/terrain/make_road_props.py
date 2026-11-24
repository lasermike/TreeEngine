"""Writes the roadside prop models the engine draws from a roads file's props (Game/Resources/Models/Roadside):

    python -I make_road_props.py ../../Game/Resources/Models/Roadside

Each is a small binary glTF in meters, y up, with its origin on the ground at the foot of its post, and its front
facing glTF -z (the engine's +z, north, before a prop's heading turns it). Colors are material factors, no textures,
so any of them can be replaced by a modelled .glb of the same name.

- mailbox: a rural mailbox (US size T1) on a 4x4 wood post, the door toward the road, the box's floor at 1.07 m.
- post: a 4x4 wood post 1.2 m tall with a white reflector, for marking a driveway or a corner.
- stop-sign: a 30 in (0.75 m) red octagon with a white border and block letters, its bottom at 2.1 m, on a square
  steel post.
- street-name-sign: two blank green blades with white borders, crossed at the top of a 3 m steel post; the lower one
  faces the front. Street names come with sign text (Tools/terrain/README.md).
"""

import json
import math
import os
import struct
import sys

# Materials: base color (linear), metallic, roughness.
MATERIALS = {
    'wood': ((0.20, 0.13, 0.07), 0.0, 0.85),
    'mailbox': ((0.035, 0.035, 0.035), 0.5, 0.45),
    'flag': ((0.55, 0.02, 0.02), 0.0, 0.5),
    'steel': ((0.45, 0.46, 0.47), 0.8, 0.45),
    'sign-back': ((0.55, 0.56, 0.57), 0.7, 0.5),
    'red': ((0.50, 0.01, 0.01), 0.0, 0.35),
    'white': ((0.80, 0.80, 0.78), 0.0, 0.35),
    'green': ((0.0, 0.18, 0.07), 0.0, 0.35),
}


class Model:
    def __init__(self):
        self.parts = {}     # material -> (positions, normals, indices)

    def _part(self, material):
        return self.parts.setdefault(material, ([], [], []))

    def quad(self, material, a, b, c, d):
        """A flat quad a-b-c-d, counterclockwise seen from its front (glTF: right-handed, front faces the viewer)."""
        positions, normals, indices = self._part(material)
        u = [b[i] - a[i] for i in range(3)]
        v = [d[i] - a[i] for i in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        length = math.sqrt(sum(x * x for x in n)) or 1
        n = [x / length for x in n]
        base = len(positions)
        positions += [a, b, c, d]
        normals += [n] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def polygon(self, material, points):
        """A flat convex polygon, counterclockwise seen from its front, as a fan."""
        positions, normals, indices = self._part(material)
        a, b, c = points[0], points[1], points[2]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        length = math.sqrt(sum(x * x for x in n)) or 1
        n = [x / length for x in n]
        base = len(positions)
        positions += list(points)
        normals += [n] * len(points)
        for i in range(1, len(points) - 1):
            indices += [base, base + i, base + i + 1]

    def box(self, material, lo, hi, faces='xyzXYZ'):
        """An axis-aligned box; `faces` picks its sides (lower case: the -axis side)."""
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        if 'x' in faces: self.quad(material, (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0))
        if 'X' in faces: self.quad(material, (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1))
        if 'y' in faces: self.quad(material, (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))
        if 'Y' in faces: self.quad(material, (x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0))
        if 'z' in faces: self.quad(material, (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0))
        if 'Z' in faces: self.quad(material, (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (x0, y0, z1))

    def half_cylinder_z(self, material, cx, cy, radius, z0, z1, segments=10):
        """The top half of a cylinder along z (a mailbox's roof), with its two half-disc ends."""
        ring = [(cx + radius * math.cos(math.pi * i / segments), cy + radius * math.sin(math.pi * i / segments))
                for i in range(segments + 1)]
        positions, normals, indices = self._part(material)
        for (xa, ya), (xb, yb), i in zip(ring, ring[1:], range(segments)):
            mid = math.pi * (i + 0.5) / segments
            n = [math.cos(mid), math.sin(mid), 0]
            base = len(positions)
            positions += [(xa, ya, z1), (xa, ya, z0), (xb, yb, z0), (xb, yb, z1)]
            normals += [n] * 4
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
        # The ring runs counterclockwise seen from +z: so the back end (facing +z) takes it as it is.
        self.polygon(material, [(x, y, z0) for x, y in reversed(ring)])  # front end (faces -z)
        self.polygon(material, [(x, y, z1) for x, y in ring])            # back end (faces +z)

    def glb(self):
        """The model as binary glTF bytes: one mesh, one primitive per material."""
        blob = bytearray()
        views, accessors, primitives, materials = [], [], [], []

        def add_view(data, target):
            while len(blob) % 4:
                blob.append(0)
            views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
            blob.extend(data)
            return len(views) - 1

        for name, (positions, normals, indices) in self.parts.items():
            p = add_view(b''.join(struct.pack('<3f', *v) for v in positions), 34962)
            lo = [min(v[i] for v in positions) for i in range(3)]
            hi = [max(v[i] for v in positions) for i in range(3)]
            accessors.append({'bufferView': p, 'componentType': 5126, 'count': len(positions), 'type': 'VEC3',
                              'min': lo, 'max': hi})
            n = add_view(b''.join(struct.pack('<3f', *v) for v in normals), 34962)
            accessors.append({'bufferView': n, 'componentType': 5126, 'count': len(normals), 'type': 'VEC3'})
            i = add_view(struct.pack(f'<{len(indices)}I', *indices), 34963)
            accessors.append({'bufferView': i, 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'})
            color, metallic, roughness = MATERIALS[name]
            materials.append({'name': name, 'pbrMetallicRoughness': {'baseColorFactor': list(color) + [1.0],
                                                                    'metallicFactor': metallic,
                                                                    'roughnessFactor': roughness}})
            primitives.append({'attributes': {'POSITION': len(accessors) - 3, 'NORMAL': len(accessors) - 2},
                               'indices': len(accessors) - 1, 'material': len(materials) - 1})
        while len(blob) % 4:
            blob.append(0)
        document = {'asset': {'version': '2.0', 'generator': 'TreeEngine make_road_props.py'},
                    'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
                    'meshes': [{'primitives': primitives}], 'materials': materials,
                    'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(blob)}]}
        text = json.dumps(document, separators=(',', ':')).encode()
        text += b' ' * (-len(text) % 4)
        return (struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(text) + 8 + len(blob))
                + struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(blob), b'BIN\x00') + bytes(blob))


def post(model, height, size=0.089, material='wood'):
    h = size / 2
    model.box(material, (-h, 0, -h), (h, height, h), faces='xXYzZ')


def mailbox():
    m = Model()
    post(m, 1.04)
    # The arm under the box, and the box: 0.16 m wide, 0.48 m long, door (front) toward -z, past the post.
    m.box('wood', (-0.09, 1.04, -0.30), (0.09, 1.07, 0.16))
    m.box('mailbox', (-0.08, 1.07, -0.34), (0.08, 1.20, 0.14), faces='xXzZ')
    m.half_cylinder_z('mailbox', 0, 1.20, 0.08, -0.34, 0.14)
    m.box('mailbox', (-0.08, 1.07, -0.34), (0.08, 1.07, 0.14), faces='y')
    # The door's latch, and the flag on the right side (seen from the road), down.
    m.box('mailbox', (-0.015, 1.17, -0.35), (0.015, 1.19, -0.34))
    m.box('flag', (0.08, 1.10, -0.02), (0.088, 1.13, 0.16))
    m.box('flag', (0.08, 1.10, 0.10), (0.088, 1.19, 0.14))
    return m


def marker_post():
    m = Model()
    post(m, 1.2)
    m.box('white', (-0.035, 1.02, -0.0455), (0.035, 1.14, -0.0455), faces='z')
    return m


def octagon(radius_flats, z, cy):
    """An octagon with flats top and bottom, counterclockwise seen from -z (its front)."""
    r = radius_flats / math.cos(math.pi / 8)
    return [(-r * math.cos(math.pi / 8 + i * math.pi / 4), cy + r * math.sin(math.pi / 8 + i * math.pi / 4), z)
            for i in range(8)]


# Block letters on a 3 x 5 grid of strokes: each letter as rectangles (x0, y0, x1, y1) in grid units.
LETTERS = {
    'S': [(0, 4, 3, 5), (0, 2, 1, 5), (0, 2, 3, 3), (2, 0, 3, 3), (0, 0, 3, 1)],
    'T': [(0, 4, 3, 5), (1, 0, 2, 5)],
    'O': [(0, 0, 3, 1), (0, 4, 3, 5), (0, 0, 1, 5), (2, 0, 3, 5)],
    'P': [(0, 0, 1, 5), (0, 4, 3, 5), (2, 2, 3, 5), (0, 2, 3, 3)],
}


def stop_sign():
    m = Model()
    bottom, size = 2.1, 0.75
    cy = bottom + size / 2
    post(m, bottom + size - 0.05, size=0.05, material='steel')
    # The plate, 5 cm in front of the post: a steel back (and edge) with a white face and a red field inset.
    z_front, z_back = -0.03, -0.025
    m.polygon('sign-back', list(reversed(octagon(size / 2, z_back, cy))))
    m.polygon('white', octagon(size / 2, z_front, cy))
    outer = octagon(size / 2, z_front, cy)
    back = octagon(size / 2, z_back, cy)
    for i in range(8):
        a, b = outer[i], outer[(i + 1) % 8]
        c, d = back[(i + 1) % 8], back[i]
        m.quad('sign-back', a, d, c, b)
    m.polygon('red', octagon(size / 2 - 0.02, z_front - 0.001, cy))
    # STOP in block letters, 0.25 m tall.
    unit = 0.05
    width = 4 * 3 * unit + 3 * unit
    x = -width / 2
    for letter in 'STOP':
        for x0, y0, x1, y1 in LETTERS[letter]:
            # Seen from the front (-z), the viewer's right is glTF -x, so letters run toward -x.
            ax, bx = -(x + x0 * unit), -(x + x1 * unit)
            ya, yb = cy - 2.5 * unit + y0 * unit, cy - 2.5 * unit + y1 * unit
            z = z_front - 0.002
            m.quad('white', (ax, ya, z), (bx, ya, z), (bx, yb, z), (ax, yb, z))
        x += 4 * unit
    return m


def blade(m, y, along_x):
    """A street-name blade 0.9 m by 0.2 m, 12 mm thick, centered on the post: along x facing -z and +z, or along z
    facing -x and +x. Green with a 15 mm white border on both faces."""
    length, height, half_t, border = 0.9, 0.2, 0.006, 0.015

    def face(sign, inset, offset, material):
        h, w = height / 2 - inset, length / 2 - inset
        if along_x:
            z = sign * (half_t + offset)
            corners = [(-w, y - h, z), (-w, y + h, z), (w, y + h, z), (w, y - h, z)]
        else:
            x = sign * (half_t + offset)
            corners = [(x, y - h, w), (x, y + h, w), (x, y + h, -w), (x, y - h, -w)]
        if sign > 0:
            corners = [corners[0], corners[3], corners[2], corners[1]]
        m.quad(material, *corners)

    for sign in (-1, 1):
        face(sign, 0, 0, 'white')
        face(sign, border, 0.001, 'green')
    h, w = height / 2, length / 2
    if along_x:
        m.box('white', (-w, y - h, -half_t), (w, y + h, half_t), faces='xXyY')
    else:
        m.box('white', (-half_t, y - h, -w), (half_t, y + h, w), faces='zZyY')


def street_name_sign():
    m = Model()
    post(m, 3.0, size=0.05, material='steel')
    blade(m, 2.72, along_x=True)
    blade(m, 2.93, along_x=False)
    return m


MODELS = {'mailbox': mailbox, 'post': marker_post, 'stop-sign': stop_sign, 'street-name-sign': street_name_sign}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                                             'Game', 'Resources', 'Models', 'Roadside')
    os.makedirs(out, exist_ok=True)
    for name, build in MODELS.items():
        data = build().glb()
        with open(os.path.join(out, name + '.glb'), 'wb') as f:
            f.write(data)
        print(f'{name}.glb: {len(data)} bytes')


if __name__ == '__main__':
    main()
