#include <iostream>
#include <filesystem>
#include <windows.h>
#include <regex>
#include "CSharpScript.h"
#include "MonoRuntime.h"
#include "GameObject.h"
#include "../../Editor/Editor.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

namespace fs = std::filesystem;

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
        if (fs::exists(path)) return fs::absolute(path).string();
    }

    return "3rdParty/Mono/DittoEngine.dll";
}

static std::string FindMSBuildPath()
{
    char* vsPathEnv = nullptr;
    size_t vsPathLen = 0;
    if (_dupenv_s(&vsPathEnv, &vsPathLen, "VSINSTALLDIR") == 0 && vsPathEnv)
    {
        std::string roslynPath = std::string(vsPathEnv) + "MSBuild\\Current\\Bin\\Roslyn";
        free(vsPathEnv);
        if (fs::exists(roslynPath))
            return roslynPath;
    }

    const char* regKey = "SOFTWARE\\Microsoft\\VisualStudio\\Setup\\Instances";
    HKEY hKey = nullptr;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char name[MAX_PATH];
        DWORD index = 0;
        DWORD nameSize = MAX_PATH;

        while (RegEnumKeyExA(hKey, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            std::string instanceKey = std::string(regKey) + "\\" + name;
            HKEY hInstKey = nullptr;

            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, KEY_READ, &hInstKey) == ERROR_SUCCESS)
            {
                char installPath[MAX_PATH];
                DWORD size = MAX_PATH;
                DWORD type = REG_SZ;

                if (RegQueryValueExA(hInstKey, "InstallLocation", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS)
                {
                    std::string roslynPath = std::string(installPath) + "MSBuild\\Current\\Bin\\Roslyn";
                    if (fs::exists(roslynPath))
                    {
                        RegCloseKey(hInstKey);
                        RegCloseKey(hKey);
                        return roslynPath;
                    }
                }
                RegCloseKey(hInstKey);
            }
            nameSize = MAX_PATH;
        }
        RegCloseKey(hKey);
    }

    char* pathEnv = nullptr;
    size_t pathLen = 0;
    if (_dupenv_s(&pathEnv, &pathLen, "PATH") == 0 && pathEnv)
    {
        std::string pathStr(pathEnv);
        free(pathEnv);

        size_t pos = 0;
        while ((pos = pathStr.find(';')) != std::string::npos)
        {
            std::string dir = pathStr.substr(0, pos);
            std::string cscPath = dir + "\\csc.exe";
            if (fs::exists(cscPath))
            {
                size_t lastSlash = dir.find_last_of("\\/");
                if (lastSlash != std::string::npos)
                {
                    std::string roslynDir = dir.substr(0, lastSlash);
                    if (roslynDir.find("Roslyn") != std::string::npos)
                        return roslynDir;
                }
            }
            pathStr.erase(0, pos + 1);
        }
    }

    const char* drives[] = { "D:", "C:", "E:", "F:" };
    for (const auto& drive : drives)
    {
        std::string vsBase = std::string(drive) + "\\Visual Studio 2022";
        if (fs::exists(vsBase))
        {
            std::string roslynPath = vsBase + "\\MSBuild\\Current\\Bin\\Roslyn";
            if (fs::exists(roslynPath))
                return roslynPath;

            for (const auto& entry : fs::directory_iterator(vsBase))
            {
                if (!entry.is_directory())
                    continue;
                roslynPath = entry.path().string() + "\\MSBuild\\Current\\Bin\\Roslyn";
                if (fs::exists(roslynPath))
                    return roslynPath;
            }
        }

        std::string progFiles = std::string(drive) + "\\Program Files\\Microsoft Visual Studio\\2022";
        if (fs::exists(progFiles))
        {
            for (const auto& entry : fs::directory_iterator(progFiles))
            {
                if (!entry.is_directory())
                    continue;
                std::string roslynPath = entry.path().string() + "\\MSBuild\\Current\\Bin\\Roslyn";
                if (fs::exists(roslynPath))
                    return roslynPath;
            }
        }
    }

    return "";
}

bool CSharpScriptSystem::s_initialized = false;
LogCallback CSharpScriptSystem::s_logCallback = nullptr;
void* CSharpScriptSystem::s_editor = nullptr;

CSharpScriptComponent::CSharpScriptComponent()
{
    index = 1 << 10;
    m_lastWriteTime = (std::numeric_limits<std::filesystem::file_time_type>::min)();
}

void CSharpScriptComponent::ParseScriptFields()
{
    fields.clear();
    if (scriptPath.empty()) return;

    std::ifstream file(scriptPath);
    if (!file.is_open()) return;

    std::string line;

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

        if (std::regex_search(line, match, vec4Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector4);
            glm::vec4 val(std::stof(match[2].str()), std::stof(match[3].str()), std::stof(match[4].str()), std::stof(match[5].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, vec3Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector3);
            glm::vec3 val(std::stof(match[2].str()), std::stof(match[3].str()), std::stof(match[4].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, vec2Regex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Vector2);
            glm::vec2 val(std::stof(match[2].str()), std::stof(match[3].str()));
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, floatRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Float);
            field.defaultValue = field.value = std::stof(match[2].str());
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, intRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Int);
            field.defaultValue = field.value = std::stoi(match[2].str());
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, boolRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::Bool);
            bool val = (match[2].str() == "true");
            field.defaultValue = field.value = val;
            fields.push_back(field);
        }
        else if (std::regex_search(line, match, stringRegex))
        {
            ScriptField field(match[1].str(), ScriptFieldType::String);
            field.defaultValue = field.value = match[2].str();
            fields.push_back(field);
        }
    }
}

void CSharpScriptComponent::Start()
{
    if (!started && enabled)
    {
        if (!scriptInstance)
        {
            if (!scriptPath.empty() && fs::exists(scriptPath))
            {
                CSharpScriptSystem::LoadScript(scriptPath, this);
            }
            else if (!scriptName.empty())
            {
                CSharpScriptSystem::LoadPrecompiledScript(scriptName, this);
            }
        }

        if (scriptInstance)
        {
            MonoRuntime::CallStart(scriptInstance);

            try
            {
                m_lastWriteTime = fs::last_write_time(scriptPath);
            }
            catch (const fs::filesystem_error&)
            {
            }
        }

        started = true;
    }
}

void CSharpScriptComponent::Update()
{
    if (enabled && gameObject && scriptInstance)
    {
        MonoRuntime::CallUpdate(scriptInstance);
    }
}

void CSharpScriptComponent::OnDestroy()
{
    if (scriptInstance) MonoRuntime::CallOnDestroy(scriptInstance);
}

void CSharpScriptComponent::Serialize(std::ofstream& file) const
{
    uint32_t nameLen = static_cast<uint32_t>(scriptName.length());
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(scriptName.c_str(), nameLen);

    uint32_t pathLen = static_cast<uint32_t>(scriptPath.length());
    file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
    file.write(scriptPath.c_str(), pathLen);

    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));

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

    uint32_t pathLen;
    file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
    scriptPath.resize(pathLen);
    file.read(&scriptPath[0], pathLen);

    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));

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

    std::vector<ScriptField> savedFields = fields;
    ParseScriptFields();

    for (auto& field : fields)
    {
        for (auto& saved : savedFields)
        {
            if (field.name == saved.name && field.type == saved.type)
            {
                field.value = saved.value; break;
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
        void* editor = CSharpScriptSystem::GetEditor();
        if (editor) static_cast<Editor*>(editor)->MarkSceneDirty();
        return;
    }

    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    ImGui::Text("Script: "); ImGui::SameLine();
    ImGui::TextDisabled("%s", scriptPath.c_str());

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

        if (modified)
        {
            void* editor = CSharpScriptSystem::GetEditor();
            if (editor) static_cast<Editor*>(editor)->MarkSceneDirty();
        }
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

void CSharpScriptSystem::Initialize()
{
    if (s_initialized) return;

    CleanOldCompiledDLLs();

    if (!MonoRuntime::Initialize(""))
    {
        return;
    }

    RegisterInternalCalls();

    s_initialized = true;
}

void CSharpScriptSystem::CleanOldCompiledDLLs()
{
    fs::path currentDir = fs::absolute(".");
    fs::path projectsRoot = currentDir / "Projects";
    if (!fs::exists(projectsRoot)) return;

    try
    {
        for (auto& entry : fs::directory_iterator(projectsRoot))
        {
            if (!entry.is_directory()) continue;
            fs::path projectDir = entry.path();
            fs::path tempDir = projectDir / "Temp";
            if (!fs::exists(tempDir)) continue;

            for (auto& tempEntry : fs::directory_iterator(tempDir))
            {
                if (tempEntry.is_regular_file() && tempEntry.path().extension() == ".dll")
                {
                    std::string name = tempEntry.path().stem().string();
                    size_t underscorePos = name.find('_');
                    if (underscorePos != std::string::npos)
                    {
                        fs::remove(tempEntry.path());
                    }
                }
            }
        }
    }
    catch (const fs::filesystem_error&)
    {
    }
}

void CSharpScriptSystem::Shutdown()
{
    if (!s_initialized) return;

    MonoRuntime::Shutdown();
    s_initialized = false;
}

void CSharpScriptSystem::LogToConsole(const std::string& message)
{
    if (s_logCallback) s_logCallback(message);
    else 
    {
        void* editor = GetEditor();
        if (editor) static_cast<Editor*>(editor)->AddConsoleMessage(message);
        else std::cout << message << std::endl;
    }
}

bool CSharpScriptSystem::LoadScript(const std::string& csPath, CSharpScriptComponent* component)
{
    if (!component) return false;

    std::string fileName = csPath;
    size_t pos = fileName.find_last_of("/\\");
    if (pos != std::string::npos) fileName = fileName.substr(pos + 1);
    pos = fileName.find_last_of('.');
    if (pos != std::string::npos) fileName = fileName.substr(0, pos);

    component->scriptName = fileName;
    component->scriptPath = csPath;

    fs::path scriptPath(csPath);
    fs::path absScriptPath = fs::absolute(scriptPath);
    fs::path projectRoot = absScriptPath.parent_path().parent_path();
    fs::path tempDir = projectRoot / "Temp";
    fs::create_directories(tempDir);

    static int s_loadCounter = 0;
    s_loadCounter++;
    std::string uniqueName = fileName + "_" + std::to_string(s_loadCounter);
    fs::path dllPath = tempDir / (uniqueName + ".dll");

    std::string dllPathStr = dllPath.string();

    if (!CompileScript(csPath, dllPathStr)) return false;

    if (CSharpScriptSystem::IsInitialized() && MonoRuntime::IsInitialized())
    {
        component->scriptInstance = MonoRuntime::LoadScript(dllPathStr, fileName);
        if (component->scriptInstance && component->gameObject)
        {
            MonoClass* klass = MonoRuntime::GetClassFromObject(component->scriptInstance->instance);
            MonoMethod* setNativeMethod = nullptr;

            while (klass && !setNativeMethod)
            {
                setNativeMethod = MonoRuntime::GetMethod(klass, "SetNativeGameObject", 1);
                if (!setNativeMethod) klass = MonoRuntime::GetParentClass(klass);
            }

            if (setNativeMethod)
            {
                void* goPtr = component->gameObject;
                void* args[1] = { &goPtr };
                MonoRuntime::InvokeMethod(component->scriptInstance->instance, setNativeMethod, args);
            }
        }
    }

    {
        std::error_code ec;
        fs::remove(dllPathStr, ec);
    }

    component->ParseScriptFields();

    try
    {
        component->m_lastWriteTime = fs::last_write_time(csPath);
    }
    catch (const fs::filesystem_error&)
    {
    }

    return true;
}

bool CSharpScriptSystem::LoadPrecompiledScript(const std::string& className, CSharpScriptComponent* component)
{
    if (!component) return false;
    if (!s_initialized || !MonoRuntime::IsInitialized()) return false;

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

    if (dllPath.empty()) return false;

    component->scriptInstance = MonoRuntime::LoadScript(dllPath, className);
    if (!component->scriptInstance) return false;

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
        }
    }

    return true;
}

void CSharpScriptSystem::ReloadAll()
{
}

bool CSharpScriptComponent::ShouldReload()
{
    if (scriptPath.empty() || !fs::exists(scriptPath)) return false;

    try
    {
        fs::file_time_type currentTime = fs::last_write_time(scriptPath);
        if (currentTime > m_lastWriteTime)
        {
            return true;
        }
    }
    catch (const fs::filesystem_error&)
    {
    }
    return false;
}

void CSharpScriptComponent::HotReloadScript()
{
    if (scriptPath.empty()) return;

    std::cerr << "[CSharpScript] HotReload: " << scriptPath << std::endl;

    if (scriptInstance)
    {
        MonoRuntime::UnloadScript(scriptInstance);
        scriptInstance.reset();
    }

    fs::path scriptPathObj(scriptPath);
    fs::path absScriptPath = fs::absolute(scriptPathObj);
    fs::path projectRoot = absScriptPath.parent_path().parent_path();
    fs::path tempDir = projectRoot / "Temp";
    fs::create_directories(tempDir);

    static int s_reloadCounter = 0;
    s_reloadCounter++;
    std::string uniqueName = absScriptPath.stem().string() + "_" + std::to_string(s_reloadCounter);
    fs::path dllPath = tempDir / (uniqueName + ".dll");

    std::string newDllPath = dllPath.string();
    if (!CSharpScriptSystem::CompileScript(scriptPath, newDllPath))
    {
        std::cerr << "[CSharpScript] HotReload compile failed" << std::endl;
        return;
    }

    std::cerr << "[CSharpScript] HotReload compiled to: " << newDllPath << std::endl;

    if (CSharpScriptSystem::IsInitialized() && MonoRuntime::IsInitialized())
    {
        scriptInstance = MonoRuntime::LoadScript(newDllPath, scriptName);
        if (!scriptInstance)
        {
            std::cerr << "[CSharpScript] HotReload LoadScript failed" << std::endl;
            return;
        }
        std::cerr << "[CSharpScript] HotReload loaded, calling SetNativeGameObject" << std::endl;

        if (gameObject)
        {
            MonoClass* klass = MonoRuntime::GetClassFromObject(scriptInstance->instance);
            MonoMethod* setNativeMethod = nullptr;

            while (klass && !setNativeMethod)
            {
                setNativeMethod = MonoRuntime::GetMethod(klass, "SetNativeGameObject", 1);
                if (!setNativeMethod) klass = MonoRuntime::GetParentClass(klass);
            }

            if (setNativeMethod)
            {
                void* goPtr = gameObject;
                void* args[1] = { &goPtr };
                MonoRuntime::InvokeMethod(scriptInstance->instance, setNativeMethod, args);
            }
        }
    }

    {
        std::error_code ec;
        fs::remove(newDllPath, ec);
    }

    ParseScriptFields();
    started = false;

    try
    {
        m_lastWriteTime = fs::last_write_time(scriptPath);
    }
    catch (const fs::filesystem_error&)
    {
    }
}

bool CSharpScriptSystem::CompileScript(const std::string& csPath, std::string& outDllPath)
{
    if (!fs::exists(csPath)) return false;

    fs::path scriptPath(csPath);
    fs::path absScriptPath = fs::absolute(scriptPath);
    if (outDllPath.empty())
    {
        fs::path dllAbsPath = absScriptPath.parent_path() / (absScriptPath.stem().string() + ".dll");
        outDllPath = dllAbsPath.string();
    }

    std::string dittoEngineDll = FindDittoEngineDll();
    if (dittoEngineDll.empty()) return false;

    std::string monoMscorlib;
    {
        const std::vector<std::string> mscorlibPaths = {
            "3rdParty/Mono/mscorlib.dll",
            "../../3rdParty/Mono/mscorlib.dll",
            "../3rdParty/Mono/mscorlib.dll",
            "Ditto/3rdParty/Mono/mscorlib.dll",
            "../../Ditto/3rdParty/Mono/mscorlib.dll",
        };
        for (const auto& path : mscorlibPaths)
        {
            if (fs::exists(path))
            {
                monoMscorlib = fs::absolute(path).string();
                break;
            }
        }
    }

    if (monoMscorlib.empty()) return false;

    std::string roslynPath = FindMSBuildPath();
    if (roslynPath.empty()) return false;

    std::string cscPath = roslynPath + "\\csc.exe";
    if (!fs::exists(cscPath)) return false;

    if (fs::exists(outDllPath))
    {
        try { fs::remove(outDllPath); }
        catch (const fs::filesystem_error&)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            try { fs::remove(outDllPath); } catch (...) {}
        }
    }

    size_t lastSlash = cscPath.find_last_of("\\/");
    std::string cscDir = cscPath.substr(0, lastSlash);
    std::string cmd = "cmd /c cd /d \"" + cscDir + "\" && csc.exe"
        + " /target:library"
        + " /nostdlib+"
        + " /reference:\"" + monoMscorlib + "\""
        + " /reference:\"" + dittoEngineDll + "\""
        + " /out:\"" + outDllPath + "\""
        + " \"" + absScriptPath.string() + "\""
        + " 2>&1";

    int result = system(cmd.c_str());
    if (result != 0 || !fs::exists(outDllPath)) return false;

    return true;
}

bool CSharpScriptSystem::HotReloadScript(CSharpScriptComponent* component)
{
    if (!component) return false;

    component->HotReloadScript();
    return component->scriptInstance != nullptr;
}

void CSharpScriptSystem::CallStart()
{
    if (!s_initialized) return;

    void* editor = GetEditor();
    if (!editor) return;
}

void CSharpScriptSystem::CallUpdate()
{
    if (!s_initialized) return;

    void* editor = GetEditor();
    if (!editor) return;
}

void CSharpScriptSystem::RegisterInternalCalls()
{
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::GetPosition", (void*)Internal_Transform_GetPosition);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::SetPosition", (void*)Internal_Transform_SetPosition);

    ::MonoRuntime::AddInternalCall("DittoEngine.MonoBehaviour::GameObject_GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetComponentByType", (void*)Internal_GameObject_GetComponentByType);

    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetColor", (void*)Internal_Renderer_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetColor", (void*)Internal_Renderer_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetShapeType", (void*)Internal_Renderer_GetShapeType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetShapeType", (void*)Internal_Renderer_SetShapeType);

    ::MonoRuntime::AddInternalCall("DittoEngine.Light::GetLightColor", (void*)Internal_Light_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::SetLightColor", (void*)Internal_Light_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::GetIntensity", (void*)Internal_Light_GetIntensity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Light::SetIntensity", (void*)Internal_Light_SetIntensity);

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

    ::MonoRuntime::AddInternalCall("DittoEngine.Time::GetDeltaTime", (void*)Internal_Time_GetDeltaTime);

    ::MonoRuntime::AddInternalCall("DittoEngine.Debug::Log", (void*)Internal_Debug_Log);
}

extern "C" {

void Internal_Transform_GetPosition(void* transform, float* outPos)
{
    if (!transform || !outPos) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    outPos[0] = trans->position.x;
    outPos[1] = trans->position.y;
    outPos[2] = trans->position.z;
}

void Internal_Transform_SetPosition(void* transform, float x, float y, float z)
{
    if (!transform) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    trans->position.x = x;
    trans->position.y = y;
    trans->position.z = z;
    trans->localDirty = true;
    trans->UpdateTransform();
}

void* Internal_GameObject_GetTransform(void* gameObject)
{
    if (!gameObject) return nullptr;

    GameObject* go = static_cast<GameObject*>(gameObject);

    for (Component* comp : go->components)
    {
        if (!comp) continue;

        if (comp->index == (1 << 0))
        {
            return comp;
        }
    }

    return nullptr;
}

float Internal_Time_GetDeltaTime()
{
    return 0.016f;
}

void Internal_Debug_Log(void* msg)
{
    std::string message = MonoRuntime::GetStringFromMono((MonoString*)msg);
    std::cout << "[C#] " << message << std::endl;
    CSharpScriptSystem::LogToConsole("[C#] " + message);
}

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

void Internal_Renderer_GetColor(void* renderer, float* outColor)
{
    if (!renderer || !outColor) return;

    RendererComponent* rend = static_cast<RendererComponent*>(renderer);
    outColor[0] = rend->color.r;
    outColor[1] = rend->color.g;
    outColor[2] = rend->color.b;
    outColor[3] = rend->color.a;
}

void Internal_Renderer_SetColor(void* renderer, float r, float g, float b, float a)
{
    if (!renderer) return;

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
