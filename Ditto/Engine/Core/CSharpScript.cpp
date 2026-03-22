#include "CSharpScript.h"
#include "GameObject.h"
#include "../../3rdParty/ImGui/imgui.h"
#include <iostream>
#include <filesystem>
#include <windows.h>

// ==================== CSharpScriptComponent 实现 ====================
void CSharpScriptComponent::Start()
{
    if (!started && enabled)
    {
        std::cout << "[CSharpScript] Start: " << scriptName << std::endl;
        started = true;
    }
}

void CSharpScriptComponent::Update()
{
    if (enabled && gameObject)
    {
        // TODO: 调用 C# Update
    }
}

void CSharpScriptComponent::OnDestroy()
{
    std::cout << "[CSharpScript] Destroy: " << scriptName << std::endl;
}

void CSharpScriptComponent::Serialize(std::ofstream& file) const
{
    uint32_t nameLen = static_cast<uint32_t>(scriptName.length());
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(scriptName.c_str(), nameLen);
    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));
}

void CSharpScriptComponent::Deserialize(std::ifstream& file)
{
    uint32_t nameLen;
    file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    scriptName.resize(nameLen);
    file.read(&scriptName[0], nameLen);
    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));
}

void CSharpScriptComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted(scriptName.empty() ? "C# Script" : scriptName.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) 
    { 
        gameObject->RemoveComponent(this); 
        return; 
    }
    
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    
    ImGui::Text("Script: "); ImGui::SameLine();
    ImGui::TextDisabled("%s", scriptPath.c_str());
    
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

// ==================== CSharpScriptSystem 实现 ====================
bool CSharpScriptSystem::s_initialized = false;

void CSharpScriptSystem::Initialize()
{
    if (s_initialized) return;
    std::cout << "[CSharpScriptSystem] Initializing..." << std::endl;
    s_initialized = true;
    std::cout << "[CSharpScriptSystem] Initialized" << std::endl;
}

void CSharpScriptSystem::Shutdown()
{
    if (!s_initialized) return;
    std::cout << "[CSharpScriptSystem] Shutting down..." << std::endl;
    s_initialized = false;
}

bool CSharpScriptSystem::LoadScript(const std::string& csPath, CSharpScriptComponent* component)
{
    if (!component) return false;
    
    // 提取类名
    std::string fileName = csPath;
    size_t pos = fileName.find_last_of("/\\");
    if (pos != std::string::npos) fileName = fileName.substr(pos + 1);
    pos = fileName.find_last_of('.');
    if (pos != std::string::npos) fileName = fileName.substr(0, pos);
    
    component->scriptName = fileName;
    component->scriptPath = csPath;
    
    std::cout << "[CSharpScriptSystem] Loading: " << fileName << " from " << csPath << std::endl;
    
    // 编译 C# 脚本 - 使用绝对路径
    std::filesystem::path scriptPath(csPath);
    std::filesystem::path absScriptPath = std::filesystem::absolute(scriptPath);
    std::string dir = absScriptPath.parent_path().string();
    std::filesystem::path dllAbsPath = absScriptPath.parent_path() / (absScriptPath.stem().string() + ".dll");
    std::string dllPath = dllAbsPath.string();
    
    // 设置 PATH 环境变量
    std::string pathCmd = "set PATH=D:\\Visual Studio 2022\\MSBuild\\Current\\Bin\\Roslyn;%PATH%&";
    std::string cmd = pathCmd + "cd /d \"" + dir + "\" && csc /target:library /out:\"" + dllPath + "\" \"" + absScriptPath.filename().string() + "\"";
    std::cout << "[CSharpScriptSystem] Compile cmd: " << cmd << std::endl;
    int result = system(cmd.c_str());
    
    if (result != 0)
    {
        std::cerr << "[CSharpScriptSystem] Compile failed: " << csPath << std::endl;
        return false;
    }
    
    std::cout << "[CSharpScriptSystem] Compiled: " << dllPath << std::endl;
    return true;
}

void CSharpScriptSystem::ReloadAll()
{
    std::cout << "[CSharpScriptSystem] Reloading..." << std::endl;
}

void CSharpScriptSystem::CallStart()
{
    // TODO: 遍历调用
}

void CSharpScriptSystem::CallUpdate()
{
    // TODO: 遍历调用
}
