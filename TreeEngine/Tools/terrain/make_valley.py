#!/usr/bin/env python3
"""Generates the Valley scene: an 8 km procedural terrain with layered materials (grass, dirt by the river, rock on
steep slopes, snow on the peaks) and woods of instanced canyon trees on the lower slopes near the start, for measuring large-world
rendering and terrain streaming.

The terrain package (TerrainTool's valley preset, 8 km at 1 m, about 200 MB) is not committed. This script writes it
to Game/Resources/Terrain/Valley.terrain when it is missing (the build copies it next to the executable), or to
--package-dir. Trees are placed with the package's own heights, so regenerate the scene if the package's settings
change. Everything is seeded, so the scene is the same every time it is made.

    python make_valley.py [--count 4000] [--seed 7] [--regenerate] [--package-only] [--package-dir DIR]
                          [--output ../../Game/Resources/Scenes/valley.scene.json]
"""
import argparse
import json
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.normpath(os.path.join(HERE, '..', '..'))
SCENES = os.path.join(ENGINE, 'Game', 'Resources', 'Scenes')
PACKAGE = os.path.join(ENGINE, 'Game', 'Resources', 'Terrain', 'Valley.terrain')
SPECIES = ['fremont-cottonwood', 'goodding-willow', 'velvet-ash', 'pinyon-pine', 'gambel-oak', 'skunkbush-sumac']
WORLD = 8192.0
TERRAIN = {'preset': 'valley', 'size': 8192, 'spacing': 1, 'seed': 7, 'relief': 600}


def terrain_tool():
    tool = os.path.join(HERE, 'bin', 'Release', 'TerrainTool.exe')
    if os.path.isfile(tool):
        return tool
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    msbuild = subprocess.run([vswhere, '-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild', '-find',
                              r'MSBuild\**\Bin\MSBuild.exe'], capture_output=True, text=True, check=True).stdout.split('\n')[0].strip()
    subprocess.run([msbuild, os.path.join(HERE, 'TerrainTool.vcxproj'), '-p:Configuration=Release', '-p:Platform=x64', '-v:minimal'], check=True)
    return tool


def ensure_package(tool, directory, regenerate):
    if not regenerate and os.path.isfile(os.path.join(directory, 'manifest.json')):
        return
    command = [tool, 'generate', '--out', directory]
    for name, value in TERRAIN.items():
        command += [f'--{name}', str(value)]
    subprocess.run(command, check=True)


def river(x):
    """The valley preset's river: its z at x (TerrainTool.cpp ProceduralHeight)."""
    return 0.12 * WORLD * math.sin(x / (0.16 * WORLD)) + 0.04 * WORLD * math.sin(x / (0.05 * WORLD) + 1.3)


def heights(tool, directory, points):
    with tempfile.TemporaryDirectory() as work:
        query, answer = os.path.join(work, 'points.txt'), os.path.join(work, 'heights.txt')
        with open(query, 'w') as f:
            f.writelines(f'{x:.3f} {z:.3f}\n' for x, z in points)
        subprocess.run([tool, 'heights', directory, '--in', query, '--out', answer], check=True)
        with open(answer) as f:
            return [float(line) for line in f]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--count', type=int, default=6000, help='instanced trees (the sources are extra)')
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--regenerate', action='store_true', help='rewrite the terrain package even if it exists')
    parser.add_argument('--package-only', action='store_true', help='make the package and stop')
    parser.add_argument('--package-dir', default=PACKAGE, help='where the package goes')
    parser.add_argument('--output', default=os.path.join(SCENES, 'valley.scene.json'))
    args = parser.parse_args()

    tool = terrain_tool()
    ensure_package(tool, args.package_dir, args.regenerate)
    if args.package_only:
        return

    with open(os.path.join(SCENES, 'leaf-atlases.scene.json'), encoding='utf-8') as f:
        base = json.load(f)
    sources = {o['id']: o for o in base['objects'] if o.get('id') in SPECIES}
    if len(sources) != len(SPECIES):
        raise SystemExit('leaf-atlases.scene.json lacks some species')

    # Candidate spots: a jittered grid over the valley's lower slopes near the start, in clumps (a coarse random field
    # decides where woods are), each checked against the terrain for height and slope.
    rng = random.Random(args.seed)
    spacing = 6.0
    clumps = {}
    def wooded(x, z):
        cell = (math.floor(x / 160), math.floor(z / 160))
        if cell not in clumps:
            clumps[cell] = random.Random(hash(cell) ^ args.seed).random()
        return clumps[cell] < 0.5
    candidates = []
    for gx in range(int(-1700 / spacing), int(-200 / spacing)):
        x = gx * spacing
        center = river(x)
        for gz in range(int(-700 / spacing), int(700 / spacing)):
            offset = gz * spacing
            if abs(offset) < 60:                      # The river and its banks stay open.
                continue
            px = x + rng.uniform(-0.4, 0.4) * spacing
            pz = center + offset + rng.uniform(-0.4, 0.4) * spacing
            if wooded(px, pz) and rng.random() < 0.8:
                candidates.append((px, pz))
    rng.shuffle(candidates)
    candidates = candidates[: args.count * 4]
    probe = 2.0
    points = [p for x, z in candidates for p in ((x, z), (x + probe, z), (x - probe, z), (x, z + probe), (x, z - probe))]
    sampled = heights(tool, args.package_dir, points)
    trees = []
    for i, (x, z) in enumerate(candidates):
        h, east, west, north, south = sampled[5 * i: 5 * i + 5]
        slope = math.degrees(math.atan(math.hypot((east - west) / (2 * probe), (north - south) / (2 * probe))))
        if slope < 26 and h < 360:
            trees.append((x, h, z))
        if len(trees) == args.count:
            break

    # The camera: above the river near the west end, looking east up the valley.
    start_x = -1500.0
    start_z = river(start_x)
    ground = heights(tool, args.package_dir, [(start_x, start_z)] + [((i - 2.5) * 5 + start_x, start_z + 30) for i in range(len(SPECIES))])
    camera = [start_x - 40, ground[0] + 14, start_z - 40]

    def layer_material(kind, roughness=1):
        return {'baseColor': [1, 1, 1, 1], 'metallic': 0, 'roughness': roughness,
                'baseColorTexture': {'source': f'Terrain/Textures/{kind}_color.png'},
                'metallicRoughnessTexture': {'source': f'Terrain/Textures/{kind}_mr.png'},
                'normalTexture': {'source': f'Terrain/Textures/{kind}_normal.png'}}
    materials = dict(base['materials'])
    materials.update({'valley-grass': layer_material('grass'), 'valley-dirt': layer_material('dirt'),
                      'valley-rock': layer_material('rock'), 'valley-snow': layer_material('snow')})
    objects = [{
        'id': 'valley', 'type': 'terrain', 'source': 'Terrain/Valley.terrain', 'lodRange': 2, 'slots': 768,
        'layers': [
            {'material': 'valley-grass', 'tile': 3},
            {'material': 'valley-dirt', 'tile': 4, 'height': [-1000, -8.6], 'heightBlend': 1, 'noise': {'height': 1.5, 'scale': 25}},
            {'material': 'valley-rock', 'tile': 8, 'biplanar': 4, 'slope': [30, 90], 'slopeBlend': 5, 'noise': {'slope': 8, 'scale': 30}},
            {'material': 'valley-snow', 'tile': 5, 'height': [420, 10000], 'heightBlend': 25, 'noise': {'height': 40, 'scale': 60}},
        ],
    }]
    lod = {'distances': [10, 30, 42, 60, 85, 120, 170, 240], 'hysteresis': 0.1, 'reduction': 'prune'}
    for index, species in enumerate(SPECIES):
        source = json.loads(json.dumps(sources[species]))
        source['transform'] = dict(source.get('transform', {}))
        source['transform']['position'] = [round((index - 2.5) * 5 + start_x, 3), round(ground[1 + index] + 0.5, 3), round(start_z + 30, 3)]
        source['lod'] = dict(lod)
        objects.append(source)
    tree_rng = random.Random(args.seed + 1)
    for i, (x, h, z) in enumerate(trees):
        scale = round(tree_rng.uniform(0.8, 1.25), 3)
        objects.append({
            'id': f'tree-{i}', 'instanceOf': tree_rng.choice(SPECIES),
            'transform': {'position': [round(x, 3), round(h + 0.5, 3), round(z, 3)],
                          'rotationDegrees': [0, round(tree_rng.uniform(0, 360), 1), 0], 'scale': [scale, scale, scale]},
        })

    scene = {
        'version': base['version'], 'id': 'Valley', 'name': f'Valley - 8 km streamed terrain, {len(trees)} instanced trees',
        'camera': {'position': [round(v, 3) for v in camera], 'rotationDegrees': [5, 80, 0]},
        'render': dict(base['render'], farClippingPlane=6000),
        'light': base['light'], 'materials': materials, 'atlases': base['atlases'], 'objects': objects,
    }
    with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(scene, f, indent=1)
        f.write('\n')
    print(f'{args.output}: terrain {os.path.relpath(args.package_dir, ENGINE)}, {len(trees)} trees of {len(candidates)} candidates')


if __name__ == '__main__':
    main()
