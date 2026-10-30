# Performance tests

`run-performance-tests.ps1` measures every built-in scene whose definition has
`"testPass": "performance"`. It records scene load times and the metrics shown
by **Enable profiling** in the debug UI. Results go in `Artifacts/` and are
committed, so a dashboard can compare runs across commits and machines.

```powershell
.\run-performance-tests.ps1 2f8956a                  # label with the commit
.\run-performance-tests.ps1 "after texture cache"    # or a note
.\run-performance-tests.ps1 smoke -Runs 1 -Scenes Instancing -Modes raytracing
```

| Parameter | Default | Meaning |
|---|---|---|
| label (first argument) | required | Commit hash or note, used in the directory name and recorded in `results.json`. The commit itself is recorded either way. |
| `-Configuration` | `Release` | Which `TreeClassic.exe` to measure. A Debug build prints a warning. |
| `-Build` | off | Build TreeClassic in that configuration first. |
| `-Executable` | | Measure a specific build, such as one from another worktree. |
| `-Runs` | `3` | Repetitions of every scene and mode. |
| `-Width`, `-Height` | `1280`, `720` | Render size. |
| `-Modes` | `raster, raytracing` | Rendering modes. |
| `-Scenes` | all tagged | Scene `id`s to measure; each must be in the pass. |
| `-StartTime` | `5` | Simulation time of frame 0, in seconds. Trees grow from time 0, so earlier times measure nearly empty scenes. |

Close other GPU-heavy programs first. The script measures whatever else the machine is doing.

## How a job runs

Runs are the outer loop: run 1 measures every scene and mode, then run 2, and so on. This spreads
background and thermal noise across scenes. Each job is a fresh `TreeClassic --render-test`
process with a `performance` request. The process:

1. **Loads the scene and times it** (`sceneLoad`):
   - `definitionMs`: read the scene file and build objects, textures and models.
   - `generationMs`: generate trees and other procedural geometry.
   - `graphicsMs`: release the previous level and create GPU buffers, materials and pipelines.
   - `totalMs`: the sum.
2. **Renders 20 frames with profiling on.** Frame *n* is at simulation time `startTime + n/60`.
   Frame 0 also generates the scene and sets up graphics, so it is slow.
   Object updates use every core, unlike image tests, which use one thread so images repeat.
   No images are written.
3. **Records a sample for every frame:**
   - `frameMs`: CPU wall time of the whole frame, including the wait for the GPU.
   - `sceneUpdateMs`: CPU scene update, including the worker wait.
   - `drawUploadMs`: CPU DXR draw upload.
   - `gpuMs`: GPU passes in the order frame, shadow, main, deformation, BLAS, TLAS, rays.
     Passes that don't run in a mode report 0; BLAS, TLAS, rays and deformation run only in ray tracing.
   - Counters: draw records, vertices, pose branches rebuilt and reused, acceleration-structure bytes, and others.

The script also times each whole process (`processMs`), which includes startup and device creation.

## Output

Each pass creates `Artifacts/<yyyyMMdd-HHmm>Z_<COMPUTERNAME>_<label>/`, for example
`20261030-0346Z_LEVEE_c15f51a`. The time is the UTC start, so directories sort in run order.
Underscores separate the parts; the machine name and label are sanitized for file names and
never contain one. If the directory already exists, a `-2`, `-3`, … suffix is added.

Tools should read the fields in `results.json` or `index.json`, not parse directory names.

- **`results.json`:** everything, for the dashboard (`schemaVersion` 2), written as compact JSON.
  - **Run information:**
    - `label`, `machine`, `startedUtc`, `finishedUtc`
    - `commit`, `commitDate`, `commitSubject`, `branch`, `dirty` (uncommitted changes when measured).
      A rebase changes commit hashes, so order runs by `commitDate` and use the subject to find
      a commit whose hash no longer exists.
    - `configuration`, `cpu`, `logicalProcessors`, `os`, `adapter`, `workerThreads`
    - `width`, `height`, `frames`, `startTime`, `frameTimeStep`, `runs`, `modes`, `scenes`
    - `gpuPasses`
  - **`windows`:** the frame ranges summarized: `frames5to10` is frames 5–9, and `frames10to20` is frames 10–19.
  - **`jobs[]`**, one per scene, mode and run:
    - `scene`, `sceneName`, `mode`, `run`, `success`, `error`, `processMs`, `sceneLoad`
    - every frame's `samples`
    - `windows.<name>.<metric>`, the `{ mean, min, max }` of `frameMs`, `sceneUpdateMs`, `drawUploadMs` and `gpu_<pass>Ms`
- **`summary.csv`:** one row per job and window, with load times and window means. Open it in Excel for a quick look.
- **`jobs/<scene>-<mode>-run<N>/`:** the raw `request.json`, the app's `report.json`, and logs.
  These are for local debugging and are not committed (see `.gitignore`); `results.json` has every sample.

A failed job is recorded with its `error` and the pass continues. The script then exits with code 1.

### Index

`Artifacts/index.json` lists every run, one per line, oldest first: `directory`, `startedUtc`,
`machine`, `label`, the commit fields, `configuration`, `adapter`, `scenes`, `modes`, `runs`,
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

This gathers `index.json` and each run's `summary.csv` into `dashboard-data.js` (generated and not
committed) and opens `dashboard.html` in the browser. It works from the local file; no server is needed.
Run it again after new runs.

- **Filters:** machine, branch, raster or ray tracing, the frame range, and whether to include runs
  with uncommitted changes. Click a scene in the legend to hide or show it.
- **Latest run compared with the previous run:** scene load, CPU frame, CPU scene update and GPU frame
  per scene, with changes beyond ±10% flagged as slower or faster.
- **Charts:** one per metric, with runs in commit order on the x-axis and a line per scene. Each point
  is the mean of the run's repeats; hover, or focus a chart and use the arrow keys, to see the range.
  Charts for passes that don't run in the chosen mode are hidden. Each chart has a table view.

## Adding a scene

Add `"testPass": "performance"` to a scene in `Game/Resources/Scenes`; the scene must have an `id`.
`testPass` may also be an array, for example `["performance", "smoke"]`.
