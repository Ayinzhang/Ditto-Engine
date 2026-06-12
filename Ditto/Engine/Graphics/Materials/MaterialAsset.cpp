#include "MaterialAsset.h"
#include "../../Core/Logger.h"
#include "../../Core/PathUtils.h"
#include "../../Core/ProjectManager.h"
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
        Project* project = ProjectManager::GetInstance().GetCurrentProject();
        if (project)
        {
            candidates.push_back(fs::path(project->path) / "Assets" / materialName);
            candidates.push_back(fs::path(project->path) / "Assets" / "Materials" / materialName);
        }
        if (!preferredRoot.empty())
        {
            candidates.push_back(preferredRoot / materialName);
            candidates.push_back(preferredRoot / "Assets" / materialName);
            candidates.push_back(preferredRoot / "Assets" / "Materials" / materialName);
        }
        candidates.push_back(PathUtils::ResolveAsset(materialName));
        candidates.push_back(PathUtils::ResolveAsset("Materials/" + materialName));

        if (p.extension().empty())
        {
            if (project)
                candidates.push_back(fs::path(project->path) / "Assets" / "Materials" / (materialName + ".mat"));
            if (!preferredRoot.empty())
            {
                candidates.push_back(preferredRoot / (materialName + ".mat"));
                candidates.push_back(preferredRoot / "Assets" / "Materials" / (materialName + ".mat"));
            }
            candidates.push_back(PathUtils::ResolveAsset("Materials/" + materialName + ".mat"));
        }

        for (const fs::path& candidate : candidates)
            if (!candidate.empty() && fs::exists(candidate))
                return fs::absolute(candidate);

        return {};
    }

    MaterialAsset LoadMaterialAsset(const std::string& materialName, const fs::path& preferredRoot)
    {
        MaterialAsset material = MakeDefaultMaterial(materialName.empty() ? "Default Material" : materialName);
        fs::path path = ResolveMaterialPath(materialName, preferredRoot);
        if (path.empty())
        {
            material.ok = materialName.empty();
            if (!material.ok) material.error = "Material not found: " + materialName;
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
            else if (key == "mainTexture")
            {
                material.mainTexturePath = StripQuotes(value);
            }
            else if (key == "color")
            {
                std::replace(value.begin(), value.end(), ',', ' ');
                std::istringstream ss(value);
                ss >> material.color.r >> material.color.g >> material.color.b >> material.color.a;
            }
        }

        if (material.shaderName.empty())
            material.shaderName = "Lit_Toon";
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

        file << "DittoMaterial 1\n";
        file << "shader = \"" << (material.shaderName.empty() ? "Lit_Toon" : material.shaderName) << "\"\n";
        file << "color = " << material.color.r << ", " << material.color.g << ", "
             << material.color.b << ", " << material.color.a << "\n";
        file << "mainTexture = \"" << material.mainTexturePath << "\"\n";
        return true;
    }
}
