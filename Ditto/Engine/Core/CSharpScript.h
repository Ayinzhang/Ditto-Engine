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
    void FixedUpdate();
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
    static void SetDeltaTime(float dt) { s_deltaTime = dt; }
    static float GetDeltaTime() { return s_deltaTime; }
    // Accumulated play-mode time (seconds since entering Play); reset on Play.
    static void SetTime(float t) { s_time = t; }
    static float GetTime() { return s_time; }

    static void SetLogCallback(LogCallback callback) { s_logCallback = callback; }
    static void Log(const std::string& message) { 
        if (s_logCallback) s_logCallback(message); 
        else Ditto::Logger::Get().Info(message);
    }
    
    static void SetEditor(void* editor) { s_editor = editor; }
    static void LogToConsole(const std::string& message);

    static void* GetEditor() { return s_editor; }

    // Physics instance for Physics.Raycast internal calls (set by Engine after
    // construction; works in both editor and standalone game mode).
    static void SetPhysics(void* physics) { s_physics = physics; }
    static void* GetPhysics() { return s_physics; }
    
    friend struct Editor;
    
    static void RegisterInternalCalls();
    
private:
    static bool s_initialized;
    static float s_deltaTime;
    static float s_time;
    static LogCallback s_logCallback;
    static void* s_editor;
    static void* s_physics;
};

// Internal call function declarations (C++ functions callable from C#)
extern "C" {
    void Internal_Transform_GetPosition(void* transform, float* outPos);
    void Internal_Transform_SetPosition(void* transform, float x, float y, float z);
    void Internal_Transform_GetRotation(void* transform, float* outRot);
    void Internal_Transform_SetRotation(void* transform, float x, float y, float z);
    void Internal_Transform_GetScale(void* transform, float* outScale);
    void Internal_Transform_SetScale(void* transform, float x, float y, float z);
    void* Internal_GameObject_GetTransform(void* gameObject);
    void* Internal_GameObject_GetComponentByType(void* gameObject, void* typeName);
    void Internal_Renderer_GetColor(void* renderer, float* outColor);
    void Internal_Renderer_SetColor(void* renderer, float r, float g, float b, float a);
    int Internal_Renderer_GetShapeType(void* renderer);
    void Internal_Renderer_SetShapeType(void* renderer, int type);
    void Internal_SpriteRenderer_GetColor(void* spriteRenderer, float* outColor);
    void Internal_SpriteRenderer_SetColor(void* spriteRenderer, float r, float g, float b, float a);
    void* Internal_SpriteRenderer_GetSprite(void* spriteRenderer);
    void Internal_SpriteRenderer_SetSprite(void* spriteRenderer, void* spritePath);
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
    int Internal_Rigidbody2D_GetBodyType(void* rigidbody);
    void Internal_Rigidbody2D_SetBodyType(void* rigidbody, int type);
    float Internal_Rigidbody2D_GetMass(void* rigidbody);
    void Internal_Rigidbody2D_SetMass(void* rigidbody, float mass);
    int Internal_Rigidbody2D_GetUseGravity(void* rigidbody);
    void Internal_Rigidbody2D_SetUseGravity(void* rigidbody, int useGravity);
    float Internal_Rigidbody2D_GetGravityScale(void* rigidbody);
    void Internal_Rigidbody2D_SetGravityScale(void* rigidbody, float gravityScale);
    void Internal_Rigidbody2D_GetVelocity(void* rigidbody, float* outVel);
    void Internal_Rigidbody2D_SetVelocity(void* rigidbody, float x, float y);
    float Internal_Rigidbody2D_GetAngularVelocity(void* rigidbody);
    void Internal_Rigidbody2D_SetAngularVelocity(void* rigidbody, float v);
    void Internal_Rigidbody2D_AddForce(void* rigidbody, float x, float y, int mode);
    void Internal_Rigidbody2D_AddTorque(void* rigidbody, float torque, int mode);
    float Internal_Time_GetDeltaTime();
    float Internal_Time_GetTime();
    void Internal_Debug_Log(void* msg);
    int Internal_Input_GetKey(int key);
    int Internal_Input_GetKeyDown(int key);
    int Internal_Input_GetKeyUp(int key);
    int Internal_Input_GetMouseButton(int button);
    int Internal_Input_GetMouseButtonDown(int button);
    int Internal_Input_GetMouseButtonUp(int button);
    void Internal_Input_GetMousePosition(float* outPos);
    // out7: point(3) + normal(3) + distance(1); outGo: hit GameObject pointer.
    // Returns 1 on hit, 0 on miss. Only valid in Play mode (colliders exist).
    int Internal_Physics_Raycast(float ox, float oy, float oz,
        float dx, float dy, float dz, float maxDist, float* out7, void** outGo);
    void Internal_AudioSource_Play(void* audioSource);
    void Internal_AudioSource_Stop(void* audioSource);
    float Internal_AudioSource_GetVolume(void* audioSource);
    void Internal_AudioSource_SetVolume(void* audioSource, float volume);
    int Internal_AudioSource_GetLoop(void* audioSource);
    void Internal_AudioSource_SetLoop(void* audioSource, int loop);
    int Internal_AudioSource_IsPlaying(void* audioSource);
    void Internal_UIText_SetText(void* uiText, void* text);
    void* Internal_UIText_GetText(void* uiText);
    void Internal_UIText_SetColor(void* uiText, float r, float g, float b, float a);
    void Internal_UIImage_SetColor(void* uiImage, float r, float g, float b, float a);
    void Internal_UIImage_GetColor(void* uiImage, float* outColor);
    int Internal_UIButton_ConsumeClick(void* uiButton);
    int Internal_UIButton_IsHovered(void* uiButton);
    void Internal_UIButton_SetLabel(void* uiButton, void* label);
    void* Internal_Object_Instantiate(void* gameObject);
    void Internal_Object_Destroy(void* gameObject);
}
