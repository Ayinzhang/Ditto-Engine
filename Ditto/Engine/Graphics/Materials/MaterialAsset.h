#pragma once
#include <filesystem>
#include <string>
#include "../../../3rdParty/GLM/glm.hpp"

namespace Ditto
{
    struct MaterialAsset
    {
        std::string materialName;
        std::string sourcePath;
        std::string shaderName;
        glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string mainTexturePath;
        bool ok = false;
        std::string error;
    };

    std::filesystem::path ResolveMaterialPath(const std::string& materialName,
        const std::filesystem::path& preferredRoot = {});
    MaterialAsset LoadMaterialAsset(const std::string& materialName,
        const std::filesystem::path& preferredRoot = {});
    bool SaveMaterialAsset(const MaterialAsset& material,
        const std::filesystem::path& path);
    MaterialAsset MakeDefaultMaterial(const std::string& name = "New Material");
}
