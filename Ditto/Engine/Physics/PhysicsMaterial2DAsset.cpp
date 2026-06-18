#include "PhysicsMaterial2DAsset.h"
#include "../Core/Logger.h"
#include "../Resources/AssetPath.h"

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
        std::string Trim(const std::string& s)
        {
            size_t first = 0;
            while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
            size_t last = s.size();
            while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
            return s.substr(first, last - first);
        }

        bool StartsWith(const std::string& s, const std::string& prefix)
        {
            return s.rfind(prefix, 0) == 0;
        }

        std::string StripQuotes(std::string s)
        {
            s = Trim(s);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        }
    }

    PhysicsMaterial2DAsset MakeDefaultPhysicsMaterial2D(const std::string& name)
    {
        PhysicsMaterial2DAsset material;
        material.materialName = name;
        material.friction = 0.6f;
        material.restitution = 0.2f;
        material.ok = true;
        return material;
    }

    fs::path ResolvePhysicsMaterial2DPath(const std::string& materialName, const fs::path& preferredRoot)
    {
        if (materialName.empty())
            return {};

        fs::path p(materialName);
        if (fs::exists(p))
            return fs::absolute(p);

        std::vector<fs::path> candidates;
        candidates.push_back(AssetPath::ResolveAssetPath(materialName, preferredRoot));
        candidates.push_back(AssetPath::ResolveTypedAssetPath(materialName, "PhysicsMaterials2D", nullptr, preferredRoot));

        if (p.extension().empty())
            candidates.push_back(AssetPath::ResolveTypedAssetPath(materialName, "PhysicsMaterials2D", ".physmat2d", preferredRoot));

        for (const fs::path& candidate : candidates)
            if (!candidate.empty() && fs::exists(candidate))
                return fs::absolute(candidate);

        return {};
    }

    PhysicsMaterial2DAsset LoadPhysicsMaterial2DAsset(const std::string& materialName, const fs::path& preferredRoot)
    {
        PhysicsMaterial2DAsset material = MakeDefaultPhysicsMaterial2D(
            materialName.empty() ? "Default Physics Material 2D" : materialName);
        fs::path path = ResolvePhysicsMaterial2DPath(materialName, preferredRoot);
        if (path.empty())
        {
            material.ok = materialName.empty();
            if (!material.ok) material.error = "Physics material 2D not found: " + materialName;
            return material;
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            material.ok = false;
            material.error = "Failed to open physics material 2D: " + path.string();
            return material;
        }

        material.sourcePath = path.string();
        material.materialName = path.stem().string();
        material.ok = true;

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            if (line.empty() || StartsWith(line, "#") || StartsWith(line, "//"))
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = Trim(line.substr(0, eq));
            std::string value = StripQuotes(line.substr(eq + 1));

            try
            {
                if (key == "friction")
                    material.friction = std::stof(value);
                else if (key == "restitution")
                    material.restitution = std::stof(value);
            }
            catch (...)
            {
                material.ok = false;
                material.error = "Invalid numeric value in physics material 2D: " + path.string();
                return material;
            }
        }

        material.friction = std::max(0.0f, material.friction);
        material.restitution = std::max(0.0f, material.restitution);
        return material;
    }

    bool SavePhysicsMaterial2DAsset(const PhysicsMaterial2DAsset& material, const fs::path& path)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            Logger::Get().Error("[PhysicsMaterial2DAsset] Failed to save material: " + path.string());
            return false;
        }

        file << "DittoPhysicsMaterial2D 1\n";
        file << "friction = " << std::max(0.0f, material.friction) << "\n";
        file << "restitution = " << std::max(0.0f, material.restitution) << "\n";
        return true;
    }
}
