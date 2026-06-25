### Ditto-Engine

[English Version](README.md)

Ditto 是一个自研 C++ 小型游戏引擎和编辑器。当前包含 GameObject 树、
组件系统、JSON 场景保存/加载、Prefab 资产、Vulkan 优先的 RHI 渲染、OpenGL fallback、
GPU Instancing、Mono/C# 脚本、2D/3D 物理、音频、材质、Shader，以及基于 ImGui 的编辑器。

## 构建

打开 `Ditto.sln`，选择 `x64` 平台，构建 `Debug` 或 `Release`。
命令行辅助脚本走同一条 Visual Studio/MSBuild 构建路径：

```powershell
python build_editor.py
```

可选依赖：

- Vulkan SDK：检测到 `VULKAN_SDK` 时启用 Vulkan 渲染后端，并作为运行时首选后端。
- Assimp：当 `Ditto/3rdParty/Assimp` 中存在头文件和 `assimp-vc143-mt.lib` 时启用。
- .NET SDK：用于构建 `Ditto/3rdParty/Mono` 下的 C# 脚本 API。

## 测试

```powershell
powershell -ExecutionPolicy Bypass -File Ditto/Tests/RunFullTests.ps1
```

测试脚本会通过 MSBuild 从 `Ditto.sln` 构建 `DittoTests` 和
`DittoRenderSmoke`，然后运行文件/场景、C# 脚本、模拟逻辑，以及
OpenGL/Vulkan 渲染冒烟测试。

![icon](icon.png)

![content](content.png)
