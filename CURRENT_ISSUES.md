# Current Issues

This file tracks known short-term engineering issues. Keep it UTF-8 encoded.

## Documentation

- Fixed: `README.md`, `README_zh.md`, and this file were rewritten as UTF-8 Markdown.
- Keep build, test, and dependency instructions updated when project files change.

## Editor UI Consistency

- In progress: shared inspector picker widgets now live in
  `Editor/ComponentInspectorWidgets.*`.
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
- Fixed: `.physmat2d` assets are project-relative, have default project assets,
  editor creation, inspector editing, scene serialization, and physics coverage.
- Recommended next step: build a real asset database later: GUIDs, `.meta` files,
  import cache, and stable cross-file references.

## Runtime Lifecycle

- Fixed: Play/Game mode startup now uses shared `Engine::EnterPlayMode()` logic for
  script preparation, `Start()`, physics rebuild, and play-on-awake components.
- Fixed: Stop now uses `Engine::ExitPlayMode()` for script teardown, physics clear,
  audio stop, and authored rotation restoration.
- Recommended next step: add dedicated tests around script `Start`/`Update` ordering
  once the runtime can be exercised without a GLFW window.

## Architecture Follow-Ups

- Continue extracting small editor/runtime utility modules from large files such as
  `GameObject.cpp`, `Editor.cpp`, `ProjectWindow.cpp`, and `CSharpScript.cpp`.
- Replace remaining global editor/scene state with explicit typed services over time.
- Keep OpenGL as the stable rendering path while Vulkan reaches feature parity.
