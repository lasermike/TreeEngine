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

# Exact cached/recomputed image parity through growth, mature time, LOD, and rewind.
.\Tests\RenderRegression\test-tree-skinning.ps1

# Also test exactly 600,000 DXR vertices and explicit overflow failure.
# Copies the deployed runtime into the ignored test artifacts directory.
.\Tests\RenderRegression\test-tree-skinning.ps1 -Capacity

# Sample 120 warmed-up frames per capture without timing PNG readback/encoding.
.\Tests\RenderRegression\run-render-tests.ps1 -Scenes test-tree,trees -BenchmarkFrames 120
```

Requires Visual Studio C++ build tools, the game's existing dependencies/resources,
and a Windows GPU supporting the game's D3D12/DXR renderer. This suite uses the
TreeClassic D3D12 build. The default executable is
`../../Binaries/Debug/x64/TreeClassic/TreeClassic.exe` relative to TreeEngine;
use `-Executable` to select another compatible build.

`scenes.json` lists ten scenes, both rendering modes, and captures at 0, 1,
and 5 seconds (60 images). It also specifies resolution, random seed,
postprocessing, warmup frames, and tolerances. Scene IDs refer to this manifest;
names select the existing GameLoader entry points, including the JSON AI Tree.
No additional scene conversions are needed to run these tests.

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
entries. Capacity checks fail explicitly instead of clipping content.

The native capture request also accepts an optional `mode` on each capture
point, overriding the request mode. `test-dxr-toggle.ps1` uses this to switch
raytracing/raster/raytracing in the same process at a fixed time, checks that the
two DXR images match, and verifies that disabling DXR restores raster output.
It covers scenes with and without a skybox, and compares the AI Tree skybox
region against raster output to verify its texture and orientation.
For scenes without a skybox, DXR ray misses use the scene's clear color. The
toggle test also checks that this background matches raster output.

## Profiling and pose-cache validation

`-BenchmarkFrames` forwards the optional native request field `benchmarkFrames`
(0 through 10,000; default 0 disables profiling). Each capture runs its warmup,
then timed frames, then a separate screenshot frame. Samples are in each worker's
`capture-report.json`, not the top-level image comparison report. These are
deterministic **synchronous** scene updates, not the normal asynchronous update
path. `frameMs` includes the renderer's existing frame fence/presentation
overhead; it is not isolated GPU time. GPU timestamps are read after the
existing completion fence; profiling introduces no additional per-frame wait.

The native `--render-test <request.json>` request also accepts:

- `benchmarkTimeStep`: finite nonnegative seconds, default 0. Warmup holds the
  requested time; timed frames advance from that time by this step. The final
  screenshot returns to the original requested time and is not a timed sample.
- Per-capture `poseCache`: default true. False forces the CPU reference pose to
  be recomputed, allowing same-process A/B comparisons.
- Per-capture `cameraPosition`: three finite coordinates. If omitted, the
  scene's original camera position is restored; orientation is unchanged.

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
two tree scenes, both modes, nine states, 72 captures. It checks growth and
rewind invalidation, mature reuse while time advances, and near/far camera
movement across LODs. Optional `-Capacity` uses a compact grammar to produce
10,000 skinned cylinders (600,000 vertices), then 10,001 (600,060). The first
must render; the second must report `ERROR_INSUFFICIENT_BUFFER` (HRESULT
2147942522), not crash or succeed with missing geometry.

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

### Known baseline limitations

The full Debug manifest currently passes 54 of 60 images. All six AI Tree gold
comparisons already failed on the original pipeline after an earlier scene
change. All six AI Tree captures are pixel-identical against the captured
pre-optimization reference; repository gold has not been replaced.

`test-dxr-toggle.ps1` also has a pre-existing AI Tree failure: its hardcoded
top-100-row "sky only" region now contains scene geometry. Original and optimized
pipelines produce the same approximately 0.007985 RMS for that assertion.
Independent SimpleBox and AI Tree checks with postprocessing disabled pass
on/off/on repeat equality (RMS 0) and actual mode changes. Do not interpret
these checks or the unchanged sky-region failure as a successful full
toggle-suite run.
