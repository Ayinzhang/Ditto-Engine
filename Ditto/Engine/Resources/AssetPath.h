#pragma once

#include <filesystem>
#include <string>

namespace Ditto::AssetPath
{
    std::filesystem::path CurrentProjectRoot();
    std::filesystem::path CurrentProjectAssetsRoot();

    std::filesystem::path ResolveAssetPath(const std::string& assetPath,
                                           const std::filesystem::path& preferredRoot = {});
    std::filesystem::path ResolveTypedAssetPath(const std::string& assetPath,
                                                const char* folder,
                                                const char* defaultExtension = nullptr,
                                                const std::filesystem::path& preferredRoot = {});

    std::string ToProjectRelativeAssetPath(const std::filesystem::path& path);
    std::string NormalizeAssetKey(const std::string& path);
}
