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
- `objects`: nonempty array of typed object definitions with unique nonempty `id` values.

Unknown properties are errors, including typos. Errors name the source file and
property path. Colors are RGBA arrays and may contain HDR values. Vector values
must be finite. Object scales must be nonzero; negative scale mirrors.

## Objects and transforms

Every object supports `transform`, `textureCoordScale`, `animationSpeed`, and
`depthLOD`. Defaults are position `[0,0,0]`, scale `[1,1,1]`, quaternion rotation
`[0,0,0,1]`, UV scale `[1,1]`, animation speed `1`, and depth LOD `4`.

Use `rotationDegrees: [pitch,yaw,roll]` for convenient Euler authoring, or
`rotation: [x,y,z,w]` for a quaternion. Specify only one. Quaternions are preserved
as authored, including the existing AI Tree camera rotation.

A `primitive` supports `shape` and `material`. Supported shapes are
`box`, `cylinder`, `cylinderLD`, `cylinderHD`, `skinnedCylinder`, `sprite`, and
`sphere` (unit diameter, like the box and cylinders). The default shape is `box`.

A `tree` supports `trunkShape` (`skinnedCylinder` by default, or `cylinder`),
named `materials.trunk` and `materials.leaf`, and a required `generator`.
The generator is currently an L-system.

A `fixedTree` creates the existing fixed procedural tree generator and supports
`trunkShape` (`skinnedCylinder` by default, or `cylinder`) and the same optional
`materials.trunk`/`materials.leaf` slots as a tree.

A `graph` requires `dataFile`, a CSV file containing the graph samples. Its
normalization matches the former `LoadGraph` behavior.
It also supports optional `materials.trunk`/`materials.leaf` slots.

A `model` requires `source`, a `.gltf` or `.glb` file, and supports optional
`forceLit` plus its transform. Its asset owns its geometry and materials.

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
  when its bounding box comes within 10 units of the camera, otherwise the
  depth-only `depthLOD` rule. The source tree keeps its per-branch LOD.
- Primitive and model instances are ordinary objects built from the copied
  definition.
- Saving writes instances back in this compact form, including only the
  materials they override.

The `Instancing` scene shows tree, primitive, and model instances.
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
specular-color maps are sRGB, and the others are linear. The renderer has eight
material texture slots shared by every texture in a scene, so heavily mapped
scenes can fail to load with `E_BOUNDS`. Emissive, occlusion, and other glTF
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
