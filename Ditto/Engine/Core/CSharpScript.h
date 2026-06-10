#pragma once
#include "GameObject.h"
#include "MonoRuntime.h"
#include "Logger.h"
#include <fstream>
#include <variant>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <filesystem>
#include <limits>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

enum class ScriptFieldType { Float, Int, Bool, String, Vector2, Vector3, Vector4 };

struct ScriptField
{
    std::string name;
    ScriptFieldType type;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> value;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> defaultValue;

    ScriptField(const std::string& n, ScriptFieldType t) : name(n), type(t) {}
};

struct CSharpScriptComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::CSharpScript;
    std::string scriptName, scriptPath;
    bool started = false;
    std::vector<ScriptField> fields;
    std::shared_ptr<MonoRuntime::ScriptInstance> scriptInstance;

    fs::file_time_type m_lastWriteTime;
    bool m_needsReload = false;
    bool m_isReloading = false;

    CSharpScriptComponent();

    void ParseScriptFields();

private:
    // Parse a single ';'-terminated declaration statement (comments already
    // stripped) and append any serializable fields it declares. Public by
    // default; also picks up [SerializeField] members and honors
    // [HideInInspector]. No-op for non-field statements (using, methods, etc.).
    void ParseFieldDeclaration(const std::string& statement);

public:

    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

    void OnInspectorGUI() override;

    void Start();
    void Update();
    void OnDestroy();

    bool ShouldReload();
    void HotReloadScript();
};

// Visit every CSharpScriptComponent on `obj` (does not recurse into children).
// Centralizes the `index == ComponentIndex::CSharpScript` check + cast that was
// previously copy-pasted across the engine's play/stop/reload loops.
template<typename Func>
inline void ForEachScriptComponent(GameObject* obj, Func&& func)
{
    if (!obj) return;
    for (const auto& comp : obj->components)
    {
        if (comp->index == ComponentIndex::CSharpScript)
            func(static_cast<CSharpScriptComponent*>(comp.get()));
    }
}

// ==================== C# Script System ====================
typedef void (*LogCallback)(const std::string& message);

struct CSharpScriptSystem
{
    static void Initialize();
    static void Shutdown();
    static void CleanOldCompiledDLLs();
    static bool LoadScript(const std::string& csPath, CSharpScriptComponent* component);
    static bool LoadPrecompiledScript(const std::string& className, CSharpScriptComponent* component);
    static void ReloadAll();
    static void CallStart();
    static void CallUpdate();

    static bool CompileScript(const std::string& csPath, std::string& outDllPath);
    static bool HotReloadScript(CSharpScriptComponent* component);

    static bool IsInitialized() { return s_initialized; }

    static void SetLogCallback(LogCallback callback) { s_logCallback = callback; }
    static void Log(const std::string& message) { 
        if (s_logCallback) s_logCallback(message); 
        else Ditto::Logger::Get().Info(message);
    }
    
    static void SetEditor(void* editor) { s_editor = editor; }
    static void LogToConsole(const std::string& message);
    
    static void* GetEditor() { return s_editor; }
    
    friend struct Editor;
    
    static void RegisterInternalCalls();
    
private:
    static bool s_initialized;
    static LogCallback s_logCallback;
    static void* s_editor;
};

// Internal call function declarations (C++ functions callable from C#)
extern "C" {
    void Internal_Transform_GetPosition(void* transform, float* outPos);
    void Internal_Transform_SetPosition(void* transform, float x, float y, float z);
    void* Internal_GameObject_GetTransform(void* gameObject);
    void* Internal_GameObject_GetComponentByType(void* gameObject, void* typeName);
    void Internal_Renderer_GetColor(void* renderer, float* outColor);
    void Internal_Renderer_SetColor(void* renderer, float r, float g, float b, float a);
    int Internal_Renderer_GetShapeType(void* renderer);
    void Internal_Renderer_SetShapeType(void* renderer, int type);
    void Internal_Light_GetColor(void* light, float* outColor);
    void Internal_Light_SetColor(void* light, float r, float g, float b);
    float Internal_Light_GetIntensity(void* light);
    void Internal_Light_SetIntensity(void* light, float intensity);
    int Internal_Rigidbody_GetBodyType(void* rigidbody);
    void Internal_Rigidbody_SetBodyType(void* rigidbody, int type);
    float Internal_Rigidbody_GetMass(void* rigidbody);
    void Internal_Rigidbody_SetMass(void* rigidbody, float mass);
    int Internal_Rigidbody_GetUseGravity(void* rigidbody);
    void Internal_Rigidbody_SetUseGravity(void* rigidbody, int useGravity);
    float Internal_Rigidbody_GetLinearDamping(void* rigidbody);
    void Internal_Rigidbody_SetLinearDamping(void* rigidbody, float damp);
    float Internal_Rigidbody_GetAngularDamping(void* rigidbody);
    void Internal_Rigidbody_SetAngularDamping(void* rigidbody, float damp);
    void Internal_Rigidbody_GetVelocity(void* rigidbody, float* outVel);
    void Internal_Rigidbody_SetVelocity(void* rigidbody, float x, float y, float z);
    void Internal_Rigidbody_GetAngularVelocity(void* rigidbody, float* outVel);
    void Internal_Rigidbody_SetAngularVelocity(void* rigidbody, float x, float y, float z);
    float Internal_Time_GetDeltaTime();
    void Internal_Debug_Log(void* msg);
}
