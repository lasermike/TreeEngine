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
```

Requires Visual Studio C++ build tools, the game's existing dependencies/resources,
and a Windows GPU supporting the game's D3D12/DXR renderer. This suite uses the
TreeClassic D3D12 build. The default executable is
`../../Binaries/Debug/x64/TreeClassic/TreeClassic.exe` relative to TreeEngine;
use `-Executable` to select another compatible build.

`scenes.json` lists all nine scenes, both rendering modes, and captures at 0, 1,
and 5 seconds (54 images). It also specifies resolution, random seed,
postprocessing, warmup frames, and tolerances. Scene IDs refer to this manifest;
names select the existing GameLoader entry points, including the JSON AI Tree.
No additional scene conversions are needed to run these tests.

Each scene/mode runs in a separate hidden game process with the scene's initial
camera and lights, no input or debug UI, and synchronous object updates. The
simulation/shader time is set directly for each sample, with three warmup frames
at that same time before capture. This tests the current time-driven scenes;
future simulations that integrate state over elapsed frames will need a fixed
step progression. Screenshots are captured from the completed D3D12 backbuffer
before presentation. Raster uses the normal raster display. Raytracing uses the
game's existing **Show DXR Debug UAV** display, including its compositing with
the raster image; it does not change the renderer's presentation behavior.

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

During initial validation, raster output was pixel-identical across runs. DXR
exposed an out-of-bounds draw-record upload, which has been corrected by padding
the CPU upload data to the destination buffer size. The Trees DXR output at
5 seconds still varies between runs and remains a failure at the configured
tolerance. It needs renderer investigation before the DXR suite can serve as a
consistently green gate for scene conversions.
