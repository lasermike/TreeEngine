# Copilot Instructions for TreeEngine

## Build

TreeEngine uses MSBuild via Visual Studio 2022. Open `TreeEngine/TreeEngine.sln` for the main engine solution.

**Build configurations:**
- `Debug|x64` / `Release|x64` — Win32 desktop (TreeClassic)
- `Debug|x64` / `Release|x64` — UWP (TreePC, requires Windows 10 SDK)
- `Debug|Gaming.Xbox.Scarlett.x64` / `Debug|Gaming.Xbox.XboxOne.x64` — Xbox (TreeXbox, requires GDK)
- `Debug12` / `Release` variants toggle between DX11 and DX12 code paths.  Deprecated.

Of these configurations, only `Debug|x64` and `Release|x64` are actively maintained. The UWP and Xbox targets are not currently built or tested.  UWP will likely be deprecated in the future.


**Shader compilation:** `TreeEngine/Game/Shaders/buildShaders.bat` compiles HLSL → `.cso` via `fxc.exe`/`dxc.exe`. PostBuild steps in vcxproj files also handle this.

There are no automated tests or CI pipelines.

## Simulation
TreeEngine is a plant and tree growth simulator.  Plants are defined by L-Systems (in `LSystemModelGenerator.cpp`) and grow over time based on growth rules.  The simulation runs in real-time, with the main loop updating plant growth, physics, and rendering each frame.

The main simulation loop is in `Game.cpp` (`Game::Update()`), which updates the scene graph, runs procedural generation, and handles input.

A number of pre-defined scenes are used for testing.  The scenes are defined in the GameLoader.cpp file as methods prefixed with "Load".  Scene objects are intitialized with a set of parameters.  For plants and trees, an axiom and a set of production rules are defined.  The L-System generator then iteratively applies the production rules to grow the plant over time.  The product rules are loosely based on the research paper "The Algorithmic Beauty of Plants" by Przemyslaw Prusinkiewicz and Aristid Lindenmayer.

## Architecture

TreeEngine is a C++ DirectX 3D engine focused on procedural tree/plant generation. It targets Windows desktop (Win32), UWP, and Xbox (One/Scarlett).  The engine is organized into several key components: Platform application, game simulation, and rendering abstraction.

### Rendering abstraction

The engine uses a pluggable renderer pattern via `RenderPlatform` (abstract base in `Game/RenderPlatform.h`), with platform-specific implementations loaded as DLLs:

| Project | API | Platform |
|---|---|---|
| `RenderPlatform12` | DirectX 12 | Win32 desktop |
| `RenderPlatform12UWP` | DirectX 12 | UWP |
| `RenderPlatform11UWP` | DirectX 11 | UWP (fallback) |
| `RenderPlatform12Xbox` | DirectX 12 | Xbox |

Rendereing DLLs may be unloaded and reloaded while the simulation is running, allowing for live shader updates and hot-reloading of rendering code. 

Conditional compilation selects the API: `TREE3D12` for DX12, `TREE3D11` for DX11. DXR ray tracing is gated behind `DXR_ENABLED`.  In the future we will remove TREE3D11 and UWP, and potentially implement a Vulkan renderer for cross-platform support.

### Application targets

- **TreeClassic** — Primary Win32 desktop app. Imports `SharedEngine.vcxitems`, includes ImGui debug UI, uses `RenderPlatform12` DLL.
- **TreePC** — UWP app using `RenderPlatform11UWP` + `RenderPlatform12UWP`.
- **TreeXbox** — Xbox native app using `RenderPlatform12Xbox`.
- **TreeWin32Dll / TreeWin32Lib** — Win32 wrappers around the shared engine core.

### Shared code

`SharedEngine/` is a `.vcxitems` shared project imported by application targets. `Game/` contains the core engine code used across all targets:

- **Game loop:** `Game.cpp` — main update/render loop
- **Rendering:** `RenderManager.cpp` — render pass orchestration, `Materials.cpp` — shader materials, `ShadowMap.cpp`
- **Scene:** `SceneRoot.cpp`, `WorldObject.cpp` (base class for all game objects), `Player.cpp`, `Camera`
- **Procedural generation:** `TreeModel.cpp`, `TreeModelGenerator.cpp`, `LSystemModelGenerator.cpp` — L-System based tree generation
- **Geometry:** `Geometry.cpp`, `GeometryGenerator.cpp`, `TreeGeometry.cpp`
- **Input:** `InputManager.h` (abstract interface), platform-specific implementations
- **Threading:** `ThreadPool.h`

### WPFHost

`WPFHost/D3DImageSample.sln` is a separate C#/WPF + C++ project for hosting D3D content in a WPF `D3DImage` control.

## Conventions

- Member variables use `m_` prefix (e.g., `m_renderManager`, `m_showPerfGraph`)
- Classes and methods use PascalCase
- DirectX COM patterns throughout (AddRef/Release, ComPtr)
- HLSL shaders live in `Game/Shaders/` and compile to `.cso`
- Textures are DDS format, fonts are ABC+TGA
- Platform differences are handled via `#if defined(TREE3D12)` / `#if defined(TREE3D11)` preprocessor guards
- ImGui (with ImPlot) is integrated for debug UI — toggled via `UpdateDebugUI()` in `Game.cpp`
- Matrix convention: second row is view/look vector (scaled), last row is translation
