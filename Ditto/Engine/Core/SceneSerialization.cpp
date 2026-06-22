#include "Scene.h"
#include "Logger.h"
#include "../Resources/AssetReferenceIO.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

std::uint32_t g_sceneLoadingVersion = 0;

namespace
{
    struct SceneLoadingVersionScope
    {
        explicit SceneLoadingVersionScope(std::uint32_t version)
            : previous(g_sceneLoadingVersion)
        {
            g_sceneLoadingVersion = version;
        }

        ~SceneLoadingVersionScope()
        {
            g_sceneLoadingVersion = previous;
        }

        std::uint32_t previous;
    };

    struct SceneHeader
    {
        char magic[4];
        uint32_t version;
        uint32_t gameObjectCount;
        uint64_t fileSize;
    };

    // Development-only scene format. Compatibility with older scene versions is
    // intentionally not preserved while the engine schema is still moving.
    constexpr uint32_t SCENE_VERSION = 16;
    constexpr char SCENE_MAGIC[4] = { 'S', 'C', 'N', '\0' };
}

void Scene::WriteToStream(std::ostream& file)
{
    SceneHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, SCENE_MAGIC, 4);
    header.version = SCENE_VERSION;
    header.fileSize = 0;
    header.gameObjectCount = 1;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    Ditto::AssetReferenceIO::WriteString(file, name);

    rootGameObject->Serialize(file);

    // Backfill total size into the header.
    std::streampos endPos = file.tellp();
    header.fileSize = static_cast<uint64_t>(endPos);
    file.seekp(0);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

bool Scene::SaveScene(const std::string& filepath)
{
    DITTO_LOG_VERBOSE_STREAM("[Scene::SaveScene] Starting save to: " << filepath);

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Failed to open file for writing: " << filepath);
        return false;
    }

    try
    {
        WriteToStream(file);
        file.close();
        DITTO_LOG_VERBOSE("[Scene::SaveScene] Save completed.");
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Error saving scene: " << e.what());
        file.close();
        return false;
    }
}

std::string Scene::CaptureSnapshot()
{
    std::ostringstream oss(std::ios::binary);
    WriteToStream(oss);
    return oss.str();
}

bool Scene::RestoreSnapshot(const std::string& data)
{
    if (data.empty()) return false;
    std::istringstream iss(data, std::ios::binary);
    return ReadFromStream(iss);
}

bool Scene::LoadScene(const std::string& filepath)
{
    DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Starting load from: " << filepath);

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Failed to open file for reading: " << filepath);
        return false;
    }
    return ReadFromStream(file);
}

bool Scene::ReadFromStream(std::istream& file)
{
    try
    {
        SceneHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Header: magic=" << header.magic[0] << header.magic[1] << header.magic[2]
            << ", version=" << header.version << ", gameObjectCount=" << header.gameObjectCount
            << ", fileSize=" << header.fileSize);

        if (std::memcmp(header.magic, SCENE_MAGIC, 4) != 0)
        {
            DITTO_LOG_ERROR("[Scene::LoadScene] Invalid scene file: wrong magic number");
            return false;
        }
        if (header.version != SCENE_VERSION)
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Unsupported scene version: " << header.version
                << " (this development build reads exactly: " << SCENE_VERSION << ")");
            return false;
        }

        // Expose the loading version only while component deserializers run.
        SceneLoadingVersionScope loadingVersion(header.version);

        name = Ditto::AssetReferenceIO::ReadString(file);
        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Scene name: " << name);

        DestroyAllObjects();

        DITTO_LOG_VERBOSE("[Scene::LoadScene] Deserializing rootGameObject...");
        rootGameObject->Deserialize(file);
        rootGameObject->name = name;
        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] rootGameObject deserialized: " << rootGameObject->name
            << ", children: " << rootGameObject->children.size());

        std::function<void(GameObject*)> collectRootObjects = [&](GameObject* obj) {
            for (const auto& child : obj->children)
            {
                if (child->children.empty())
                {
                    gameObjects.push_back(child.get());
                }
                else
                {
                    collectRootObjects(child.get());
                    gameObjects.push_back(child.get());
                }
            }
        };
        collectRootObjects(rootGameObject.get());
        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Collected " << gameObjects.size() << " objects to gameObjects list");

        mainLight = nullptr;
        std::function<void(GameObject*)> findLight = [&](GameObject* obj) {
            if (!mainLight && obj->GetComponent<LightComponent>())
                mainLight = obj;
            for (const auto& child : obj->children)
                findLight(child.get());
        };
        findLight(rootGameObject.get());

        mainCamera = nullptr;
        EnsureDefaultCamera();

        DITTO_LOG_VERBOSE("[Scene::LoadScene] === Raycast-capable objects ===");
        int raycastObjCount = 0;
        std::function<void(GameObject*)> reportRaycastObjects = [&](GameObject* obj) {
            if (!obj) return;
            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();
            if (renderer && transform)
            {
                DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Raycast object: " << obj->name
                    << " (enabled=" << obj->enabled
                    << ", renderer=" << (renderer->enabled ? "enabled" : "disabled")
                    << ", transform=" << (transform->enabled ? "enabled" : "disabled")
                    << ", meshPath=" << renderer->meshPath
                    << ", pos=[" << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << "])");
                raycastObjCount++;
            }
            for (const auto& child : obj->children)
                reportRaycastObjects(child.get());
        };
        reportRaycastObjects(rootGameObject.get());
        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Total raycast-capable objects: " << raycastObjCount);

        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("Error loading scene: " << e.what());
        ClearScene();
        name = "Load Failed";
        return false;
    }
}
