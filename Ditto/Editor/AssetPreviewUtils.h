#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Ditto
{
    struct MaterialAsset;
}

namespace Ditto::EditorUtils
{
    std::string LowerExt(std::string ext);
    bool IsImageExtension(const std::string& ext);
    bool IsMaterialExtension(const std::string& ext);
    std::vector<unsigned char> GenerateMaterialPreviewPixels(const MaterialAsset& material, int size);
    std::filesystem::path MakeUniquePath(const std::filesystem::path& desired);
    unsigned char* LoadImageRGBA(const std::filesystem::path& path, int* width, int* height, int* channels);
}
