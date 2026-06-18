#pragma once

#include <filesystem>
#include <string>

namespace Ditto
{
    struct PhysicsMaterial2DAsset
    {
        std::string materialName;
        std::string sourcePath;
        float friction = 0.6f;
        float restitution = 0.2f;
        bool ok = false;
        std::string error;
    };

    PhysicsMaterial2DAsset MakeDefaultPhysicsMaterial2D(const std::string& name = "New Physics Material 2D");
    std::filesystem::path ResolvePhysicsMaterial2DPath(const std::string& materialName,
        const std::filesystem::path& preferredRoot = {});
    PhysicsMaterial2DAsset LoadPhysicsMaterial2DAsset(const std::string& materialName,
        const std::filesystem::path& preferredRoot = {});
    bool SavePhysicsMaterial2DAsset(const PhysicsMaterial2DAsset& material,
        const std::filesystem::path& path);
}
