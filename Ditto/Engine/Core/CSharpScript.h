#pragma once
#include "GameObject.h"
#include <fstream>

// ==================== C# 脚本组件 ====================
struct CSharpScriptComponent : Component
{
    std::string scriptName;
    std::string scriptPath;
    bool started = false;
    
    CSharpScriptComponent() { index = 1 << 10; }
    
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
struct CSharpScriptSystem
{
    static void Initialize();
    static void Shutdown();
    static bool LoadScript(const std::string& csPath, CSharpScriptComponent* component);
    static void ReloadAll();
    static void CallStart();
    static void CallUpdate();
    
    static bool IsInitialized() { return s_initialized; }
    
private:
    static bool s_initialized;
};
