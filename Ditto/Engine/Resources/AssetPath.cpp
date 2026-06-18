#include "AssetPath.h"
#include "../Core/PathUtils.h"
#include "../Core/ProjectManager.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace Ditto::AssetPath
{
    namespace
    {
        bool Exists(const fs::path& path)
        {
            std::error_code ec;
            return fs::exists(path, ec);
        }

        fs::path Normalize(const fs::path& path)
        {
            std::error_code ec;
            fs::path absolute = path.is_absolute() ? path : fs::absolute(path, ec);
            if (ec) absolute = path;
            return absolute.lexically_normal();
        }

        bool IsInsideAssets(const fs::path& relative)
        {
            auto it = relative.begin();
            if (it == relative.end()) return false;
            std::string first = it->string();
            std::transform(first.begin(), first.end(), first.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return first == "assets";
        }

        fs::path StripAssetsPrefix(const fs::path& path)
        {
            if (!IsInsideAssets(path)) return path;

            fs::path stripped;
            auto it = path.begin();
            ++it;
            for (; it != path.end(); ++it)
                stripped /= *it;
            return stripped;
        }

        fs::path AppendDefaultExtension(fs::path path, const char* defaultExtension)
        {
            if (!defaultExtension || !*defaultExtension || path.has_extension())
                return path;

            std::string ext = defaultExtension;
            if (!ext.empty() && ext.front() != '.')
                ext.insert(ext.begin(), '.');
            path += ext;
            return path;
        }

        fs::path ExistingOrFallback(const std::vector<fs::path>& candidates, const fs::path& fallback)
        {
            for (const fs::path& candidate : candidates)
            {
                if (!candidate.empty() && Exists(candidate))
                    return Normalize(candidate);
            }
            return Normalize(fallback.empty() && !candidates.empty() ? candidates.front() : fallback);
        }
    }

    fs::path CurrentProjectRoot()
    {
        Project* project = ProjectManager::GetInstance().GetCurrentProject();
        return project ? fs::path(project->path) : fs::path{};
    }

    fs::path CurrentProjectAssetsRoot()
    {
        fs::path root = CurrentProjectRoot();
        return root.empty() ? fs::path{} : root / "Assets";
    }

    fs::path ResolveAssetPath(const std::string& assetPath, const fs::path& preferredRoot)
    {
        if (assetPath.empty()) return {};

        fs::path input(assetPath);
        if (input.is_absolute())
            return Normalize(input);

        fs::path relative = StripAssetsPrefix(input);
        fs::path projectRoot = CurrentProjectRoot();

        std::vector<fs::path> candidates;
        if (!projectRoot.empty())
            candidates.push_back(projectRoot / "Assets" / relative);
        if (!preferredRoot.empty())
        {
            candidates.push_back(preferredRoot / "Assets" / relative);
            candidates.push_back(preferredRoot / relative);
        }
        candidates.push_back(PathUtils::ResolveAsset(relative.generic_string(), preferredRoot));

        fs::path fallback = !projectRoot.empty()
            ? projectRoot / "Assets" / relative
            : PathUtils::ResolveAsset(relative.generic_string(), preferredRoot);
        return ExistingOrFallback(candidates, fallback);
    }

    fs::path ResolveTypedAssetPath(const std::string& assetPath,
                                   const char* folder,
                                   const char* defaultExtension,
                                   const fs::path& preferredRoot)
    {
        if (assetPath.empty()) return {};

        fs::path input = AppendDefaultExtension(fs::path(assetPath), defaultExtension);
        if (input.is_absolute())
            return Normalize(input);

        fs::path relative = StripAssetsPrefix(input);
        fs::path folderPath = folder ? fs::path(folder) : fs::path{};
        bool alreadyTyped = !folderPath.empty() && relative.begin() != relative.end()
            && relative.begin()->string() == folderPath.begin()->string();

        std::vector<fs::path> relativeCandidates;
        relativeCandidates.push_back(relative);
        if (!folderPath.empty() && !alreadyTyped)
            relativeCandidates.push_back(folderPath / relative);

        fs::path projectRoot = CurrentProjectRoot();
        std::vector<fs::path> candidates;
        for (const fs::path& candidateRelative : relativeCandidates)
        {
            if (!projectRoot.empty())
                candidates.push_back(projectRoot / "Assets" / candidateRelative);
            if (!preferredRoot.empty())
            {
                candidates.push_back(preferredRoot / "Assets" / candidateRelative);
                candidates.push_back(preferredRoot / candidateRelative);
            }
            candidates.push_back(PathUtils::ResolveAsset(candidateRelative.generic_string(), preferredRoot));
        }

        fs::path fallbackRelative = relativeCandidates.back();
        fs::path fallback = !projectRoot.empty()
            ? projectRoot / "Assets" / fallbackRelative
            : PathUtils::ResolveAsset(fallbackRelative.generic_string(), preferredRoot);
        return ExistingOrFallback(candidates, fallback);
    }

    std::string ToProjectRelativeAssetPath(const fs::path& path)
    {
        if (path.empty()) return {};

        fs::path assetsRoot = CurrentProjectAssetsRoot();
        if (assetsRoot.empty())
            return path.generic_string();

        fs::path normalizedPath = Normalize(path);
        fs::path normalizedAssets = Normalize(assetsRoot);
        fs::path relative = normalizedPath.lexically_relative(normalizedAssets);
        if (relative.empty() || IsInsideAssets(relative) || relative.string().find("..") != std::string::npos)
            return path.generic_string();
        return relative.generic_string();
    }

    std::string NormalizeAssetKey(const std::string& path)
    {
        if (path.empty()) return {};

        fs::path p(path);
        std::string key = p.is_absolute()
            ? ToProjectRelativeAssetPath(p)
            : StripAssetsPrefix(p).generic_string();
        std::replace(key.begin(), key.end(), '\\', '/');
        return key;
    }
}
