---
issue: 36
---
# Road, driveway and trail materials

## Goal
Roads, driveways and trails should look like the real ones around 190 E Scott Dr: today there are only two road
surfaces (paved and gravel) and one trail material, so a county road, a worn gravel drive and a forest path all look
alike. Add more surface types, chosen per line in the roads file, so each can match its site photos.

## What it should do
- The surfaces that matter most for Hungerford, in order:
  1. Paved road with lane markings (today's county roads keep their markings).
  2. Paved road without lane markings.
  3. Chip seal (common on rural Mason County roads).
  4. Gravel road.
  5. Partial gravel with dirt patches (worn gravel, the dirt showing through).
  6. Extra-large compacted gravel, for trailer pads.
  7. Packed dirt with occasional weeds.
- Textures: generated for now (`TerrainTool texture`, extended with the new surfaces), tuned against the wide site
  photos. Close-up photos come later; when they do, a tool (like `Tools/make-lichen-bark.ps1`) turns each into a
  tileable set (colour; normal and roughness derived from it) that replaces that surface's generated one, with no
  other change.
- Later, if wanted: concrete aprons, two-track with a grassy centre, and more trail types (forest duff path, gravel
  path, boardwalk).
- Each line in `Name.roads.geojson` picks its type with its `surface` property (trails too). Lines without one keep
  today's look, so existing scenes don't change.
- A scene maps each surface type to a material (as `roadSurfaces` does today for gravel), so a scene can tune or
  photo-match a surface without changing the roads file.
- Edges blend into the terrain rather than ending in a hard line: gravel scatters onto the shoulder, trails fade
  into the duff.
- Realism beyond the surface types, so a surface doesn't visibly repeat or look like a flat ribbon:
  - No visible tiling: each surface samples its texture with stochastic (hex-tile) blending, which breaks the grid
    at close range, plus a large-scale variation map (tens of metres) that shifts colour and roughness so distant
    road doesn't repeat either. The same anti-tiling applies to the terrain's ground layers (duff, grass, rock and the
    rest), which repeat the same way.
  - Several textures per surface, blended by masks across the road's width: wheel ruts and a grassy or mossy
    centre strip on gravel and two-track, a cleaner wheel path and dirtier edges on asphalt, a worn centre on
    trails. The road mesh carries its position across the width (0 at one edge, 1 at the other) for these masks.
  - Height-based blending where the road meets the terrain, and where two surfaces meet (a gravel drive joining
    asphalt): stones stand proud of the dirt between them rather than cross-fading.
  - Scattered detail: decals for potholes, patches, cracks, puddles and oil stains on roads, fallen needles and
    leaves on drives and trails under trees; optionally small instanced stones and needle litter along the edges.
  - Surface relief: normal maps everywhere, and parallax on gravel and boardwalk so stones and planks have depth
    at a low angle.
- Hungerford uses the new types where the photos show them (the 190 driveway's reddish gravel, E Scott Dr's
  surface, the trail).
- The QGIS workflow in `Tools/terrain/README.md` shows the new `surface` values, so they can be set by hand.

## Plan (a PR each)
1. Anti-tiling and large-scale variation for road, driveway and trail surfaces and the terrain's ground layers, in
   raster and DXR. Before/after captures of Hungerford close up and at a distance; frame time measured.
2. Across-width masks and the new surface types, chosen by `surface` and mapped by the scene.
3. Height blending where roads meet the terrain and each other.
4. Decals and scattered detail.

## Out of scope
- Road geometry (widths, grading, ditches) beyond what a material needs.
- Puddles or wetness that change with weather.
- Traffic, vehicles, signs.

## References
- Site photos: `E:\Source\Tree\Content\photos\scott-dr`.
- Current surfaces: `roadSurfaces` in `hungerford.scene.json`, `surface` and `kind` in
  `Hungerford.roads.geojson`, `TerrainTool texture` (asphalt, gravel, dirt maps), `Tools/terrain/README.md`.
- Earlier work: #26 (drive gravel), #19 (trails).

## Photos to take later (one or more per surface)
- Straight down from standing height (about 1.5 to 2 m of ground in the frame), the phone level, no shadows of you
  or the phone in it: an overcast day or full shade is best, low sun the worst.
- A few overlapping shots along the surface help the tool pick a seamless tile; a coin or shoe in one shot gives the
  scale (not in the others).
- One wider shot per surface, at eye level along the road, for checking the result.
- Into `E:\Source\Tree\Content\photos\scott-dr\surfaces\<surface>` (e.g. `chip-seal`, `trailer-pad`), HEIC is fine.

## Open questions
- None blocking. The surfaces start with generated textures; photos replace them as they're taken.
