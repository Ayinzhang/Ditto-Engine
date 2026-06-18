### Ditto-Engine

[中文版本](README_zh.md)

Ditto is a small self-developed C++ game engine/editor. It uses a GameObject tree
with component-driven behavior, binary scene save/load, OpenGL rendering with GPU
instancing, optional Vulkan RHI work, C# scripting through Mono, 2D/3D physics,
audio, materials, shaders, and an ImGui-based editor.

## Build

### Visual Studio

Open `Ditto.sln`, select `x64`, then build `Debug` or `Release`.

Optional dependencies:

- Vulkan SDK: enables the Vulkan renderer when `VULKAN_SDK` is available.
- Assimp: enabled when `Ditto/3rdParty/Assimp` contains headers and `assimp-vc143-mt.lib`.
- .NET SDK: builds the C# scripting API in `Ditto/3rdParty/Mono`.

### CMake

```powershell
cmake -S Ditto -B build-tests
cmake --build build-tests --config Debug
```

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File Ditto/Tests/RunFullTests.ps1
```

The test suite covers file/scene behavior, C# script parsing and compilation,
simulation behavior, and an OpenGL render smoke test.

![icon](icon.png)

![content](content.png)
