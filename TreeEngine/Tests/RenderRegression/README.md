# Render regression tests

From the TreeEngine directory, use Windows PowerShell 5.1:

```powershell
# Build the D3D12 game and compare every scene in both modes.
.\Tests\RenderRegression\run-render-tests.ps1 -Build

# Compare an existing build, or select scenes/modes.
.\Tests\RenderRegression\run-render-tests.ps1
.\Tests\RenderRegression\run-render-tests.ps1 -Scenes ai-tree,trees -Modes raster

# Explicitly replace gold images after reviewing an intentional visual change.
.\Tests\RenderRegression\run-render-tests.ps1 -UpdateGold

# Check the comparison math independently of the renderer.
.\Tests\RenderRegression\test-image-comparison.ps1

# Verify DXR on/off/on in one process, with and without postprocessing.
.\Tests\RenderRegression\test-dxr-toggle.ps1

# Resize the live window in raster and DXR, with and without postprocessing.
.\Tests\RenderRegression\test-window-resize.ps1

# Toggle the shadow-map preview off/on/off in both rendering modes.
.\Tests\RenderRegression\test-shadow-buffer-toggle.ps1

# Exact cached/recomputed image parity through growth, mature time, LOD, and rewind.
.\Tests\RenderRegression\test-tree-skinning.ps1

# Also test exactly 3,000,000 DXR vertices and explicit overflow failure.
# Requires a Release build for the expected overflow case.
# Copies the deployed runtime into the ignored test artifacts directory.
.\Tests\RenderRegression\test-tree-skinning.ps1 -Capacity

# Camera motion vectors and jitter in LeafAtlasTest and AI Tree, raster and DXR.
.\Tests\RenderRegression\test-motion-vectors.ps1

# Edit, save, reload, restart, and compare scene appearance in both modes.
# Uses an isolated runtime copy; never modifies the deployment's UserScenes.
.\Tests\RenderRegression\test-scene-persistence.ps1

# Sample 120 warmed-up frames per capture without timing PNG readback/encoding.
.\Tests\RenderRegression\run-render-tests.ps1 -Scenes test-tree,trees -BenchmarkFrames 120
```

Requires Visual Studio C++ build tools, the game's existing dependencies/resources,
and a Windows GPU supporting the game's D3D12/DXR renderer. This suite uses the
TreeClassic D3D12 build. The default executable is
`TreeClassic\bin\x64\Debug\TreeClassic.exe` relative to the solution directory
(`TreeEngine` within the checkout); use `-Executable` to select another compatible
build, such as `TreeClassic\bin\x64\Release\TreeClassic.exe`.

Desktop binaries and intermediates are project-local under
`bin\x64\<Configuration>` and `obj\x64\<Configuration>`. Run the app from its
TreeClassic bin directory, where the build deploys the renderer DLL, shaders,
resources, and runtime dependencies. The debugger defaults to `$(OutDir)`; if an
existing ignored `TreeClassic.vcxproj.user` still names the old external binary
directory, remove that working-directory override or update it to `$(OutDir)`.
External Binaries/Intermediates directories are no longer used by the maintained
desktop builds. Do not remove shared legacy outputs; any cleanup requires
inspection and separate approval of the exact generated subdirectories.

`scenes.json` lists fourteen scene entries, both rendering modes, and captures at
0, 1, and 5 seconds (84 images). It also specifies resolution, random seed,
postprocessing, warmup frames, and tolerances. An entry may set `"antialiasing":
"smaa"` to capture with SMAA 1x (`leaf-atlas-test-smaa`, `tree-smaa`); other entries
render without anti-aliasing, whatever the app's own setting. A request's `alphaTest` is `"hashed"` (the default, like the app)
or `"cutoff"` (each material's alpha cutoff). Scene IDs refer to this manifest;
names select the legacy built-in IDs discovered from each scene's JSON metadata.
User scenes cannot override these IDs. Catalog display order does not affect
regression selection.

Captures can also test motion vectors. A capture's `cameraRotationDegrees` (pitch, yaw, roll; it needs
`cameraPosition`) sets the camera's rotation. `previousCameraPosition` and `previousCameraRotationDegrees`
render the warm-up frames from another camera, so the captured frame has known motion. `debugView` captures
a motion debug view instead of the scene: `"motion"`, `"reprojection"` (last frame's color fetched along the
motion vectors) or `"history"` (last frame's color unmoved). `jitter: [x, y]`, on a request or a capture,
offsets the camera by a fixed sub-pixel amount, in pixels. `previousTime` renders the warm-up frames at an
earlier simulation time, so growing trees move. `test-motion-vectors.ps1` checks that:
- reprojection is much closer to the new frame than the unmoved history, for sideways, forward and yaw moves;
- an unmoved camera gives exactly zero motion;
- `jitter: [1, 0]` shifts the image one pixel right;
- growing trees (LeafAtlasTest; Instancing's shared layouts) reproject much better than unmoved on the pixels
  that moved, and show exactly zero motion when nothing changed between frames.

`test-scene-persistence.ps1` covers primitives, L-system/fixed trees, graphs,
glTF assets, and scene-authored PBR materials (`PbrMaterials`: a mapped sphere
and a tree with PBR trunk/leaf slots), and instancing (`Instancing`, which must
save back in `instanceOf` form) in raster and DXR modes, using the same
0.002 RMS tolerance.
It copies the deployed runtime (excluding existing UserScenes) into a uniquely
named ignored artifact directory and runs from a different working directory.
It checks live camera, lighting, material, transform, and rule edits; Save As
without resetting time; reload; declined/confirmed overwrites; invalid captured
state; and failed loads retaining the active scene. It also compares no-edit
save/reload images and restarts a worker to discover/load the saved user file.
Use `-Scenes LoadGraph,LoadGltfDemo`, `-Modes raster`, or `-Executable` to narrow
or select another build. Artifacts include before/after/restart images, diffs,
serialized scenes, requests, and worker reports.

The native capture request enables these opt-in runtime checks with
`"validateScenePersistence": true`; success adds `scenePersistenceValidated`
to its report. Alongside the normal report it writes `.before.png`, `.after.png`,
and `.scene.json` sidecars. The check temporarily creates a uniquely named user
scene and cleans up only its own file; run through the isolated script above.

Each scene/mode runs in a separate hidden game process with the scene's initial
camera and lights, no input or debug UI, and synchronous object updates. The
simulation/shader time is set directly for each sample, with three warmup frames
at that same time before capture. This tests the current time-driven scenes;
future simulations that integrate state over elapsed frames will need a fixed
step progression. Screenshots are captured from the completed D3D12 backbuffer
before presentation. Raster uses the normal raster display. Raytracing enables
DXR and replaces the main color image with its output before postprocessing and
UI. Gold from the previous DXR debug panel must be regenerated for this change.

The comparison decodes PNGs and computes RGB RMS over all pixels and channels:
`sqrt(mean((actual - gold)^2)) / 255`. Alpha is ignored. The default RMS tolerance
is 0.002 (about 0.51 channel levels on the 0–255 scale). Maximum channel error is
also reported, with an optional independent threshold; the default 1.0 imposes
no additional maximum-error restriction. Dimension mismatches, missing gold,
incompatible capture settings, worker errors/crashes, and timeouts fail the run.
The script returns exit code 0 on success and 1 for test failures.

Each run writes `Artifacts/<UTC timestamp>/report.json`, actual PNGs, difference
PNGs (absolute differences amplified four times), requests, worker reports, and
stdout/stderr logs. Artifacts are ignored by Git. Worker stderr records the last
frame attempted to help diagnose crashes. Reports include executable hash,
capture settings, RMS, maximum error, and changed pixel counts.

Gold PNGs and their settings sidecars are in `Gold/`. Initial captures are
provided as a starting baseline; review them visually before accepting them as
the desired appearance. PNGs use the repository's existing Git LFS rules. A
normal run never creates or updates gold. `-UpdateGold` replaces captures that
complete successfully and still reports failed jobs; it can partially update a
baseline if other jobs fail. After an intentional change, inspect the output,
update only affected scenes/modes, and rerun comparison before committing gold.
Keep baseline generation and comparison on the same GPU/driver/build settings
where possible; different hardware can produce legitimate pixel differences.

DXR uploads only live draw records through frame-owned mapped upload buffers
on the existing command list; it no longer creates a full-capacity temporary
upload and waits for a separate upload batch every frame. Existing frame fences
protect upload-buffer reuse. Deformation dispatches 128-thread groups with one
writer per unique instance/vertex, rather than repeatedly transforming index
entries. Capacity checks use one generated glTF mesh at the 3,000,000-vertex limit and one vertex over.
The expected overflow is tested in Release to avoid a headless Debug assertion dialog.

The native capture request also accepts an optional `mode` on each capture
point, overriding the request mode. `test-dxr-toggle.ps1` uses this to switch
raytracing/raster/raytracing in the same process at a fixed time, checks that the
two DXR images match, and verifies that disabling DXR restores raster output.
It covers scenes with and without a skybox, and compares the Tree scene skybox
region against raster output to verify its texture and orientation.
For scenes without a skybox, DXR ray misses use the scene's clear color. The
toggle test also checks that this background matches raster output.

## Profiling and pose-cache validation

During normal interactive use, expand **Profile** in the Debug UI (a peer of
Stats, Controls, and Scene) and check **Enable profiling**. It defaults off
and takes effect at the next frame boundary; collapsing the section or hiding
the Debug UI does not disable an enabled profiler. Uncheck it to stop collection.
The section shows the last completed frame's CPU/GPU timings, work counters,
adapter, and resource memory counters. CPU scene timing includes the normal
threaded update and worker completion wait; tree counters are aggregated after
workers finish. GPU timings use the existing frame fence, not an extra wait.
Timers, queries, and counter aggregation still add profiling overhead.

`-BenchmarkFrames` forwards the optional native request field `benchmarkFrames`
(0 through 10,000; default 0 disables profiling). Each capture runs its warmup,
then timed frames, then a separate screenshot frame. Samples are in each worker's
`capture-report.json`, not the top-level image comparison report. These are
deterministic **synchronous** scene updates, not the normal asynchronous update
path. `frameMs` includes the renderer's existing frame fence/presentation
overhead; it is not isolated GPU time. GPU timestamps are read after the
existing completion fence; profiling introduces no additional per-frame wait.
For repeatable measurements across commits, use the performance pass in
`Tests/Performance` instead. It sends a `performance` request (frames,
`frameTimeStep`, `startTime`), which profiles every frame with all worker
threads, writes no images, and also reports scene load time.

The native `--render-test <request.json>` request also accepts:

- `benchmarkTimeStep`: finite nonnegative seconds, default 0. Warmup holds the
  requested time; timed frames advance from that time by this step. The final
  screenshot returns to the original requested time and is not a timed sample.
- Per-capture `showShadowBuffer`: default false. Shows the shadow-map preview for that capture.
- Per-capture `poseCache`: default true. False forces the CPU reference pose to
  be recomputed, allowing same-process A/B comparisons.
- Per-capture `cameraPosition`: three finite coordinates. If omitted, the
  scene's original camera position is restored; orientation is unchanged.
- Per-capture `width` and `height`: both must be present, from 32 to 2048.
  The hidden test window and renderer resize before that capture; omitted sizes
  use the request's initial dimensions.

Example request fields (alongside the normal scene, seed, size, report, etc.):

```json
{
  "benchmarkFrames": 120,
  "benchmarkTimeStep": 0.016666666666666666,
  "warmupFrames": 15,
  "captures": [
    { "time": 60, "poseCache": false, "filename": "reference.png" },
    { "time": 60, "poseCache": true, "filename": "cached.png" }
  ]
}
```

Each sample reports `sceneUpdateMs`, CPU draw-record `drawUploadMs`, and
`gpuMs` ordered by the report's `gpuPasses`: frame, shadow, main, deformation,
BLAS, TLAS, rays. Passes are not exhaustive or additive to the whole frame;
inactive passes report zero. Counters include draw records, world/skinned
vertices, source index entries, dispatch groups, rebuilt/reused pose branches,
and whether the BLAS was rebuilt. Memory counters distinguish CPU pose-cache
bytes, live draw upload bytes, reserved upload bytes, reserved world-vertex
bytes, and AS plus scratch bytes. These are resource/capacity counters, not a
process-wide peak-memory profiler.

The pose cache uses stable generated branch IDs independently of packed draw
offsets. Growth changes, rewind, placement/rotation changes, and initialization
invalidate it. Once fully mature, advancing time can reuse poses; a stationary
camera can also reuse packed draw instances. Camera/LOD changes still repack
instances and parent offsets. This does not remove normal instance uploads or
move procedural growth to the GPU.

`test-tree-skinning.ps1` compares cached and recomputed output pixel-for-pixel:
three tree scenes (including `Instancing`), both modes, nine states, 108 captures. It checks growth and
rewind invalidation, mature reuse while time advances, and near/far camera
movement across LODs. Optional `-Capacity` uses a compact grammar to produce
10,000 skinned cylinders (600,000 vertices), then 10,001 (600,060). The first
must render; the second must report `ERROR_INSUFFICIENT_BUFFER` (HRESULT
2147942522), not crash or succeed with missing geometry.

It also checks that instances add no CPU pose work: `Instancing` must rebuild
exactly as many branch poses as the same scene with its instances removed.

### Measured scope and remaining work

Local measurements used an NVIDIA GeForce RTX 4060 Laptop GPU, driver
32.0.15.9155, and i7-13800H on a shared machine. Compare equivalent optimized
Release builds; shader `/O3` versus `/Od` alone showed no clear GPU-stage gain.
At five seconds, removing the separate draw-record upload wait and duplicate
vertex work gave these medians (milliseconds):

| Scene | CPU draw upload, before / after | GPU DXR deformation, before / after |
|---|---|---|
| Trees | 4.209 / 0.026 | 0.229 / 0.053 |
| AI Tree | 6.032 / 0.041 | 0.533 / 0.164 |

For mature trees with a fixed camera, three interleaved cache-off/on trials
each sampled 120 frames while simulation time advanced by 1/60 second from
60 seconds, after 15 warmup frames. Median of trial-median CPU scene updates:

| Scene / mode | Recompute / cached (ms) |
|---|---|
| AI Tree / raster | 1.694 / 0.257 |
| AI Tree / DXR | 2.294 / 0.355 |
| Trees / raster | 1.340 / 0.086 |
| Trees / DXR | 1.635 / 0.104 |

All 12 A/B screenshot pairs were pixel-identical. These CPU gains apply to
mature stationary-camera reuse, not arbitrary growing/moving scenes. Frame
wall times were noisy; no hardware-independent total-frame speedup is claimed.
For the mature DXR samples, CPU pose storage was 401,472 bytes (AI Tree) and
234,532 bytes (Trees); live draw uploads were 53,988 and 660 bytes respectively,
with 270,336 bytes reserved for uploads. AS plus scratch remained 7,234,560 and
30,232,192 bytes respectively.

The selected work implements measurement, DXR work/upload reduction, and CPU
pose/draw reuse. Shared pre-skinning across shadow/main/DXR, GPU growth,
local-space BLAS/compatible variants, compression, and staggered AS refresh
remain **deferred**, not implemented. Remaining measured DXR deformation is
0.053-0.164 ms in these scenes; a broader cache adds traffic and needs a
representative forest workload and target to justify it. No quality defaults
were changed and no forest-scale or normal asynchronous CPU result is claimed.

### Baseline status

All gold images were regenerated when the built-in scenes moved from Phong to
PBR materials; see the Scenes README for how materials and lights were converted.
`Instancing` gold images were regenerated when 32-bit BGRA PNGs (most PNGs)
started getting mipmaps; before that, DirectXTK skipped their mip generation on
desktop GPUs.

`LeafAtlasTest` covers atlas leaves: a compact subset of the canyon plants in
`Leaf Atlases`, using both canyon atlases, the pinyon atlas, leaf twist and size
variation, and tree instances. The full `Leaf Atlases` showcase is not in this
manifest because its many leaves make DXR in Debug take minutes per frame; it
is measured by the performance pass instead.

As of 2026-11-01 the full Debug manifest passes 64 of 72 images. These eight
fail with or without the leaf-atlas and mipmap changes:
- `tree` ray tracing at 1 and 5 seconds (RMS about 0.010 and 0.016).
- `ai-tree` raster and ray tracing at every time (RMS about 0.32 and 0.29),
  because `ai-tree.scene.json` and `terra.glb` changed after its gold images
  were made. Before the mipmap change, ai-tree ray tracing stopped at a D3D12
  debug-layer error break instead of capturing.
`test-dxr-toggle.ps1` covers SimpleBox and Tree only.
