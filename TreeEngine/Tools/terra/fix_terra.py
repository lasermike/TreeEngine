"""Repairs Terra's terrain in terra.glb and builds its weathering tint.

The terrain ("Limestone - baked weathering", mesh Landscape.010) showed a patchwork of hard-edged blocks:
  - Its UV map is cut into islands (38,622 seam vertices, already in the original .blend). Every terrain texture is
    tiled 20x, so each seam jumped to another part of the rock texture. TEXCOORD_0 becomes one continuous planar
    (x, z) projection, least-squares fit to the old UVs so texture scale and orientation stay the same.
  - Its base color was a 1K-texel weathering bake read through those UVs, with the seams' facets and black holes
    baked in. The base color becomes the tiled rock_face_03 photoscan, through the same KHR_texture_transform as the
    normal map, and the weathering survives as a low-frequency tint the engine multiplies in (base color x 2 x tint;
    scene "materialTint"). Each vertex samples the bake at its old UV and is splatted at its new UV; holes are filled
    and seams blurred away with a normalized Gaussian; dividing by the rock color's mean makes 2 x tint x rock
    average to the weathering's own colors. The tint is linear (no sRGB chunk).

Run with Blender's Python (numpy and image decoding):
  blender -b --factory-startup --python fix_terra.py -- <terra.glb in> <rock_face_03_diff_4k.jpg> <terra.glb out> <tint.png out>
"""
import json
import os
import struct
import sys
import tempfile

import bpy
import numpy as np

MATERIAL = 'Limestone - baked weathering'
MESH = 'Landscape.010'
OLD_IMAGE = 'Canyon weathering base color'
TINT_SIZE = 512
SIGMA = 6.0           # tint texels; wider than the bake's seam facets
HOLE = 8 / 255.0      # bake texels at or below this are holes

source, rock_path, target_glb, target_tint = sys.argv[sys.argv.index('--') + 1:][:4]
data = bytearray(open(source, 'rb').read())
assert data[:4] == b'glTF'
json_length = struct.unpack_from('<I', data, 12)[0]
gltf = json.loads(bytes(data[20:20 + json_length]))
bin_start = 20 + json_length + 8
bin_length = struct.unpack_from('<I', data, 20 + json_length)[0]

def accessor(index, components, dtype):
    a = gltf['accessors'][index]
    v = gltf['bufferViews'][a['bufferView']]
    assert v.get('byteStride', components * np.dtype(dtype).itemsize) == components * np.dtype(dtype).itemsize
    start = bin_start + v.get('byteOffset', 0) + a.get('byteOffset', 0)
    return start, np.frombuffer(bytes(data), dtype=dtype, count=a['count'] * components, offset=start).reshape(a['count'], components)

def decode_srgb(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def load_pixels(path):
    image = bpy.data.images.load(path)
    w, h = image.size
    return np.array(image.pixels[:], dtype=np.float64).reshape(h, w, 4)[::-1]    # Rows top first, like glTF v.

def blur(image, sigma):
    pad = int(3 * sigma) + 1
    padded = np.pad(image, [(pad, pad), (pad, pad)] + [(0, 0)] * (image.ndim - 2), mode='edge')
    fy = np.fft.fftfreq(padded.shape[0])[:, None]; fx = np.fft.fftfreq(padded.shape[1])[None, :]
    kernel = np.exp(-2 * (np.pi * sigma) ** 2 * (fx ** 2 + fy ** 2))
    if image.ndim == 3:
        kernel = kernel[:, :, None]
    return np.real(np.fft.ifft2(np.fft.fft2(padded, axes=(0, 1)) * kernel, axes=(0, 1)))[pad:-pad, pad:-pad]

# --- The terrain primitive and its continuous UVs.
mesh = next(m for m in gltf['meshes'] if m.get('name') == MESH)
primitive = mesh['primitives'][0]
material = gltf['materials'][primitive['material']]
assert material['name'] == MATERIAL
_, P = accessor(primitive['attributes']['POSITION'], 3, np.float32)
uv_start, U = accessor(primitive['attributes']['TEXCOORD_0'], 2, np.float32)
A = np.c_[P[:, 0], P[:, 2], np.ones(len(P))].astype(np.float64)
coef, *_ = np.linalg.lstsq(A, U.astype(np.float64), rcond=None)
newU = A @ coef

# --- The tint: the bake sampled at each vertex's old UV, splatted at its new UV.
pbr = material['pbrMetallicRoughness']
base_texture = gltf['textures'][pbr['baseColorTexture']['index']]
image_index = base_texture['source']
image = gltf['images'][image_index]
assert image.get('name') == OLD_IMAGE, f"base color image is {image.get('name')!r}, not the weathering bake"
assert sum(1 for t in gltf['textures'] if t['source'] == image_index) == 1
view = gltf['bufferViews'][image['bufferView']]
bake_bytes = bytes(data[bin_start + view.get('byteOffset', 0):bin_start + view.get('byteOffset', 0) + view['byteLength']])
with tempfile.TemporaryDirectory() as scratch:
    bake_path = os.path.join(scratch, 'bake.png')
    open(bake_path, 'wb').write(bake_bytes)
    bake = load_pixels(bake_path)[:, :, :3]
bh, bw = bake.shape[:2]
# Bilinear sample at the old UVs (texel centers at (i + 0.5) / size).
sx = np.clip(U[:, 0] * bw - 0.5, 0, bw - 1.001); sy = np.clip(U[:, 1] * bh - 0.5, 0, bh - 1.001)
x0 = sx.astype(int); y0 = sy.astype(int); fx = (sx - x0)[:, None]; fy = (sy - y0)[:, None]
samples = (bake[y0, x0] * (1 - fx) + bake[y0, x0 + 1] * fx) * (1 - fy) + (bake[y0 + 1, x0] * (1 - fx) + bake[y0 + 1, x0 + 1] * fx) * fy
valid = samples.max(axis=1) > HOLE
colors = decode_srgb(samples)

lo, hi = newU.min(axis=0), newU.max(axis=0)
margin = (hi - lo) * 0.02
lo, hi = lo - margin, hi + margin
tx = np.clip(((newU[:, 0] - lo[0]) / (hi[0] - lo[0]) * TINT_SIZE).astype(int), 0, TINT_SIZE - 1)
ty = np.clip(((newU[:, 1] - lo[1]) / (hi[1] - lo[1]) * TINT_SIZE).astype(int), 0, TINT_SIZE - 1)
accum = np.zeros((TINT_SIZE, TINT_SIZE, 3)); weight = np.zeros((TINT_SIZE, TINT_SIZE))
np.add.at(accum, (ty[valid], tx[valid]), colors[valid])
np.add.at(weight, (ty[valid], tx[valid]), 1.0)
w = blur(weight, SIGMA)
filled = blur(accum, SIGMA) / np.maximum(w, 1e-9)[:, :, None]
# Beyond the terrain, extend the edge colors so mips don't average in black.
wide_w = blur(weight, 8 * SIGMA)
wide = blur(accum, 8 * SIGMA) / np.maximum(wide_w, 1e-9)[:, :, None]
coverage = np.clip(w / (0.25 * w.max()), 0, 1)[:, :, None]
filled = filled * coverage + wide * (1 - coverage)

rock_mean = decode_srgb(load_pixels(rock_path)[:, :, :3]).reshape(-1, 3).mean(axis=0)
tint = np.clip(0.5 * filled / rock_mean, 0, 1)
out = bpy.data.images.new('tint', TINT_SIZE, TINT_SIZE, alpha=True)
out.colorspace_settings.name = 'Non-Color'
out.pixels[:] = np.concatenate([tint, np.ones((TINT_SIZE, TINT_SIZE, 1))], axis=2)[::-1].astype(np.float32).ravel()
out.filepath_raw = target_tint
out.file_format = 'PNG'
out.save()
# Blender tags even Non-Color PNGs as sRGB; drop the color chunks so the file reads as linear data.
png = open(target_tint, 'rb').read()
kept, i = [png[:8]], 8
while i < len(png):
    length = struct.unpack('>I', png[i:i + 4])[0]
    if png[i + 4:i + 8] not in (b'sRGB', b'gAMA', b'cHRM', b'iCCP'):
        kept.append(png[i:i + 12 + length])
    i += 12 + length
open(target_tint, 'wb').write(b''.join(kept))

# --- The glb: new UVs in place, the rock as base color, the binary chunk repacked.
data[uv_start:uv_start + newU.size * 4] = newU.astype(np.float32).tobytes()
binary = bytes(data[bin_start:bin_start + bin_length])
transform = material['normalTexture'].get('extensions', {}).get('KHR_texture_transform')
assert transform, 'the normal map should carry the tiling transform'
pbr['baseColorTexture'] = {'index': pbr['baseColorTexture']['index'],
                           'extensions': {'KHR_texture_transform': dict(transform)}}
image['name'] = 'rock_face_03_diff_4k'
image['mimeType'] = 'image/jpeg'
rock = open(rock_path, 'rb').read()
replaced_view = image['bufferView']
packed = bytearray()
for index, buffer_view in enumerate(gltf['bufferViews']):
    start = buffer_view.get('byteOffset', 0)
    chunk = rock if index == replaced_view else binary[start:start + buffer_view['byteLength']]
    while len(packed) % 4:
        packed.append(0)
    buffer_view['byteOffset'] = len(packed)
    buffer_view['byteLength'] = len(chunk)
    packed += chunk
while len(packed) % 4:
    packed.append(0)
gltf['buffers'][0]['byteLength'] = len(packed)
json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
json_bytes += b' ' * (-len(json_bytes) % 4)
out_glb = struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(json_bytes) + 8 + len(packed))
out_glb += struct.pack('<I4s', len(json_bytes), b'JSON') + json_bytes + struct.pack('<I4s', len(packed), b'BIN\x00') + bytes(packed)
open(target_glb, 'wb').write(out_glb)

seams = len(U) - len(np.unique(np.round(P, 6), axis=0))
scale = 1 / (hi - lo)
print(f"FIX terrain UVs: planar fit u = {coef[0, 0]:.5f} x + {coef[1, 0]:.5f} z + {coef[2, 0]:.5f}, "
      f"v = {coef[0, 1]:.5f} x + {coef[1, 1]:.5f} z + {coef[2, 1]:.5f} ({seams} split seam vertices now share UVs)")
print(f"FIX tint: {valid.sum()} of {len(valid)} vertices sampled the bake; rock mean {rock_mean.round(4)}; "
      f"tint mean {tint.reshape(-1, 3).mean(axis=0).round(3)}")
print(f"FIX materialTint uvScale [{scale[0]:.6f}, {scale[1]:.6f}] uvOffset [{-lo[0] * scale[0]:.6f}, {-lo[1] * scale[1]:.6f}]")
print(f"FIX {target_glb}: {len(out_glb) // 1024} KB")
