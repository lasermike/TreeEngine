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
| label (first argument) | required | Commit hash or note, used in the directory name and recorded in `results.json`. |
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

Each pass creates `Artifacts/<label>-<COMPUTERNAME>-<yyyyMMdd-HHmm>/`. The label is
sanitized for file names. If the directory already exists, a `-2`, `-3`, … suffix is added.

- **`results.json`:** everything, for the dashboard (`schemaVersion` 1).
  - **Run information:**
    - `label`, `machine`, `startedUtc`, `finishedUtc`
    - `commit`, `branch`, `dirty` (uncommitted changes when measured)
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

A failed job is recorded with its `error` and the pass continues. The script then exits with code 1.

## Adding a scene

Add `"testPass": "performance"` to a scene in `Game/Resources/Scenes`; the scene must have an `id`.
`testPass` may also be an array, for example `["performance", "smoke"]`.
