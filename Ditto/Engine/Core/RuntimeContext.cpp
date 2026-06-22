#include "RuntimeContext.h"

namespace Ditto::RuntimeContext
{
    namespace
    {
        Editor* currentEditor = nullptr;
        Scene* currentScene = nullptr;
        std::uint32_t sceneLoadingVersion = 0;
    }

    Editor* CurrentEditor()
    {
        return currentEditor;
    }

    void SetCurrentEditor(Editor* editor)
    {
        currentEditor = editor;
    }

    Scene* CurrentScene()
    {
        return currentScene;
    }

    void SetCurrentScene(Scene* scene)
    {
        currentScene = scene;
    }

    std::uint32_t SceneLoadingVersion()
    {
        return sceneLoadingVersion;
    }

    void SetSceneLoadingVersion(std::uint32_t version)
    {
        sceneLoadingVersion = version;
    }

    SceneLoadingVersionScope::SceneLoadingVersionScope(std::uint32_t version)
        : previous(sceneLoadingVersion)
    {
        sceneLoadingVersion = version;
    }

    SceneLoadingVersionScope::~SceneLoadingVersionScope()
    {
        sceneLoadingVersion = previous;
    }
}
