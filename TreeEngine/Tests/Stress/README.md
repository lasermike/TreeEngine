# Stress tests

`run-stress-test.ps1` cycles scenes inside one `TreeClassic --render-test` process, as the app does when you
step through scenes, and changes rendering settings between steps. Each step loads its scene with
`Game::LoadScene` when it differs from the current one, then renders and captures it. Render tests elsewhere
load one scene per process, so they can't see state that leaks from one level to the next.

```powershell
.\run-stress-test.ps1                                   # 40 steps from a random seed (printed)
.\run-stress-test.ps1 -Seed 100 -Iterations 20          # seeds 100..119, stop at the first failure
.\run-stress-test.ps1 -Configuration Debug -Iterations 5   # with the debug layer and asserts
.\run-stress-test.ps1 -AllCores -Iterations 10          # the interactive app's multi-threaded updates
.\run-stress-test.ps1 -Replay Artifacts\<failure folder>   # rerun a failing sequence exactly
```

| Parameter | Default | Meaning |
|---|---|---|
| `-Seed` | random | Sequence seed. A seed always produces the same sequence for the same scenes and step count. |
| `-Steps` | `40` | Steps per process. |
| `-Iterations` | `1` | Runs with seeds `Seed`, `Seed+1`, …; stops at the first failure. |
| `-Configuration` | `Release` | Which build to run. Debug adds the D3D12 debug layer (errors break into the crash handler) and asserts. |
| `-Executable` | | A specific `TreeClassic.exe`. |
| `-Scenes` | the render-regression scenes | Scene names to draw from. |
| `-AllCores` | off | Update objects on every core. If images don't repeat run to run with it, use `-NoImageCheck`. |
| `-NoImageCheck` | off | Only detect crashes, device removal and step errors. |
| `-KeepPassing` | off | Keep the folders of passing runs (normally deleted). |
| `-Replay` | | A failure folder; reruns its `sequence.json`. |
| `-TimeoutSeconds` | `900` | Per process. |

## Steps
Each step picks, from the seeded generator:
- **Scene:** a new scene, or (30%) one of the last three again, so levels are reloaded after others.
- **Mode:** raster or ray tracing, so DXR is toggled on and off.
- **Size:** 640x480, 800x600 or 1024x576, so the window and its targets are resized.
- **Anti-aliasing:** none or SMAA.
- **Post-processing:** on or off.
- **Time:** 0, 1 or 5 seconds.

## What counts as a failure
| Kind | Evidence |
|---|---|
| `crash` | `crash.dmp`: a minidump written by the app's crash handler (unhandled exceptions, `abort`, failed asserts, invalid CRT parameters, pure calls), exit code 5. Open it in Visual Studio with the matching build's PDBs. |
| `device-removed` | `dred.txt`: the removal reason, DRED breadcrumbs for the command lists with unfinished GPU work, and the page-fault address with the allocations that lived or were recently freed there. |
| `step-error` | The capture failed; `report.json` has the error. |
| `timeout` | The process didn't exit in time; check for a hang. |
| `image-mismatch` | A step's image differs from a fresh render of the same step in its own process (`step-NNN.png`, `step-NNN-fresh.png`, `step-NNN-diff.png`). That points to state carried over from earlier steps. |

The failing step is the last `step N:` line in `stderr.log`. Every failure folder in `Artifacts/` (not committed)
also holds `sequence.json`, `request.json`, the logs, `summary.json` and the images of the steps that finished,
and the script prints the replay command.

Fresh renders are cached in `Artifacts/references/<configuration>-<build>/` by step settings and reused until the
executable or `RenderPlatform12.dll` changes. The image check uses the render-regression RMS tolerance.

## Request fields the stress script uses
These render-test request fields are available to any render test:
- Per capture: `scene` (load it first when it differs), `antialiasing` and `postProcessing` (override the request).
- Per request: `allCores`, `dred` with `dredReport`, and `dumpFile`.
