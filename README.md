# Ditto Engine

[中文版](README_zh.md)

Ditto is a small C++ game engine and editor inspired by Unity-style workflows. It has a GameObject/component scene model, C# scripting through Mono, a custom RHI with DirectX 12/Vulkan/OpenGL backends, materials and shaders, 2D/3D physics, audio, UI, prefabs, project assets, and an ImGui-based editor.

The project is currently Windows-first. Windows editor/player builds are the supported target. Android is visible in the build UI as a future target, but the Android runtime/exporter is not implemented yet.

![content](content.png)

## Features

- Editor: hierarchy, inspector, project window, scene/game views, gizmos, asset previews, layout persistence, project creation, and Windows build export.
- Scene model: GameObject tree, Transform, Camera, Renderer, SpriteRenderer, UI, C# script components, prefab assets, and binary/JSON scene utilities.
- Rendering: RHI abstraction with DirectX 12, Vulkan, and OpenGL; render targets; material/shader assets; sprite and mesh rendering; GPU instancing; ImGui rendering for editor UI.
- Scripting: C# `MonoBehaviour`-style scripts, serialized fields, script compilation, runtime calls such as `Start`, `Update`, `FixedUpdate`, input, transform, camera, UI, audio, particles, and animation bindings.
- Input: shared native key codes with a C# `KeyCode`/mouse API direction.
- Physics: 2D and 3D physics components, colliders, rigid bodies, materials, raycast helpers, and deterministic test coverage.
- Audio: miniaudio-backed audio source support.
- Tests: native unit tests plus render smoke tests for OpenGL/Vulkan/DX12.

## Repository Layout

```text
Ditto/
  Editor/                 Editor panels, build system, inspector widgets
  Engine/
    Core/                 engine loop, scene, GameObject, input, C# bridge
    Graphics/
      RHI/                renderer interface and GL/Vulkan/DX12 backends
      Shaders/            shader asset parsing and shader compilation
      UI/                 screen-space UI renderer and font atlas
    Physics/              2D/3D physics and collision code
    Audio/                miniaudio wrapper
    Resources/            asset paths, asset database, references
  Assets/                 built-in shaders, materials, icons, sprites, models
  3rdParty/               vendored libraries: GLFW, GLAD, ImGui, GLM, Mono, Assimp, etc.
  Tests/                  native tests and render smoke executable
Projects/                 sample and local project directory
x64/                      Visual Studio build output, ignored by git
```

## Requirements

Minimal required to build the native editor:

- Windows 10/11.
- Windows 10/11 SDK (for DirectX 12 headers/libs).
- MSVC C++ toolchain (Platform Toolset v143) — Visual Studio with the C++ workload or Build Tools.


Optional (recommended):

- .NET SDK — needed only if you want the engine to build/regenerate the C# runtime DLLs and compile C# scripts.
- Vulkan SDK — required for the Vulkan backend and useful for providing `dxc.exe`/`spirv-cross.exe` used by the shader toolchain.


## Build

Open `Ditto.sln`, select `x64`, then build `Debug` or `Release`.

Command line:

```powershell
python build_editor.py
```

Or call MSBuild directly:

```powershell
& "D:\Visual Studio 2022\MSBuild\Current\Bin\MSBuild.exe" Ditto.sln `
  /p:Configuration=Debug /p:Platform=x64 /t:Ditto /m:1 /v:minimal /nologo
```

Useful build flags:

```powershell
/p:EnableDX12=false
/p:EnableVulkan=false
/p:EnableAssimp=false
```

## Running

The editor executable is produced at:

```text
x64/Debug/Ditto.exe
```

Runtime RHI order is:

```text
DirectX 12 -> Vulkan -> OpenGL
```

Force a backend with `DITTO_RHI`:

```powershell
$env:DITTO_RHI = "dx12"
$env:DITTO_RHI = "vulkan"
$env:DITTO_RHI = "opengl"
.\x64\Debug\Ditto.exe
```

## Tests

Full test flow:

```powershell
powershell -ExecutionPolicy Bypass -File Ditto/Tests/RunFullTests.ps1
```

Build and run render smoke manually:

```powershell
& "D:\Visual Studio 2022\MSBuild\Current\Bin\MSBuild.exe" Ditto\Tests\DittoRenderSmoke.vcxproj `
  /p:Configuration=Debug /p:Platform=x64 /p:PreBuildEventUseInBuild=false `
  /m:1 /v:minimal /nologo

Ditto\Tests\x64\Debug\DittoRenderSmoke.exe --backend dx12   --out TestOutput\RenderSmokeDX12
Ditto\Tests\x64\Debug\DittoRenderSmoke.exe --backend vulkan --out TestOutput\RenderSmokeVK
Ditto\Tests\x64\Debug\DittoRenderSmoke.exe --backend opengl --out TestOutput\RenderSmokeGL
```

The render smoke creates a cube, sprite, and UI image, renders to offscreen targets, reads back pixels, and writes diagnostics such as `render.ppm`, `render.scene.json`, and `render.pixels.json`.
