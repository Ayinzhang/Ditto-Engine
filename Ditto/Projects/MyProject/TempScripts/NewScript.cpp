#include "../../../../Ditto/Ditto/Engine/Core/ScriptComponent.h"

struct NewScript : ScriptComponent
{
    // 成员变量
    float speed = 5.0f;
    int health = 100;

    // 获取类型名\n    const char* GetTypeName() const override { return "NewScript"; }

    // 获取成员变量列表（用�?Inspector 和序列化）\n    const ScriptVarMember* GetMembers() const override {
        static ScriptVarMember members[] = {
            { "speed", ScriptVarType::Float, &speed },
            { "health", ScriptVarType::Int, &health },
            { nullptr, ScriptVarType::Float, nullptr }
        };
        return members;
    }

    void Start() override
    {
        printf("NewScript Started!\\n");
    }

    void Update() override
    {
        // transform->position += transform->forward * speed * Time::deltaTime;
    }
};
REGISTER_SCRIPT(NewScript)
