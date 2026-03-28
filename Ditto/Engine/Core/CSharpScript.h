#pragma once
#include "GameObject.h"
#include <fstream>
#include <variant>
#include <vector>
#include <string>
#include <iostream>

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
    
private:
    static bool s_initialized;
    static LogCallback s_logCallback;
    static void* s_editor;
};
