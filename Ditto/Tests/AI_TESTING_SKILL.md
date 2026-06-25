# Ditto AI Testing Skill

Use this document when an AI assistant needs to validate Ditto Engine changes.
The full test flow is ordered and must stay ordered:

1. File operations
2. C# scripting
3. Rendering and shader output
4. Physics simulation

Do not start C#, rendering, or simulation validation until the file-operation
stage is green. Later stages depend on scene files, asset paths, script paths,
shader paths, output directories, and diagnostic dumps being trustworthy first.

## Commands

Build the current test executables through Visual Studio/MSBuild:

```powershell
msbuild Ditto.sln /p:Configuration=Debug /p:Platform=x64 /t:DittoTests /m:1 /v:minimal /nologo
msbuild Ditto.sln /p:Configuration=Debug /p:Platform=x64 /t:DittoRenderSmoke /m:1 /v:minimal /nologo
```

Run the full ordered flow:

```powershell
powershell -ExecutionPolicy Bypass -File Ditto\Tests\RunFullTests.ps1
```

Run only file-operation tests:

```powershell
x64\Debug\DittoTests.exe --stage file
```

Run only C# scripting tests:

```powershell
x64\Debug\DittoTests.exe --stage csharp
```

Run only simulation tests:

```powershell
x64\Debug\DittoTests.exe --stage simulation
```

Dump a sample scene tree for AI inspection:

```powershell
x64\Debug\DittoTests.exe --dump-scene
```

## Current Coverage

The `file` stage currently covers:

- GameObject component add/remove and component mask updates.
- GameObject reparenting and cycle prevention.
- Scene snapshot serialization and restoration.
- Scene save/load through real files.
- Corrupt scene file rejection.
- Path ancestor lookup.
- JSON scene/component dump for AI-side inspection.

The `csharp` stage currently covers:

- Parsing script fields from current C# source files.
- Supported serialized field types: bool, int, float, double, string, Vector2,
  Vector3, and Vector4.
- `[SerializeField]` private fields.
- `[HideInInspector]` fields being excluded from editable metadata.
- CSharpScript component serialization and deserialization.
- Attaching CSharpScript components to GameObject instances.
- Compiling a fixture script against the current `DittoEngine.dll` API.

The C# API assembly is built by the Visual Studio test projects before native
compilation when `dotnet` is available.

The expected API output is:

```text
Ditto/3rdParty/Mono/bin/Release/netstandard2.0/DittoEngine.dll
```

The `render` stage currently covers:

- A hidden-window OpenGL render smoke test.
- Loading/parsing a user shader through `ShaderAsset`.
- Loading/parsing a Material asset and rendering through `Renderer.materialPath`.
- Creating an RHI pipeline from generated engine HLSL.
- Rendering a cube into an offscreen render target.
- Pixel-stat validation that the image is not mostly background.
- A rendered PPM image per shader case.
- A scene/component JSON dump.
- A shader/material JSON dump.
- Pixel statistics JSON.
- A deterministic pass/fail result before any AI visual interpretation.

Default output directory:

```text
x64/TestOutput/RenderSmoke/
```

Default render outputs:

```text
render.ppm
render.scene.json
render.shader.json
render.material.json
render.pixels.json
RenderSmoke.mat
UnlitSmoke.shader
```

Run the render smoke directly:

```powershell
x64\Debug\DittoRenderSmoke.exe --out x64\TestOutput\RenderSmoke
```

Run it with a user shader:

```powershell
x64\Debug\DittoRenderSmoke.exe --shader Ditto\Assets\Shaders\Lit_Toon.shader --out x64\TestOutput\LitToon
```

The `simulation` stage currently covers:

- Dynamic body gravity.
- Multiple colliders sharing one rigidbody without duplicate integration.
- Kinematic bodies not being integrated by physics.
- Dynamic vs static position correction from a deterministic collision.

Planned simulation extensions:

- Dynamic vs static narrow-phase collision detection fixtures.
- Kinematic body pushing a dynamic body.
- Trigger colliders not applying impulses.

## AI Validation Rules

- Treat nonzero process exit as failure.
- Read stdout/stderr and report the first failing test with its stage.
- Use `--dump-scene` output to compare expected components against rendered or
  simulated results.
- Do not rely on screenshots alone. Rendering tests must also provide JSON
  state and pixel statistics.
- Keep generated build outputs under `x64/`; they are ignored by git.
- Keep test assets and expected outputs small and deterministic.

## Adding Tests

Register new tests with an explicit stage:

```cpp
TEST_CASE("file", MyFileTest)
TEST_CASE("csharp", MyCSharpTest)
TEST_CASE("render", MyRenderTest)
TEST_CASE("simulation", MySimulationTest)
```

The test runner executes stages in this fixed order: `file`, `csharp`,
`render`, `simulation`.
