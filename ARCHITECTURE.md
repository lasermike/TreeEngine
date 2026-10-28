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
  - Materials are either legacy Phong values or a `pbr` block. The `pbr` block uses the same glTF metallic-roughness BRDF as models: `BuildPbrShaderMaterial` in `GltfLoader.cpp` maps factors to `ShaderMaterial` for both, and `RenderManager::BindMaterialTexture` binds the maps.
- **Procedural generation:** `ModelGenerator` → `TreeModelGenerator` (`virtual TreeModel* Create() = 0`), plus `FixedTreeModelGenerator`, `PlaneModelGenerator` and `LSystemModelGenerator`. Their output becomes `TreeModel`/`TreeSegment` and then `TreeGeometry`. Trees use a skinned cylinder mesh and a pose cache.
- **Input:** `IInputManager::GetFrameInput(frame)` (`Game/InputManager.h`), with a separate implementation per platform.
- **Utilities:** `ThreadPool`, `DynamicProperties`, `Settings`, `StepTimer`, `GltfLoader`/`GltfModel`.

**Structs shared between C++ and HLSL.** These must stay byte-for-byte in sync with their shader counterparts:
- `DrawRecord` (`RenderPlatform.h`) ↔ `Shaders/Blur.hlsl`
- `ShaderMaterial` (`Materials.h`) ↔ `Shaders/Materials.fx`
- `DxrGeometryInfo` (`RenderPlatform.h`) ↔ `Shaders/SharedTypes.hlsli`
- `CBChangesPerPass`, `CBChangesEveryFrame` and `InstancedData` (`ConstBufferDefinitions.h`) ↔ `Shaders/SharedTypes.hlsli`

Shaders live in `Game/Shaders/`, including a DXR library (`RaytracingLibrary.hlsl`, with global and local root signatures). `buildShaders.bat` compiles them with fxc/dxc as part of the build.

## 3. Active vs. inactive projects

| Project | Role | Status |
|---|---|---|
| **TreeClassic** | Win32 desktop app, ImGui/ImPlot debug UI, render-test capture | **Active**: the main target (`Debug\|x64`, `Release\|x64`) |
| **RenderPlatform12** | D3D12 + DXR renderer DLL (sources mostly in `Game/`) | **Active** |
| **Game/** + **SharedEngine** | Engine core (~110 source files) | **Active**: most commits since 2025 |
| **SceneDefinitionTests** (`Tests/`) | Scene definition/persistence unit tests; `RenderRegression/` holds PowerShell-driven GPU image tests (DXR toggle, resize, skinning, shadow buffer) and C# image comparison | **Active**, Debug-only, no CI |
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
- `Microsoft.Direct3D.D3D12` 1.619.6 (Agility SDK), used by TreeClassic and RenderPlatform12
- `WinPixEventRuntime` 1.0.240308001
- Older copies of both are still in `packages/` for the stale UWP projects (1.610.2, 1.619.4, 1.0.170625002, 1.0.190604001)

**Vendored in the repo:**
- Dear ImGui with ImPlot (Win32 and DX12 backends)
- NVIDIA NvAPI (`nvapi64.lib`), used only to enable DXR validation messages in `Game/RenderPlatform12.cpp`
- `cgltf.h` for glTF loading, including optical materials and texture transforms
- DirectXTK12 headers (linked as `DirectXTK12.lib`) and D3DX12 helpers, copied into each RenderPlatform project
- DirectXTex and DDS loaders, plus the legacy Oculus SDK

**Windows SDK and system libraries:** `d3d12`, `dxgi`, `d3dcompiler`, `d2d1`, `dwrite`, `windowscodecs` (WIC), and `fxc`/`dxc` for shaders.

**Toolchain:**
- Visual Studio 2022 MSBuild (build the solution, not individual projects)
- The Xbox targets need the Microsoft GDK and the UWP targets need the Windows 10 SDK; desktop builds need neither
- Running the GPU tests needs a DX12/DXR-capable GPU; the NVIDIA-specific DXR validation needs an NVIDIA driver

**No package manager beyond NuGet:** no vcpkg, CMake or CI pipeline.

See also: `.github/copilot-instructions.md` (build commands, conventions, DPI/window handling) and `TreeEngine/Tests/RenderRegression/README.md` (render tests).
