#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "../../../3rdParty/GLM/glm.hpp"

namespace Ditto
{
    enum class ShaderPropertyType
    {
        Color,
        Float,
        Range,
        Texture2D
    };

    struct ShaderProperty
    {
        std::string name;
        std::string displayName;
        ShaderPropertyType type = ShaderPropertyType::Float;
        glm::vec4 colorDefault{ 1.0f };
        float floatDefault = 0.0f;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
        std::string textureDefault = "white";
    };

    struct ShaderAsset
    {
        std::string shaderName;
        std::string sourcePath;
        std::vector<ShaderProperty> properties;
        std::string engineHLSL;
        bool ok = false;
        std::string error;

        const ShaderProperty* FindProperty(const std::string& name) const;
        bool HasColorProperty() const;
        bool HasTexture2DProperty() const;
    };

    std::filesystem::path ResolveShaderPath(const std::string& shaderName,
        const std::filesystem::path& preferredRoot = {});
    ShaderAsset LoadShaderAsset(const std::string& shaderName,
        const std::filesystem::path& preferredRoot = {});
}
