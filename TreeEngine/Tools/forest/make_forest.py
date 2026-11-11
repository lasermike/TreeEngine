"""Generates the Forest scene: thousands of instanced canyon trees, for measuring how rendering scales with tree count.

The source trees, materials, leaf atlases, light and environment come from leaf-atlases.scene.json. The instances
sit on a jittered grid with a seeded random species, yaw and scale, so the scene is the same every time it is made.

    python make_forest.py [--count 2000] [--standalone 30] [--spacing 6] [--seed 7] [--lod 10,30,42,60,85,120,170,240 | --screen-heights 600,300,...]
                          [--reduction prune|merge]
                          [--id Forest] [--output ../../Game/Resources/Scenes/forest.scene.json]

--lod sets the source trees' (and so the instances') LOD distances: an instance draws full-detail sticks within the
first, the far layout to the second, and one reduced layout per distance after that (see the Scenes README).
--screen-heights uses projected tree sizes in display pixels (descending) in place of distances.
--standalone adds non-instanced copies of the sources among the instances, which draw their own reduced layouts.
"""
import argparse
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
SCENES = os.path.join(HERE, '..', '..', 'Game', 'Resources', 'Scenes')
SPECIES = ['fremont-cottonwood', 'goodding-willow', 'velvet-ash', 'pinyon-pine', 'gambel-oak', 'skunkbush-sumac']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--count', type=int, default=2000, help='instanced trees (the sources are extra)')
    parser.add_argument('--standalone', type=int, default=30,
                        help='extra non-instanced trees among the instances (each its own copy of a source)')
    parser.add_argument('--spacing', type=float, default=6.0, help='grid spacing in meters')
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--lod', default='10,30,42,60,85,120,170,240', help='comma-separated LOD distances for the source trees')
    parser.add_argument('--screen-heights', help='comma-separated LOD screen heights in pixels, in place of --lod')
    parser.add_argument('--reduction', choices=['prune', 'merge'], default='prune')
    parser.add_argument('--id', default='Forest', help='scene ID (for side-by-side variants)')
    parser.add_argument('--output', default=os.path.join(SCENES, 'forest.scene.json'))
    args = parser.parse_args()

    with open(os.path.join(SCENES, 'leaf-atlases.scene.json'), encoding='utf-8') as f:
        base = json.load(f)
    sources = {o['id']: o for o in base['objects'] if o.get('id') in SPECIES}
    missing = [s for s in SPECIES if s not in sources]
    if missing:
        raise SystemExit(f'leaf-atlases.scene.json lacks {missing}')

    random.seed(args.seed)
    side = math.ceil(math.sqrt(args.count))
    extent = side * args.spacing
    objects = [{
        'id': 'ground', 'type': 'primitive', 'shape': 'cylinderHD',
        'transform': {'scale': [extent * 0.75 + 20, 0.01, extent * 0.75 + 20]}, 'material': 'canyon-soil',
    }]
    if args.screen_heights:
        lod = {'screenHeights': [float(h) for h in args.screen_heights.split(',')]}
    else:
        lod = {'distances': [float(d) for d in args.lod.split(',')]}
    lod.update(hysteresis=0.1, reduction=args.reduction)
    # Each source stands in a row in front of the camera, so its own (non-instanced) cost is in the scene too.
    for index, species in enumerate(SPECIES):
        source = json.loads(json.dumps(sources[species]))
        source['transform'] = dict(source.get('transform', {}))
        source['transform']['position'] = [(index - (len(SPECIES) - 1) / 2) * 5.0, 0.5, -extent / 2 - 8]
        source['lod'] = dict(lod)
        objects.append(source)
    for i in range(args.count):
        row, column = divmod(i, side)
        x = (column + 0.5) * args.spacing - extent / 2 + random.uniform(-0.35, 0.35) * args.spacing
        z = (row + 0.5) * args.spacing - extent / 2 + random.uniform(-0.35, 0.35) * args.spacing
        scale = round(random.uniform(0.8, 1.2), 3)
        objects.append({
            'id': f'tree-{i}', 'instanceOf': random.choice(SPECIES),
            'transform': {'position': [round(x, 3), 0.5, round(z, 3)], 'rotationDegrees': [0, round(random.uniform(0, 360), 1), 0],
                          'scale': [scale, scale, scale]},
        })
    # Non-instanced trees between the grid cells (a separate random stream, so the instances stay the same). Each
    # computes its own pose and, far enough away, draws its own reduced layout.
    standalone = random.Random(args.seed + 1)
    for i in range(args.standalone):
        tree = json.loads(json.dumps(sources[standalone.choice(SPECIES)]))
        tree.pop('id', None)
        cell = standalone.randrange(args.count)
        row, column = divmod(cell, side)
        x = (column + 1.0) * args.spacing - extent / 2
        z = (row + 1.0) * args.spacing - extent / 2
        tree['id'] = f'standalone-{i}'
        tree['transform'] = dict(tree.get('transform', {}), position=[round(x, 3), 0.5, round(z, 3)],
                                 rotationDegrees=[0, round(standalone.uniform(0, 360), 1), 0])
        tree['lod'] = dict(lod)
        objects.append(tree)

    scene = {
        'version': base['version'], 'id': args.id, 'name': f'{args.id} - {args.count} instanced canyon trees ({args.reduction})',
        # From the sources' row, looking into the forest.
        'camera': {'position': [0, 6, -extent / 2 - 22], 'rotationDegrees': [8, 0, 0]},
        'render': dict(base['render'], farClippingPlane=max(300, extent * 1.6)),
        'light': base['light'], 'materials': base['materials'], 'atlases': base['atlases'], 'objects': objects,
    }
    with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(scene, f, indent=1)
        f.write('\n')
    print(f'{args.output}: {len(SPECIES)} sources, {args.count} instances, {args.standalone} standalone over {extent:.0f} m, lod {args.screen_heights or args.lod}'
          f'{" px" if args.screen_heights else ""} ({args.reduction})')


if __name__ == '__main__':
    main()
