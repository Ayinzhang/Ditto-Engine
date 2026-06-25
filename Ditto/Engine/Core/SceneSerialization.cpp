#include "Scene.h"

#include "GameObjectJson.h"
#include "JsonSceneSchema.h"
#include "JsonValue.h"
#include "Logger.h"

#include <fstream>
#include <functional>
#include <sstream>

namespace
{
    constexpr int SCENE_VERSION = 17;
    constexpr const char* SCENE_FORMAT = "DittoScene";
}

void Scene::WriteToStream(std::ostream& file)
{
    Ditto::Json::Value::Object json;
    json["format"] = SCENE_FORMAT;
    json["version"] = SCENE_VERSION;
    json["name"] = name;
    json["root"] = rootGameObject ? Ditto::GameObjectJson::ToJson(*rootGameObject) : Ditto::Json::Value{};

    file << Ditto::Json::Write(json, 2);
    file << '\n';
}

bool Scene::SaveScene(const std::string& filepath)
{
    DITTO_LOG_VERBOSE_STREAM("[Scene::SaveScene] Starting save to: " << filepath);

    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Failed to open file for writing: " << filepath);
        return false;
    }

    try
    {
        WriteToStream(file);
        DITTO_LOG_VERBOSE("[Scene::SaveScene] Save completed.");
        return file.good();
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Error saving scene: " << e.what());
        return false;
    }
}

std::string Scene::CaptureSnapshot()
{
    std::ostringstream oss;
    WriteToStream(oss);
    return oss.str();
}

bool Scene::RestoreSnapshot(const std::string& data)
{
    if (data.empty()) return false;
    std::istringstream iss(data);
    return ReadFromStream(iss);
}

bool Scene::LoadScene(const std::string& filepath)
{
    DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Starting load from: " << filepath);

    std::ifstream file(filepath);
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
        std::ostringstream buffer;
        buffer << file.rdbuf();

        Ditto::Json::Value json;
        std::string error;
        if (!Ditto::Json::Parse(buffer.str(), json, &error))
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Invalid JSON scene: " << error);
            return false;
        }

        if (!json.IsObject())
        {
            DITTO_LOG_ERROR("[Scene::LoadScene] Invalid scene: root JSON is not an object");
            return false;
        }

        Ditto::JsonSceneSchema::ValidationResult validation =
            Ditto::JsonSceneSchema::ValidateScene(json, SCENE_VERSION);
        if (!validation.ok)
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Scene schema validation failed: " << validation.Summary());
            return false;
        }

        const std::string format = json.Find("format") ? json.Find("format")->String() : std::string();
        const int version = json.Find("version") ? json.Find("version")->Int(0) : 0;
        if (format != SCENE_FORMAT)
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Unsupported scene format: " << format);
            return false;
        }
        if (version != SCENE_VERSION)
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Unsupported scene version: " << version
                << " (this development build reads exactly: " << SCENE_VERSION << ")");
            return false;
        }

        const Ditto::Json::Value* rootJson = json.Find("root");
        if (!rootJson)
        {
            DITTO_LOG_ERROR("[Scene::LoadScene] Invalid scene: missing root GameObject");
            return false;
        }

        std::string newName = json.Find("name") ? json.Find("name")->String("Default") : "Default";
        auto newRoot = std::make_unique<GameObject>(false);
        if (!Ditto::GameObjectJson::FromJson(*newRoot, *rootJson, &error))
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Invalid root GameObject: " << error);
            return false;
        }

        name = std::move(newName);
        rootGameObject = std::move(newRoot);
        rootGameObject->parent = nullptr;
        rootGameObject->name = name;

        gameObjects.clear();
        mainLight = nullptr;
        mainCamera = nullptr;
        for (const auto& child : rootGameObject->children)
            RegisterSubtree(child.get());

        EnsureDefaultCamera();

        DITTO_LOG_VERBOSE_STREAM("[Scene::LoadScene] Loaded scene '" << name << "' with "
            << gameObjects.size() << " tracked objects");
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
