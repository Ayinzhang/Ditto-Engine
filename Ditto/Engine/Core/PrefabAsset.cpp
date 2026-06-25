#include "PrefabAsset.h"

#include "GameObject.h"
#include "GameObjectJson.h"
#include "JsonSceneSchema.h"
#include "JsonValue.h"
#include "Logger.h"
#include "../Resources/AssetDatabase.h"
#include "../Resources/AssetPath.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Ditto::PrefabAsset
{
    namespace
    {
        constexpr int PREFAB_VERSION = 1;
        constexpr const char* PREFAB_FORMAT = "DittoPrefab";

        std::filesystem::path ResolvePrefabSource(const GameObject& instance)
        {
            if (!instance.prefabSourceGuid.empty())
            {
                std::filesystem::path path = Ditto::AssetDatabase::Get().PathForGuid(instance.prefabSourceGuid);
                if (!path.empty()) return path;
            }
            if (!instance.prefabSourcePath.empty())
                return Ditto::AssetPath::ResolveAssetPath(instance.prefabSourcePath);
            return {};
        }

        void MarkPrefabSource(GameObject& object, const std::filesystem::path& path)
        {
            object.prefabSourcePath = Ditto::AssetPath::NormalizeAssetKey(path.string());
            object.prefabSourceGuid = Ditto::AssetDatabase::Get().GuidForPath(path);
        }

        void ReplaceContents(GameObject& target, std::unique_ptr<GameObject> source)
        {
            GameObject* oldParent = target.parent;
            std::string oldSourcePath = target.prefabSourcePath;
            std::string oldSourceGuid = target.prefabSourceGuid;

            target.enabled = source->enabled;
            target.locked = source->locked;
            target.name = source->name;
            target.components = std::move(source->components);
            target.children = std::move(source->children);
            target.removeComps.clear();
            target.compMask = 0;

            for (auto& component : target.components)
            {
                if (!component) continue;
                component->gameObject = &target;
                target.compMask |= component->index;
            }
            for (auto& child : target.children)
                if (child) child->parent = &target;

            target.parent = oldParent;
            target.prefabSourcePath = std::move(oldSourcePath);
            target.prefabSourceGuid = std::move(oldSourceGuid);
        }

        std::string CompactJson(const Json::Value& value)
        {
            std::string text = Json::Write(value, 0);
            text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
            return text;
        }

        void DiffJson(const Json::Value& base, const Json::Value& current,
            const std::string& path, std::vector<Override>& overrides)
        {
            if (base.IsObject() && current.IsObject())
            {
                const auto& baseObject = base.AsObject();
                const auto& currentObject = current.AsObject();
                for (const auto& [key, currentValue] : currentObject)
                {
                    if (path == "root" && key == "prefab") continue;
                    auto it = baseObject.find(key);
                    std::string childPath = path + "." + key;
                    if (it == baseObject.end())
                    {
                        overrides.push_back({ childPath, "added", CompactJson(currentValue) });
                        continue;
                    }
                    DiffJson(it->second, currentValue, childPath, overrides);
                }
                for (const auto& [key, baseValue] : baseObject)
                {
                    if (path == "root" && key == "prefab") continue;
                    if (currentObject.find(key) == currentObject.end())
                        overrides.push_back({ path + "." + key, "removed", CompactJson(baseValue) });
                }
                return;
            }

            if (base.IsArray() && current.IsArray())
            {
                const auto& baseArray = base.AsArray();
                const auto& currentArray = current.AsArray();
                size_t shared = std::min(baseArray.size(), currentArray.size());
                for (size_t i = 0; i < shared; ++i)
                    DiffJson(baseArray[i], currentArray[i], path + "[" + std::to_string(i) + "]", overrides);
                for (size_t i = shared; i < currentArray.size(); ++i)
                    overrides.push_back({ path + "[" + std::to_string(i) + "]", "added", CompactJson(currentArray[i]) });
                for (size_t i = shared; i < baseArray.size(); ++i)
                    overrides.push_back({ path + "[" + std::to_string(i) + "]", "removed", CompactJson(baseArray[i]) });
                return;
            }

            if (CompactJson(base) != CompactJson(current))
                overrides.push_back({ path, "changed", CompactJson(current) });
        }
    }

    bool Save(const GameObject& root, const std::filesystem::path& path)
    {
        if (path.empty()) return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            DITTO_LOG_ERROR_STREAM("[PrefabAsset] Failed to open prefab for writing: " << path.string());
            return false;
        }

        Json::Value::Object json;
        json["format"] = PREFAB_FORMAT;
        json["version"] = PREFAB_VERSION;
        Json::Value rootJson = GameObjectJson::ToJson(root);
        rootJson.AsObject().erase("prefab");
        json["root"] = std::move(rootJson);

        file << Json::Write(json, 2);
        file << '\n';
        return file.good();
    }

    std::unique_ptr<GameObject> Load(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            DITTO_LOG_ERROR_STREAM("[PrefabAsset] Failed to open prefab for reading: " << path.string());
            return nullptr;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        Json::Value json;
        std::string error;
        if (!Json::Parse(buffer.str(), json, &error)
            || !json.IsObject()
            || !(json.Find("format") && json.Find("format")->String() == PREFAB_FORMAT)
            || !(json.Find("version") && json.Find("version")->Int() == PREFAB_VERSION)
            || !json.Find("root"))
        {
            DITTO_LOG_ERROR_STREAM("[PrefabAsset] Unsupported prefab file: " << path.string());
            return nullptr;
        }

        JsonSceneSchema::ValidationResult validation = JsonSceneSchema::ValidatePrefab(json, PREFAB_VERSION);
        if (!validation.ok)
        {
            DITTO_LOG_ERROR_STREAM("[PrefabAsset] Schema validation failed: " << validation.Summary());
            return nullptr;
        }

        auto root = std::make_unique<GameObject>(false);
        if (!GameObjectJson::FromJson(*root, *json.Find("root"), &error))
        {
            DITTO_LOG_ERROR_STREAM("[PrefabAsset] Invalid prefab root: " << error);
            return nullptr;
        }
        root->parent = nullptr;
        return root;
    }

    std::unique_ptr<GameObject> Instantiate(const std::filesystem::path& path)
    {
        std::unique_ptr<GameObject> instance = Load(path);
        if (instance) MarkPrefabSource(*instance, path);
        return instance;
    }

    std::vector<Override> CollectOverrides(const GameObject& instance)
    {
        std::vector<Override> overrides;
        std::filesystem::path source = ResolvePrefabSource(instance);
        if (source.empty()) return overrides;

        std::unique_ptr<GameObject> base = Load(source);
        if (!base) return overrides;

        Json::Value baseJson = GameObjectJson::ToJson(*base);
        baseJson.AsObject().erase("prefab");
        Json::Value currentJson = GameObjectJson::ToJson(instance);
        currentJson.AsObject().erase("prefab");
        DiffJson(baseJson, currentJson, "root", overrides);
        return overrides;
    }

    bool Apply(GameObject& instance)
    {
        std::filesystem::path source = ResolvePrefabSource(instance);
        if (source.empty()) return false;
        return Save(instance, source);
    }

    bool Revert(GameObject& instance)
    {
        std::filesystem::path source = ResolvePrefabSource(instance);
        if (source.empty()) return false;

        std::unique_ptr<GameObject> loaded = Load(source);
        if (!loaded) return false;

        ReplaceContents(instance, std::move(loaded));
        return true;
    }
}
