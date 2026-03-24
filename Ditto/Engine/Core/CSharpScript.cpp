#include "CSharpScript.h"
#include "GameObject.h"
#include "../../Editor/Editor.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <regex>

// 静态成员初始化
bool CSharpScriptSystem::s_initialized = false;
LogCallback CSharpScriptSystem::s_logCallback = nullptr;
void* CSharpScriptSystem::s_editor = nullptr;

// ==================== 解析 C# 脚本字段 ====================
void CSharpScriptComponent::ParseScriptFields()
{
    fields.clear();
    if (scriptPath.empty()) return;
    
    std::ifstream file(scriptPath);
    if (!file.is_open()) 
    {
        std::cerr << "[CSharpScript] Cannot open file: " << scriptPath << std::endl;
        return;
    }
    
    std::string line;
    
    // 使用 std::regex::ECMAScript 模式
    std::regex floatRegex("public\\s+float\\s+(\\w+)\\s*=\\s*([0-9.-]+f?)\\s*;");
    std::regex intRegex("public\\s+int\\s+(\\w+)\\s*=\\s*(-?[0-9]+)\\s*;");
    std::regex boolRegex("public\\s+bool\\s+(\\w+)\\s*=\\s*(true|false)\\s*;");
    std::regex stringRegex("public\\s+string\\s+(\\w+)\\s*=\\s*\"([^\"]*)\"\\s*;");
    std::regex vec2Regex("public\\s+Vector2\\s+(\\w+)\\s*=\\s*new\\s+Vector2\\s*\\(\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*\\)");
    std::regex vec3Regex("public\\s+Vector3\\s+(\\w+)\\s*=\\s*new\\s+Vector3\\s*\\(\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*\\)");
    std::regex vec4Regex("public\\s+Vector4\\s+(\\w+)\\s*=\\s*new\\s+Vector4\\s*\\(\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*,\\s*([0-9.-]+f?)\\s*\\)");
    
    while (std::getline(file, line))
    {
        std::smatch match;
        
        // Vector4
        if (std::regex_search(line, match, vec4Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector4);
            glm::vec4 val(std::stof(match[2].str()), std::stof(match[3].str()), 
                         std::stof(match[4].str()), std::stof(match[5].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        // Vector3
        else if (std::regex_search(line, match, vec3Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector3);
            glm::vec3 val(std::stof(match[2].str()), std::stof(match[3].str()), std::stof(match[4].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        // Vector2
        else if (std::regex_search(line, match, vec2Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector2);
            glm::vec2 val(std::stof(match[2].str()), std::stof(match[3].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        // float
        else if (std::regex_search(line, match, floatRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Float);
            field.defaultValue = field.value = std::stof(match[2].str());
            fields.push_back(field);
        }
        // int
        else if (std::regex_search(line, match, intRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Int);
            field.defaultValue = field.value = std::stoi(match[2].str());
            fields.push_back(field);
        }
        // bool
        else if (std::regex_search(line, match, boolRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Bool);
            bool val = (match[2].str() == "true");
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        // string
        else if (std::regex_search(line, match, stringRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::String);
            field.defaultValue = field.value = match[2].str();
            fields.push_back(field);
        }
    }
    
    std::cout << "[CSharpScript] Parsed " << fields.size() << " fields from " << scriptName << std::endl;
}

// ==================== CSharpScriptComponent 实现 ====================
void CSharpScriptComponent::Start()
{
    if (!started && enabled)
    {
        CSharpScriptSystem::LogToConsole("[CSharpScript] Start: " + scriptName);
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
    CSharpScriptSystem::LogToConsole("[CSharpScript] Destroy: " + scriptName);
}

void CSharpScriptComponent::Serialize(std::ofstream& file) const
{
    uint32_t nameLen = static_cast<uint32_t>(scriptName.length());
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(scriptName.c_str(), nameLen);
    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    
    // 序列化字段
    uint32_t fieldCount = static_cast<uint32_t>(fields.size());
    file.write(reinterpret_cast<const char*>(&fieldCount), sizeof(fieldCount));
    
    for (const auto& field : fields)
    {
        uint8_t type = static_cast<uint8_t>(field.type);
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));
        
        uint32_t nameLen2 = static_cast<uint32_t>(field.name.length());
        file.write(reinterpret_cast<const char*>(&nameLen2), sizeof(nameLen2));
        file.write(field.name.c_str(), nameLen2);
        
        switch (field.type)
        {
            case ScriptFieldType::Float:
            {
                float v = std::get<float>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ScriptFieldType::Int:
            {
                int v = std::get<int>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool v = std::get<bool>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ScriptFieldType::String:
            {
                const std::string& v = std::get<std::string>(field.value);
                uint32_t len = static_cast<uint32_t>(v.length());
                file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                file.write(v.c_str(), len);
                break;
            }
            case ScriptFieldType::Vector2:
            {
                glm::vec2 v = std::get<glm::vec2>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ScriptFieldType::Vector3:
            {
                glm::vec3 v = std::get<glm::vec3>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
            case ScriptFieldType::Vector4:
            {
                glm::vec4 v = std::get<glm::vec4>(field.value);
                file.write(reinterpret_cast<const char*>(&v), sizeof(v));
                break;
            }
        }
    }
}

void CSharpScriptComponent::Deserialize(std::ifstream& file)
{
    uint32_t nameLen;
    file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    scriptName.resize(nameLen);
    file.read(&scriptName[0], nameLen);
    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));
    
    // 反序列化字段
    uint32_t fieldCount = 0;
    file.read(reinterpret_cast<char*>(&fieldCount), sizeof(fieldCount));
    
    fields.clear();
    for (uint32_t i = 0; i < fieldCount; i++)
    {
        uint8_t type;
        file.read(reinterpret_cast<char*>(&type), sizeof(type));
        
        uint32_t nameLen2;
        file.read(reinterpret_cast<char*>(&nameLen2), sizeof(nameLen2));
        std::string fieldName(nameLen2, '\0');
        file.read(&fieldName[0], nameLen2);
        
        ScriptField field(fieldName, static_cast<ScriptFieldType>(type));
        
        switch (field.type)
        {
            case ScriptFieldType::Float:
            {
                float v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::Int:
            {
                int v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::String:
            {
                uint32_t len; file.read(reinterpret_cast<char*>(&len), sizeof(len));
                std::string v(len, '\0');
                file.read(&v[0], len);
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::Vector2:
            {
                glm::vec2 v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::Vector3:
            {
                glm::vec3 v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
            case ScriptFieldType::Vector4:
            {
                glm::vec4 v; file.read(reinterpret_cast<char*>(&v), sizeof(v));
                field.value = field.defaultValue = v;
                break;
            }
        }
        fields.push_back(field);
    }
    
    // 重新解析脚本以获取字段定义，然后应用保存的值
    std::vector<ScriptField> savedFields = fields;
    ParseScriptFields();
    
    // 将保存的值应用回字段
    for (auto& field : fields)
    {
        for (auto& saved : savedFields)
        {
            if (field.name == saved.name && field.type == saved.type)
            {
                field.value = saved.value;
                break;
            }
        }
    }
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
    
    // 显示 public 字段
    for (auto& field : fields)
    {
        std::string label = field.name;
        std::string id = "##" + field.name;
        
        switch (field.type)
        {
            case ScriptFieldType::Float:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                float val = std::get<float>(field.value);
                if (ImGui::DragFloat(id.c_str(), &val, 0.1f))
                    field.value = val;
                break;
            }
            case ScriptFieldType::Int:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                int val = std::get<int>(field.value);
                if (ImGui::DragInt(id.c_str(), &val))
                    field.value = val;
                break;
            }
            case ScriptFieldType::Bool:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                bool val = std::get<bool>(field.value);
                if (ImGui::Checkbox(id.c_str(), &val))
                    field.value = val;
                break;
            }
            case ScriptFieldType::String:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                std::string& val = std::get<std::string>(field.value);
                char buffer[256] = {};
                strncpy_s(buffer, val.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer)))
                    val = buffer;
                break;
            }
            case ScriptFieldType::Vector2:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec2& val = std::get<glm::vec2>(field.value);
                float vec2[2] = { val.x, val.y };
                if (ImGui::DragFloat2(id.c_str(), vec2, 0.1f))
                    val = glm::vec2(vec2[0], vec2[1]);
                break;
            }
            case ScriptFieldType::Vector3:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec3& val = std::get<glm::vec3>(field.value);
                float vec3[3] = { val.x, val.y, val.z };
                if (ImGui::DragFloat3(id.c_str(), vec3, 0.1f))
                    val = glm::vec3(vec3[0], vec3[1], vec3[2]);
                break;
            }
            case ScriptFieldType::Vector4:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec4& val = std::get<glm::vec4>(field.value);
                float vec4[4] = { val.x, val.y, val.z, val.w };
                if (ImGui::DragFloat4(id.c_str(), vec4, 0.1f))
                    val = glm::vec4(vec4[0], vec4[1], vec4[2], vec4[3]);
                break;
            }
        }
    }
    
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

// ==================== CSharpScriptSystem 实现 ====================

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

void CSharpScriptSystem::LogToConsole(const std::string& message)
{
    if (s_logCallback) {
        s_logCallback(message);
    } else if (s_editor) {
        Editor* editor = static_cast<Editor*>(s_editor);
        if (editor) {
            editor->AddConsoleMessage(message);
        }
    } else {
        std::cout << message << std::endl;
    }
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
    
    std::string dittoEngineDll = "e:\\Engine Source\\Ditto\\Ditto\\3rdParty\\Mono\\DittoEngine.dll";
    
    std::string pathCmd = "set PATH=D:\\Visual Studio 2022\\MSBuild\\Current\\Bin\\Roslyn;%PATH%&";
    std::string cmd = pathCmd + "cd /d \"" + dir + "\" && csc /target:library /reference:\"" + dittoEngineDll + "\" /out:\"" + dllPath + "\" \"" + absScriptPath.filename().string() + "\"";
    std::cout << "[CSharpScriptSystem] Compile cmd: " << cmd << std::endl;
    int result = system(cmd.c_str());
    
    if (result != 0)
    {
        std::cerr << "[CSharpScriptSystem] Compile failed: " << csPath << std::endl;
        return false;
    }
    
    std::cout << "[CSharpScriptSystem] Compiled: " << dllPath << std::endl;
    
    // 解析 public 变量
    component->ParseScriptFields();
    
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
