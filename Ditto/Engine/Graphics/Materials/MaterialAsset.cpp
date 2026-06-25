#include "MaterialAsset.h"
#include "../../Core/Logger.h"
#include "../../Resources/AssetDatabase.h"
#include "../../Resources/AssetPath.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace Ditto
{
    namespace
    {
        static std::string Trim(const std::string& s)
        {
            size_t first = 0;
            while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
            size_t last = s.size();
            while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
            return s.substr(first, last - first);
        }

        static bool StartsWith(const std::string& s, const std::string& prefix)
        {
            return s.rfind(prefix, 0) == 0;
        }

        static std::string StripQuotes(std::string s)
        {
            s = Trim(s);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        }
    }

    MaterialAsset MakeDefaultMaterial(const std::string& name)
    {
        MaterialAsset material;
        material.materialName = name;
        material.shaderName = "Lit_Toon";
        material.color = glm::vec4(0.72f, 0.72f, 0.72f, 1.0f);
        material.ok = true;
        return material;
    }

    fs::path ResolveMaterialPath(const std::string& materialName, const fs::path& preferredRoot)
    {
        if (materialName.empty())
            return {};

        fs::path p(materialName);
        if (fs::exists(p))
            return fs::absolute(p);

        std::vector<fs::path> candidates;
        candidates.push_back(AssetPath::ResolveAssetPath(materialName, preferredRoot));
        candidates.push_back(AssetPath::ResolveTypedAssetPath(materialName, "Materials", nullptr, preferredRoot));

        if (p.extension().empty())
            candidates.push_back(AssetPath::ResolveTypedAssetPath(materialName, "Materials", ".mat", preferredRoot));

        for (const fs::path& candidate : candidates)
            if (!candidate.empty() && fs::exists(candidate))
                return fs::absolute(candidate);

        return {};
    }

    MaterialAsset LoadMaterialAsset(const std::string& materialName, const fs::path& preferredRoot)
    {
        MaterialAsset material;
        material.materialName = materialName.empty() ? "Material" : materialName;
        fs::path path = ResolveMaterialPath(materialName, preferredRoot);
        if (path.empty())
        {
            material.ok = false;
            material.error = materialName.empty()
                ? "Material path is empty"
                : "Material not found: " + materialName;
            return material;
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            material.ok = false;
            material.error = "Failed to open material: " + path.string();
            return material;
        }

        material.sourcePath = path.string();
        material.materialName = path.stem().string();
        material.ok = true;

        std::string line;
        std::string shaderGuid;
        std::string textureGuid;
        while (std::getline(file, line))
        {
            line = Trim(line);
            if (line.empty() || StartsWith(line, "#") || StartsWith(line, "//"))
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));

            if (key == "shader")
            {
                material.shaderName = StripQuotes(value);
            }
            else if (key == "shaderGuid")
            {
                shaderGuid = StripQuotes(value);
            }
            else if (key == "mainTexture")
            {
                material.mainTexturePath = StripQuotes(value);
            }
            else if (key == "mainTextureGuid")
            {
                textureGuid = StripQuotes(value);
            }
            else if (key == "color")
            {
                std::replace(value.begin(), value.end(), ',', ' ');
                std::istringstream ss(value);
                ss >> material.color.r >> material.color.g >> material.color.b >> material.color.a;
            }
        }

        std::string shaderFromGuid = AssetDatabase::Get().RelativePathForGuid(shaderGuid);
        if (!shaderFromGuid.empty())
            material.shaderName = shaderFromGuid;
        std::string textureFromGuid = AssetDatabase::Get().RelativePathForGuid(textureGuid);
        if (!textureFromGuid.empty())
            material.mainTexturePath = textureFromGuid;

        if (material.shaderName.empty())
        {
            material.ok = false;
            material.error = "Material has no shader: " + path.string();
        }
        return material;
    }

    bool SaveMaterialAsset(const MaterialAsset& material, const fs::path& path)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            Logger::Get().Error("[MaterialAsset] Failed to save material: " + path.string());
            return false;
        }

        std::string shaderName = AssetPath::NormalizeAssetKey(material.shaderName);
        if (shaderName.empty())
        {
            Logger::Get().Error("[MaterialAsset] Cannot save material without a shader: " + path.string());
            return false;
        }

        std::string mainTexturePath = AssetPath::NormalizeAssetKey(material.mainTexturePath);
        std::string shaderGuid = AssetDatabase::Get().GuidForPath(shaderName);
        std::string mainTextureGuid = AssetDatabase::Get().GuidForPath(mainTexturePath);

        file << "DittoMaterial 1\n";
        file << "shader = \"" << shaderName << "\"\n";
        file << "shaderGuid = \"" << shaderGuid << "\"\n";
        file << "color = " << material.color.r << ", " << material.color.g << ", "
             << material.color.b << ", " << material.color.a << "\n";
        file << "mainTexture = \"" << mainTexturePath << "\"\n";
        file << "mainTextureGuid = \"" << mainTextureGuid << "\"\n";
        return true;
    }
}
