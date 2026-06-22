### Ditto-Engine

[English Version](README.md)

Ditto 是一个自研 C++ 小型游戏引擎和编辑器。当前包含 GameObject 树、
组件系统、二进制场景保存/加载、Vulkan 优先的 RHI 渲染、OpenGL fallback、
GPU Instancing、Mono/C# 脚本、2D/3D 物理、音频、材质、Shader，以及基于 ImGui 的编辑器。

## 构建

### Visual Studio

打开 `Ditto.sln`，选择 `x64` 平台，构建 `Debug` 或 `Release`。

可选依赖：

- Vulkan SDK：检测到 `VULKAN_SDK` 时启用 Vulkan 渲染后端，并作为运行时首选后端。
- Assimp：当 `Ditto/3rdParty/Assimp` 中存在头文件和 `assimp-vc143-mt.lib` 时启用。
- .NET SDK：用于构建 `Ditto/3rdParty/Mono` 下的 C# 脚本 API。

### CMake

```powershell
cmake -S Ditto -B build-tests
cmake --build build-tests --config Debug
```

## 测试

```powershell
powershell -ExecutionPolicy Bypass -File Ditto/Tests/RunFullTests.ps1
```

测试覆盖文件/场景、C# 脚本解析和编译、模拟逻辑，以及渲染冒烟测试。

![icon](icon.png)

![content](content.png)
