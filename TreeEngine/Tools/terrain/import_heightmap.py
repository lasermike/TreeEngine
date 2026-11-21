#!/usr/bin/env python3
"""Imports a height raster (GeoTIFF, DEM, IMG, VRT, or anything else GDAL reads) into a terrain package.

GDAL's command-line tools do the GIS work: reprojecting to a metric CRS, cropping, and resampling to the package's
sample spacing. TerrainTool then packs the result. Needs GDAL on PATH (gdalwarp, gdalinfo), for example from OSGeo4W,
QGIS, or conda-forge, and TerrainTool built (Tools/terrain/TerrainTool.vcxproj).

Examples:
  # A 4 km square at 1 m around a point, in that area's UTM zone.
  import_heightmap.py dem.tif out/Valley.terrain --center 46.55 8.05 --size 4096 --spacing 1

  # The whole raster, reprojected to EPSG:32632, at 2 m.
  import_heightmap.py dem.tif out/Alps.terrain --crs EPSG:32632 --spacing 2

Plain heightmaps without georeferencing (16-bit PNG, RAW) go straight to TerrainTool pack; see README.md.
"""
import argparse
import array
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def find_terrain_tool(explicit):
    candidates = [explicit] if explicit else [
        os.path.join(HERE, 'bin', 'Release', 'TerrainTool.exe'),
        os.path.join(HERE, 'bin', 'Debug', 'TerrainTool.exe'),
    ]
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    sys.exit('TerrainTool.exe not found; build Tools/terrain/TerrainTool.vcxproj or pass --tool')


def require(tool):
    if shutil.which(tool) is None:
        sys.exit(f'{tool} not found on PATH; install GDAL (OSGeo4W, QGIS or conda-forge) or use TerrainTool pack directly')


def run(command):
    print('>', ' '.join(f'"{c}"' if ' ' in c else c for c in command))
    subprocess.run(command, check=True)


def check_coverage(raw, count, source, bounds, crs):
    """Stops when the source has no heights in the area, and warns when it covers only part of it (the rest becomes
    flat ground at the lowest height)."""
    heights = array.array('f')
    with open(raw, 'rb') as f:
        heights.fromfile(f, count)
    missing = sum(1 for h in heights if not (-1e30 < h < 1e30))
    if missing == count:
        sys.exit(f'{source} has no heights in this area. If it is a VRT, check that its tiles are where it says '
                 f'(gdalinfo lists them, with an error for each one missing; gdalbuildvrt with absolute paths avoids '
                 f'this), and that the area {bounds} ({crs}) overlaps the tiles (gdalinfo prints their corners in '
                 f'latitude and longitude).')
    if missing:
        print(f'warning: {100 * missing / count:.0f}% of the area has no source heights and will be flat at the lowest '
              f'height; get more tiles, or move --center or shrink --size to fit the data', file=sys.stderr)


def utm_crs(latitude, longitude):
    zone = int((longitude + 180) // 6) + 1
    return f'EPSG:{32600 + zone if latitude >= 0 else 32700 + zone}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('source', help='input raster')
    parser.add_argument('output', help='package directory to write (for example Scenes/Terrain/Name.terrain)')
    parser.add_argument('--spacing', type=float, default=1.0, help='meters between samples (default 1)')
    parser.add_argument('--crs', help='target metric CRS (default: the UTM zone of --center, or the source CRS)')
    parser.add_argument('--center', type=float, nargs=2, metavar=('LAT', 'LON'), help='crop around this WGS84 point')
    parser.add_argument('--size', type=float, help='crop to a square this many meters wide (with --center)')
    parser.add_argument('--bounds', type=float, nargs=4, metavar=('XMIN', 'YMIN', 'XMAX', 'YMAX'),
                        help='crop to these bounds in the target CRS')
    parser.add_argument('--resampling', default='cubic', help='gdalwarp resampling (default cubic)')
    parser.add_argument('--height-scale', type=float, default=1.0, help='meters per raster unit (default 1)')
    parser.add_argument('--height-offset', type=float, default=0.0, help='meters added after scaling (default 0)')
    parser.add_argument('--tool', help='path to TerrainTool.exe')
    args = parser.parse_args()

    tool = find_terrain_tool(args.tool)
    require('gdalwarp')
    require('gdalinfo')
    require('gdal_translate')
    crs = args.crs or (utm_crs(*args.center) if args.center else None)
    bounds = args.bounds
    if args.center and args.size:
        if not crs:
            sys.exit('--center needs a target CRS')
        require('gdaltransform')
        result = subprocess.run(['gdaltransform', '-s_srs', 'EPSG:4326', '-t_srs', crs, '-output_xy'],
                                input=f'{args.center[1]} {args.center[0]}\n', capture_output=True, text=True, check=True)
        x, y = (float(v) for v in result.stdout.split()[:2])
        half = args.size / 2
        bounds = [x - half, y - half, x + half, y + half]

    with tempfile.TemporaryDirectory() as work:
        warped = os.path.join(work, 'heights.tif')
        command = ['gdalwarp', '-overwrite', '-r', args.resampling, '-tr', str(args.spacing), str(args.spacing),
                   '-ot', 'Float32', '-dstnodata', '-3.4e38', '-of', 'GTiff']
        if crs:
            command += ['-t_srs', crs]
        if bounds:
            command += ['-te'] + [str(v) for v in bounds]
        run(command + [args.source, warped])
        info = json.loads(subprocess.run(['gdalinfo', '-json', warped], capture_output=True, text=True, check=True).stdout)
        width, height = info['size']
        raw = os.path.join(work, 'heights.raw')
        run(['gdal_translate', '-of', 'ENVI', '-ot', 'Float32', warped, raw])
        check_coverage(raw, width * height, args.source, bounds, crs)
        source = {
            'importer': 'import_heightmap.py',
            'file': os.path.basename(args.source),
            'crs': crs or 'source',
            'bounds': bounds,
            'spacing': args.spacing,
            'resampling': args.resampling,
            'size': [width, height],
        }
        # Raster rows run north to south and package rows run from -z, so north is -z.
        source['north'] = '-z'
        run([tool, 'pack', '--out', args.output, '--raw', raw, '--width', str(width), '--height', str(height),
             '--type', 'f32', '--nodata', '-3.4e38', '--spacing', str(args.spacing),
             '--height-scale', str(args.height_scale), '--height-offset', str(args.height_offset),
             '--source-json', json.dumps(source, separators=(',', ':'))])
    run([tool, 'info', args.output])


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(f'{os.path.basename(error.cmd[0])} failed (exit {error.returncode}); see its messages above')
