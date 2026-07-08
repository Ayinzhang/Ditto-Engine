# Ditto Engine

[中文版本](README_zh.md)

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

Required for the native editor build:

- Windows 10/11.
- MSVC C++ toolchain with platform toolset `v143`.
- Windows 10/11 SDK. DirectX 12 uses the SDK-provided `d3d12.lib`, `dxgi.lib`, and `dxguid.lib`; there is no separate legacy DirectX SDK install.

Recommended for the easiest setup:

- Visual Studio 2022 with the C++ desktop workload, or Visual Studio Build Tools with the same C++ workload.
- .NET SDK, so the build can regenerate `DittoEngine.dll` and compile C# scripts.
- Vulkan SDK, installed manually with `VULKAN_SDK` visible to your shell/IDE. This enables the Vulkan backend and provides `dxc.exe`/`spirv-cross.exe`, which this project currently uses for shader compilation.

Bundled dependencies:

- Assimp is already included under `Ditto/3rdParty/Assimp` with headers, `assimp-vc143-mt.lib`, and `assimp-vc143-mt.dll`. You normally do not need to install Assimp separately.
- GLFW, GLAD, GLM, ImGui, ImGuizmo, miniaudio, VMA, stb, and Mono runtime files are also vendored under `Ditto/3rdParty`.

Toolchain notes:

- Visual Studio 2022 is the tested baseline because the project file uses `PlatformToolset v143`.
- Newer Visual Studio versions can be used if they can install/use the `v143` toolset, or if you retarget the project to a newer toolset and verify the build.
- VS Code is fine as an editor, but it does not replace MSVC/MSBuild. Use it together with Visual Studio Build Tools or a full Visual Studio install.

DX12 notes:

- DX12 is enabled by default through `EnableDX12=true`.
- DX12 itself does not require the Vulkan runtime.
- Runtime HLSL compilation normally uses `dxc.exe`. The engine will now try to locate `dxc.exe` (Vulkan SDK or Windows Kits) and, if `dxc` fails to compile a shader, automatically fall back to `fxc.exe` (Shader Model 5.0) where possible. If DX12 initialization (device/swapchain/shader compile) fails at runtime, the engine will mark the DX12 backend as unavailable and automatically try the next backend in the list (see "Runtime RHI fallback" below).

Vulkan notes:

- Vulkan is optional and auto-enabled only when the project finds `$(VULKAN_SDK)\Include\vulkan\vulkan.h` and `$(VULKAN_SDK)\Lib\vulkan-1.lib`.
- The Vulkan shader path uses `dxc.exe` to produce SPIR-V (`-spirv`) and `spirv-cross` when GLSL output is needed. The engine now validates the Vulkan shader toolchain during initialization; if the tools are missing or shader compilation fails, Vulkan will be disabled at runtime and the engine will fall back to the next available backend.
- If neither Vulkan nor DX12 can initialize properly, the engine will automatically fall back to the OpenGL backend, so the editor stays usable on systems without a full modern toolchain.

Window resizing and ImGui docking:

- The editor now detects display/framebuffer size changes and triggers a docking/layout rebuild when the ImGui display size changes. This fixes cases where inspector panels or other docked windows appear cropped at startup or after resizing.
- The engine notifies the active renderer when the framebuffer size changes so swapchains/backbuffers and GPU render targets are resized before rendering. This keeps ImGui and scene viewports synchronized with the window size.
- On high-DPI systems you may still need to enable per-monitor DPI awareness (application manifest or early Win32 DPI APIs) to get exact pixel-perfect scaling; see next section.

Runtime RHI fallback and shader tool notes:

- Runtime RHI order (default): DirectX 12 -> Vulkan -> OpenGL. The engine will attempt each candidate in order and move to the next if initialization fails.
- To force a specific backend use the `DITTO_RHI` environment variable (e.g., `dx12`, `vulkan`, or `opengl`).
- Recommended: install the Vulkan SDK to get `dxc.exe` and `spirv-cross.exe` for the smoothest shader toolchain; however the engine now provides safer fallback behavior when tools are missing or compilation fails.

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

## Build/Packaging Status

- Windows desktop: supported through the editor build window and MSBuild.
- DX12/OpenGL desktop rendering: supported.
- Vulkan desktop rendering: supported when the Vulkan SDK is installed.
- Android: UI entry exists, but exporter/runtime support is not implemented yet.

## Common Issues

- `No .NET SDKs were found`: install the .NET SDK or skip the C# pre-build only for native-only test builds with `/p:PreBuildEventUseInBuild=false`.
- Vulkan backend missing: install the Vulkan SDK and restart the shell/IDE so `VULKAN_SDK` is visible.
- DX12 shader compile failure: make sure `dxc.exe` is available. Installing the Vulkan SDK is the simplest current setup.
- Assimp DLL missing at runtime: ensure `Ditto/3rdParty/Assimp/bin` contains `assimp-vc143-mt.dll`; the post-build step copies it when Assimp is enabled.
