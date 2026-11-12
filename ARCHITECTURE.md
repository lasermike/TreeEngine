# TreeEngine Architecture

_Snapshot as of 2026-10-28 (branch `aitree`). Paths are relative to `TreeEngine/` unless noted._

## 1. What it is

TreeEngine is a C++ DirectX engine that simulates plant and tree growth in real time. Plants are defined as L-systems: a starting string (axiom) plus production rules, based on *The Algorithmic Beauty of Plants*. Each frame the engine grows the plants, rebuilds their geometry and renders them with shadows, post-processing and optional DXR ray tracing. The repo history goes back to 2014. Most current work is the Win32 desktop build, the `Game/` core and the test suite.

**Layout:** three layers that the build keeps separate.

```
 App host (TreeClassic / TreePC / TreeXbox)    ← window, message pump, input, DPI
        │ owns
 Game core (Game/, via SharedEngine.vcxitems)  ← simulation, scene graph, procedural gen
        │ LoadLibrary + GetProcAddress
 RenderPlatform DLL (RenderPlatform12 …)       ← D3D12/DXR device, PSOs, swap chain, ImGui
```

`SharedEngine.vcxitems` is a shared-items project. Each app target compiles the `Game/` sources itself. The renderer is a separate DLL that the game loads at runtime. Because it is loaded at runtime, the renderer can be **unloaded and reloaded while the simulation runs**, which allows hot-reloading of shaders and rendering code (`Game/Game.cpp`, `RenderManager::ChangePlatform`). Preprocessor defines pick the API: `TREE3D12`, `TREE3D11`, `DXR_ENABLED` and `TREENGINE_WIN32`.

## 2. Notable interfaces

**`RenderPlatform`** (`Game/RenderPlatform.h`) is the central abstraction. It is an abstract class with about 35 pure-virtual methods, covering:
- device and swap-chain lifecycle: `InitDevice`, `OnResize`, `ReleaseSwapChainResources`
- level setup: `InitGameLevelGraphics`
- the frame: `BeginNewFrame` → `RenderProlog` → `RenderSceneSetup` / `SetRenderUnit` / `DrawIndexedInstanced` → `RenderPostProcess` → `RenderDebugUI` → `RenderEpilog` → `EndFrame`
- resource creation: `LoadTexture*`, `CreateTexture2D`, `CreateD3DBuffer`, `Load{Vertex,Pixel}Shader`, `CreateRenderUnit`
- ImGui hooks

Its implementations are `RenderPlatform12`, `RenderPlatform11` and `RenderPlatformDLL`. `RenderPlatformDLL` is a proxy: it resolves a matching set of C-style exports (`Create`, `InitDevice`, …, the `*Func` typedefs near the end of `RenderPlatform.h`) through `GetProcAddress` in `RenderManager::SetPlatform` (`Game/RenderManager.cpp`). That set of flat C exports is the DLL's real ABI.

**`Upscaler`** (`Game/Upscaler.h`) is the interface for temporal anti-aliasing and upscaling backends. TAA is the
first (`Game/UpscalerTaa.cpp`), AMD FSR the second (`Game/UpscalerFsr.cpp`) and NVIDIA DLSS the third
(`Game/UpscalerDlss.cpp`). `TreeEngine/Game/UPSCALING.md`
documents it in full: the frame, the interface contract, the TAA reconstruction, and how to add a backend.
- **Sizes.** The scene, ray tracing and motion vectors run at the render size, `Settings::m_renderScale` of the window
  (arithmetic in `Game/UpscalerMath.h`). Tonemapping, blur, the UI and captures run at the window's size.
- **The platform's side.** `RenderPlatform12` picks a backend per anti-aliasing mode in `BeginNewFrame`. It jitters
  the camera over the backend's phase count and biases texture LOD by the backend's `MipBias`. It then calls
  `Resize` and `Evaluate` with an `UpscaleFrame`: color, depth and motion, the output, the camera, and a reset flag.
- **The backend's side.** A backend owns its history, root signature and pipelines. It writes descriptors only in a
  16-descriptor block reserved for its mode.
- **Native.** At render scale 1, TAA runs the original `TaaResolveCS`.

**`RenderManager`** (`Game/RenderManager.h`) sits between the game and whichever renderer is loaded. It handles render-pass orchestration, the shadow map, materials and meshes, glTF textures, instance allocation and scene render settings. It implements two small interfaces:
- **`IRenderFrame`**: `SetInstances`, `GetInstanceIndex`, `GetRenderData`. World objects use it to publish their instances.
- **`IDebugUI`**: `UpdateDebugUI(ImGuiContext*)`. `Game` implements it.

**`Game`** (`Game/Game.h`) owns the loop: `Update` → `ComputeCPU` / `ComputeGPU` → `Render` / `Present`. It also handles:
- scenes: `LoadScene`, `SaveScene`, `RefreshScenes`
- render tests: `ConfigureRenderTest`, `RenderTestFrame`, used for automated GPU capture
- `SetWindow` overloads for an HWND or a UWP `CoreWindow`

**Scene and simulation model:**
- **`WorldObject`** is the base class for everything in the scene and is configured by `WorldObjectParams`. `SceneRoot` holds the graph, and `Player` and the camera live alongside it.
- **Scene definitions** (`Game/SceneDefinition.h`) are plain data structs: `SceneDefinition`, `ObjectDefinition` and its subtypes (Primitive, Tree, FixedTree, Graph, Model), `LSystemDefinition`/`RuleDefinition`, and light definitions.
  - `SceneCatalog` lists the available scenes.
  - `SceneInstantiation` turns a definition into a live `SceneRoot`, and `CaptureScene` turns live objects back into a definition for saving.
  - Built-in scenes are `Game/Resources/Scenes/*.scene.json`; the schema is documented in that folder's `README.md`. Their IDs (e.g. `LoadAITree`) keep the names of the old `Load*` loader functions.
  - Materials use glTF metallic-roughness properties (`baseColor`, `metallic`, `roughness`, maps) and the same BRDF as models; legacy Phong fields are rejected: `BuildPbrShaderMaterial` in `GltfLoader.cpp` maps factors to `ShaderMaterial` for both, and `RenderManager::BindMaterialTexture` binds the maps.
- **Procedural generation:** `ModelGenerator` → `TreeModelGenerator` (`virtual TreeModel* Create() = 0`), plus `FixedTreeModelGenerator`, `PlaneModelGenerator` and `LSystemModelGenerator`. Their output becomes `TreeModel`/`TreeSegment` and then `TreeGeometry`. Trees use a skinned cylinder mesh and a pose cache.
- **Input:** `IInputManager::GetFrameInput(frame)` (`Game/InputManager.h`), with a separate implementation per platform.
- **Utilities:** `ThreadPool`, `DynamicProperties`, `Settings`, `StepTimer`, `GltfLoader`/`GltfModel`.

**DXR acceleration structures** (`RenderPlatform12::BuildBLASandTLAS`). Static geometry (`RenderUnit::staticRayTracingGeometry`, set by glTF models) has one BLAS, built once with fast trace, compacted the next frame, and rebuilt only when its inputs change. Dynamic geometry has one BLAS per drawing object (`RenderData::currentDrawOwner`, set by `RenderManager::Render`), and instanced trees share one per source layout. Each is reused, refit or rebuilt on its own, from per-BLAS input snapshots and layout hashes. The TLAS has an instance per object (with its object transform), plus the static BLAS, and the hit shaders index their per-geometry tables with `InstanceID() + GeometryIndex()`. Profiling reports builds, refits and reuses per frame. Dynamic BLASes are sub-allocated from a heap (`AllocateBlas`: pool chunks for BLASes under 1 MB, a dedicated chunk for larger ones), and each frame's builds share one scratch buffer.

**Raster shadows** (`RenderManager::BuildShadowTransform`, `CascadedShadowFactor` in `Tree.hlsl`). Up to four cascades (`Settings::m_shadowCascades`, default 3) cover the view out to `m_shadowDistance` (150 m), split between logarithmic and uniform. Each is a 2048-texel quadrant of one 4096 shadow map, fitted to a bounding sphere of its slice and snapped to whole texels (no shimmer), with its near plane reaching back to the scene bounds toward the light so off-slice casters still cast. The shadow pass draws each cascade with its own per-pass constants and culls objects against that cascade's box. The pixel shader takes the first cascade holding the point, with a normal offset and depth bias sized to the cascade's texel (the shadow PSO keeps only slope-scaled bias). DXR traces its own shadow rays and skips the raster shadow pass.

**Structs shared between C++ and HLSL.** These must stay byte-for-byte in sync with their shader counterparts:
- `DrawRecord` (`RenderPlatform.h`) ↔ `Shaders/Blur.hlsl`
- `ShaderMaterial` (`Materials.h`) ↔ `Shaders/Materials.fx` (PBR-only; mirrored in `Blur.hlsl`'s `DrawRecord`, with size checks in `Materials.h` and `RenderPlatform.h`)
- Per-draw object transform: `IRenderFrame::SetInstances` takes an optional instance-buffer slot whose `World` the GPU applies after each instance's own (root constant `b4` in `Tree.hlsl`). Tree instances (`instanceOf`) use it to draw their source tree's branch layouts; a source with instances reserves its own branches plus its far, near and reduced layouts (one per LOD distance after the second) in the instance buffer, and `SceneRoot::Update` computes sources before instances. In DXR the transform goes to the TLAS instead: `RenderPlatform12::PrepareInstancedGeometry` clears `DrawRecord.objectTransformSlot`, so `Blur.hlsl` writes those vertices in layout space, and instances drawing the same layout share one BLAS.
  - Batched instance draws: in opaque units, `RenderManager::Render` groups instanced objects that draw the same instance range (a layout) into one draw (`RenderPlatform::DrawBatched`). The platform writes the batch's object transform slots into a list region after the reservations, four per `InstancedData` (its uint fields), and `b4` carries the range and list; `BeginInstance` in `Tree.hlsl` fetches each instance's data from `SV_InstanceID`. DXR still records each object's own (culled) draw. `Settings::m_batchInstances` and the render-test field `batchInstances` turn it off.
  - Instance upload: only the slots objects wrote are copied from the CPU instance array to the GPU each frame (`WorldObject::AppendInstanceUploadRanges`; trees list their branches, written layouts and atlas rects, other objects their whole reservation).
- `DxrGeometryInfo` (`RenderPlatform.h`) ↔ `Shaders/SharedTypes.hlsli`
- `CBChangesPerPass`, `CBChangesEveryFrame` and `InstancedData` (`ConstBufferDefinitions.h`) ↔ `Shaders/SharedTypes.hlsli`
- `TaaConstants` (`UpscalerTaa.cpp`) ↔ `Shaders/Taa.hlsl`

Shaders live in `Game/Shaders/`, including a DXR library (`RaytracingLibrary.hlsl`, with global and local root signatures). `buildShaders.bat` compiles them with fxc/dxc as part of the build.

## 3. Active vs. inactive projects

| Project | Role | Status |
|---|---|---|
| **TreeClassic** | Win32 desktop app, ImGui/ImPlot debug UI, render-test capture | **Active**: the main target (`Debug\|x64`, `Release\|x64`) |
| **RenderPlatform12** | D3D12 + DXR renderer DLL (sources mostly in `Game/`) | **Active** |
| **Game/** + **SharedEngine** | Engine core (~110 source files) | **Active**: most commits since 2025 |
| **SceneDefinitionTests** (`Tests/`) | Scene definition/persistence unit tests; `RenderRegression/` holds PowerShell-driven GPU image tests (DXR toggle, resize, skinning, shadow buffer) and C# image comparison; `Performance/` runs the steady and frame-pacing tests in `Performance/scenes.json` and commits the results | **Active**, Debug-only, no CI |
| **ImGui**, **nvapi**, **ThirdParty** | Vendored libraries | Kept up to date |
| **TreePC** | UWP app | Inactive: in the solution but not built or tested; planned for deprecation |
| **RenderPlatform12UWP**, **RenderPlatform11UWP** | UWP renderers (D3D12, D3D11 fallback) | Inactive: last changed Feb 2023; `TREE3D11` is slated for removal |
| **TreeXbox**, **RenderPlatform12Xbox** | GDK Xbox One/Scarlett | Inactive: still in the solution, but no recent changes (RenderPlatform12Xbox last changed Feb 2024) |
| **Tree2012** | Older Xbox One (Durango) app | Effectively dead, legacy |
| **TreeWin32Dll / TreeWin32Lib** | Win32 wrappers around the core | Dead: last commit Dec 2019, nearly empty |
| **SharedGameFiles** | Shared-items project | Empty (no sources) |
| **WPFHost** (repo root, `D3DImageSample.sln`) | Separate C#/WPF host for D3DImage interop, .NET 4.5 | Dead: last commit Dec 2019 |
| **Inc/** | Oculus SDK 0.8 headers, DirectXTex, DDS | Legacy: the "oculus" render flags remain, but no OVR calls exist in the current code |

The future plan in `.github/copilot-instructions.md` is to drop UWP and D3D11 and possibly add a Vulkan renderer.

## 4. External dependencies

**NuGet** (via `packages.config`):
- `Microsoft.Direct3D.D3D12` 1.722.0-preview (Agility SDK), used by TreeClassic and RenderPlatform12
- `WinPixEventRuntime` 1.0.240308001
- Older copies of both are still in `packages/` for the stale UWP projects (1.610.2, 1.619.4, 1.0.170625002, 1.0.190604001)

**Vendored in the repo:**
- Dear ImGui with ImPlot (Win32 and DX12 backends)
- NVIDIA NvAPI (`nvapi64.lib`), used only to enable DXR validation messages in `Game/RenderPlatform12.cpp`
- `cgltf.h` for glTF loading, including optical materials and texture transforms
- DirectXTK12 headers (linked as `DirectXTK12.lib`) and D3DX12 helpers, copied into each RenderPlatform project
- DirectXTex and DDS loaders, plus the legacy Oculus SDK
- AMD FidelityFX SDK v2.4.0, headers plus the signed loader and upscaler DLLs (Git LFS), for the FSR upscaler
  (`ThirdParty/FidelityFX`, loaded at run time; see `Game/UPSCALING.md`)
- NVIDIA DLSS SDK 310.9.16, NGX headers and static libraries plus the signed `nvngx_dlss.dll` (Git LFS), for the DLSS
  upscaler (`ThirdParty/DLSS`, NGX linked into RenderPlatform12). Its license, unlike the others, carries release
  obligations (notify NVIDIA, attribution); see `ThirdParty/DLSS/README.md`

**Windows SDK and system libraries:** `d3d12`, `dxgi`, `d3dcompiler`, `d2d1`, `dwrite`, `windowscodecs` (WIC), and `fxc`/`dxc` for shaders.

**Toolchain:**
- Visual Studio 2022 MSBuild (build the solution, not individual projects)
- The Xbox targets need the Microsoft GDK and the UWP targets need the Windows 10 SDK; desktop builds need neither
- Running the GPU tests needs a DX12/DXR-capable GPU; the NVIDIA-specific DXR validation needs an NVIDIA driver

**No package manager beyond NuGet:** no vcpkg, CMake or CI pipeline.

See also: `.github/copilot-instructions.md` (build commands, conventions, DPI/window handling) and `TreeEngine/Tests/RenderRegression/README.md` (render tests), and `TreeEngine/Tests/Performance/README.md` (performance pass).
