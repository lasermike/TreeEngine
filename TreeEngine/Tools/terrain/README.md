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
| `TerrainTool texture` | Tileable procedural maps for terrain material layers and road surfaces: `grass`, `rock`, `snow`, `dirt`, `asphalt`, `gravel`. |
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

Hungerford was imported from the three WA DNR surveys plus that cut, with its roads graded in (see Roads below;
the roads file is made from an import without `--roads` first, then the package is imported again with it):

```bash
python import_heightmap.py puget_lowlands_2005.vrt olympics_south_2019.vrt mason_county_2025.vrt Hungerford.terrain \
    --center 47.2089 -122.94 --size 4096 --spacing 1 --height-scale 0.3048006 \
    --water-level 1.9 --seabed Puget_Sound_CoNED_Topobathy_DEM_2020_m9112_hungerford.tif \
    --roads Hungerford.roads.geojson
```

**Tides.** `noaa_tide.py` writes a `water` object's tide from a NOAA CO-OPS station's harmonic constituents, plus
the station's mean sea level above NAVD88 for the water's height (the WA DNR and CoNED heights are NAVD88). Many
stations only publish predictions made from another station's; `--fit-to` scales and delays a harmonic station's
tide to match one's highs and lows. Hungerford's is Budd Inlet's fitted to Arcadia (9446666), 1.4 km from the
package's center:

```bash
python noaa_tide.py --station 9446807 --fit-to 9446666 --start 2026-06-21T19:00:00Z
```

It reports the fit (x1.089, +56 minutes, 9.2 cm RMS over June 2026's highs and lows) and prints JSON: `level` for
the transform's y and `tide` for the scene.

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

**Roads.** `import_roads.py` writes a package's roads file from Mason County's Street Centerlines and Site Address
layers and OpenStreetMap's service ways, downloading them once into `--cache` (kept outside the repo):

```bash
python import_roads.py ../../Game/Resources/Terrain/Hungerford.terrain ../../Game/Resources/Terrain/Hungerford.roads.geojson \
    --cache E:/Source/Tree/Content/roads --snap-heights Hungerford-ungraded.terrain
```

It snaps each county centerline sideways onto the road bed in the package's heights (the flattest strip of the
road's width within `--snap` meters, default 8, smoothed along the road; ends that met still meet), drops
OpenStreetMap ways that duplicate a county road, and turns the rest into driveways whose road end is moved onto the
nearest road when it's within 15 m. A graded package's beds already follow the roads it was graded with, so snap to
the same area imported without `--roads` (`--snap-heights`); then unedited roads come out as they were.

Then it generates a driveway for every site address no driveway serves yet, from the road the address names to
the house: the nearest OpenStreetMap building within 30 m (ending 5 m short of its center, toward the road), else the
address point. It skips an address within 12 m of a road, a second unit of an address (or one within 8 m of
another), one whose house is within 30 m of an existing driveway's end, and one whose straight path would cross a road
or another driveway or run through water (heights at or below `--water-level`, default 1.9 m); when the named road's
path is blocked it takes the nearest clear road. Generated driveways are straight lines from the road: a first draft
to check over imagery. Where several houses lie past a road's end, their driveways fan out from it, where there is
often a shared private lane to draw instead.

For Hungerford: 74 roads moved a median 1.0 m (90% under 3 m); 123 OpenStreetMap driveways, 98 joined to a road;
and 350 generated from 502 addresses (130 already served, 2 on a road, 1 a second unit, 19 blocked), a median 45 m
long. Re-running keeps every feature whose `edited` is true.

The roads file is GeoJSON in the package's CRS (its `crs` member names it, such as EPSG:32610), so QGIS opens it over
aerial imagery for editing. Each feature's `properties`:

| Property | Features | Meaning |
| --- | --- | --- |
| `kind` | all | `road`, `driveway` or `trail` (lines), or `prop` (points) |
| `id` | all | stable id: `county-<road number or name>-<start x>-<start y>`, `osm-way-<id>`, `address-<parcel>-<number>`, `prop-<model>-...` |
| `source` | all | `county`, `osm`, `generated`, or `manual` (drawn by hand) |
| `edited` | all | true once changed by hand: the importer leaves it alone |
| `removed` | all | true: deleted by hand. The engine skips it; imports and prop placement don't make it again |
| `name` | lines | roads: street name, for signs; generated driveways: the address |
| `surface` | roads, driveways | `paved` or `gravel` (trails have their own) |
| `owner` | roads | `county` or `private`; private roads stay plain gravel lanes |
| `width`, `shoulder` | lines | meters: the traveled width (trails 1 m unless set, 0.3 m at least), and the shoulder each side |
| `ditchDepth`, `ditchWidth` | roads | meters, each side; 0 for none |
| `speedLimit` | roads | mph, from the county |
| `class` | roads | the county's functional class, when it has one (lower is more important) |
| `prop` | props | the model: `mailbox`, `post`, `stop-sign` or `street-name-sign` |
| `heading` | props | degrees clockwise from north that the prop's front faces (a mailbox's door, a sign's face) |
| `text` | props | a mailbox's address; a street-name sign's names, the lower blade's first, separated by `\|` |

Widths come from the surface and owner: county paved 6 m with 0.6 m shoulders (6.6 m and 1.2 m at 40 mph and up),
county gravel 5 m, private paved 4 m, private gravel 3.6 m, driveways 3 m; county roads get 0.5 to 0.6 m deep ditches.
A terrain object names its roads file with `roads` (Scenes README). Every line is clipped to what the package's
heights cover. Its package coordinates are about the packed grid's center: TerrainTool puts the source raster's
southwest pixel center at the package's -x/-z corner and pads north and east, so that center is not the source
bounds' center. `python -I -B test_import_roads.py` checks the importer's geometry (junctions, clipping, driveway
joins) on synthetic data.

**Props.** `road_props.py` places mailboxes and signs from the lines (`import_roads.py` and `finish_road_edits.py`
run it; `python road_props.py FILE` runs it alone), replacing the generated props that weren't edited:

- a street-name sign at each intersection of differently named roads (where one ends on another, or where they
  cross), private roads included, on the right corner of the lowest-ranked road's approach, facing square to the
  highest-ranked road so its lower blade runs along it;
- a stop sign on each approach of a county road that yields: it ends or crosses at a higher-ranked road (county over
  private, then functional class, speed limit, paved over gravel), or ends (a T) at one of its rank or higher that
  carries on through. Private roads and driveways get none. The county splits roads where they meet, so a road
  carries on through where two pieces with its name meet;
- a mailbox for each driveway, at the mouth on the road where it (or the lane it branches from) starts: on the
  driveway's side, 2.5 m past it in the direction of travel, the door to the road. Driveways sharing a mouth line
  their mailboxes up.

A sign's post stands at least 0.5 m off every road's shoulder: where roads meet at a sharp angle it moves further back
along the approach or further out, and where no spot within 15 m is clear it's left out and counted ("sign without
room"). Hungerford: 27 street-name signs, 15 stop signs and 465 mailboxes (8 OpenStreetMap driveways reach no road). Moving a
prop in QGIS and marking it `edited` keeps it there; a point you add becomes a prop (set its `prop` and `heading`).
`python -I -B test_road_props.py` checks the rules. The models are `Game/Resources/Models/Roadside/<prop>.glb`, from
`make_road_props.py` (meters, y up, the origin at the foot of the post, the front facing glTF -z): replace one with any
glTF of the same name. A model that isn't installed draws none of its props; the scene still loads.

Street-name signs show their `text`: the engine draws each name (upper case, in a stroke font of its own: letters,
digits and `. - ' / &`) white on green with a white border into a texture when the roads load, and maps it on the
blade faces (the model's `sign-blade-lower` and `sign-blade-upper` materials, UV-mapped 0 to 1 as seen from each
side). Letters are 0.1 m tall; a blade grows from the model's 0.9 m to fit its name, up to 1.2 m, and a longer name is
squeezed to fit that (Game/SignText.h). Editing a sign's `text` in QGIS changes it at the next load.

**Editing props in the engine.** Click a prop (with the debug UI open) to select it: the click selects its
terrain, and the prop the click's ray hits within its model's bounds; it's highlighted. The terrain's **Props**
section in the Objects panel then shows it: drag its position with the position gizmo (it stays on the ground) or
type its heading, or **Remove prop**. A click picks only a prop in front of the ground it meets, so one hidden behind a
hill isn't. **Save prop edits to the roads file** writes them into the running copy, and when that is the copy the
build put beside the executable, into the file it was copied from (found above the executable as
`Game\Resources\<roads>`); a roads file loaded from anywhere else is saved alone. Only the edited props' lines change,
each marked `edited` (and `removed` when removed), in the same layout as `import_roads.py`'s, so the diff shows just
them. Every file's new text is staged beside it before any is replaced, so a failed save changes nothing (or says
which file it did save). Unsaved edits are lost when the scene reloads. `Tests/RenderRegression/test-prop-editing.ps1`
picks, edits and saves (into a copy) in raster and DXR.

The render suite's `terrain-roads` scene draws `RoadsTest.terrain`: 256 m of Hungerford around the E Scott Dr and
E Benson Loop intersection, imported like Hungerford but with `--crs EPSG:32610 --bounds 504346.2248 5228427.0807
504602.2248 5228683.0807` (on Hungerford's grid) and `--roads RoadsTest.roads.geojson`, whose features were cut from
Hungerford's roads file (lines clipped to the area, its props inside it). It's a fixture: it changes only on purpose.

**Editing roads in QGIS.** Open the roads file first (Layer → Add Layer → Add Vector Layer), so the project takes its
CRS, then aerial imagery as an XYZ tile layer below it (Esri World Imagery:
`https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`). Style the layer
Categorized by `source`, turn on snapping (Vertex and Segment), toggle editing, and use the Vertex Tool to reshape
lines (click a point, click where it goes; double-click a line to add a point) and Add Line Feature to draw new ones
from the road outward (type `driveway` in `kind`; leave the rest empty). Save Layer Edits, then:

**Trails** are drawn by hand the same way: Add Line Feature anywhere, with `trail` in `kind` (and a `name` if you
like; `width` defaults to 1 m). They're their own material (the dirt maps, under every road and driveway where they
cross), lie on the ground as it is rather than being graded (raised just enough to clear a ridge between their mesh
rows), and nothing imports or generates them, so a re-run of
`import_roads.py` keeps them whether or not they're marked `edited`. Since they aren't graded, a trail edit needs
`finish_road_edits.py` and a rebuild, but no terrain re-import. Hungerford has one example,
`trail-example` (from E Scott Dr near the middle of the map, 250 m west): reshape or delete it.

```bash
python finish_road_edits.py ../../Game/Resources/Terrain/Hungerford.roads.geojson
```

It compares the saved file with its last commit and marks every changed or new feature `edited`, fills in a new
driveway's id (`manual-<x>-<y>`), source `manual`, gravel and 3 m width, snaps a changed line's first point onto the
nearest line within 6 m when QGIS missed it, and restores one feature per line with unchanged features exactly as
before (QGIS rewrites every coordinate), so the diff shows just the edits. A deleted imported or generated feature
(a county road, an OpenStreetMap or generated driveway, a generated prop) stays in the file as a removed marker
(`removed: true`, `edited: true`), so a later `import_roads.py` or prop placement leaves it out; a deleted hand-drawn
one just goes. It places the props again for the edited lines, and reports deletions and real crossings. Then
re-import the package with `--roads` (below) and rebuild. `python -I -B test_finish_road_edits.py` checks it.

**Grading.** `import_heightmap.py --roads FILE` (TerrainTool `pack --roads`) grades the roads into the heights before
packing (Game/RoadGrading.h). Along each smoothed centerline the bed follows the ground's running median and mean
(7 and 17 samples, a meter apart), so short bumps are planed off; across it, the traveled way is crowned (2% paved,
3% gravel, 1% driveways), shoulders fall 4% more, and roads with ditches get a foreslope down to the ditch's depth and
a backslope up to the ground over the ditch's width. Without a ditch the bed blends back into the ground over 3 m.
Where lines overlap, a traveled way wins (paved roads over gravel roads over driveways), else the nearest line. On
side slopes the uphill ditch cuts into the hill and the downhill foreslope builds a small embankment. Hungerford:
560,000 samples changed (roads and all 473 driveways), by 0.11 m on average and under 0.76 m for 99% of them; 3.10 m cut and 2.26 m fill at most.
Grading works from the imported heights, so after editing roads, import the package again rather than re-grading a
graded one.

**Layer textures.** `TerrainTool texture --out Textures/rock --kind rock --size 512 --seed 3` writes
`rock_color.png` (sRGB), `rock_normal.png` (tangent space, +Y up the image) and `rock_mr.png` (roughness in G,
metallic 0 in B), all tiling seamlessly, for a terrain layer's material. `Game/Resources/Terrain/Textures` holds a set
made this way (seed 3, 512 px); terrains with roads use its `asphalt` and `gravel` maps for their surfaces.

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
