#include "EngineLifecycle.h"

#include "CSharpScript.h"
#include "GameObject.h"
#include "Scene.h"
#include "../Animation/AnimatorComponent.h"
#include "../Graphics/ParticleSystemComponent.h"
#include "../Physics/Physics.h"
#include "../Physics/Physics2D.h"
#ifndef DITTO_HEADLESS_TESTS
#include "../Audio/AudioEngine.h"
#endif

#include <functional>
#include <vector>

namespace Ditto::EngineLifecycle
{
    namespace
    {
        template<typename Func>
        void ForEachGameObject(Scene* scene, Func&& func)
        {
            if (!scene || !scene->rootGameObject) return;

            std::function<void(GameObject*)> traverse = [&](GameObject* obj)
            {
                if (!obj) return;
                if (obj->removeComps.empty())
                    func(obj);
                for (const auto& child : obj->children)
                    traverse(child.get());
            };
            traverse(scene->rootGameObject.get());
        }
    }

    void EnterPlayMode(Scene* scene, Physics* physics, Physics2DWorld* physics2D, float& physics2DAccumulator)
    {
        if (scene && scene->rootGameObject && physics)
        {
            std::vector<GameObject*> rootObjects;
            rootObjects.push_back(scene->rootGameObject.get());
            physics->GenerateColliders(rootObjects);
        }
        if (physics2D) physics2D->Rebuild(scene);
        physics2DAccumulator = 0.0f;

        ForEachGameObject(scene, [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                if (script->ShouldReload())
                    script->HotReloadScript();
                else
                {
                    script->scriptInstance.reset();
                    script->started = false;
                }
            });
        });

        ForEachGameObject(scene, [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                script->Start();
            });

#ifndef DITTO_HEADLESS_TESTS
            for (AudioSourceComponent* audio : obj->GetComponents<AudioSourceComponent>())
                if (audio->enabled && audio->playOnAwake)
                    audio->Play();
#endif

            if (auto* anim = obj->GetComponent<AnimatorComponent>())
                if (anim->playOnAwake) anim->Play();
            if (auto* ps = obj->GetComponent<ParticleSystemComponent>())
                if (ps->playOnAwake) ps->Play();
        });

        CSharpScriptSystem::SetTime(0.0f);
    }

    void StepPlayModeFrame(Scene* scene, Physics* physics, Physics2DWorld* physics2D,
        float deltaTime, float& physics2DAccumulator)
    {
        CSharpScriptSystem::SetDeltaTime(deltaTime);
        CSharpScriptSystem::SetTime(CSharpScriptSystem::GetTime() + deltaTime);

        if (physics2D)
        {
            float cappedDelta = deltaTime > 0.25f ? 0.25f : deltaTime;
            physics2DAccumulator += cappedDelta;
            int steps2D = 0;
            while (physics2DAccumulator >= physics2D->fixedDeltaTime && steps2D < 5)
            {
                float step2D = physics2D->fixedDeltaTime;
                CSharpScriptSystem::SetDeltaTime(step2D);
                ForEachGameObject(scene, [](GameObject* obj)
                {
                    ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
                    {
                        script->FixedUpdate();
                    });
                });
                physics2D->StepFixed(scene, step2D);
                physics2DAccumulator -= step2D;
                ++steps2D;
            }
            CSharpScriptSystem::SetDeltaTime(deltaTime);
        }

        if (physics)
            physics->UpdatePhysics(deltaTime);

        ForEachGameObject(scene, [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                script->Update();
            });
        });

        ForEachGameObject(scene, [deltaTime](GameObject* obj)
        {
            if (!obj->enabled) return;
            if (auto* anim = obj->GetComponent<AnimatorComponent>())
                anim->Update(deltaTime);
            if (auto* ps = obj->GetComponent<ParticleSystemComponent>())
                ps->Update(deltaTime);
        });
    }

    void ExitPlayMode(Scene* scene, Physics* physics, Physics2DWorld* physics2D, float& physics2DAccumulator)
    {
        ForEachGameObject(scene, [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                script->OnDestroy();
                script->started = false;
            });

            if (TransformComponent* t = obj->GetComponent<TransformComponent>())
            {
                t->useQuatRotation = false;
                t->localDirty = true;
            }
        });

        if (physics) physics->ClearColliders();
        if (physics2D) physics2D->Clear();
        physics2DAccumulator = 0.0f;
#ifndef DITTO_HEADLESS_TESTS
        AudioEngine::StopAll();
#endif
    }
}
