# Current Issues

This file tracks known short-term engineering issues. Keep it UTF-8 encoded.

## Documentation

- Fixed: `README.md`, `README_zh.md`, and this file were rewritten as UTF-8 Markdown.
- Fixed: README build/feature summary now describes JSON scene files, prefab assets,
  and the Visual Studio solution helper script.
- Keep build, test, and dependency instructions updated when project files change.

## Visual Studio Build

- Fixed: the hand-maintained `Ditto.sln` path now matches the CMake/MSVC path by
  excluding GLM's C++ module interface file (`3rdParty/GLM/glm.cppm`) from
  compilation and disabling MSVC source module dependency scanning.
- Fixed: `build_editor.py` normalizes duplicate `Path`/`PATH` process environment
  entries before launching MSBuild, avoiding VC task startup failures on Windows.
- Fixed: `Ditto.vcxproj` now disables vcpkg MSBuild integration for the hand-maintained
  Visual Studio project, matching the CMake-generated MSVC targets.
- Fixed: Debug Visual Studio builds use the x64 host compiler and disable Just My
  Code instrumentation, avoiding MSVC C1001 failures on large third-party
  translation units such as ImGui.
- Fixed: `build_editor.py` decodes MSBuild output as UTF-8 so Chinese diagnostics
  are readable in PowerShell.
- Fixed: `Ditto.sln` is now the primary Windows build path; the solution carries
  the editor plus the `DittoTests` and `DittoRenderSmoke` test projects.
- Recommended next step: keep mirroring source-file changes into the VS project
  lists whenever files are split.

## Editor UI Consistency

- In progress: shared inspector picker widgets now live in
  `Editor/ComponentInspectorWidgets.*`.
- Fixed: component Inspector drawing implementations now live in the editor
  module (`Editor/ComponentInspectorWidgets.cpp`), leaving runtime component
  code focused on data, behavior, and serialization.
- Fixed: Project window filters generated sidecar files from the asset view:
  `.meta` files and script-compile `.dll` outputs no longer appear as project
  assets.
- In progress: project asset preview helpers now live in
  `Editor/AssetPreviewUtils.*`.
- Fixed: mesh, material, sprite, texture, audio clip, font, and Rigidbody2D
  physics-material inspector pickers now use the shared asset object-field helper.
- Fixed: Rigidbody2D now uses real `.physmat2d` assets for 2D friction and
  restitution overrides.
- Recommended next step: keep moving new inspector fields onto shared helpers
  when new component asset references are added.

## Shader and Asset Paths

- In progress: `Engine/Resources/AssetPath.*` centralizes project-relative,
  `Assets/`-prefixed, absolute, typed, and preferred-root asset resolution.
- Fixed: shader, material, texture, audio, mesh, physics mesh, and UI font/shader
  loads now route through the shared path helpers in the touched code paths.
- Fixed: file tests cover project-relative assets, `Assets/`-prefixed paths,
  typed material/shader lookup, absolute path normalization, and preferred roots.
- Fixed: component scene serialization normalizes asset references before writing,
  so absolute project asset paths are saved as project-relative keys.
- Fixed: material saving normalizes `mainTexture` references before writing `.mat`
  files.
- Fixed: basic asset database support now creates `.meta` files, resolves `guid:`
  references, keeps meta paths fresh on touched asset operations, and gives
  scenes/materials/scripts GUID-backed references for rename resilience.
- Fixed: asset database scan now records lightweight import data (extension,
  size, content hash, imported flag) and reports missing meta, invalid meta,
  duplicate GUID, and missing GUID-reference diagnostics.
- Fixed: asset import cache now persists to `.ditto/import-cache.txt` and reloads
  on project scans, giving the editor a durable baseline for import state.
- Fixed: import records now keep artifact and dependency fields covered by tests.
- Fixed: `.physmat2d` assets are project-relative, have default project assets,
  editor creation, inspector editing, scene serialization, and physics coverage.
- Recommended next step: evolve the persisted import records into artifact
  outputs with dependency tracking, incremental reimport decisions, and editor
  repair UI for broken or duplicate metadata.

## Runtime Lifecycle

- Fixed: Play/Game mode startup now uses shared `Engine::EnterPlayMode()` logic for
  script preparation, `Start()`, physics rebuild, and play-on-awake components.
- Fixed: Stop now uses `Engine::ExitPlayMode()` for script teardown, physics clear,
  audio stop, and authored rotation restoration.
- Fixed: the main runtime loop is split into explicit input, Play update,
  physics, collision dispatch, UI interaction, runtime component update, and
  render stages.
- Fixed: `CSharpScriptComponent::OnDestroy()` now resets lifecycle state even
  when no managed runtime instance exists.
- Fixed: script lifecycle tests now cover idempotent preparation and stop calls,
  including repeated prepare/stop paths.
- Recommended next step: add dedicated tests around full engine
  `EnterPlayMode()`/`ExitPlayMode()` ordering once runtime startup can be
  exercised without a GLFW window.

## Architecture Follow-Ups

- Fixed: `ComponentIndex`, `Component`, and `DerivedFromComponent` now live in
  `Engine/Core/Component.h` instead of being embedded in `GameObject.h`.
- In progress: runtime/editor singleton access is moving through
  `Ditto::RuntimeContext`; `GameObject.cpp` and shared inspector widgets now use
  context accessors with legacy globals only as fallback.
- Fixed: scene save/load and snapshot serialization now live in
  `Engine/Core/SceneSerialization.cpp`, leaving `Scene.cpp` focused on runtime
  scene behavior.
- Fixed: scene object picking and raycast mesh caching logic now live in
  `Engine/Core/SceneRaycast.cpp`.
- Fixed: scene loading is now intentionally development-only: this build reads
  exactly the current scene version instead of keeping old-version compatibility.
- Fixed: normal scene serialization/load traces now log at verbose level, keeping
  test and editor console output focused on actionable messages.
- Continue extracting small editor/runtime utility modules from large files such as
  `GameObject.cpp`, `Editor.cpp`, `ProjectWindow.cpp`, and `CSharpScript.cpp`.
- Replace remaining global editor/scene state with explicit typed services over time.
- Keep Vulkan as the primary rendering path and preserve OpenGL as the reliable
  fallback while Vulkan reaches full feature parity.

## Rendering Validation

- Fixed: render smoke now runs through backend-selectable RHI readback instead of
  direct OpenGL pixel reads.
- Fixed: Vulkan render smoke covers mesh, sprite, UI, render target, shader, and
  pixel readback when the Vulkan SDK is available.
- Fixed: OpenGL render-target depth clears no longer inherit a previous pipeline's
  disabled depth-write state.
- Fixed: shader assets now narrow pipeline vertex attributes to the fields the
  vertex shader actually consumes, avoiding Vulkan validation warnings for
  unlit/sprite shaders that do not read normal or UV inputs.
- Fixed: CMake-generated MSVC targets disable automatic vcpkg MSBuild wildcard
  linking/AppLocal steps, removing the missing `pwsh.exe` build noise.
- Fixed: Debug links ignore release CRT defaultlibs advertised by local
  third-party release `.lib` files, removing the `LNK4098` warning while keeping
  Ditto objects on Debug CRT.
- Fixed: render smoke now covers multiple render targets and destroy/recreate
  readback paths on both OpenGL and Vulkan.
- Fixed: render smoke now has a `--stress` loop and full-test coverage runs
  repeated render-target create/readback/destroy passes on both OpenGL and
  Vulkan.
- Recommended next step: add heavier Vulkan stress coverage for swapchain resize,
  device-loss-like teardown/reinit loops, and multi-pass render graphs.

## Logging and Test Signal

- Fixed: logger now has a `Verbose` level and configurable console threshold.
- Fixed: high-frequency successful asset/shader/scene load logs are verbose by
  default; full tests now surface pass/fail signal without routine engine noise.
- Fixed: C# compile diagnostics now return structured success/error/warning
  counts plus compiler output through `CompileScriptDetailed()`.
- Fixed: C# test fixtures are warning-free by default, and compiler failure tests
  assert structured error reporting instead of relying on noisy console output.
- Recommended next step: route structured diagnostics into the editor UI so script
  errors can be grouped by asset and source location.
