#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GameObject;

namespace Ditto::PrefabAsset
{
    struct Override
    {
        std::string path;
        std::string kind;
        std::string value;
    };

    bool Save(const GameObject& root, const std::filesystem::path& path);
    std::unique_ptr<GameObject> Load(const std::filesystem::path& path);
    std::unique_ptr<GameObject> Instantiate(const std::filesystem::path& path);
    std::vector<Override> CollectOverrides(const GameObject& instance);
    bool Apply(GameObject& instance);
    bool Revert(GameObject& instance);
}
