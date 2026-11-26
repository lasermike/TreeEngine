# Scene definitions

The fifteen built-in scenes are defined by `*.scene.json` files in this directory.
At startup, the engine discovers these files in `Scenes` and `UserScenes` beside
the executable, without descending into subdirectories. Adding a scene requires
no catalog-code or project-file change. Build TreeClassic to deploy source scenes
into `Scenes`; user-authored files belong in `UserScenes`.

Scenes are sorted by display name, ignoring case, with source/filename
tie-breakers. Duplicate display names are retained and labeled with their source
and filename. Malformed files and all same-source duplicate IDs are excluded with
path-specific diagnostics in the Scene panel and debugger. A missing `UserScenes`
folder is normal. Startup prefers `builtin:LoadAITree`, falling back to the first
valid scene with a diagnostic. Startup fails clearly if there are no valid scenes.
Scene indexes are not stable identifiers.

Each file can supply an optional `id`; otherwise its filename without
`.scene.json` is its ID. IDs are case-sensitive, must be nonempty, and cannot
contain control characters, colons, or path separators. The loader accepts
source-qualified IDs (`builtin:LoadAITree`, `user:my-tree`), legacy built-in IDs
(`LoadAITree`), unique display names (`AI Tree`), and explicit JSON paths.
Unqualified built-in IDs take priority over user IDs. Ambiguous names fail rather
than selecting an arbitrary file. Existing regression loader IDs remain unchanged.

## Loading and saving

The debug **Scene** panel has a discovered-scene picker, **Load**, **Reload**,
**Refresh**, **Save**, and **Save As**. Refresh explicitly rescans both directories;
reload reads the active file again. Failed loads preserve the active scene,
camera, and settings. Successful loads reset scene time.

### Scene time and growth time

Two clocks run while a scene plays:
- **Scene time** moves camera paths, tides, water ripples and the rotating sun.
- **Growth time** is how far plants have grown. It advances with scene time,
  times the growth speed, unless growth is paused.

Keys (and the debug UI's **Controls**, with **Scene time** and **Growth time**
sliders, **Pause growth**, **Restart growth** and **Growth speed**):
- **0** restarts growth from nothing; the camera path, tides and the rest go on.
- **Shift+0** restarts everything: scene time from 0, growth from the scene's
  start.
- **G** pauses and resumes growth; **P** pauses both clocks.
- **<** and **>** step both clocks by a frame.

Shortcuts are ignored only while a debug UI field takes the keyboard.

A scene's optional `"growth": { "startTime": 60, "speed": 1 }` sets where growth
starts on load and Shift+0 (seconds of growth, 0 to 100000, default 0: plants
start as seeds) and how fast it runs against scene time (above 0 to 100, default
1). A scene that should open on grown plants starts its growth clock late rather
than raising every tree's `animationSpeed`, so 0 still replays growth at the
species' own speed. Render tests take a capture's `growthTime` (see
`Tests/RenderRegression/README.md`); without one the growth clock is
`startTime + time x speed`.

Save As asks for a display name and filename stem and creates `UserScenes` on the
first save. It generates a user ID from the stem. Filenames cannot contain path
separators, Windows-invalid characters, reserved device names, or trailing dots
or spaces. Built-in and external scenes require Save As; Save is enabled only
for an active user scene. Both Save and Save As require confirmation before
replacing an existing file. Cancel leaves the file unchanged.

Saving captures live camera/object transforms, supported materials, L-system
parameters and rules, scene lighting, and render settings. Materials are saved
as independent inline values, including renderer defaults that affect appearance.
Model materials remain in their referenced glTF asset. Simulation time, generated
geometry, growth pose/cache state, GPU resources, HMD state, and application
preferences such as DXR/postprocessing are **not** saved. Unsupported callbacks,
untracked objects, and invalid live values fail rather than producing partial
definitions. Saving never reloads the scene or resets its time.

Files are validated, written as readable UTF-8 JSON to a unique same-directory
temporary file, flushed/closed, and published with explicit overwrite permission.
Failed writes preserve the existing target; errors include the destination path.
There is no fallback to another writable directory. After saving, the active
source becomes the user file and the catalog is refreshed without reinstantiation.
A successful write that cannot be rediscovered reports a warning.

Programmatic APIs on `Game` are intended to be called **between frames**; ImGui
queues the same operations for the next update boundary:

```cpp
game.RefreshScenes();
game.LoadScene("builtin:LoadAITree");
game.SaveScene("My tree", "my-tree");  // Save As, without overwrite permission.
game.LoadScene("user:my-tree");
game.SaveScene({}, {}, true);         // Save after caller confirms replacement.
```

Check each returned `HRESULT` and use `GetSceneError()` for operation errors.
`S_FALSE` means the file was saved but rediscovery needs attention.
`SceneData::SerializeScene` and `WriteScene` are also available for definitions.

User files are deployment-local: Debug and Release have separate `UserScenes`
directories. Build/deployment and solution Clean do not manage these files.
**Back them up before deleting a bin/deployment directory.** No unsaved-change
prompts, autosave, directory watcher, or asset packaging are provided.

## Structure

- `version`: required schema version, currently `1`.
- `id`: optional stable scene identity, described above.
- `name`: required nonempty display name.
- `camera`: transform with optional `position`, `rotation`, and `scale`.
- `render`: optional `clearColor`, `farClippingPlane`, `useShadowMaps`, `useAlphaBlendedRenderTarget`, `environment`, and `sky`. The far plane defaults to 30 and must exceed the 0.2-unit near plane. `environment` requires `diffuse` and `specular` paths to linear HDR DDS cube maps. The specular cube's mip levels run from smooth to rough; the environment lights glTF materials in both raster and DXR without changing the background. `sky` draws a background: `cubeMap` (required) is a DDS cube map, and `rotationDegrees` (default `0`) is a yaw about +Y. The sky follows the camera, is drawn behind all objects, and supplies the DXR ray-miss color; without it the background is `clearColor`. Skies are not objects and have no material.
- `light`: optional directional-light `ambient`, `diffuse`, `specular`, `direction`, and `shadowPosition`. Omitting `shadowPosition` retains automatic shadow placement; supplying it preserves the authored light-position override.
- `pointLightCount`: point-light activation, `0` or `1`.
- `pointLight`: optional `ambient`, `diffuse`, `specular`, `position`, `range`, and `attenuation`. Range and attenuation components must be nonnegative.
- `materials`: map of names to material definitions.
- `atlases`: map of names to texture-atlas layouts for tree leaves; see [Leaf atlases](#leaf-atlases).
- `objects`: nonempty array of typed object definitions with unique nonempty `id` values.

Unknown properties are errors, including typos. Errors name the source file and
property path. Colors are RGBA arrays and may contain HDR values. Vector values
must be finite. Object scales must be nonzero; negative scale mirrors.

## Objects and transforms

Every object supports `transform`, `textureCoordScale`, `animationSpeed`, and
`depthLOD`. Defaults are position `[0,0,0]`, scale `[1,1,1]`, quaternion rotation
`[0,0,0,1]`, UV scale `[1,1]`, animation speed `1`, and depth LOD `4`.

Trees, fixed trees and graphs also take `"lod": {"distances": [10], "hysteresis": 0, "reduction": "prune"}` (those
are the defaults). Level 0 is full detail, and each distance from the camera passed lowers the level by one. A tree
draws full-detail sticks at level 0 (per branch, from where the branch starts) and otherwise follows `depthLOD`.
Instanced trees (`instanceOf`) choose a level as a whole and draw a layout their source publishes for it: near (level
0), far (level 1), and for each further distance a reduced layout with about half the leaves of the one before. A tree
with more than one distance also chooses a level as a whole and, from level 2, draws its own reduced layout in place of
its branches (instanced or not).
`reduction` picks how:
- `"prune"` keeps a fixed subset of leaves (by branch ID) and, from level 3, drops the finest twigs.
- `"merge"` replaces groups of sibling leaves with one at their average position and, from level 3, pairs of sibling
  twigs with one averaged twig. It needs sibling leaves and twigs to group: on the canyon L-system trees, which
  mostly grow single-stem chains with leaves along them, it halves the leaves once and then barely reduces more.
Either way the remaining leaves grow (by the square root of the leaves each stands for) to keep the total leaf area.
Distances must be 1 to 8 ascending positive values. `hysteresis` (0 to 0.5) is the fraction of a distance an instance
must pass before it switches level, so instances near a threshold don't flicker; branches keep no history and ignore
it. The debug UI's "LOD distance scale" multiplies every object's distances. Instances inherit `lod` from their source.

In place of `distances`, `"screenHeights": [600, 300, 212, ...]` sets the levels by the tree's projected size in display
pixels (its bounding box's largest extent, usually its height): level i while the tree appears taller than
`screenHeights[i]` pixels. The heights must be 1 to 8 descending positive values. Larger trees, taller windows and
narrower fields of view keep detail farther away. Dividing the height by √2 per level suits the reduced layouts: each
halves the leaves while the projected area halves, so the leaves per pixel stay about the same. With prune, though,
the kept leaves grow to keep leaf area, and on small trees (shrubs) those enlarged leaves show well before the tree is
small on screen; distances, which treat every species alike, look better on the canyon trees (see Forest).

Use `rotationDegrees: [pitch,yaw,roll]` for convenient Euler authoring, or
`rotation: [x,y,z,w]` for a quaternion. Specify only one. Quaternions are preserved
as authored, including the existing AI Tree camera rotation.

A `primitive` supports `shape` and `material`. Supported shapes are
`box`, `cylinder`, `cylinderLD`, `cylinderHD`, `skinnedCylinder`, `sprite`, and
`sphere` (unit diameter, like the box and cylinders). The default shape is `box`.

A `tree` supports `trunkShape` (`skinnedCylinder` by default, or `cylinder`),
named `materials.trunk` and `materials.leaf`, optional `leaves`, and a required
`generator`. The generator is currently an L-system.

A `fixedTree` creates the existing fixed procedural tree generator and supports
`trunkShape` (`skinnedCylinder` by default, or `cylinder`) and the same optional
`materials.trunk`/`materials.leaf` slots and `leaves` as a tree.

A `graph` requires `dataFile`, a CSV file containing the graph samples. Its
normalization matches the former `LoadGraph` behavior.
It also supports optional `materials.trunk`/`materials.leaf` slots and `leaves`.

A `model` requires `source`, a `.gltf` or `.glb` file, and supports optional
`forceLit` plus its transform. Its asset owns its geometry and materials.

A `terrain` draws a heightfield from a terrain package, a directory made by
`Tools/terrain` (see its README for making one from a procedural map, a heightmap
or GIS elevation data). It requires `source`, the package directory, such as
`"Terrain/Hills.terrain"`, resolved like other assets. Optional fields:

- `material`: a material reference, as for primitives. Its UVs are world meters
  (u along x, v along -z), so `textureCoordScale` sets the tiling.
- `layers`: instead of `material`, one to four material layers blended by height
  and slope (below).
- `lodRange` (1 to 16, default 2): a quadtree node splits into four finer ones
  when the camera is within `lodRange` times its size. Higher is more detailed.
- `slots` (16 to 4096, default 512): how many nodes can be resident at once,
  each about 215 KB of vertex buffer, reserved at load.
- `roads`: a roads file for the package, such as
  `"Terrain/Hungerford.roads.geojson"` (made by `Tools/terrain/import_roads.py`;
  see that README). The package must record its source CRS and bounds (GIS
  imports do). Roads and driveways draw as surfaces draped on the terrain,
  in raster and DXR: asphalt or gravel (`Terrain/Textures/asphalt_*` and
  `gravel_*`, tiled every 4 m), with a dashed yellow center line on county paved
  roads and white edge lines on those signed 40 mph or more. Where surfaces
  overlap, paved roads show over gravel roads, and both over driveways. They
  cast no raster shadow. The debug UI's **Show road centerlines** (render tests:
  a capture's `"showRoads": true`) draws the centerlines: paved roads orange,
  gravel roads tan, driveways cyan, trails green. Trails draw 1 m wide (or
  their `width`) in their own dirt material (`Terrain/Textures/dirt_*`), on
  the ungraded ground as each terrain tile draws it at its level of detail,
  and stop at the edges of roads and driveways crossing them. The file's props (mailboxes, posts, stop
  signs, street-name signs) stand on the terrain at their points, turned to
  their headings, drawn instanced from `Models/Roadside/<prop>.glb` in raster
  (with shadows) and DXR. Each model loads when a prop uses it; a model that
  isn't installed draws none of its props. Street-name signs show their street
  names, drawn into a texture at load (`Game/SignText.h`), on blades as long
  as each name needs (0.9 to 1.2 m). Features marked `removed` are skipped.
  Clicking a prop selects it for editing in the terrain's Props section
  of the Objects panel: move it, turn it, remove it, and save the edits back
  into the roads file (Tools/terrain/README.md).
- `transform`: a position only; terrain cannot be rotated or scaled.

Ray-traced camera rays reach `render.farClippingPlane` (at least 200 m) and shadow,
reflection and transmission rays as far (at least 100 m), so a large terrain
should set the far plane to how far it should be seen and shadowed.

Each layer has a required `material` (a reference or inline material; base color,
metallic-roughness and normal maps are used) and optional fields:

- `tile` (default 4): meters per texture repeat.
- `biplanar` (0 to 16, default 0): above 0, steep faces also take side
  projections, blended this sharply, so rock doesn't stretch down cliffs.
- `height` `[lowest, highest]` meters and `heightBlend`: the heights the layer
  covers, fading in and out over `heightBlend` meters on each side.
- `slope` `[least, most]` degrees from flat (0 to 90) and `slopeBlend`.
- `noise` `{ "height": m, "slope": degrees, "scale": m }`: shifts the height and
  slope limits by up to these amounts in noise cells `scale` meters across, so
  edges wander.

Later layers paint over earlier ones; the first covers everything and takes no
rules. A typical terrain: grass, then rock on slopes over 30 degrees with
`biplanar`, then snow above a height. The layers' maps count toward the 32 scene
textures. `TerrainTool texture` makes tileable grass, rock, snow and dirt maps.

```json
"layers": [
  { "material": "grass", "tile": 3 },
  { "material": "rock", "tile": 6, "biplanar": 4, "slope": [30, 90], "slopeBlend": 5,
    "noise": { "slope": 8, "scale": 12 } },
  { "material": "snow", "height": [400, 10000], "heightBlend": 20, "noise": { "height": 30, "scale": 40 } }
]
```

Terrain cannot be instanced. The `LoadTerrainHills` scene shows a
1 km procedural package with grass, dirt in the hollows and rock on slopes.

A `water` object is a flat water surface centered on its transform's position,
whose y is the mean water height (it can be moved, not rotated or scaled). Fields:

- `size` `[x, z]` meters (default `[100, 100]`) and `tile` (meters per normal-map
  repeat, default 4).
- `material`: a material reference. Without one it is clear seawater (transmissive,
  IOR 1.333, roughness 0.12). A `normalTexture` gives it ripples;
  `Terrain/Textures/water_ripple_normal.png` is AI Tree's.
- `ripples` `{ "velocity1": [u, v], "velocity2": [u, v], "strength": s }`: two
  drifting copies of the normal map, in repeats per second; `strength` (0 to 16)
  multiplies the normal scale.
- `tide`, one of:
  - `{ "range": m, "period": s, "phase": s }`: the surface rises and falls
    `range / 2` either side of the mean with simulation time (period default
    44712 s, a 12.42-hour semidiurnal tide).
  - `{ "start": "2026-06-21T19:00:00Z", "constituents": [{ "name": "M2",
    "amplitude": m, "phase": degrees }, ...], "source": "..." }`: a real tide
    predicted from a tide station's harmonic constituents (NOAA's amplitudes and
    Greenwich phases), with `start` the UTC time at simulation time 0 and the
    transform's y the station's mean sea level. `simulationTicksPerDay` is how
    many seconds of simulation time a day of tide takes (default 300, a day
    every 5 minutes); the rest of the scene keeps its own pace. `source` is a
    free-text note.
    `Tools/terrain/noaa_tide.py` writes this block.

Water draws like a primitive in raster and DXR, and casts no raster shadow.
Elevation surveys record water as a flat surface rather than its bottom, so for
tides import terrain with `--water-level` and, where there is any, bathymetry
(`Tools/terrain/README.md`). The `Hungerford` scene shows both, with the tide
at Arcadia, at the mouth of Totten Inlet.

Hungerford is also planted in a 300 m square about the 190 E Scott Dr driveway,
where it starts:
- the 701 trees LiDAR found there (`Terrain/Hungerford.trees.geojson`), each
  scaled to its measured height;
- 8,000 understory plants, 5,000 of them within 60 m of the start.

The plants are instances of `PNW Plants` species, written into the scene by
`Tools/terrain/place_plants.py` (objects `pnw-*` and `plant-*`), with leaner
materials so the scene stays within its 32 textures. It costs about 28 ms a
frame in raster (most of it the shadow cascades drawing the trees) and 21 ms in
DXR, from under 2 ms unplanted; see `Tests/Performance` (`Hungerford`,
`Hungerford-walk`).

### Instancing

An object with `instanceOf` copies an earlier object by ID. It sets only `id`,
`transform`, and optionally its own materials; everything else, including the
type, shape, generator, UV scale, animation speed, and depth LOD, comes from the
referenced object. An omitted transform is the identity, as for other objects.

```json
{ "id": "tree-2", "instanceOf": "tree", "transform": { "position": [4, 0, -1], "rotationDegrees": [0, 90, 0] },
  "materials": { "leaf": "autumn-needles" } }
```

- Primitive instances may set `material`. Tree, fixed-tree, and graph instances
  may set `materials.trunk` and/or `materials.leaf`; slots they omit keep the
  source's material. If the source has no materials, give both slots. glTF model
  instances use the model's own materials.
- `instanceOf` must name an object earlier in the file, and not the instance
  itself. An instance of an instance resolves to the original object. Any other
  property, including `type`, is an error naming that property.
- Tree-kind instances reuse the source tree's generated model and per-frame
  branch transforms. Each instance costs one transform and a few draw calls on
  the CPU, regardless of branch count, and grows with the source. The GPU applies
  the instance's transform to the source's branch data in raster, shadow, and
  ray-tracing passes. An instance chooses its LOD as a whole: full-detail sticks
  when its bounding box comes within the tree's first `lod` distance of the
  camera, otherwise the depth-only `depthLOD` rule. The source tree keeps its
  per-branch LOD.
- Primitive and model instances are ordinary objects built from the copied
  definition.
- Saving writes instances back in this compact form, including only the
  materials they override.

The `Instancing` scene shows tree, primitive, and model instances.

### Leaf atlases

A leaf texture can be an atlas: one image holding many leaf sprites. A scene's
`atlases` section names each sprite once, and a tree's `leaves` chooses which
ones its leaves use. The leaf material points at the atlas images as usual; the
layout applies to all of its maps, so color, normal, and roughness atlases must
share one layout.

```json
"atlases": {
  "plants-4": {
    "regions": {
      "lilac": { "rect": [0.0396, 0.4824, 0.1577, 0.7085], "stem": [0.1039, 0.7085] },
      "oak":   { "rect": [0.6055, 0.2212, 0.7534, 0.5117] }
    }
  }
},
"objects": [{
  "id": "shrub", "type": "tree",
  "materials": { "trunk": "bark", "leaf": "plants-4-leaf" },
  "leaves": { "atlas": "plants-4", "regions": { "lilac": 3, "oak": 1 } },
  "generator": { ... }
}]
```

- `rect` (required) is `[u0, v0, u1, v1]` in 0–1 UV measured from the image's
  top-left corner, so it does not depend on the image's resolution.
- `stem` is where the sprite attaches to its branch, in the same UV space, and
  must lie inside `rect`. It defaults to the bottom center of `rect`. The leaf
  quad is moved so this point sits on the branch.
- `aspect` (default `1`) is the image's width divided by its height. Each leaf
  keeps its region's shape: an `L(length)` leaf is `length` tall and as wide as
  its region's proportions require.
- `leaves.atlas` names the atlas. `leaves.regions` maps region names to positive
  weights; each leaf picks one at random in proportion to its weight. Omit
  `regions` to use every region of the atlas equally. The choice is repeatable:
  it comes from a hash of the leaf's branch and the object ID.
- `leaves.twistDegrees` (default `0`, up to `180`) rolls each leaf about its own
  axis by a random angle within plus or minus this value, so leaves stop facing
  the same way. `leaves.sizeVariation` (default `0`, up to `0.9`) scales each
  leaf's length by a random factor within 1 plus or minus this fraction. Both
  use the same repeatable hash as the region choice.
- Sprites must be upright, with the stem toward the bottom of the image. To use
  sideways sprites, rotate them while composing an atlas (below).
- Instances inherit their source's `leaves` and cannot change them.
- Trees without `leaves` draw the whole leaf texture on every leaf, as before.

Leaves sample their region in raster, shadow, and DXR passes.

#### Opacity micromaps

On GPUs with DXR tier 1.2, ray tracing uses opacity micromaps (OMMs) for
alpha-tested leaves. Each leaf triangle gets a map of 4,096 micro-triangles
(subdivision level 6) marked opaque, transparent, or unknown. The hardware
decides opaque and transparent hits itself, and only unknown micro-triangles,
along leaf edges, still run the any-hit alpha test.
- Maps are baked on the CPU when the scene loads, once per leaf texture, cutoff,
  UV transform, and atlas region. The bake uses the same mip-0 texels as the
  any-hit shader and is conservative: a micro-triangle is certain only when
  every texel its bilinear footprint can touch agrees. Images are the same with
  OMMs on or off.
- Only PNG and JPEG base-color textures are baked. DDS textures, alpha blending,
  and transmission keep the any-hit test.
- Ray tracing uses only the front quad of each alpha-masked leaf sprite. The
  back quad is coplanar, so either could win a hit. Back-face hits shade with the
  flipped normal instead.
- Turn them off with **Opacity micromaps** in the debug UI's Settings, which also
  shows how many micro-triangles are opaque, transparent, and left to any-hit.
  Render-test requests take `"opacityMicromaps": false`.

`Leaf Atlases` (`leaf-atlases.scene.json`) is a canyon plant showcase in Terra's
lighting: 12 species that grow together in a semi-arid sandstone canyon, laid
out from the river to the dry slope.
- Water's edge: sedge, common reed, scouring rush (horsetail), and seep fern.
- Bank: seepwillow, Goodding's willow, and Fremont cottonwood.
- Bench and slope: velvet ash, skunkbush sumac, Gambel oak, pinyon pine, and
  dry bunchgrass.

Its many leaves make DXR in Debug builds very slow, so the render regression
suite uses `Leaf Atlas Test` (`leaf-atlas-test.scene.json`) instead: the lighter
species from every atlas, with instances, close up. Both scenes are performance
tests (`Tests/Performance/scenes.json`), and `LeafAtlases-pan` measures frame
pacing while the camera moves through the canyon.

`Forest` (`forest.scene.json`) measures how rendering scales with tree count: six of
the canyon species as source trees, in a row in front of the camera, and 2,000
`instanceOf` copies on a jittered 270 m grid with random species, yaw and scale.
The sources use `"lod": {"distances": [10, 30, 42, 60, 85, 120, 170, 240],
"hysteresis": 0.1}`: past 30 m the distances grow by about √2 per level, so each
band's extra trees (area grows with distance squared) are offset by the halved
leaves, and distant instances draw reduced layouts. Thirty more non-instanced trees (copies of
the sources with their own pose) stand in the forest, so they draw their own
reduced layouts. It is generated by
`Tools/forest/make_forest.py` (`--count`, `--standalone`, `--spacing`, `--seed`,
`--lod`, `--reduction`, `--id`) from `leaf-atlases.scene.json`, so regenerate it after
changing those species. It is a performance test, not a render regression scene.

`PNW Plants` (`pnw-plants.scene.json`) holds the south Puget Sound species that
are placed on the Hungerford terrain, one specimen of each, full size, on a
meadow:
- Trees (back row): Douglas fir, western hemlock, western red cedar, red alder,
  bigleaf maple and Pacific madrone, with Forest's `lod` schedule.
- Understory (front): vine maple, red huckleberry, Himalayan blackberry, salal,
  Oregon grape, sword fern and pasture grass, with a patch of grass and fern
  instances as they will be scattered. These have no `lod`.
- From photos around 190 E Scott Dr:
  - a stand-grown Douglas fir (the bulk of the canopy there: a long clear
    trunk with dead stubs and a short crown at the top)
  - a bluff madrone of several red stems leaning out over the water
  - a lichen-hung standing dead alder
  - western red cedar and hemlock saplings coming up in the understory
  - bracken
  - oceanspray, with its cream plumes, on the bluff
  - tall orchard grass gone to seed in the clearings
  Flowers and seed heads are separate objects at the same place, as in Leaf
  Atlases.

Trees are drawn half a metre below their position, so every plant stands at
`y = 0.5` on the meadow at 0.

Every species is an L-system (`tree`), with its own rules for its growth habit:
- Conifers grow one whorl of four limbs per rewrite step, so lower limbs have
  grown longest and the crown tapers by itself.
  - Fir limbs turn up at the tips.
  - Hemlock limbs droop.
  - Cedar limbs are flared and hang their sprays.
- The broadleaf trees branch from a leader (alder) or from a forked trunk
  (maple, and madrone, which leans).
- The shrubs grow several stems from the base.

Leaves come from two new atlases in `Resources/Nature`, packed from the
Nature Pack by `pnw-atlases.json`:
- `pnw_conifer`: needle sprays, a lacy frond for cedar, and sword fern fronds.
- `pnw_broadleaf`: leaves standing in by shape for alder, maple, madrone, salal,
  Oregon grape, huckleberry and bramble.

Grass uses `canyon_grasses`. Materials tint the shared atlases and barks per
species (`baseColor` multiplies the texture). Photos of the real plants are to
replace these stand-ins.

The scene is generated by `Tools/forest/make_pnw_plants.py`, where the rules
live. Edit them there and re-run it; `--no-lod` writes every species at full
detail at any distance, for comparing them side by side. It is a render
regression scene (`pnw-plants`).

The canyon atlases are composed from several Nature Pack images, which keeps
the scene within its 32 texture slots: `canyon_leaves_*` and `canyon_grasses_*`
in `Resources/Nature`. `Resources/Nature/canyon-atlases.json` lists each
sprite's source image, rectangle, rotation, and relative size. Rebuild them with
`TreeEngine/Tools/pack-leaf-atlas.ps1 -Manifest <canyon-atlases.json> -SourceRoot <Nature_Pack_Textures> -PreviewDirectory <dir>`.
The tool keeps only each sprite's own connected pixels, turns sprites upright
(rotating normal-map vectors too), packs them at one texel density, bleeds
color into transparent texels so mipmaps do not darken leaf edges, and writes
each atlas's `regions` (`<atlas>.regions.json`) for the scene.

To find regions in a new atlas, run
`TreeEngine/Tools/find-atlas-regions.ps1 -Images <color map> -PreviewDirectory <dir>`.
It prints `rect` and `stem` for each sprite it finds in the alpha channel, and
draws the boxes on a preview image. Check the preview: packed sprites can merge
into one box, and the stem guess (the lowest opaque point) is wrong for
sideways or upside-down sprites.

## Materials

A material property can be a named reference such as `"material": "ground"`
or an inline definition. Trees use the same convention for their named slots.
References are resolved to independent values before runtime construction.

Materials use the same metallic-roughness BRDF that glTF models use, in both
raster and DXR. They work on primitives, on tree, fixed-tree, and graph
`trunk`/`leaf` slots, and through named references. The `PBR Materials` scene
uses every property.

```json
"chrome": { "baseColor": [0.95, 0.95, 0.95, 1], "metallic": 1, "roughness": 0.05 }
```

| Property | glTF source | Default | Range |
|---|---|---|---|
| `baseColor` | `baseColorFactor` | `[1,1,1,1]` | RGBA |
| `metallic`, `roughness` | `metallicFactor`, `roughnessFactor` | `1`, `1` | 0–1 |
| `specular`, `specularColor` | `KHR_materials_specular` | `1`, `[1,1,1]` | 0–1, nonnegative RGB |
| `ior` | `KHR_materials_ior` | `1.5` | `0` or ≥ 1 |
| `transmission` | `KHR_materials_transmission` | `0` | 0–1; above 0 alpha blends in raster |
| `alphaMode`, `alphaCutoff` | `alphaMode`, `alphaCutoff` | `opaque`, `0.5` | `opaque`, `mask`, `blend`; 0–1 |
| `unlit` | `KHR_materials_unlit` | `false` | cannot be combined with specular, IOR, transmission, or non-base maps |
| `textureCoordScale` | — | `[1,1]` | UV scale |

Defaults follow glTF, so an empty material is a rough white metal; set
`metallic: 0` for most surfaces.

Optional maps are `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture`,
`specularTexture`, and `specularColorTexture`. Each has a required `source` and
optional `wrap` (default `true`; `false` clamps) and `transform` (`offset`,
`rotation` in radians, and `scale`, as in `KHR_texture_transform`).
`normalTexture` also takes `scale` (default `1`, nonnegative). Maps may be PNG,
JPEG, or DDS (glTF assets themselves remain PNG/JPEG only); base-color and
specular-color maps are sRGB, and the others are linear. A scene can load 32
distinct textures, counting every map, model texture, and default texture;
past that, loading fails with `E_BOUNDS`. Emissive, occlusion, and other glTF
extensions are not supported, as for models.

Asset references resolve relative to the scene file first, with existing
current-directory, executable-directory, and development-resource fallbacks.
This applies to texture maps, graph data, and model sources. Missing assets
report the object ID without replacing the active scene. Saves rebase references
relative to the destination scene; resources are not copied or packaged.
Cross-drive/share references that cannot be made relative are rejected. Keep
shared runtime resources available when moving user scenes.

Object and material UV scales multiply; `[4,4]` repeats the texture four times in
each axis. Negative UV scale mirrors. Runtime material creation uses copies so
rebuilding does not compound UV scales. Saving folds a material UV scale into
each map's `transform`.

Primitives and trees authored without materials get defaults: a rough green
primitive over `snow.dds`, and a red-tinted `Bark_0005_diffuse.dds` trunk with
dark green leaves.

### Converting Phong materials

Older scene files used Phong fields (`ambient`, `diffuse`, a `specular` color,
`reflect`, `texture`, `alphaClipThreshold`, `flags`) and later a nested `pbr`
block. Both are rejected with an error naming the field. For a `pbr` block, move
its properties up into the material. For Phong materials, PBR lighting runs in
linear space and divides direct diffuse light by π, so a literal conversion
looks darker. The built-in scenes were converted this way:

- `diffuse` becomes `baseColor`, converted from display to linear values
  (for example `0.5` becomes about `0.22`). `metallic` is `0`.
- The specular exponent (`specular[3]`) maps to roughness: `1` → `0.8`, `4` → `0.6`.
- `texture` becomes `baseColorTexture`; `alphaClipThreshold` becomes
  `alphaMode: "mask"` with that `alphaCutoff`.
- Ambient-only materials, such as axis gizmos, become `unlit` with the color.
- When every lit object in a scene is PBR, multiply the scene light's `diffuse`
  by π. Scenes that also contain glTF models keep their light, because the models
  were already lit physically.
- PBR ambient is `0.15 × baseColor × light ambient`. A Phong material with a high
  `ambient` needs a brighter scene ambient instead (Curves uses `4`).
- Colored Phong highlights (`specular` tints) have no PBR equivalent; tint
  `baseColor` instead.

## L-systems

`generator` requires a nonempty `axiom`. It supports `constants`, `rules`,
`iterations`, `angle` (radians), `segmentLength`, `thickness`, `initialDirection`,
and `segmentLengthBehavior`.

Rules have required `input` and `output` strings and optional `iterations`,
matching the existing generator's per-rule iteration behavior. Scene iterations
and per-rule iterations range from `0` to `20`. Segment length and thickness
must be positive. Initial direction must be nonzero.

Defaults are zero iterations and angle, segment length `0.01`, thickness `0.02`,
initial direction `[0,1,0]`, no constants or rules, and constant segment length.
`segmentLengthBehavior` is `constant` or `randomAddition`; the latter adds a
random value from zero to `0.2`, matching the existing sea-scene callback.

Rule parameters support one operation each, `k * s` or `s + k`: a bare `s`
evaluates to 0 (write `s * 1`), and `a * s + b` gives a wrong value. Each `F`
and `L` adds one branch; models are sized to fit, so large trees are fine.

## Validation tests

`SceneDefinitionTests` is included in `TreeEngine.sln` and builds with the
desktop Debug/x64 solution configuration. In Visual Studio,
set it as the startup project and use Start Without Debugging (Ctrl+F5).
The build copies all catalog JSON into `Tests\bin\x64\Debug\TestData`. The test executable
locates that data relative to itself and can run from any working directory.

Build through `TreeEngine\TreeEngine.sln` from the Git checkout root with MSBuild
(`Debug`, `x64`) and run
`TreeEngine\Tests\bin\x64\Debug\SceneDefinitionTests.exe`.
Its intermediates are under `Tests\obj\x64\Debug` relative to the solution
directory; the old `Tests\build` directory is no longer used.
The tests exercise defaults, references, transforms, rule settings, malformed
input, material omission semantics, PBR material parsing/validation/round trips,
the shared glTF material mapping, fixed trees, graphs, every catalog scene,
and preservation of the AI Tree scene data.

Use `SceneDefinitionTests.exe --persistence-only` to run just the CPU serialization,
atomic writing, Unicode-path, discovery, identity-conflict, and overwrite checks.
For live capture/save/reload/restart and rendered-appearance checks, use
`Tests\RenderRegression\test-scene-persistence.ps1` from the solution directory;
see the render regression README.

The JSON parser is the vendored nlohmann/json 3.11.3 single header under
`Game/ThirdParty/nlohmann`, with its MIT license.

## DXR contact shading

The optional render.ambientOcclusion object accepts radius (nonnegative world
distance, default 0, disabled) and strength (0 to 1, default 1). Nearby opaque
geometry blocks ambient diffuse light, preserving direct sun and specular
reflections. Transmissive surfaces do not receive the effect. Raster ignores it.
Sixteen deterministic hemisphere probes per opaque hit add GPU cost; use a radius
near the size of stones or crevices. Settings survive scene save/reload.

Example: "ambientOcclusion": { "radius": 1.25, "strength": 0.65 }

## Animated model normal maps

A model can specify "normalAnimation": {"material": "material name", "velocity1": [0.012, 0.006], "velocity2": [-0.005, 0.009]}. The name must uniquely identify a glTF material with a repeating normal map. Velocities are UV units per simulation second; two moving samples are blended. Optional strength multiplies the imported normal scale (0 to 16, default 1). The debug UI Water motion speed slider scales the animation and zero freezes its current phase. The effect uses scene time in raster and DXR and pauses with the simulation. Omit the property for static normals.

A model can also specify "materialTint": {"material": "material name", "texture": "Terrain/tint.png", "uvScale": [su, sv], "uvOffset": [ou, ov]}. The texture multiplies that glTF material's base color by 2 x its RGB, so 0.5 leaves the color unchanged; it is read as linear data. Its UV is the model's UV times uvScale plus uvOffset (defaults 1 and 0), so a low-resolution tint can vary a tiled texture over a whole surface. Terra's terrain uses one to keep its baked weathering as broad light and dark variation over the tiled rock photoscan (`Tools/terra/fix_terra.py`).

A model can also specify "biplanar": {"material": "material name", "uvPerUnit": 0.0483, "sharpness": 1} for a surface whose UVs are a top-down planar projection, such as Terra's terrain. On steep faces a top-down projection stretches the texture into vertical streaks, so that material's base color, normal, metallic-roughness and specular maps are also projected along the object's X and Z axes at uvPerUnit UV units per object-space unit (match the top-down projection's scale; required, positive), each through the material's own texture transform. Every pixel blends the two projections its normal leans on most, the model's own UVs serving as the Y projection, so flat ground keeps its current look and each map is read at most twice. Optional sharpness (greater than 0, at most 16, default 1) is the exponent on the blend weights; higher values narrow the transitions. The tint and normal animation still use the model's UVs only. Raster and DXR match.

## Alpha-tested surfaces

Materials with `alphaMode: "mask"` (leaves, needles) are opaque where alpha reaches `alphaCutoff`. Ray tracing
tests the full-resolution texture, so distant foliage keeps its true density. Raster samples distant textures from
mipmaps, where averaged alpha crosses a fixed cutoff unevenly: foliage would thin out, or with a cutoff of `1` all
but vanish. So once a texture is minified, raster compares alpha with a random threshold fixed to each point of
the surface instead, covering a pixel with probability equal to its averaged alpha (a hashed alpha test). Raster
then matches ray tracing at any distance, while close-up cutouts keep their exact `alphaCutoff` shape. Distant
foliage looks slightly speckled; temporal anti-aliasing will smooth that. **Hashed alpha test** in Settings turns it
off. PNG textures get their mipmaps from an area-weighted filter that keeps each level's average alpha.

## Anti-aliasing

Anti-aliasing is an app setting, not part of a scene: **Anti-aliasing** in the debug UI's Settings, saved
with the debug UI's preferences. It works on the final image, so it applies to raster and DXR alike.
- `SMAA 1x` finds edges in the color image and blends across them: about 0.03 ms at
  1280x720 on an RTX 5080. **Show SMAA edges** draws the detected edges, horizontal in red and vertical
  in green, over the dimmed scene.
- `TAA` (the default; temporal anti-aliasing, `Game/Shaders/Taa.hlsl`) moves the camera by a sub-pixel offset every frame
  and accumulates the frames along the motion vectors, so each pixel averages several samples. Unlike SMAA it
  also resolves detail smaller than a pixel: thin branches and needles, the distant foliage speckle of the
  hashed alpha test, specular glints and ray-traced noise. Last frame's result is clipped to the colors around
  each pixel now, which keeps moving and uncovered edges from ghosting. It settles in a few frames after a
  camera cut and is slightly softer than SMAA.
- `None` turns it off.

SMAA and TAA run before the post-processing blur (`blurAmount`). The blur also softens jagged edges, so with
anti-aliasing on, a lower `blurAmount` keeps more texture detail.

Upscalers (DLSS, FSR) are planned on the same inputs as TAA. The scene pass writes
each surface's motion, from its position last frame (growing trees, moved and instanced objects) and last
frame's camera. A compute pass (`Game/Shaders/MotionVectors.hlsl`) fills the sky from the camera alone. Blended
surfaces that don't write depth, like water, keep the motion of what's behind them, as depth does. Raster
always draws the scene, so DXR uses the same vectors. **Motion vectors** in Settings shows them:
- `Motion`: direction as hue, length as brightness (16 pixels or more is full brightness).
- `Reprojected history`: last frame's image fetched along the motion vectors. While the camera moves, it
  should look like the live image, except at edges the move uncovered.
- `History`: last frame's image unmoved, for comparison.

The compute pass runs while TAA or a view is on; the scene pass always writes velocity. The camera jitters only
with TAA.

## HDR and tonemapping

**HDR** in Settings (on by default) renders the scene as linear light into a 16-bit float target instead of
each shader encoding its own 8-bit display output, then maps it to the display at the end of the frame
(`Game/Shaders/Tonemap.hlsl`), after TAA and before SMAA, the blur and the UI:
- **Exposure (EV)** scales the light by 2^EV first.
- **Tonemap** chooses how light above 1 reaches the display. `Clamp` clips it, as the 8-bit path does, so HDR
  with Clamp at EV 0 looks like HDR off. `ACES` (Stephen Hill's fit) and `AgX` roll highlights off instead;
  AgX keeps saturated highlights such as sunlit foliage from shifting hue.

With HDR, ray-traced reflections and transmission add up in linear light, and blended surfaces blend in
linear light without clipping first, so water reflects more of the sky than in the 8-bit path. TAA resolves the
linear image. Upscalers (DLSS, FSR) will take this HDR image as their input.

## Sun shadow softness

The directional light can specify "angularDiameterDegrees" (0 to 10, default 0) and "shadowSamples" (1 to 32, default 8). A positive angle with more than one sample traces the apparent sun disk in DXR; zero keeps hard shadows. The angle is the sun's full angular diameter. Raster shadow maps retain their existing filtering. Soft sun sampling applies to directly visible surfaces; secondary reflection and transmission hits use a single shadow ray to limit cost. More samples increase ray tracing cost.

L-system rotations support an optional second parameter for symmetric pseudo-random
angle spread: Z(38,8) samples 30 through 46 angle units independently for each
expanded occurrence. The spread uses the same generator.angle units as the first
parameter. A fixed local seed makes regeneration repeatable; one-parameter
rotations are unchanged. Use a nonnegative spread. X(0,10) adds a small random tilt.

F and L also accept an optional fractional length spread: F(0.5,0.2) varies
length from 0.4 to 0.6 per occurrence, using a separate fixed local seed.
Use a spread from 0 to less than 1 to retain positive lengths.
