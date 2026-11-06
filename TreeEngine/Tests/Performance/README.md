# Performance tests

`run-performance-tests.ps1` runs the tests listed in [`scenes.json`](scenes.json). It records scene
load times and the metrics shown by **Enable profiling** in the debug UI. Results go in `Artifacts/`
and are committed, so a dashboard can compare runs across commits and machines.

There are two kinds of test:
- **Steady** tests render 20 frames with a fixed camera and summarize frame windows: what a scene
  costs once it is running.
- **Frame-pacing** tests move the camera along a keyframed path for a few seconds of simulation
  and report the distribution of frame times and the spikes: hitches that averages hide.

```powershell
.\run-performance-tests.ps1 2f8956a                  # label with the commit
.\run-performance-tests.ps1 "after texture cache"    # or a note
.\run-performance-tests.ps1 smoke -Runs 1 -Tests Instancing -Modes raytracing
.\run-performance-tests.ps1 pan -Tests LeafAtlases-pan -Modes raytracing
```

| Parameter | Default | Meaning |
|---|---|---|
| label (first argument) | required | Commit hash or note, used in the directory name and recorded in `results.json`. The commit itself is recorded either way. |
| `-Configuration` | `Release` | Which `TreeClassic.exe` to measure. A Debug build prints a warning. |
| `-Build` | off | Build TreeClassic in that configuration first. |
| `-Executable` | | Measure a specific build, such as one from another worktree. |
| `-Runs` | `3` | Repetitions of every test and mode. |
| `-Width`, `-Height` | `1280`, `720` | Render size. |
| `-Modes` | `raster, raytracing` | Rendering modes; a test runs in the ones it also lists. |
| `-Tests` (alias `-Scenes`) | all | Test `id`s from `scenes.json`. Steady tests use their scene's `id`. |
| `-StartTime` | from `scenes.json` | Simulation time of frame 0 for steady tests, in seconds. Trees grow from time 0, so earlier times measure nearly empty scenes. |
| `-DisableOpacityMicromaps` | off | Ray trace leaves without opacity micromaps, to measure what they save (see the scenes README). |
| `-Antialiasing` | from `scenes.json` | `none`, `smaa` or `taa`, for every test in the run. |
| `-RenderScale` | 1 | Scene resolution as a fraction of `-Width` x `-Height`; below 1, `taa` upscales (other modes render native). |

Close other GPU-heavy programs first. The script measures whatever else the machine is doing.

## Tests: `scenes.json`

```json
{
  "version": 1,
  "defaults": { "modes": ["raster", "raytracing"], "frameTimeStep": 0.016666666666666666, "startTime": 5, "frames": 20 },
  "tests": [
    { "id": "LeafAtlases", "scene": "LeafAtlases", "type": "steady" },
    { "id": "LeafAtlases-pan", "scene": "LeafAtlases", "type": "pacing",
      "startTime": 3, "duration": 3, "settleFrames": 30,
      "camera": [
        { "time": 0, "position": [0, 2.4, -14], "rotationDegrees": [-4, 0, 0] },
        { "time": 3, "position": [6, 2.4, -14], "rotationDegrees": [-4, -25, 0] } ] }
  ]
}
```

- `id`: letters, digits and hyphens, unique. It names the job directories and the dashboard series.
  Give a steady test its scene's `id`, so its history continues across runs.
- `scene`: the `id` of a scene in `Game/Resources/Scenes`.
- `modes`, `frameTimeStep` (seconds per frame) and `antialiasing` (`none`, the default, `smaa` or `taa`) default to `defaults`.
- **Steady** tests: `startTime` and `frames` (at least 20) default to `defaults`.
- **Pacing** tests:
  - `startTime`: simulation time when the camera starts moving.
  - `duration`: seconds of simulation to measure; the frame count is `duration / frameTimeStep`.
  - `settleFrames` (default 30): frames rendered at `startTime` with the camera on the first key
    before measuring. They absorb the scene load and the driver's slow updates after the first
    acceleration-structure build, so the measured frames show what the motion costs. At least 1:
    the first frame loads the scene, which places the camera from the scene definition.
  - `camera`: at least two keys, `time` from 0 to `duration` in increasing order. `position` and
    `rotationDegrees` (pitch, yaw, roll) are like a scene's camera. Positions are interpolated
    linearly and rotations spherically.

## How a job runs

Runs are the outer loop: run 1 measures every test and mode, then run 2, and so on. This spreads
background and thermal noise across tests. Each job is a fresh `TreeClassic --render-test`
process with a `performance` request. The process:

1. **Loads the scene and times it** (`sceneLoad`):
   - `definitionMs`: read the scene file and build objects, textures and models.
   - `generationMs`: generate trees and other procedural geometry.
   - `graphicsMs`: release the previous level and create GPU buffers, materials and pipelines.
   - `totalMs`: the sum.
2. **Renders the frames with profiling on**, 20 for steady tests. Frame *n* is at simulation time
   `startTime + n * frameTimeStep`. A pacing test first renders its settle frames, numbered from
   `-settleFrames` to -1, then moves the camera to frame *n*'s point on the path before each frame.
   Frame 0 also generates the scene and sets up graphics, so it is slow.
   In ray tracing, the driver's CPU cost for a BLAS build grows with its geometry count until it warms
   up (about 2.5 µs per geometry on an RTX 5080). The BLAS has one geometry per draw record (~190 in
   LeafAtlases), so builds stay under a millisecond; runs before the merge had one per instance and
   show slow frames for about 11 frames after each full build, two of them inside `frames10to20`.
   Object updates use every core, unlike image tests, which use one thread so images repeat.
   No images are written.
3. **Records a sample for every frame:**
   - `frameMs`: CPU wall time of the whole frame, including the wait for the GPU.
   - `sceneUpdateMs`: CPU scene update, including the worker wait.
   - `drawUploadMs`: CPU DXR draw upload.
   - `blasSnapshotMs`: CPU time to snapshot and compare BLAS geometry and transform inputs.
   - `blasCpuMs`: total CPU wall time for BLAS work, including input checks, descriptor preparation, resource setup, and command recording.
   - `firstBlasBuildCpuMs` and `firstBlasBuildGpuMs`: the first actual BLAS rebuild in the run, independent of the selected frame window; GPU time also includes any initial opacity-micromap build. Older runs without the CPU metric leave it blank.
   - `blasHashMs` and `blasDescriptorsMs`: CPU time to hash BLAS inputs and prepare geometry descriptors and associated data.
   - `blasSubmitMs` and `tlasSubmitMs`: CPU wall time inside the respective build-command API calls, not GPU execution time.
   - `gpuMs`: GPU passes in the order frame, shadow, main, deformation, BLAS, TLAS, rays, anti-aliasing.
     Passes that don't run in a mode report 0; BLAS, TLAS, rays and deformation run only in ray tracing,
     and anti-aliasing only with SMAA. Runs before it was added have seven entries.
   - Counters: draw records, vertices, pose branches rebuilt and reused, acceleration-structure bytes,
     whether the frame rebuilt the BLAS (`rebuiltBLAS`), and others.
   - `cameraPosition`, and `settle`: true for settle frames.

The script also times each whole process (`processMs`), which includes startup and device creation.

## Output

Each pass creates `Artifacts/<yyyyMMdd-HHmm>Z_<COMPUTERNAME>_<label>/`, for example
`20261030-0346Z_LEVEE_c15f51a`. The time is the UTC start, so directories sort in run order.
Underscores separate the parts; the machine name and label are sanitized for file names and
never contain one. If the directory already exists, a `-2`, `-3`, … suffix is added.

Tools should read the fields in `results.json` or `index.json`, not parse directory names.

- **`results.json`:** everything, for the dashboard (`schemaVersion` 3), written as compact JSON.
  Schema 3 added `tests`, and `test`, `type` and `pacing` on jobs; schema 2 runs had only steady jobs.
  - **Run information:**
    - `label`, `machine`, `startedUtc`, `finishedUtc`
    - `commit`, `commitDate`, `commitSubject`, `branch`, `dirty` (uncommitted changes when measured).
      A rebase changes commit hashes, so order runs by `commitDate` and use the subject to find
      a commit whose hash no longer exists.
    - `configuration`, `cpu`, `logicalProcessors`, `os`, `adapter`, `workerThreads`
    - `width`, `height`, `runs`, `modes`
    - `tests`: the `scenes.json` entries measured, with defaults filled in.
    - `scenes`, `frames`, `startTime`, `frameTimeStep`: the steady tests and their settings, as in schema 2.
    - `opacityMicromaps`: false when measured with `-DisableOpacityMicromaps`; missing in older runs.
      The dashboard marks such runs "opacity micromaps off".
    - `antialiasing`: the `-Antialiasing` value, or null when each test used its own setting (in `tests`).
      The dashboard marks SMAA runs.
    - `renderScale`: the `-RenderScale` value; missing in older runs (native). Each job's report also gives
      `renderWidth` and `renderHeight`.
    - `gpuPasses`
  - **`windows`:** the frame ranges summarized: `frames5to10` is frames 5–9, and `frames10to20` is frames 10–19.
  - **`jobs[]`**, one per test, mode and run:
    - `test`, `type` (`steady` or `pacing`), `scene` (the test id for steady tests, so older runs line up;
      the scene id for pacing tests), `sceneName`, `mode`, `run`, `success`, `error`, `processMs`, `sceneLoad`
    - `opacityMicromaps`: `requested`, `supported`, and what the load baked: `micromaps`, `specialTriangles`
      (uniform triangles that need no map), `subdivisionLevel`, `bytes`, and `states`
      (micro-triangles transparent, opaque, unknown-transparent, unknown-opaque)
    - every frame's `samples`
    - Steady jobs: `windows.<name>.<metric>`, the `{ mean, min, max }` of CPU timing metrics and `gpu_<pass>Ms`
    - Pacing jobs: `pacing`, from the measured (not settle) frames:
      - `frameP50Ms`, `frameP90Ms`, `frameP99Ms`, `frameMaxMs`, `frameMeanMs`: frame-time percentiles
        (nearest rank), maximum and mean.
      - `spikes`: frames longer than `spikeThresholdMs`, which is twice the median or one 60 fps frame
        (16.7 ms), whichever is longer.
      - `spikeExcessMs`: the time those frames took beyond the median, summed.
      - `over33Ms`: frames longer than 33.3 ms (under 30 fps).
      - `rebuiltBLASFrames` and `maxBlasCpuMs`: frames that rebuilt the BLAS, and the longest CPU BLAS work.
- **`summary.csv`:** one row per steady job and window, with load times and window means. Open it in Excel for a quick look.
- **`pacing.csv`:** one row per pacing job with its `pacing` summary.
- **`jobs/<test>-<mode>-run<N>/`:** the raw `request.json`, the app's `report.json`, and logs.
  These are for local debugging and are not committed (see `.gitignore`); `results.json` has every sample.

A failed job is recorded with its `error` and the pass continues. The script then exits with code 1.

### Index

`Artifacts/index.json` lists every run, one per line, oldest first: `directory`, `startedUtc`,
`machine`, `label`, the commit fields, `configuration`, `adapter`, `scenes` (steady tests), `tests`, `modes`, `runs`,
`jobs` and `failures`. A dashboard reads it to list runs, then loads a run's `results.json`
when needed. Each pass adds its own line. After merging runs from another machine, or if the
file has a merge conflict, rebuild it from the run directories:

```powershell
.\update-performance-index.ps1
```

### Storage

The run directories and `index.json` are the permanent record and are committed. Each run's
`results.json` is about 300 KB, and about 30 KB once git compresses it, so a thousand runs add
roughly 30 MB to the repository. A database such as SQLite, if a dashboard needs one, should be
built from these files and not committed: a binary file that changes on every run can't be
merged, and git stores a full new copy of it each time.

## Dashboard

```powershell
.\show-dashboard.ps1
```

This gathers `index.json`, each run's `summary.csv`, and the pacing jobs' frame times from `results.json`
into `dashboard-data.js` (generated and not
committed) and opens `dashboard.html` in the browser. It works from the local file; no server is needed.
Run it again after new runs.

- **Filters:** machine, branch, raster or ray tracing, the frame range, and whether to include runs
  with uncommitted changes. Click a scene in the legend to hide or show it.
- **Latest run compared with the previous run:** scene load, CPU frame, CPU scene update and GPU frame
  per scene, with changes beyond ±10% flagged as slower or faster.
- **Frame pacing while the camera moves:** pick a pacing test, a run and a run to compare with. The chart
  plots every measured frame's time along the camera path (the median of the run's repeats), with a
  33.3 ms line and ticks under the axis at frames that rebuilt the BLAS. Hover or use the arrow keys for
  each frame's values and range. The table lists every run's percentiles, spikes and rebuilds.
- **Charts:** one per metric, with runs in commit order on the x-axis and a line per scene. Each point
  is the mean of the run's repeats; hover, or focus a chart and use the arrow keys, to see the range.
  Charts for passes that don't run in the chosen mode are hidden. Each chart has a table view.

## Adding a test

Add an entry to `scenes.json`. The scene must have an `id`. For a pacing path, a scene's `camera`
is a good first key, and the debug UI's **Camera Position** shows where to put the others. (Its
**Camera Orientation** is a quaternion, not degrees.) Check the first run's `cameraPosition` samples and the dashboard trace to confirm the
camera moves as intended.
