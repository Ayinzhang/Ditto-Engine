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

struct Editor;
struct Physics;

enum class ScriptFieldType { Float, Int, Bool, String, Vector2, Vector3, Vector4 };

struct ScriptField
{
    std::string name;
    ScriptFieldType type;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> value;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> defaultValue;

    ScriptField(const std::string& n, ScriptFieldType t) : name(n), type(t) {}
};

struct CSharpCompileResult
{
    struct Diagnostic
    {
        std::string file;
        int line = 0;
        int column = 0;
        std::string severity;
        std::string code;
        std::string message;
    };

    bool ok = false;
    std::string scriptPath;
    std::string outputDllPath;
    std::string output;
    int warningCount = 0;
    int errorCount = 0;
    std::vector<Diagnostic> diagnostics;
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
    CSharpCompileResult lastCompileResult;

    CSharpScriptComponent();

    void ParseScriptFields();

private:
    
    
    
    
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
    static CSharpCompileResult CompileScriptDetailed(const std::string& csPath, std::string& outDllPath);
    static bool HotReloadScript(CSharpScriptComponent* component);

    static bool IsInitialized() { return s_initialized; }
    static void SetDeltaTime(float dt) { s_deltaTime = dt; }
    static float GetDeltaTime() { return s_deltaTime; }
    
    static void SetTime(float t) { s_time = t; }
    static float GetTime() { return s_time; }

    static void SetLogCallback(LogCallback callback) { s_logCallback = callback; }
    static void Log(const std::string& message) { 
        if (s_logCallback) s_logCallback(message); 
        else Ditto::Logger::Get().Info(message);
    }
    
    static void SetEditor(Editor* editor) { s_editor = editor; }
    static void LogToConsole(const std::string& message);

    static Editor* GetEditor() { return s_editor; }

    
    
    static void SetPhysics(Physics* physics) { s_physics = physics; }
    static Physics* GetPhysics() { return s_physics; }
    
    friend struct Editor;
    
    static void RegisterInternalCalls();
    
private:
    static bool s_initialized;
    static float s_deltaTime;
    static float s_time;
    static LogCallback s_logCallback;
    static Editor* s_editor;
    static Physics* s_physics;
};


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
    void* Internal_Camera_GetMainCamera();
    int Internal_Camera_GetProjectionType(void* camera);
    void Internal_Camera_SetProjectionType(void* camera, int projectionType);
    float Internal_Camera_GetFieldOfView(void* camera);
    void Internal_Camera_SetFieldOfView(void* camera, float fieldOfView);
    float Internal_Camera_GetOrthographicSize(void* camera);
    void Internal_Camera_SetOrthographicSize(void* camera, float orthographicSize);
    float Internal_Camera_GetNearClipPlane(void* camera);
    void Internal_Camera_SetNearClipPlane(void* camera, float nearClipPlane);
    float Internal_Camera_GetFarClipPlane(void* camera);
    void Internal_Camera_SetFarClipPlane(void* camera, float farClipPlane);
    void Internal_Camera_ScreenPointToRay(void* camera, float x, float y, float* outRay);
    void Internal_Camera_ScreenToWorldPoint(void* camera, float x, float y, float distance, float* outPoint);
    void Internal_Camera_ScreenToWorldPointOnPlane(void* camera, float x, float y, float worldZ, float* outPoint);
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
    void Internal_Input_GetGameViewportSize(float* outSize);
    float Internal_Input_GetAxis(void* axisName);
    float Internal_Input_GetAxisRaw(void* axisName);
    int Internal_Input_GetButton(void* buttonName);
    int Internal_Input_GetButtonDown(void* buttonName);
    int Internal_Input_GetButtonUp(void* buttonName);
    
    
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
    void Internal_Animator_Play(void* animator, void* clipName);
    void Internal_Animator_Stop(void* animator);
    void Internal_Animator_Pause(void* animator);
    void Internal_Animator_Resume(void* animator);
    float Internal_Animator_GetSpeed(void* animator);
    void Internal_Animator_SetSpeed(void* animator, float speed);
    int Internal_Animator_IsPlaying(void* animator);
    void Internal_ParticleSystem_Play(void* particleSystem);
    void Internal_ParticleSystem_Stop(void* particleSystem);
    void Internal_ParticleSystem_Clear(void* particleSystem);
    int Internal_ParticleSystem_IsPlaying(void* particleSystem);
}
