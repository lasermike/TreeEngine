# Scene definitions

All nine catalog scenes are defined by `*.scene.json` files in this directory.
The catalog in `GameLoader.cpp` determines scene order, loader IDs, display names,
and filenames. Existing scene indexes and loader IDs remain unchanged.

Edit the source JSON here and build TreeClassic to copy it into the executable's
`Scenes` folder. During development you can also edit the deployed JSON directly
and cycle away from a scene and back to reload it. Live rule editing continues
to affect runtime parameters; it does not save changes to JSON.

The loader accepts a catalog name (`AI Tree`), loader ID (`LoadAITree`), or JSON
filename through its string overload. Failed scene switches keep the previous
scene and show an error in the debug Scene panel. Initial load failures are
reported to the debugger and returned to application initialization.

Scene loaders produce a `SceneRenderSettings` value containing background and
lighting values. On successful activation, `RenderManager::ApplySceneSettings`
applies it without changing renderer resources or frame state. Scene time resets
separately during activation. All catalog scenes use this path.

## Structure

- `version`: required schema version, currently `1`.
- `name`: required nonempty display name.
- `camera`: transform with optional `position`, `rotation`, and `scale`.
- `render`: optional `clearColor`, `farClippingPlane`, `useShadowMaps`, and `useAlphaBlendedRenderTarget`. The far plane defaults to 30 and must exceed the 0.2-unit near plane.
- `light`: optional directional-light `ambient`, `diffuse`, `specular`, and `direction`.
- `pointLightCount`: legacy point-light activation, `0` or `1`; point-light authoring is not migrated yet.
- `materials`: map of names to material definitions.
- `objects`: nonempty array of typed object definitions with unique nonempty `id` values.

Unknown properties are errors, including typos. Errors name the source file and
property path. Colors are RGBA arrays and may contain HDR values. Vector values
must be finite. Object scales must be nonzero; negative scale supports skyboxes.

## Objects and transforms

Every object supports `transform`, `textureCoordScale`, `animationSpeed`, and
`depthLOD`. Defaults are position `[0,0,0]`, scale `[1,1,1]`, quaternion rotation
`[0,0,0,1]`, UV scale `[1,1]`, animation speed `1`, and depth LOD `4`.

Use `rotationDegrees: [pitch,yaw,roll]` for convenient Euler authoring, or
`rotation: [x,y,z,w]` for a quaternion. Specify only one. Quaternions are preserved
as authored, including the existing AI Tree camera rotation.

A `primitive` supports `shape`, `cubeMap`, and `material`. Supported shapes are
`box`, `cylinder`, `cylinderLD`, `cylinderHD`, `skinnedCylinder`, and `sprite`.
The default shape is `box`. Cube maps require a DDS texture.

A `tree` supports `trunkShape` (`skinnedCylinder` by default, or `cylinder`),
named `materials.trunk` and `materials.leaf`, and a required `generator`.
The generator is currently an L-system.

A `fixedTree` creates the existing fixed procedural tree generator and supports
`trunkShape` (`skinnedCylinder` by default, or `cylinder`).

A `graph` requires `dataFile`, a CSV file containing the graph samples. Its
normalization matches the former `LoadGraph` behavior.

## Materials

A material property can be a named reference such as `"material": "ground"`
or an inline definition. Trees use the same convention for their named slots.
References are resolved to independent values before runtime construction.

Materials support `ambient`, `diffuse`, `specular`, `reflect`, `texture`,
`textureCoordScale`, and `alphaClipThreshold`. The fourth `specular` component
is the specular exponent. Color defaults match `ShaderMaterial`: `[0,0,0,1]`.
An omitted texture means untextured shading. Filenames refer to runtime resources
copied next to the executable, as they did in the original loaders.
The adapter checks the current directory, executable directory, and development
resource directories. Missing textures report the object ID before replacing
the active scene.

Alpha clip defaults to `0` and must be between `0` and `1`. UV scale defaults to
`[1,1]`. Object and material UV scales multiply; `[4,4]` repeats the texture four
times in each axis using the existing wrap sampler. Negative UV scale mirrors.
Runtime material creation uses copies so rebuilding does not compound UV scales.

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
desktop Debug/x64 and Debug12/x64 solution configurations. In Visual Studio,
set it as the startup project and use Start Without Debugging (Ctrl+F5).
The build copies all catalog JSON into `Tests\bin\x64\Debug\TestData`. The test executable
locates that data relative to itself and can run from any working directory.

Build through `TreeEngine\TreeEngine.sln` from the Git checkout root with MSBuild
(`Debug`, `x64`) and run
`TreeEngine\Tests\bin\x64\Debug\SceneDefinitionTests.exe`.
Its intermediates are under `Tests\obj\x64\Debug` relative to the solution
directory; the old `Tests\build` directory is no longer used.
The tests exercise defaults, references, transforms, rule settings, malformed
input, material omission semantics, fixed trees, graphs, every catalog scene,
and preservation of the AI Tree scene data.

The JSON parser is the vendored nlohmann/json 3.11.3 single header under
`Game/ThirdParty/nlohmann`, with its MIT license.
