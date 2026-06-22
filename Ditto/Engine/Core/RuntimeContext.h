#pragma once

#include <cstdint>

struct Editor;
struct Scene;

namespace Ditto::RuntimeContext
{
    Editor* CurrentEditor();
    void SetCurrentEditor(Editor* editor);

    Scene* CurrentScene();
    void SetCurrentScene(Scene* scene);

    std::uint32_t SceneLoadingVersion();
    void SetSceneLoadingVersion(std::uint32_t version);

    class SceneLoadingVersionScope
    {
    public:
        explicit SceneLoadingVersionScope(std::uint32_t version);
        ~SceneLoadingVersionScope();

    private:
        std::uint32_t previous;
    };
}
