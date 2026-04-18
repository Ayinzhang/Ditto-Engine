#pragma once
#include "GameObject.h"
#include "MonoRuntime.h"
#include <fstream>
#include <variant>
#include <vector>
#include <string>
#include <iostream>
#include <memory>

// ==================== 脚本字段 ====================
enum class ScriptFieldType { Float, Int, Bool, String, Vector2, Vector3, Vector4 };

struct ScriptField
{
    std::string name;
    ScriptFieldType type;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> value;
    std::variant<float, int, bool, std::string, glm::vec2, glm::vec3, glm::vec4> defaultValue;
    
    ScriptField() {}
    ScriptField(const std::string& n, ScriptFieldType t) : name(n), type(t) {}
};

// ==================== C# 脚本组件 ====================
struct CSharpScriptComponent : Component
{
    std::string scriptName;
    std::string scriptPath;
    bool started = false;
    std::vector<ScriptField> fields;  // 解析出的public字段
    
    // Mono 运行时脚本实例
    std::shared_ptr<MonoRuntime::ScriptInstance> scriptInstance;
    
    CSharpScriptComponent() { index = 1 << 10; }
    
    // 解析 C# 文件中的 public 变量
    void ParseScriptFields();
    
    // 序列化
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
    
    // Inspector
    void OnInspectorGUI() override;
    
    // 生命周期
    void Start();
    void Update();
    void OnDestroy();
};

// ==================== C# 脚本系统 ====================
typedef void (*LogCallback)(const std::string& message);

struct CSharpScriptSystem
{
    static void Initialize();
    static void Shutdown();
    static bool LoadScript(const std::string& csPath, CSharpScriptComponent* component);
    static bool LoadPrecompiledScript(const std::string& className, CSharpScriptComponent* component);
    static void ReloadAll();
    static void CallStart();
    static void CallUpdate();
    
    static bool IsInitialized() { return s_initialized; }
    
    // 设置日志回调
    static void SetLogCallback(LogCallback callback) { s_logCallback = callback; }
    static void Log(const std::string& message) { 
        if (s_logCallback) s_logCallback(message); 
        else std::cout << message << std::endl;
    }
    
    // 设置 Editor 指针用于 Console 输出
    static void SetEditor(void* editor) { s_editor = editor; }
    static void LogToConsole(const std::string& message);
    
    // 获取 Editor 指针（用于标记场景脏）
    static void* GetEditor() { return s_editor; }
    
    // 声明 Editor 类为友元，以便访问 s_editor
    friend class Editor;
    
    // 注册内部调用函数
    static void RegisterInternalCalls();
    
private:
    static bool s_initialized;
    static LogCallback s_logCallback;
    static void* s_editor;
};

// 内部调用函数声明（C++ 函数供 C# 调用）
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
