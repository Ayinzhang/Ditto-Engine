#pragma once
#include <string>
#include <filesystem>

// Centralized, cross-platform path resolution.
//
// Rationale: shader/asset lookup used to rely on hard-coded ladders of
// relative-path guesses ("../../Ditto/Ditto/Assets/...") that depended on the
// process working directory and broke when the binary moved. PathUtils anchors
// everything to the executable's own location instead, which is stable
// regardless of where the program is launched from.
namespace PathUtils
{
    // Absolute directory containing the running executable. Computed once and
    // cached. Falls back to the current working directory if the platform
    // query fails.
    const std::filesystem::path& GetExecutableDir();

    // Walk up from `start` (inclusive) looking for the first ancestor directory
    // that contains a child entry named `marker` (e.g. "Assets"). Returns an
    // empty path if none is found before the filesystem root.
    std::filesystem::path FindAncestorContaining(const std::filesystem::path& start,
                                                 const std::string& marker);

    // Resolve a resource given as a path relative to an "Assets" directory
    // (e.g. "Shaders/Vertex.glsl" or "Models/Cube.obj").
    //
    // Search order:
    //   1. <preferredRoot>/Assets/<relative>      (when preferredRoot non-empty)
    //   2. <preferredRoot>/<relative>             (preferredRoot already an Assets dir)
    //   3. <exeDir>/Assets/<relative>
    //   4. nearest ancestor of exeDir that has an "Assets" dir -> .../Assets/<relative>
    //   5. <cwd>/Assets/<relative>                (last-resort dev fallback)
    //
    // Returns the first path that exists. If none exist, returns the option (3)
    // path so callers still get a sensible, absolute, log-friendly value.
    std::filesystem::path ResolveAsset(const std::string& relativeToAssets,
                                       const std::filesystem::path& preferredRoot = {});
}
