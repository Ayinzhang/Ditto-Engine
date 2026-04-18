#include "CSharpScript.h"
#include "MonoRuntime.h"
#include "GameObject.h"
#include "../../Editor/Editor.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <regex>

namespace fs = std::filesystem;

// Helper function to find DittoEngine.dll
static std::string FindDittoEngineDll()
{
    const std::vector<std::string> possiblePaths = {
        "3rdParty/Mono/DittoEngine.dll",
        "../../3rdParty/Mono/DittoEngine.dll",
        "../3rdParty/Mono/DittoEngine.dll",
        "Ditto/3rdParty/Mono/DittoEngine.dll",
        "../../Ditto/3rdParty/Mono/DittoEngine.dll",
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path))
            return fs::absolute(path).string();
    }
    
    // Fallback to original hardcoded path for compatibility
    std::cerr << "[CSharpScript] Warning: DittoEngine.dll not found in standard locations" << std::endl;
    return "3rdParty/Mono/DittoEngine.dll";
}

// Helper function to find MSBuild Roslyn path
static std::string FindMSBuildPath()
{
    // Check environment variable first
    char* vsPath = nullptr;
    size_t vsPathLen = 0;
    _dupenv_s(&vsPath, &vsPathLen, "VSINSTALLDIR");
    if (vsPath)
    {
        std::string roslynPath = std::string(vsPath) + "MSBuild\Current\Bin\Roslyn";
        free(vsPath);
        if (fs::exists(roslynPath))
            return roslynPath;
    }
    
    // Check common installation paths
    const std::vector<std::string> possiblePaths = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/Roslyn",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/Roslyn",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/Roslyn",
        "D:/Visual Studio 2022/MSBuild/Current/Bin/Roslyn",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/MSBuild/Current/Bin/Roslyn",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/MSBuild/Current/Bin/Roslyn",
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path))
            return path;
    }
    
    // Fallback - try to find csc in PATH
    std::cerr << "[CSharpScript] Warning: MSBuild Roslyn not found, relying on PATH" << std::endl;
    return "";
}

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
    std::cout << "[CSharpScript] Start called, started=" << started 
              << " enabled=" << enabled 
              << " scriptInstance=" << (scriptInstance ? "valid" : "null") << std::endl;
    
    if (!started && enabled)
    {
        CSharpScriptSystem::LogToConsole("[CSharpScript] Start: " + scriptName);
        
        if (!scriptInstance)
        {
            if (!scriptPath.empty() && fs::exists(scriptPath))
            {
                std::cout << "[CSharpScript] Loading script from source: " << scriptPath << std::endl;
                CSharpScriptSystem::LoadScript(scriptPath, this);
            }
            else if (!scriptName.empty())
            {
                std::cout << "[CSharpScript] Script source not found, trying precompiled DLL for: " << scriptName << std::endl;
                CSharpScriptSystem::LoadPrecompiledScript(scriptName, this);
            }
        }
        
        if (scriptInstance)
        {
            std::cout << "[CSharpScript] Calling MonoRuntime::CallStart" << std::endl;
            MonoRuntime::CallStart(scriptInstance);
        }
        else
        {
            std::cerr << "[CSharpScript] scriptInstance is null after load attempt!" << std::endl;
        }
        
        started = true;
    }
}

void CSharpScriptComponent::Update()
{
    static int updateCount = 0;
    if (++updateCount < 5)  // 只输出前5次，避免刷屏
    {
        std::cout << "[CSharpScript] Update called, enabled=" << enabled 
                  << " gameObject=" << gameObject 
                  << " scriptInstance=" << (scriptInstance ? "valid" : "null") << std::endl;
    }
    
    if (enabled && gameObject && scriptInstance)
    {
        // 调用 Mono 脚本的 Update 方法
        MonoRuntime::CallUpdate(scriptInstance);
    }
}

void CSharpScriptComponent::OnDestroy()
{
    CSharpScriptSystem::LogToConsole("[CSharpScript] Destroy: " + scriptName);
    
    // 调用 Mono 脚本的 OnDestroy 方法
    if (scriptInstance)
    {
        MonoRuntime::CallOnDestroy(scriptInstance);
    }
}

void CSharpScriptComponent::Serialize(std::ofstream& file) const
{
    // 保存脚本名称
    uint32_t nameLen = static_cast<uint32_t>(scriptName.length());
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(scriptName.c_str(), nameLen);
    
    // 保存脚本路径
    uint32_t pathLen = static_cast<uint32_t>(scriptPath.length());
    file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
    file.write(scriptPath.c_str(), pathLen);
    
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
    // 读取脚本名称
    uint32_t nameLen;
    file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    scriptName.resize(nameLen);
    file.read(&scriptName[0], nameLen);
    
    // 读取脚本路径
    uint32_t pathLen;
    file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
    scriptPath.resize(pathLen);
    file.read(&scriptPath[0], pathLen);
    
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
        // 标记场景为已修改
        void* editor = CSharpScriptSystem::GetEditor();
        if (editor)
            static_cast<Editor*>(editor)->MarkSceneDirty();
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
        bool modified = false;
        
        switch (field.type)
        {
            case ScriptFieldType::Float:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                float val = std::get<float>(field.value);
                if (ImGui::DragFloat(id.c_str(), &val, 0.1f))
                {
                    field.value = val;
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::Int:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                int val = std::get<int>(field.value);
                if (ImGui::DragInt(id.c_str(), &val))
                {
                    field.value = val;
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::Bool:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                bool val = std::get<bool>(field.value);
                if (ImGui::Checkbox(id.c_str(), &val))
                {
                    field.value = val;
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::String:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                std::string& val = std::get<std::string>(field.value);
                char buffer[256] = {};
                strncpy_s(buffer, val.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer)))
                {
                    val = buffer;
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::Vector2:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec2& val = std::get<glm::vec2>(field.value);
                float vec2[2] = { val.x, val.y };
                if (ImGui::DragFloat2(id.c_str(), vec2, 0.1f))
                {
                    val = glm::vec2(vec2[0], vec2[1]);
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::Vector3:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec3& val = std::get<glm::vec3>(field.value);
                float vec3[3] = { val.x, val.y, val.z };
                if (ImGui::DragFloat3(id.c_str(), vec3, 0.1f))
                {
                    val = glm::vec3(vec3[0], vec3[1], vec3[2]);
                    modified = true;
                }
                break;
            }
            case ScriptFieldType::Vector4:
            {
                ImGui::Text("%s", (label + " ").c_str()); ImGui::SameLine();
                glm::vec4& val = std::get<glm::vec4>(field.value);
                float vec4[4] = { val.x, val.y, val.z, val.w };
                if (ImGui::DragFloat4(id.c_str(), vec4, 0.1f))
                {
                    val = glm::vec4(vec4[0], vec4[1], vec4[2], vec4[3]);
                    modified = true;
                }
                break;
            }
        }
        
        // 如果有修改，标记场景为脏
        if (modified)
        {
            void* editor = CSharpScriptSystem::GetEditor();
            if (editor)
                static_cast<Editor*>(editor)->MarkSceneDirty();
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
    
    // 初始化 Mono 运行时
    if (!MonoRuntime::Initialize(""))
    {
        std::cerr << "[CSharpScriptSystem] Failed to initialize Mono runtime" << std::endl;
        return;
    }
    
    // 注册内部调用函数
    RegisterInternalCalls();
    
    s_initialized = true;
    std::cout << "[CSharpScriptSystem] Initialized" << std::endl;
}

void CSharpScriptSystem::Shutdown()
{
    if (!s_initialized) return;
    std::cout << "[CSharpScriptSystem] Shutting down..." << std::endl;
    
    MonoRuntime::Shutdown();
    s_initialized = false;
}

void CSharpScriptSystem::LogToConsole(const std::string& message)
{
    if (s_logCallback) {
        s_logCallback(message);
    } else {
        void* editor = GetEditor();
        if (editor) {
            static_cast<Editor*>(editor)->AddConsoleMessage(message);
        } else {
            std::cout << message << std::endl;
        }
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
    
    std::string dittoEngineDll = FindDittoEngineDll();
    std::string msbuildPath = FindMSBuildPath();
    
    std::string pathCmd;
    if (!msbuildPath.empty())
        pathCmd = "set PATH=" + msbuildPath + ";%PATH%&";
    
    std::string cmd = pathCmd + "cd /d \"" + dir + "\" && csc /target:library /reference:\"" + dittoEngineDll + "\" /out:\"" + dllPath + "\" \"" + absScriptPath.filename().string() + "\"";
    std::cout << "[CSharpScriptSystem] Compile cmd: " << cmd << std::endl;
    int result = system(cmd.c_str());
    
    if (result != 0)
    {
        std::cerr << "[CSharpScriptSystem] Compile failed: " << csPath << std::endl;
        return false;
    }
    
    std::cout << "[CSharpScriptSystem] Compiled: " << dllPath << std::endl;
    std::cout << "[CSharpScriptSystem] s_initialized=" << s_initialized 
              << " MonoRuntime::IsInitialized()=" << MonoRuntime::IsInitialized() << std::endl;
    
    // 加载到 Mono 运行时
    if (s_initialized && MonoRuntime::IsInitialized())
    {
        std::cout << "[CSharpScriptSystem] Loading script into Mono runtime..." << std::endl;
        component->scriptInstance = MonoRuntime::LoadScript(dllPath, fileName);
        if (component->scriptInstance)
        {
            std::cout << "[CSharpScriptSystem] Script loaded into Mono runtime: " << fileName << std::endl;
            
            // 设置 GameObject 指针到 C# 脚本实例
            if (component->gameObject && component->scriptInstance->instance)
            {
                // 调用 SetNativeGameObject 方法设置 GameObject 指针
                // 从实例获取类，然后查找方法（包括基类）
                MonoClass* klass = MonoRuntime::GetClassFromObject(component->scriptInstance->instance);
                MonoMethod* setNativeMethod = nullptr;
                
                // 在当前类及其基类中查找方法
                while (klass && !setNativeMethod)
                {
                    setNativeMethod = MonoRuntime::GetMethod(klass, "SetNativeGameObject", 1);
                    if (!setNativeMethod)
                    {
                        // 获取父类
                        klass = MonoRuntime::GetParentClass(klass);
                    }
                }
                
                if (setNativeMethod)
                {
                    void* goPtr = component->gameObject;
                    void* args[1] = { &goPtr };
                    MonoRuntime::InvokeMethod(component->scriptInstance->instance, setNativeMethod, args);
                    std::cout << "[CSharpScriptSystem] Set gameObject pointer: " << goPtr << std::endl;
                }
                else
                {
                    std::cerr << "[CSharpScriptSystem] SetNativeGameObject method not found in class hierarchy!" << std::endl;
                }
            }
        }
        else
        {
            std::cerr << "[CSharpScriptSystem] Failed to load script into Mono runtime: " << fileName << std::endl;
        }
    }
    else
    {
        std::cerr << "[CSharpScriptSystem] Mono runtime not initialized! s_initialized=" 
                  << s_initialized << " MonoInitialized=" << MonoRuntime::IsInitialized() << std::endl;
    }
    
    // 解析 public 变量
    component->ParseScriptFields();
    
    return true;
}

bool CSharpScriptSystem::LoadPrecompiledScript(const std::string& className, CSharpScriptComponent* component)
{
    if (!component) return false;
    if (!s_initialized || !MonoRuntime::IsInitialized())
    {
        std::cerr << "[CSharpScriptSystem] Mono runtime not initialized for precompiled loading" << std::endl;
        return false;
    }

    std::vector<std::string> searchPaths = {
        "GameScripts.dll",
        "Assets/GameScripts.dll",
        "../GameScripts.dll",
    };

    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::string exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            std::string dir = exeDir.substr(0, lastSlash);
            searchPaths.insert(searchPaths.begin(), dir + "/GameScripts.dll");
        }
    }

    std::string dllPath;
    for (const auto& p : searchPaths)
    {
        if (fs::exists(p))
        {
            dllPath = fs::absolute(p).string();
            break;
        }
    }

    if (dllPath.empty())
    {
        std::cerr << "[CSharpScriptSystem] GameScripts.dll not found" << std::endl;
        return false;
    }

    std::cout << "[CSharpScriptSystem] Loading precompiled script: " << className << " from " << dllPath << std::endl;

    component->scriptInstance = MonoRuntime::LoadScript(dllPath, className);
    if (!component->scriptInstance)
    {
        std::cerr << "[CSharpScriptSystem] Failed to load precompiled script: " << className << std::endl;
        return false;
    }

    std::cout << "[CSharpScriptSystem] Precompiled script loaded: " << className << std::endl;

    if (component->gameObject && component->scriptInstance->instance)
    {
        MonoClass* klass = MonoRuntime::GetClassFromObject(component->scriptInstance->instance);
        MonoMethod* setNativeMethod = nullptr;

        while (klass && !setNativeMethod)
        {
            setNativeMethod = MonoRuntime::GetMethod(klass, "SetNativeGameObject", 1);
            if (!setNativeMethod)
                klass = MonoRuntime::GetParentClass(klass);
        }

        if (setNativeMethod)
        {
            void* goPtr = component->gameObject;
            void* args[1] = { &goPtr };
            MonoRuntime::InvokeMethod(component->scriptInstance->instance, setNativeMethod, args);
            std::cout << "[CSharpScriptSystem] Set gameObject pointer for precompiled: " << goPtr << std::endl;
        }
        else
        {
            std::cerr << "[CSharpScriptSystem] SetNativeGameObject method not found!" << std::endl;
        }
    }

    return true;
}

void CSharpScriptSystem::ReloadAll()
{
    std::cout << "[CSharpScriptSystem] Reloading..." << std::endl;
}

void CSharpScriptSystem::CallStart()
{
    if (!s_initialized) return;
    
    // 遍历所有 GameObject 的 C# 脚本组件并调用 Start
    // 注意：这里需要通过某种方式获取所有 GameObject
    // 暂时通过 Editor 获取 Scene 来遍历
    void* editor = GetEditor();
    if (!editor) return;
    
    // TODO: 实现遍历逻辑
}

void CSharpScriptSystem::CallUpdate()
{
    if (!s_initialized) return;
    
    // 遍历所有 GameObject 的 C# 脚本组件并调用 Update
    void* editor = GetEditor();
    if (!editor) return;
    
    // TODO: 实现遍历逻辑
}

// 注册内部调用函数（C++ 函数供 C# 调用）
void CSharpScriptSystem::RegisterInternalCalls()
{
    // 注册 Transform 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::GetPosition", (void*)Internal_Transform_GetPosition);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::SetPosition", (void*)Internal_Transform_SetPosition);
    
    // 注册 GameObject 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.MonoBehaviour::GameObject_GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetComponentByType", (void*)Internal_GameObject_GetComponentByType);
    
    // 注册 Renderer 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetColor", (void*)Internal_Renderer_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetColor", (void*)Internal_Renderer_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetShapeType", (void*)Internal_Renderer_GetShapeType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetShapeType", (void*)Internal_Renderer_SetShapeType);
    
    // 注册 Light 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::GetLightColor", (void*)Internal_Light_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::SetLightColor", (void*)Internal_Light_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::GetIntensity", (void*)Internal_Light_GetIntensity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::SetIntensity", (void*)Internal_Light_SetIntensity);
    
    // 注册 Rigidbody 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetBodyType", (void*)Internal_Rigidbody_GetBodyType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetBodyType", (void*)Internal_Rigidbody_SetBodyType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetMass", (void*)Internal_Rigidbody_GetMass);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetMass", (void*)Internal_Rigidbody_SetMass);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetUseGravity", (void*)Internal_Rigidbody_GetUseGravity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetUseGravity", (void*)Internal_Rigidbody_SetUseGravity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetLinearDamping", (void*)Internal_Rigidbody_GetLinearDamping);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetLinearDamping", (void*)Internal_Rigidbody_SetLinearDamping);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetAngularDamping", (void*)Internal_Rigidbody_GetAngularDamping);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetAngularDamping", (void*)Internal_Rigidbody_SetAngularDamping);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetVelocity", (void*)Internal_Rigidbody_GetVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetVelocity", (void*)Internal_Rigidbody_SetVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::GetAngularVelocity", (void*)Internal_Rigidbody_GetAngularVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody::SetAngularVelocity", (void*)Internal_Rigidbody_SetAngularVelocity);
    
    // 注册 Time 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Time::GetDeltaTime", (void*)Internal_Time_GetDeltaTime);
    
    // 注册 Debug 相关函数
    ::MonoRuntime::AddInternalCall("DittoEngine.Debug::Log", (void*)Internal_Debug_Log);
    
    std::cout << "[CSharpScriptSystem] Internal calls registered" << std::endl;
}

// 内部调用实现
extern "C" {
    // Transform 组件操作 - transform 指针实际上是 TransformComponent*
    void Internal_Transform_GetPosition(void* transform, float* outPos)
    {
        std::cout << "[Internal] GetPosition called with transform: " << transform << std::endl;
        
        if (!transform || !outPos) 
        {
            std::cerr << "[Internal] transform or outPos is null!" << std::endl;
            return;
        }
        
        TransformComponent* trans = static_cast<TransformComponent*>(transform);
        std::cout << "[Internal] Transform position: (" << trans->position.x << ", " << trans->position.y << ", " << trans->position.z << ")" << std::endl;
        
        outPos[0] = trans->position.x;
        outPos[1] = trans->position.y;
        outPos[2] = trans->position.z;
    }

    void Internal_Transform_SetPosition(void* transform, float x, float y, float z)
    {
        std::cout << "[Internal] SetPosition called with transform: " << transform << " pos: (" << x << ", " << y << ", " << z << ")" << std::endl;
        
        if (!transform) 
        {
            std::cerr << "[Internal] transform is null!" << std::endl;
            return;
        }
        
        TransformComponent* trans = static_cast<TransformComponent*>(transform);
        trans->position.x = x;
        trans->position.y = y;
        trans->position.z = z;
        trans->localDirty = true;
        
        std::cout << "[Internal] Position set successfully" << std::endl;
    }
    
    // 通过 GameObject 指针获取 Transform 组件指针
    void* Internal_GameObject_GetTransform(void* gameObject)
    {
        std::cout << "[Internal] GetTransform called with gameObject: " << gameObject << std::endl;
        
        if (!gameObject) 
        {
            std::cerr << "[Internal] gameObject is null!" << std::endl;
            return nullptr;
        }
        
        GameObject* go = static_cast<GameObject*>(gameObject);
        std::cout << "[Internal] GameObject name: " << (go->name.empty() ? "<empty>" : go->name) << std::endl;
        std::cout << "[Internal] GameObject components count: " << go->components.size() << std::endl;
        
        for (Component* comp : go->components)
        {
            if (!comp) 
            {
                std::cerr << "[Internal] Found null component!" << std::endl;
                continue;
            }
            
            std::cout << "[Internal] Checking component with index: " << comp->index << std::endl;
            
            if (comp->index == (1 << 0)) // TRANSFORM_INDEX
            {
                std::cout << "[Internal] Found Transform component: " << comp << std::endl;
                return comp;
            }
        }
        
        std::cerr << "[Internal] Transform component not found!" << std::endl;
        return nullptr;
    }

    float Internal_Time_GetDeltaTime()
    {
        // TODO: 返回引擎的 deltaTime
        return 0.016f; // 默认 60fps
    }

    void Internal_Debug_Log(void* msg)  // MonoString*
    {
        std::string message = MonoRuntime::GetStringFromMono((MonoString*)msg);
        std::cout << "[C#] " << message << std::endl;
        CSharpScriptSystem::LogToConsole("[C#] " + message);
    }
    
    // 通过类型名获取组件
    void* Internal_GameObject_GetComponentByType(void* gameObject, void* typeName)
    {
        if (!gameObject || !typeName) return nullptr;
        
        std::string typeStr = MonoRuntime::GetStringFromMono((MonoString*)typeName);
        GameObject* go = static_cast<GameObject*>(gameObject);
        
        if (typeStr == "Transform")
        {
            for (Component* comp : go->components)
                if (comp && comp->index == (1 << 0)) return comp;
        }
        else if (typeStr == "Light")
        {
            for (Component* comp : go->components)
                if (comp && comp->index == (1 << 1)) return comp;
        }
        else if (typeStr == "Renderer")
        {
            for (Component* comp : go->components)
                if (comp && comp->index == (1 << 2)) return comp;
        }
        else if (typeStr == "Rigidbody")
        {
            for (Component* comp : go->components)
                if (comp && comp->index == (1 << 3)) return comp;
        }
        
        return nullptr;
    }
    
    // Renderer 组件颜色操作
    void Internal_Renderer_GetColor(void* renderer, float* outColor)
    {
        if (!renderer || !outColor)
        {
            std::cerr << "[Internal] Renderer_GetColor: null pointer!" << std::endl;
            return;
        }
        
        RendererComponent* rend = static_cast<RendererComponent*>(renderer);
        outColor[0] = rend->color.r;
        outColor[1] = rend->color.g;
        outColor[2] = rend->color.b;
        outColor[3] = rend->color.a;
        
        std::cout << "[Internal] Renderer_GetColor: (" << outColor[0] << ", " << outColor[1] << ", " << outColor[2] << ", " << outColor[3] << ")" << std::endl;
    }
    
    void Internal_Renderer_SetColor(void* renderer, float r, float g, float b, float a)
    {
        if (!renderer)
        {
            std::cerr << "[Internal] Renderer_SetColor: null pointer!" << std::endl;
            return;
        }
        
        RendererComponent* rend = static_cast<RendererComponent*>(renderer);
        rend->color.r = r;
        rend->color.g = g;
        rend->color.b = b;
        rend->color.a = a;
    }
    
    int Internal_Renderer_GetShapeType(void* renderer)
    {
        if (!renderer) return 0;
        RendererComponent* rend = static_cast<RendererComponent*>(renderer);
        return static_cast<int>(rend->type);
    }
    
    void Internal_Renderer_SetShapeType(void* renderer, int type)
    {
        if (!renderer) return;
        RendererComponent* rend = static_cast<RendererComponent*>(renderer);
        rend->type = static_cast<RendererComponent::Type>(type);
    }
    
    void Internal_Light_GetColor(void* light, float* outColor)
    {
        if (!light || !outColor) return;
        LightComponent* l = static_cast<LightComponent*>(light);
        outColor[0] = l->color.r;
        outColor[1] = l->color.g;
        outColor[2] = l->color.b;
    }
    
    void Internal_Light_SetColor(void* light, float r, float g, float b)
    {
        if (!light) return;
        LightComponent* l = static_cast<LightComponent*>(light);
        l->color.r = r;
        l->color.g = g;
        l->color.b = b;
    }
    
    float Internal_Light_GetIntensity(void* light)
    {
        if (!light) return 1.0f;
        LightComponent* l = static_cast<LightComponent*>(light);
        return l->intensity;
    }
    
    void Internal_Light_SetIntensity(void* light, float intensity)
    {
        if (!light) return;
        LightComponent* l = static_cast<LightComponent*>(light);
        l->intensity = intensity;
    }
    
    int Internal_Rigidbody_GetBodyType(void* rigidbody)
    {
        if (!rigidbody) return 0;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        return static_cast<int>(rb->type);
    }
    
    void Internal_Rigidbody_SetBodyType(void* rigidbody, int type)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->type = static_cast<RigidbodyComponent::Type>(type);
    }
    
    float Internal_Rigidbody_GetMass(void* rigidbody)
    {
        if (!rigidbody) return 1.0f;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        return rb->mass;
    }
    
    void Internal_Rigidbody_SetMass(void* rigidbody, float mass)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->mass = mass;
    }
    
    int Internal_Rigidbody_GetUseGravity(void* rigidbody)
    {
        if (!rigidbody) return 0;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        return rb->useGravity ? 1 : 0;
    }
    
    void Internal_Rigidbody_SetUseGravity(void* rigidbody, int useGravity)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->useGravity = (useGravity != 0);
    }
    
    float Internal_Rigidbody_GetLinearDamping(void* rigidbody)
    {
        if (!rigidbody) return 0.0f;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        return rb->damp;
    }
    
    void Internal_Rigidbody_SetLinearDamping(void* rigidbody, float damp)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->damp = damp;
    }
    
    float Internal_Rigidbody_GetAngularDamping(void* rigidbody)
    {
        if (!rigidbody) return 0.0f;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        return rb->angularDamp;
    }
    
    void Internal_Rigidbody_SetAngularDamping(void* rigidbody, float damp)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->angularDamp = damp;
    }
    
    void Internal_Rigidbody_GetVelocity(void* rigidbody, float* outVel)
    {
        if (!rigidbody || !outVel) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        outVel[0] = rb->velocity.x;
        outVel[1] = rb->velocity.y;
        outVel[2] = rb->velocity.z;
    }
    
    void Internal_Rigidbody_SetVelocity(void* rigidbody, float x, float y, float z)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->velocity.x = x;
        rb->velocity.y = y;
        rb->velocity.z = z;
    }
    
    void Internal_Rigidbody_GetAngularVelocity(void* rigidbody, float* outVel)
    {
        if (!rigidbody || !outVel) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        outVel[0] = rb->angularVelocity.x;
        outVel[1] = rb->angularVelocity.y;
        outVel[2] = rb->angularVelocity.z;
    }
    
    void Internal_Rigidbody_SetAngularVelocity(void* rigidbody, float x, float y, float z)
    {
        if (!rigidbody) return;
        RigidbodyComponent* rb = static_cast<RigidbodyComponent*>(rigidbody);
        rb->angularVelocity.x = x;
        rb->angularVelocity.y = y;
        rb->angularVelocity.z = z;
    }
}
