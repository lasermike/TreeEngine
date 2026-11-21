"""Applies fix_terra.py's terrain repair to the Blender source, saved as a new file.

  - The terrain's UV map becomes one continuous planar (x, y) projection, least-squares fit to the old UVs (Blender is
    z-up, so this matches the glb's (x, z) fit).
  - "Limestone - baked weathering" takes its base color from rock_face_03_diff_4k.jpg through the Mapping node its
    normal and roughness maps already share. The weathering tint itself lives in the scene ("materialTint"), since glTF
    has no slot for it.
The opened file is never overwritten.

  blender -b <terra.blend> --python fix_terra_blend.py -- <rock_face_03_diff_4k.jpg> <output.blend>
"""
import sys
import bpy
import numpy as np

rock_path, output_path = sys.argv[sys.argv.index('--') + 1:][:2]
terrain = bpy.data.objects['Weathered limestone canyon']
me = terrain.data
uv = me.uv_layers.active.data
loops_uv = np.zeros(len(uv) * 2); uv.foreach_get('uv', loops_uv); loops_uv = loops_uv.reshape(-1, 2)
loop_vert = np.zeros(len(me.loops), dtype=np.int64); me.loops.foreach_get('vertex_index', loop_vert)
co = np.zeros(len(me.vertices) * 3); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
A = np.c_[co[loop_vert, 0], co[loop_vert, 1], np.ones(len(loop_vert))]
coef, *_ = np.linalg.lstsq(A, loops_uv, rcond=None)
uv.foreach_set('uv', (A @ coef).astype(np.float32).ravel())
me.update()

material = bpy.data.materials['Limestone - baked weathering']
nodes, links = material.node_tree.nodes, material.node_tree.links
base = nodes['Image Texture.004']
base.image = bpy.data.images.load(rock_path, check_existing=True)
for link in list(links):
    if link.to_node == base and link.to_socket.name == 'Vector':
        links.remove(link)
links.new(nodes['Reroute'].outputs['Output'], base.inputs['Vector'])

bpy.ops.wm.save_as_mainfile(filepath=output_path, copy=True)
print(f"BLEND planar fit {coef.T.round(5).tolist()}; base color {base.image.name}; saved {output_path}")
