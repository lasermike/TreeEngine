# Terrain tools

Terrain in TreeEngine is a streamed heightfield read from a **terrain package**, a directory (by convention
`Name.terrain/`) that the engine opens with `Game/TerrainPackage.h`. The engine reads only packages; these tools make
them.

| Tool | Does |
| --- | --- |
| `TerrainTool generate` | Procedural maps (seeded, reproducible): `valley`, `hills`, `flat`. |
| `TerrainTool pack` | Packs a height raster: 16-bit PNG, or RAW float32/uint16/int16. |
| `TerrainTool info` | Prints a package's manifest and reads every block. |
| `TerrainTool heights` | Samples heights at points (bilinear on the finest level), for placing objects. |
| `TerrainTool texture` | Tileable procedural maps for terrain material layers: `grass`, `rock`, `snow`, `dirt`. |
| `import_heightmap.py` | GIS import: reprojects, crops and resamples with GDAL, then runs `TerrainTool pack`. |

Build TerrainTool with `msbuild Tools\terrain\TerrainTool.vcxproj -p:Configuration=Release -p:Platform=x64`. It is not
in the solution; its output goes to `Tools\terrain\bin\Release\TerrainTool.exe`.

## Workflows

**Procedural map.** An 8 km valley at 1 m, about 200 MB, written in a couple of seconds:

```
TerrainTool generate --out Valley.terrain --preset valley --size 8192 --spacing 1 --seed 7 --relief 600
```

`--relief` is the mountains' height in meters. The same seed always gives the same map.

**GIS elevation data** (GeoTIFF, USGS 3DEP, Copernicus DEM, SRTM, national LiDAR DTMs, private surveys; anything GDAL
reads). Install GDAL (OSGeo4W, QGIS or conda-forge) so `gdalwarp`, `gdalinfo`, `gdal_translate` and `gdaltransform`
are on PATH, then:

```
python import_heightmap.py dem.tif Alps.terrain --center 46.55 8.05 --size 4096 --spacing 1
python import_heightmap.py dem.tif Alps.terrain --crs EPSG:32632 --bounds 410000 5150000 418192 5158192 --spacing 1
```

Several sources can be given, oldest first: later ones win where they have data, so a full-coverage older survey can
sit under a newer partial one (they may differ in CRS and resolution).

Surveys see a water surface, not the bottom, and hydro-flattened DTMs make each water body one flat sheet at the level
it was flown at. `--water-level M` turns ground at or below `M` meters into a seabed that deepens with distance from
dry ground (`--water-slope`, meters per meter, default 0.05) down to `--water-depth` (default 8 m) below the level,
so a `water` object's tide can uncover the shallows.

Better, give real bathymetry with `--seabed` (one or more rasters in meters, stacked like the sources): water samples
take its depth wherever it has one, and only the rest is carved. For Puget Sound, the USGS CoNED topobathymetric DEM
(2020, 1 m, NAVD88 meters, the same vertical datum as WA DNR LiDAR) is on NOAA's public bucket as one VRT. GDAL can cut
an area out of it without downloading the whole 1,364-tile set. In a bash shell (Git Bash on Windows); `-projwin`
takes the area's corners in the DEM's CRS, UTM zone 10N meters (west, north, east, south), a little past the
package; these are Hungerford's:

```bash
curl -o coned.vrt https://noaa-nos-coastal-lidar-pds.s3.amazonaws.com/dem/Puget_Sound_CoNED_DEM_2020_9112/Puget_Sound_CoNED_DEM_2020_m9112_EPSG-6339.vrt
sed 's#relativeToVRT="1">tiles/#relativeToVRT="0">/vsicurl/https://noaa-nos-coastal-lidar-pds.s3.amazonaws.com/dem/Puget_Sound_CoNED_DEM_2020_9112/tiles/#' coned.vrt > coned-remote.vrt
gdal_translate -projwin 502396 5230529 506692 5226233 coned-remote.vrt Puget_Sound_CoNED_Topobathy_DEM_2020_m9112_hungerford.tif
```

Hungerford was imported from the three WA DNR surveys plus that cut:

```bash
python import_heightmap.py puget_lowlands_2005.vrt olympics_south_2019.vrt mason_county_2025.vrt Hungerford.terrain \
    --center 47.2089 -122.94 --size 4096 --spacing 1 --height-scale 0.3048006 \
    --water-level 1.9 --seabed Puget_Sound_CoNED_Topobathy_DEM_2020_m9112_hungerford.tif
```

With `--center`, the target CRS defaults to that point's UTM zone, so the package is in meters. Mosaics of several
tiles work through a GDAL VRT (`gdalbuildvrt all.vrt *.tif`). Cells with no data take the lowest valid height. The
source file, CRS, bounds and spacing are recorded in the manifest's `source`. North is +z and east +x.

**Plain heightmaps** (from a terrain editor, World Machine, Gaea, and so on):

```
TerrainTool pack --out Island.terrain --png height.png --spacing 1 --height-scale 0.0183 --height-offset -20
TerrainTool pack --out Island.terrain --raw height.r32 --width 4097 --height 4097 --type f32 --spacing 0.5
```

Height in meters is `height-offset + height-scale * value`. The image's top row is north (+z) and its left column west
(-x), so the terrain seen from above with +z up matches the image; the engine is left-handed with y up. Rasters that are not 64 * 2^n + 1 samples per side are
padded to the next size by repeating their edge.

**Layer textures.** `TerrainTool texture --out Textures/rock --kind rock --size 512 --seed 3` writes
`rock_color.png` (sRGB), `rock_normal.png` (tangent space, +Y up the image) and `rock_mr.png` (roughness in G,
metallic 0 in B), all tiling seamlessly, for a terrain layer's material. `Game/Resources/Terrain/Textures` holds a set
made this way (seed 3, 512 px).

## Package format (version 1)

The world is a square `worldSize` meters wide, centered on the origin: x and z run from -worldSize/2 to +worldSize/2,
and y is up. It is cut into a quadtree. Level 0 is one node covering everything; level k has 2^k x 2^k nodes, and the
finest level is `levels - 1`. Every node, at every level, is a grid of 64 x 64 quads (65 x 65 vertices), so the
finest level's spacing is `sampleSpacing` and each level up doubles it. An 8192 m world at 1 m has 8 levels and 21,845
nodes.

`manifest.json`:

| Field | Meaning |
| --- | --- |
| `format`, `version` | `"TreeTerrain"`, 1. |
| `worldSize`, `sampleSpacing`, `levels` | Extent in meters, finest spacing in meters, level count. `worldSize = sampleSpacing * 64 * 2^(levels-1)`. |
| `nodeQuads`, `blockSamples`, `blockStride` | 64, 67, 9216: the block layout, checked against the engine's. |
| `heightOffset`, `heightScale` | Height in meters = `heightOffset + value * heightScale`. |
| `minHeight`, `maxHeight` | The height range, in meters. |
| `nodesFile`, `boundsFile` | `nodes.bin`, `bounds.bin`. |
| `source` | Free-form: generator settings, or GIS file, CRS and bounds. |

`nodes.bin` holds one block per node, each `blockStride` (9216) bytes so blocks start on 512-byte boundaries for
unbuffered and DirectStorage reads. Blocks are in implicit order: level by level from the root, and within a level
row-major by z, then x. So node (level, x, z) is at block `(4^level - 1) / 3 + z * 2^level + x`.

A block is 67 x 67 little-endian uint16 heights, row-major from -x/-z: the node's 65 x 65 vertices plus a one-sample
border on every side, so normals at the node's edges match its neighbors'. Coarser levels point-sample the finest grid;
samples past the world's edge repeat the edge.

`bounds.bin` holds each node's lowest and highest height, two uint16 per node in the same order and units,
taken over the finest samples in the node's area so they bound every level's geometry there. The engine reads it whole
when it opens a package, to choose and cull nodes before their blocks load.

Blocks are uncompressed for now. GDeflate compression (DirectStorage) would add a per-node offset table without
changing the block contents.
