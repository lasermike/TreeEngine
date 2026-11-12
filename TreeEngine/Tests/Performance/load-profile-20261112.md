# Forest and AI Tree load profile

Measured November 12, 2026 UTC on LEVEE, NVIDIA GeForce RTX 5080. Release build of
`5a9c7a8` plus the load instrumentation in this working tree. Three fresh processes
per scene and rendering mode, 1280x720, simulation start time 5 seconds. OS and
driver caches were warm; this is not a cold-disk benchmark or an interactive
scene-switch benchmark.

## Results

Median milliseconds (each column has its own median, so columns need not sum):

| Scene / mode | Total load | Definition | Generation | Graphics | First-frame work beyond generation + graphics |
| --- | ---: | ---: | ---: | ---: | ---: |
| Forest / raster | 1603 | 161 | 724 | 719 | 146 |
| Forest / ray tracing | 1636 | 163 | 734 | 740 | 174 |
| AI Tree / raster | 1955 | 124 | 124 | 1707 | 76 |
| AI Tree / ray tracing | 1901 | 124 | 119 | 1658 | 100 |

The final column is frame 0 wall time minus generation and graphics time.
Definition loading occurs before that frame. It includes scene update and other
frame work, not just rendering. Starting at simulation time 5 may cost more than
an interactive scene starting at time 0. Device/process startup is outside total load.

## Forest

- 36 L-system generations: command rewriting takes 322–329 ms; serialization to
  text, including debug output, takes 335–336 ms; skeleton construction takes
  49 ms. These are mode medians, not min/max sample ranges.
- `LSystemModelGenerator::Create` already has structured commands, but writes
  them through a stringstream and then calls `CreateSkeleton` with that text.
  First optimization: let skeleton construction consume commands directly and
  make expanded-command debug output optional. The serialization bucket alone
  is about 21% of load time, an opportunity ceiling rather than a measured saving.
  Preserve or explicitly assess the numeric rounding currently introduced by
  stringstream formatting; compare generated geometry before accepting a change.
- Rewriting repeatedly constructs command vectors. After eliminating the text
  round trip, investigate capacity reservation and buffer reuse, preserving rule
  application order and random behavior.
- 15 texture loads take 308–323 ms, including 39–45 ms of explicit upload waits.
- Platform initialization takes 311–316 ms. This aggregate still needs finer
  subdivision before choosing an optimization; object PSO timings below do not
  cover platform initialization PSOs.
- Opacity alpha loading/baking takes 66–67 ms. CPU geometry-buffer assembly is
  below 1 ms. Object pipeline creation is only 3.7 ms.

## AI Tree

- 27 texture loads take 1399–1442 ms, about three quarters of total loading.
  Explicit texture upload waits account for only 96–115 ms of this bucket.
- The embedded `terra.glb` is the largest target: texture 5 alone takes about
  0.73 seconds; textures 6 and 7 add roughly 0.27 seconds. These are texture
  loading costs, not mesh generation costs.
- First optimization: cache decoded textures and their complete mip chains, or
  prepare DDS assets offline. Preserve color-space handling, alpha-weighted
  filtering, and the existing area-weighted mip behavior for odd dimensions.
  `CreateRgbaTextureFromBgraImage` currently decodes pixels and builds CPU mips
  during loading. The timers combine decode, mip generation, allocation, and
  upload, so they do not yet establish which CPU substep dominates.
- Batch uploads after preparing textures, but do not expect this alone to remove
  the 1.4-second texture cost. The measured explicit waits bound that particular
  opportunity at roughly 0.1 seconds; overlapping preparation and upload needs
  separate measurement.
- Opacity alpha loading/baking takes about 117 ms. Caching baked micromaps is a
  secondary target after textures. Generation is only 119–124 ms; object pipeline
  creation is below 5 ms. Platform initialization is about 95–109 ms.

## Instrumentation and reproduction

`SceneLoadProfile` records inclusive wall-clock durations and call counts during
profiled regeneration. Detailed results are written under `sceneLoad.scopes` in
each capture's `report.json`. Parent scopes contain child scopes: do not add all
scope entries together. The opacity bake bucket includes alpha-image loading.
No rendering or generation algorithm was changed.

```powershell
& '.\Tests\Performance\run-performance-tests.ps1' -Label 'load-detail' -Scenes Forest,LoadAITree -Runs 3 -Configuration Release -TimeoutSeconds 600
```

[Detailed capture artifacts](Artifacts/20261112-0620Z_LEVEE_load-detail/) contain
all 12 successful jobs, requests, timings, and frame metrics. The earlier
`load-baseline` captures used the preexisting executable and are exploratory;
do not use them as a controlled before/after comparison against this build.

Validation: Release build passed; 12/12 instrumented captures passed;
`git diff --check` passed. Profiling changes and artifacts remain uncommitted.

## Optimization 1: interpret commands directly

`CreateSkeleton` now reads the expanded command vector. Removed the stringstream,
expanded-command debug output, second parsing pass, and unused command-history
string. Rewriting and deterministic variation seeds are unchanged. Numeric values
retain full precision rather than being rounded to the stream's six significant
digits; this intentionally produces small geometry differences.

Same Release performance protocol, three runs per mode:

| Scene / mode | Generation before | Generation after | Total before | Total after |
| --- | ---: | ---: | ---: | ---: |
| Forest / raster | 724 ms | 361 ms | 1603 ms | 1231 ms |
| Forest / ray tracing | 734 ms | 361 ms | 1636 ms | 1281 ms |
| AI Tree / raster | 124 ms | 63 ms | 1955 ms | 1956 ms |
| AI Tree / ray tracing | 119 ms | 65 ms | 1901 ms | 1938 ms |

Forest generation is about 50% faster and total loading is 22–23% faster.
Skeleton interpretation drops from about 49 ms to 24 ms in addition to removing
the 335 ms serialization stage. AI Tree generation also improves, but graphics
timing variation offsets the saving in these total-load samples; no overall AI
Tree speedup is established yet. Texture loading has not been changed.

[Performance artifacts](Artifacts/20261112-0628Z_LEVEE_load-direct-commands/):
12/12 jobs passed. Before editing the interpreter, captured fresh reference images
for Forest and AI Tree in raster and ray tracing at times 0, 1, and 5. All 12
post-change comparisons passed the existing RMS tolerance of 0.002; maximum
observed RMS was 0.000305 (normalized 0–1). These are visual-regression checks,
not proof of bit-identical geometry. References and comparison reports are in the
local ignored RenderRegression/Artifacts directory. Release build and diff checks
passed. The optimization remains uncommitted for review before the texture work.
