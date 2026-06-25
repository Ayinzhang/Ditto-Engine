#include <iostream>
#include <filesystem>
#include <windows.h>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <functional>
#include "CSharpScript.h"
#include "CSharpScriptCompiler.h"
#include "RuntimeContext.h"
#include "MonoRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Logger.h"
#include "../Resources/AssetReferenceIO.h"
#ifndef DITTO_HEADLESS_TESTS
#include "../../Editor/Editor.h"
#include "../../Editor/ProjectWindow.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "Input.h"
#include "../Audio/AudioEngine.h"
#include "../Resources/AssetPath.h"
#endif
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include "../Physics/Physics.h"
#include "../Animation/AnimatorComponent.h"
#include "../Graphics/ParticleSystemComponent.h"

namespace fs = std::filesystem;

bool CSharpScriptSystem::s_initialized = false;
float CSharpScriptSystem::s_deltaTime = 0.016f;
float CSharpScriptSystem::s_time = 0.0f;
Physics* CSharpScriptSystem::s_physics = nullptr;
LogCallback CSharpScriptSystem::s_logCallback = nullptr;
Editor* CSharpScriptSystem::s_editor = nullptr;

CSharpScriptComponent::CSharpScriptComponent()
{
    index = ComponentIndex::CSharpScript;
    m_lastWriteTime = (std::numeric_limits<std::filesystem::file_time_type>::min)();
}

namespace
{
    // Remove // line comments and /* */ block comments while preserving the
    // contents of string ('"') and char ('\'') literals, so a "//" inside a
    // string default value is not mistaken for a comment. Replaces comments
    // with a single space to keep tokens that surrounded them separated.
    std::string StripComments(const std::string& src)
    {
        std::string out;
        out.reserve(src.size());
        enum { Code, LineComment, BlockComment, StringLit, CharLit } state = Code;
        for (size_t i = 0; i < src.size(); ++i)
        {
            char c = src[i];
            char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
            switch (state)
            {
            case Code:
                if (c == '/' && n == '/') { state = LineComment; ++i; out += ' '; }
                else if (c == '/' && n == '*') { state = BlockComment; ++i; out += ' '; }
                else if (c == '"') { state = StringLit; out += c; }
                else if (c == '\'') { state = CharLit; out += c; }
                else out += c;
                break;
            case LineComment:
                if (c == '\n') { state = Code; out += c; }
                break;
            case BlockComment:
                if (c == '*' && n == '/') { state = Code; ++i; out += ' '; }
                else if (c == '\n') out += c;
                break;
            case StringLit:
                out += c;
                if (c == '\\' && n != '\0') { out += n; ++i; }   // escape
                else if (c == '"') state = Code;
                break;
            case CharLit:
                out += c;
                if (c == '\\' && n != '\0') { out += n; ++i; }
                else if (c == '\'') state = Code;
                break;
            }
        }
        return out;
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // Map a C# type keyword to our editor field type. Returns false for types
    // we don't expose in the inspector (the declaration is then skipped).
    bool MapFieldType(const std::string& typeName, ScriptFieldType& outType)
    {
        if (typeName == "float" || typeName == "double") { outType = ScriptFieldType::Float; return true; }
        if (typeName == "int" || typeName == "long" ||
            typeName == "short" || typeName == "byte" || typeName == "uint") { outType = ScriptFieldType::Int; return true; }
        if (typeName == "bool") { outType = ScriptFieldType::Bool; return true; }
        if (typeName == "string") { outType = ScriptFieldType::String; return true; }
        if (typeName == "Vector2") { outType = ScriptFieldType::Vector2; return true; }
        if (typeName == "Vector3") { outType = ScriptFieldType::Vector3; return true; }
        if (typeName == "Vector4") { outType = ScriptFieldType::Vector4; return true; }
        return false;
    }

    // Parse up to `count` floats from the arguments of a `new Vector_(...)`
    // initializer. Missing components default to 0. Tolerates trailing 'f'.
    void ParseVectorArgs(const std::string& init, float* out, int count)
    {
        for (int i = 0; i < count; ++i) out[i] = 0.0f;
        size_t open = init.find('(');
        size_t close = init.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) return;
        std::string args = init.substr(open + 1, close - open - 1);
        std::stringstream ss(args);
        std::string tok;
        int i = 0;
        while (std::getline(ss, tok, ',') && i < count)
        {
            tok = Trim(tok);
            if (!tok.empty() && (tok.back() == 'f' || tok.back() == 'F')) tok.pop_back();
            try { out[i] = std::stof(tok); } catch (...) { out[i] = 0.0f; }
            ++i;
        }
    }

    // Assign a parsed default value (from the initializer text, possibly empty)
    // into a ScriptField according to its type. Empty/invalid initializers fall
    // back to a sensible zero/false/empty default.
    void AssignDefault(ScriptField& field, const std::string& initRaw)
    {
        std::string init = Trim(initRaw);
        switch (field.type)
        {
        case ScriptFieldType::Float:
        {
            float v = 0.0f;
            std::string t = init;
            if (!t.empty() && (t.back() == 'f' || t.back() == 'F')) t.pop_back();
            try { if (!t.empty()) v = std::stof(t); } catch (...) {}
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::Int:
        {
            int v = 0;
            try { if (!init.empty()) v = std::stoi(init); } catch (...) {}
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::Bool:
        {
            bool v = (init == "true");
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::String:
        {
            std::string v;
            size_t q1 = init.find('"');
            size_t q2 = init.rfind('"');
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1)
                v = init.substr(q1 + 1, q2 - q1 - 1);
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::Vector2:
        {
            float c[2]; ParseVectorArgs(init, c, 2);
            glm::vec2 v(c[0], c[1]);
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::Vector3:
        {
            float c[3]; ParseVectorArgs(init, c, 3);
            glm::vec3 v(c[0], c[1], c[2]);
            field.defaultValue = field.value = v;
            break;
        }
        case ScriptFieldType::Vector4:
        {
            float c[4]; ParseVectorArgs(init, c, 4);
            glm::vec4 v(c[0], c[1], c[2], c[3]);
            field.defaultValue = field.value = v;
            break;
        }
        }
    }

    bool IsModifier(const std::string& w)
    {
        static const char* mods[] = {
            "public", "private", "protected", "internal", "static", "readonly",
            "const", "volatile", "new", "unsafe", "extern", "abstract",
            "virtual", "override", "sealed", "partial", "event"
        };
        for (const char* m : mods) if (w == m) return true;
        return false;
    }

    bool IsIdentifier(const std::string& s)
    {
        if (s.empty()) return false;
        if (!(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
        for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
        return true;
    }

    CameraComponent* FindMainCameraComponent(Scene* scene)
    {
        if (!scene || !scene->rootGameObject) return nullptr;

        CameraComponent* result = nullptr;
        GameObject* resultObject = nullptr;
        std::function<void(GameObject*)> visit = [&](GameObject* obj)
        {
            if (!obj || result || !obj->enabled) return;
            CameraComponent* camera = obj->GetComponent<CameraComponent>();
            if (camera && camera->enabled && camera->mainCamera)
            {
                result = camera;
                resultObject = obj;
                return;
            }
            for (const auto& child : obj->children)
                visit(child.get());
        };
        visit(scene->rootGameObject.get());
        if (resultObject) scene->mainCamera = resultObject;
        return result;
    }

    glm::vec2 CurrentGameViewportSize()
    {
#ifndef DITTO_HEADLESS_TESTS
        glm::vec2 size = Input::GetGameViewportSize();
        return glm::vec2(glm::max(1.0f, size.x), glm::max(1.0f, size.y));
#else
        return glm::vec2(1.0f, 1.0f);
#endif
    }

    Camera ToRuntimeCamera(CameraComponent* component)
    {
        TransformComponent* transform = component && component->gameObject
            ? component->gameObject->GetComponent<TransformComponent>() : nullptr;
        return component ? component->ToCamera(transform)
            : Camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 ScreenToWorldPointAtDistance(CameraComponent* component,
        const glm::vec2& screenPoint, float distance)
    {
        Camera runtimeCamera = ToRuntimeCamera(component);
        glm::vec2 viewport = CurrentGameViewportSize();
        Camera::Ray ray = runtimeCamera.ScreenPointToRayFull(screenPoint,
            static_cast<int>(viewport.x), static_cast<int>(viewport.y));
        return ray.origin + ray.direction * distance;
    }

    glm::vec3 ScreenToWorldPointOnZ(CameraComponent* component,
        const glm::vec2& screenPoint, float worldZ)
    {
        Camera runtimeCamera = ToRuntimeCamera(component);
        glm::vec2 viewport = CurrentGameViewportSize();
        Camera::Ray ray = runtimeCamera.ScreenPointToRayFull(screenPoint,
            static_cast<int>(viewport.x), static_cast<int>(viewport.y));

        if (std::abs(ray.direction.z) <= 0.0001f)
            return ray.origin;

        float t = (worldZ - ray.origin.z) / ray.direction.z;
        return ray.origin + ray.direction * t;
    }

    // Split a declarator list ("a = 1, b = new Vector3(1,2,3), c") on commas
    // that sit at parenthesis depth 0, so vector initializers stay intact.
    std::vector<std::string> SplitTopLevelCommas(const std::string& s)
    {
        std::vector<std::string> parts;
        int depth = 0;
        std::string cur;
        for (char c : s)
        {
            if (c == '(' || c == '[' || c == '<') ++depth;
            else if (c == ')' || c == ']' || c == '>') { if (depth > 0) --depth; }
            if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!Trim(cur).empty()) parts.push_back(cur);
        return parts;
    }
}

void CSharpScriptComponent::ParseFieldDeclaration(const std::string& statement)
{
    std::string s = Trim(statement);
    if (s.empty()) return;

    // Strip and inspect leading attributes: [SerializeField], [HideInInspector].
    bool hasSerializeField = false, hideInInspector = false;
    while (!s.empty() && s[0] == '[')
    {
        size_t close = s.find(']');
        if (close == std::string::npos) return;   // malformed
        std::string attr = s.substr(1, close - 1);
        if (attr.find("SerializeField") != std::string::npos) hasSerializeField = true;
        if (attr.find("HideInInspector") != std::string::npos) hideInInspector = true;
        s = Trim(s.substr(close + 1));
    }
    if (hideInInspector) return;

    // A method/indexer/expression-bodied member has '(' or "=>" in its head
    // (before any '='). A field's only '(' is inside a `new Vector_(...)`
    // initializer, which is after the '='.
    size_t posEq = s.find('=');
    size_t posArrow = s.find("=>");
    size_t posParen = s.find('(');
    if (posArrow != std::string::npos) return;
    if (posParen != std::string::npos && (posEq == std::string::npos || posParen < posEq)) return;

    // Consume leading modifier keywords; remember access/storage class.
    std::stringstream head(posEq == std::string::npos ? s : s.substr(0, posEq));
    std::string word, typeName;
    bool isPublic = false, isStatic = false, isConst = false;
    std::vector<std::string> headWords;
    while (head >> word) headWords.push_back(word);
    if (headWords.empty()) return;

    size_t wi = 0;
    for (; wi < headWords.size(); ++wi)
    {
        const std::string& w = headWords[wi];
        if (IsModifier(w))
        {
            if (w == "public") isPublic = true;
            else if (w == "static") isStatic = true;
            else if (w == "const") isConst = true;
            continue;
        }
        break;   // first non-modifier word is the type
    }
    if (wi >= headWords.size()) return;
    typeName = headWords[wi++];

    // Only public fields (or [SerializeField] ones) are editable; never expose
    // static/const storage.
    if (isStatic || isConst) return;
    if (!isPublic && !hasSerializeField) return;

    ScriptFieldType fieldType;
    if (!MapFieldType(typeName, fieldType)) return;

    // The first declarator name is the remaining head word (if any); the rest
    // of the statement (after the type) forms the full declarator list.
    // Rebuild the declarator portion: everything in `s` after the type token.
    size_t typePos = s.find(typeName);
    std::string declPart = (typePos == std::string::npos) ? s : s.substr(typePos + typeName.size());

    for (std::string& decl : SplitTopLevelCommas(declPart))
    {
        std::string d = Trim(decl);
        if (d.empty()) continue;

        std::string name, init;
        size_t eq = d.find('=');
        if (eq == std::string::npos) name = Trim(d);
        else { name = Trim(d.substr(0, eq)); init = Trim(d.substr(eq + 1)); }

        if (!IsIdentifier(name)) continue;

        ScriptField field(name, fieldType);
        AssignDefault(field, init);
        fields.push_back(field);
    }
}

void CSharpScriptComponent::ParseScriptFields()
{
    fields.clear();
    if (scriptPath.empty()) return;

    std::ifstream file(scriptPath, std::ios::binary);
    if (!file.is_open()) return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = StripComments(buffer.str());

    // Walk the source one statement at a time. A statement ends at ';'.
    // Braces clear the pending head: class/namespace braces should not hide
    // fields inside the type, while property/method blocks should not be parsed
    // as fields. Method-body statements are later ignored by ParseFieldDeclaration
    // because they contain '(' before any '='.
    std::string stmt;
    for (size_t i = 0; i < source.size(); ++i)
    {
        char c = source[i];
        if (c == '{')
        {
            stmt.clear();
        }
        else if (c == '}')
        {
            stmt.clear();
        }
        else if (c == ';')
        {
            ParseFieldDeclaration(stmt);
            stmt.clear();
        }
        else
        {
            stmt += c;
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

void CSharpScriptComponent::FixedUpdate()
{
    if (enabled && gameObject && scriptInstance)
    {
        MonoRuntime::CallFixedUpdate(scriptInstance);
    }
}

void CSharpScriptComponent::OnDestroy()
{
    if (scriptInstance) MonoRuntime::CallOnDestroy(scriptInstance);
    started = false;
}

void CSharpScriptComponent::Serialize(std::ostream& file) const
{
    Ditto::AssetReferenceIO::WriteString(file, scriptName);
    Ditto::AssetReferenceIO::WriteAssetReference(file, scriptPath);

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

void CSharpScriptComponent::Deserialize(std::istream& file)
{
    scriptName = Ditto::AssetReferenceIO::ReadString(file);
    std::uint32_t assetReferenceVersion = Ditto::RuntimeContext::SceneLoadingVersion();
    if (assetReferenceVersion == 0)
        assetReferenceVersion = 16;
    scriptPath = Ditto::AssetReferenceIO::ReadAssetReference(file, assetReferenceVersion);

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
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted(scriptName.empty() ? "C# Script" : scriptName.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X"))
    {
        gameObject->RemoveComponent(this);
        Editor* editor = CSharpScriptSystem::GetEditor();
        if (editor) editor->MarkSceneDirty();
        return;
    }

    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    ImGui::Text("Script: "); ImGui::SameLine();
    ImGui::TextDisabled("%s", scriptPath.c_str());

    if (!lastCompileResult.scriptPath.empty())
    {
        ImVec4 color = lastCompileResult.ok
            ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
            : ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        ImGui::TextColored(color, "Compile: %s (%d errors, %d warnings)",
            lastCompileResult.ok ? "OK" : "Failed",
            lastCompileResult.errorCount,
            lastCompileResult.warningCount);
        for (size_t i = 0; i < lastCompileResult.diagnostics.size() && i < 5; ++i)
        {
            const auto& diagnostic = lastCompileResult.diagnostics[i];
            std::string label = diagnostic.file + "(" + std::to_string(diagnostic.line)
                + "," + std::to_string(diagnostic.column) + "): "
                + diagnostic.severity + " " + diagnostic.code + ": " + diagnostic.message;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
            {
                Editor* editor = CSharpScriptSystem::GetEditor();
                std::string fileToOpen = diagnostic.file.empty() ? scriptPath : diagnostic.file;
                if (editor && editor->GetProjectWindow())
                    editor->GetProjectWindow()->OpenCSharpFile(fileToOpen, diagnostic.line, diagnostic.column);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open source file");
            ImGui::PopID();
        }
        if (!lastCompileResult.output.empty() && ImGui::CollapsingHeader("Compiler Output"))
        {
            std::string output = lastCompileResult.output.substr(0, 4096);
            if (lastCompileResult.output.size() > output.size())
                output += "\n...";
            ImGui::TextWrapped("%s", output.c_str());
        }
    }

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
            Editor* editor = CSharpScriptSystem::GetEditor();
            if (editor) editor->MarkSceneDirty();
        }
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
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
    // All script-side logs flow into the shared Logger, which mirrors them to
    // stdout and feeds the editor's Console tab. Keep the optional external
    // callback hook for callers that redirect logs elsewhere.
    if (s_logCallback) s_logCallback(message);
    Ditto::Logger::Get().Info(message);
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

    component->lastCompileResult = CompileScriptDetailed(csPath, dllPathStr);
    if (!component->lastCompileResult.ok)
    {
        Ditto::Logger::Get().Error("[CSharpScript] Compile failed for " + csPath
            + " (" + std::to_string(component->lastCompileResult.errorCount) + " errors, "
            + std::to_string(component->lastCompileResult.warningCount) + " warnings)");
        if (!component->lastCompileResult.output.empty())
            Ditto::Logger::Get().Error(component->lastCompileResult.output);
        return false;
    }

    if (component->lastCompileResult.warningCount > 0)
    {
        Ditto::Logger::Get().Warning("[CSharpScript] Compile warnings for " + csPath
            + " (" + std::to_string(component->lastCompileResult.warningCount) + " warnings)");
        if (!component->lastCompileResult.output.empty())
            Ditto::Logger::Get().Warning(component->lastCompileResult.output);
    }

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

    DITTO_LOG_INFO_STREAM("[CSharpScript] HotReload: " << scriptPath);

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
        DITTO_LOG_ERROR("[CSharpScript] HotReload compile failed");
        return;
    }

    DITTO_LOG_INFO_STREAM("[CSharpScript] HotReload compiled to: " << newDllPath);

    if (CSharpScriptSystem::IsInitialized() && MonoRuntime::IsInitialized())
    {
        scriptInstance = MonoRuntime::LoadScript(newDllPath, scriptName);
        if (!scriptInstance)
        {
            DITTO_LOG_ERROR("[CSharpScript] HotReload LoadScript failed");
            return;
        }
        DITTO_LOG_INFO("[CSharpScript] HotReload loaded, calling SetNativeGameObject");

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

CSharpCompileResult CSharpScriptSystem::CompileScriptDetailed(const std::string& csPath, std::string& outDllPath)
{
    return Ditto::CSharpScriptCompiler::CompileDetailed(csPath, outDllPath);
}

bool CSharpScriptSystem::CompileScript(const std::string& csPath, std::string& outDllPath)
{
    CSharpCompileResult result = CompileScriptDetailed(csPath, outDllPath);
    if (!result.ok)
    {
        Ditto::Logger::Get().Error("[CSharpScript] Compile failed for " + csPath
            + " (" + std::to_string(result.errorCount) + " errors, "
            + std::to_string(result.warningCount) + " warnings)");
        if (!result.output.empty())
            Ditto::Logger::Get().Error(result.output);
    }
    else if (result.warningCount > 0)
    {
        Ditto::Logger::Get().Warning("[CSharpScript] Compile warnings for " + csPath
            + " (" + std::to_string(result.warningCount) + " warnings)");
        if (!result.output.empty())
            Ditto::Logger::Get().Warning(result.output);
    }
    return result.ok;
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

    Editor* editor = GetEditor();
    if (!editor) return;
}

void CSharpScriptSystem::CallUpdate()
{
    if (!s_initialized) return;

    Editor* editor = GetEditor();
    if (!editor) return;
}

void CSharpScriptSystem::RegisterInternalCalls()
{
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::GetPosition", (void*)Internal_Transform_GetPosition);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::SetPosition", (void*)Internal_Transform_SetPosition);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::GetRotation", (void*)Internal_Transform_GetRotation);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::SetRotation", (void*)Internal_Transform_SetRotation);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::GetScale", (void*)Internal_Transform_GetScale);
    ::MonoRuntime::AddInternalCall("DittoEngine.Transform::SetScale", (void*)Internal_Transform_SetScale);

    ::MonoRuntime::AddInternalCall("DittoEngine.MonoBehaviour::GameObject_GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetTransform", (void*)Internal_GameObject_GetTransform);
    ::MonoRuntime::AddInternalCall("DittoEngine.GameObject::GetComponentByType", (void*)Internal_GameObject_GetComponentByType);

    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetColor", (void*)Internal_Renderer_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetColor", (void*)Internal_Renderer_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::GetShapeType", (void*)Internal_Renderer_GetShapeType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Renderer::SetShapeType", (void*)Internal_Renderer_SetShapeType);

    ::MonoRuntime::AddInternalCall("DittoEngine.SpriteRenderer::GetColor", (void*)Internal_SpriteRenderer_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.SpriteRenderer::SetColor", (void*)Internal_SpriteRenderer_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.SpriteRenderer::GetSprite", (void*)Internal_SpriteRenderer_GetSprite);
    ::MonoRuntime::AddInternalCall("DittoEngine.SpriteRenderer::SetSprite", (void*)Internal_SpriteRenderer_SetSprite);

    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetMainCameraNative", (void*)Internal_Camera_GetMainCamera);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetProjectionTypeNative", (void*)Internal_Camera_GetProjectionType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::SetProjectionTypeNative", (void*)Internal_Camera_SetProjectionType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetFieldOfViewNative", (void*)Internal_Camera_GetFieldOfView);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::SetFieldOfViewNative", (void*)Internal_Camera_SetFieldOfView);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetOrthographicSizeNative", (void*)Internal_Camera_GetOrthographicSize);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::SetOrthographicSizeNative", (void*)Internal_Camera_SetOrthographicSize);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetNearClipPlaneNative", (void*)Internal_Camera_GetNearClipPlane);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::SetNearClipPlaneNative", (void*)Internal_Camera_SetNearClipPlane);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::GetFarClipPlaneNative", (void*)Internal_Camera_GetFarClipPlane);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::SetFarClipPlaneNative", (void*)Internal_Camera_SetFarClipPlane);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::ScreenPointToRayNative", (void*)Internal_Camera_ScreenPointToRay);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::ScreenToWorldPointNative", (void*)Internal_Camera_ScreenToWorldPoint);
    ::MonoRuntime::AddInternalCall("DittoEngine.Camera::ScreenToWorldPointOnPlaneNative", (void*)Internal_Camera_ScreenToWorldPointOnPlane);

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

    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetBodyType", (void*)Internal_Rigidbody2D_GetBodyType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetBodyType", (void*)Internal_Rigidbody2D_SetBodyType);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetMass", (void*)Internal_Rigidbody2D_GetMass);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetMass", (void*)Internal_Rigidbody2D_SetMass);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetUseGravity", (void*)Internal_Rigidbody2D_GetUseGravity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetUseGravity", (void*)Internal_Rigidbody2D_SetUseGravity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetGravityScale", (void*)Internal_Rigidbody2D_GetGravityScale);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetGravityScale", (void*)Internal_Rigidbody2D_SetGravityScale);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetVelocity", (void*)Internal_Rigidbody2D_GetVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetVelocity", (void*)Internal_Rigidbody2D_SetVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::GetAngularVelocity", (void*)Internal_Rigidbody2D_GetAngularVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::SetAngularVelocity", (void*)Internal_Rigidbody2D_SetAngularVelocity);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::AddForceNative", (void*)Internal_Rigidbody2D_AddForce);
    ::MonoRuntime::AddInternalCall("DittoEngine.Rigidbody2D::AddTorqueNative", (void*)Internal_Rigidbody2D_AddTorque);

    ::MonoRuntime::AddInternalCall("DittoEngine.Time::GetDeltaTime", (void*)Internal_Time_GetDeltaTime);
    ::MonoRuntime::AddInternalCall("DittoEngine.Time::GetTime", (void*)Internal_Time_GetTime);

    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetKeyNative", (void*)Internal_Input_GetKey);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetKeyDownNative", (void*)Internal_Input_GetKeyDown);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetKeyUpNative", (void*)Internal_Input_GetKeyUp);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetMouseButtonNative", (void*)Internal_Input_GetMouseButton);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetMouseButtonDownNative", (void*)Internal_Input_GetMouseButtonDown);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetMouseButtonUpNative", (void*)Internal_Input_GetMouseButtonUp);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetMousePositionNative", (void*)Internal_Input_GetMousePosition);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetGameViewportSizeNative", (void*)Internal_Input_GetGameViewportSize);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetAxisNative", (void*)Internal_Input_GetAxis);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetAxisRawNative", (void*)Internal_Input_GetAxisRaw);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetButtonNative", (void*)Internal_Input_GetButton);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetButtonDownNative", (void*)Internal_Input_GetButtonDown);
    ::MonoRuntime::AddInternalCall("DittoEngine.Input::GetButtonUpNative", (void*)Internal_Input_GetButtonUp);

    ::MonoRuntime::AddInternalCall("DittoEngine.Physics::RaycastNative", (void*)Internal_Physics_Raycast);

    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::PlayNative", (void*)Internal_AudioSource_Play);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::StopNative", (void*)Internal_AudioSource_Stop);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::GetVolume", (void*)Internal_AudioSource_GetVolume);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::SetVolume", (void*)Internal_AudioSource_SetVolume);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::GetLoop", (void*)Internal_AudioSource_GetLoop);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::SetLoop", (void*)Internal_AudioSource_SetLoop);
    ::MonoRuntime::AddInternalCall("DittoEngine.AudioSource::IsPlayingNative", (void*)Internal_AudioSource_IsPlaying);

    ::MonoRuntime::AddInternalCall("DittoEngine.UIText::SetTextNative", (void*)Internal_UIText_SetText);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIText::GetTextNative", (void*)Internal_UIText_GetText);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIText::SetColorNative", (void*)Internal_UIText_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIImage::SetColorNative", (void*)Internal_UIImage_SetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIImage::GetColorNative", (void*)Internal_UIImage_GetColor);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIButton::ConsumeClick", (void*)Internal_UIButton_ConsumeClick);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIButton::IsHoveredNative", (void*)Internal_UIButton_IsHovered);
    ::MonoRuntime::AddInternalCall("DittoEngine.UIButton::SetLabelNative", (void*)Internal_UIButton_SetLabel);

    ::MonoRuntime::AddInternalCall("DittoEngine.Object::InstantiateNative", (void*)Internal_Object_Instantiate);
    ::MonoRuntime::AddInternalCall("DittoEngine.Object::DestroyNative", (void*)Internal_Object_Destroy);

    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::PlayNative", (void*)Internal_Animator_Play);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::StopNative", (void*)Internal_Animator_Stop);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::PauseNative", (void*)Internal_Animator_Pause);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::ResumeNative", (void*)Internal_Animator_Resume);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::GetSpeedNative", (void*)Internal_Animator_GetSpeed);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::SetSpeedNative", (void*)Internal_Animator_SetSpeed);
    ::MonoRuntime::AddInternalCall("DittoEngine.Animator::IsPlayingNative", (void*)Internal_Animator_IsPlaying);

    ::MonoRuntime::AddInternalCall("DittoEngine.ParticleSystem::PlayNative", (void*)Internal_ParticleSystem_Play);
    ::MonoRuntime::AddInternalCall("DittoEngine.ParticleSystem::StopNative", (void*)Internal_ParticleSystem_Stop);
    ::MonoRuntime::AddInternalCall("DittoEngine.ParticleSystem::ClearNative", (void*)Internal_ParticleSystem_Clear);
    ::MonoRuntime::AddInternalCall("DittoEngine.ParticleSystem::IsPlayingNative", (void*)Internal_ParticleSystem_IsPlaying);

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

void Internal_Transform_GetRotation(void* transform, float* outRot)
{
    if (!transform || !outRot) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    outRot[0] = trans->rotation.x;
    outRot[1] = trans->rotation.y;
    outRot[2] = trans->rotation.z;
}

void Internal_Transform_SetRotation(void* transform, float x, float y, float z)
{
    if (!transform) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    trans->rotation.x = x;
    trans->rotation.y = y;
    trans->rotation.z = z;
    trans->useQuatRotation = false;
    trans->localDirty = true;
    trans->UpdateTransform();
}

void Internal_Transform_GetScale(void* transform, float* outScale)
{
    if (!transform || !outScale) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    outScale[0] = trans->scale.x;
    outScale[1] = trans->scale.y;
    outScale[2] = trans->scale.z;
}

void Internal_Transform_SetScale(void* transform, float x, float y, float z)
{
    if (!transform) return;

    TransformComponent* trans = static_cast<TransformComponent*>(transform);
    trans->scale.x = x;
    trans->scale.y = y;
    trans->scale.z = z;
    trans->localDirty = true;
    trans->UpdateTransform();
}

void* Internal_GameObject_GetTransform(void* gameObject)
{
    if (!gameObject) return nullptr;

    GameObject* go = static_cast<GameObject*>(gameObject);

    for (const auto& comp : go->components)
    {
        if (!comp) continue;

        if (comp->index == (1 << 0))
        {
            return comp.get();
        }
    }

    return nullptr;
}

float Internal_Time_GetDeltaTime()
{
    return CSharpScriptSystem::GetDeltaTime();
}

float Internal_Time_GetTime()
{
    return CSharpScriptSystem::GetTime();
}

#ifndef DITTO_HEADLESS_TESTS
int Internal_Input_GetKey(int key)            { return Input::GetKey(key) ? 1 : 0; }
int Internal_Input_GetKeyDown(int key)        { return Input::GetKeyDown(key) ? 1 : 0; }
int Internal_Input_GetKeyUp(int key)          { return Input::GetKeyUp(key) ? 1 : 0; }
int Internal_Input_GetMouseButton(int b)      { return Input::GetMouseButton(b) ? 1 : 0; }
int Internal_Input_GetMouseButtonDown(int b)  { return Input::GetMouseButtonDown(b) ? 1 : 0; }
int Internal_Input_GetMouseButtonUp(int b)    { return Input::GetMouseButtonUp(b) ? 1 : 0; }

void Internal_Input_GetMousePosition(float* outPos)
{
    if (!outPos) return;
    glm::vec2 p = Input::GetMousePosition();
    outPos[0] = p.x;
    outPos[1] = p.y;
}

void Internal_Input_GetGameViewportSize(float* outSize)
{
    if (!outSize) return;
    glm::vec2 size = Input::GetGameViewportSize();
    outSize[0] = size.x;
    outSize[1] = size.y;
}

float Internal_Input_GetAxis(void* axisName)
{
    if (!axisName) return 0.0f;
    std::string n = MonoRuntime::GetStringFromMono((MonoString*)axisName);
    return Input::GetAxis(n.c_str());
}
float Internal_Input_GetAxisRaw(void* axisName)
{
    if (!axisName) return 0.0f;
    std::string n = MonoRuntime::GetStringFromMono((MonoString*)axisName);
    return Input::GetAxisRaw(n.c_str());
}
int Internal_Input_GetButton(void* buttonName)
{
    if (!buttonName) return 0;
    std::string n = MonoRuntime::GetStringFromMono((MonoString*)buttonName);
    return Input::GetButton(n.c_str()) ? 1 : 0;
}
int Internal_Input_GetButtonDown(void* buttonName)
{
    if (!buttonName) return 0;
    std::string n = MonoRuntime::GetStringFromMono((MonoString*)buttonName);
    return Input::GetButtonDown(n.c_str()) ? 1 : 0;
}
int Internal_Input_GetButtonUp(void* buttonName)
{
    if (!buttonName) return 0;
    std::string n = MonoRuntime::GetStringFromMono((MonoString*)buttonName);
    return Input::GetButtonUp(n.c_str()) ? 1 : 0;
}
#else
// Headless test builds (DittoTests) compile this file without window/Input.
int Internal_Input_GetKey(int)            { return 0; }
int Internal_Input_GetKeyDown(int)        { return 0; }
int Internal_Input_GetKeyUp(int)          { return 0; }
int Internal_Input_GetMouseButton(int)    { return 0; }
int Internal_Input_GetMouseButtonDown(int){ return 0; }
int Internal_Input_GetMouseButtonUp(int)  { return 0; }
void Internal_Input_GetMousePosition(float* outPos) { if (outPos) { outPos[0] = 0; outPos[1] = 0; } }
void Internal_Input_GetGameViewportSize(float* outSize) { if (outSize) { outSize[0] = 1; outSize[1] = 1; } }
float Internal_Input_GetAxis(void*)       { return 0.0f; }
float Internal_Input_GetAxisRaw(void*)    { return 0.0f; }
int Internal_Input_GetButton(void*)       { return 0; }
int Internal_Input_GetButtonDown(void*)   { return 0; }
int Internal_Input_GetButtonUp(void*)     { return 0; }
#endif

int Internal_Physics_Raycast(float ox, float oy, float oz,
    float dx, float dy, float dz, float maxDist, float* out7, void** outGo)
{
    Physics* physics = CSharpScriptSystem::GetPhysics();
    if (!physics || !out7 || !outGo) return 0;

    RaycastHit hit;
    if (!physics->Raycast(glm::vec3(ox, oy, oz), glm::vec3(dx, dy, dz), maxDist, hit))
        return 0;

    out7[0] = hit.point.x;  out7[1] = hit.point.y;  out7[2] = hit.point.z;
    out7[3] = hit.normal.x; out7[4] = hit.normal.y; out7[5] = hit.normal.z;
    out7[6] = hit.distance;
    *outGo = hit.gameObject;
    return 1;
}

void Internal_Debug_Log(void* msg)
{
    std::string message = MonoRuntime::GetStringFromMono((MonoString*)msg);
    CSharpScriptSystem::LogToConsole("[C#] " + message);
}

void* Internal_GameObject_GetComponentByType(void* gameObject, void* typeName)
{
    if (!gameObject || !typeName) return nullptr;

    std::string typeStr = MonoRuntime::GetStringFromMono((MonoString*)typeName);
    GameObject* go = static_cast<GameObject*>(gameObject);

    if (typeStr == "Transform")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == (1 << 0)) return comp.get();
    }
    else if (typeStr == "Light")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == (1 << 1)) return comp.get();
    }
    else if (typeStr == "Renderer")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::Renderer) return comp.get();
    }
    else if (typeStr == "SpriteRenderer")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::SpriteRenderer) return comp.get();
    }
    else if (typeStr == "Camera")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::Camera) return comp.get();
    }
    else if (typeStr == "Rigidbody")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::Rigidbody) return comp.get();
    }
    else if (typeStr == "Rigidbody2D")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::Rigidbody2D) return comp.get();
    }
    else if (typeStr == "Collider2D")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::Collider2D) return comp.get();
    }
    else if (typeStr == "AudioSource")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::AudioSource) return comp.get();
    }
    else if (typeStr == "UIText")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::UIText) return comp.get();
    }
    else if (typeStr == "UIImage")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::UIImage) return comp.get();
    }
    else if (typeStr == "UIButton")
    {
        for (const auto& comp : go->components)
            if (comp && comp->index == ComponentIndex::UIButton) return comp.get();
    }

    return nullptr;
}

void Internal_UIText_SetText(void* uiText, void* text)
{
    if (!uiText || !text) return;
    static_cast<UITextComponent*>(uiText)->text =
        MonoRuntime::GetStringFromMono((MonoString*)text);
}

void* Internal_UIText_GetText(void* uiText)
{
    if (!uiText) return nullptr;
    return MonoRuntime::CreateString(static_cast<UITextComponent*>(uiText)->text);
}

void Internal_UIText_SetColor(void* uiText, float r, float g, float b, float a)
{
    if (!uiText) return;
    static_cast<UITextComponent*>(uiText)->color = glm::vec4(r, g, b, a);
}

void Internal_UIImage_SetColor(void* uiImage, float r, float g, float b, float a)
{
    if (!uiImage) return;
    static_cast<UIImageComponent*>(uiImage)->color = glm::vec4(r, g, b, a);
}

void Internal_UIImage_GetColor(void* uiImage, float* outColor)
{
    if (!uiImage || !outColor) return;
    const glm::vec4& c = static_cast<UIImageComponent*>(uiImage)->color;
    outColor[0] = c.r; outColor[1] = c.g; outColor[2] = c.b; outColor[3] = c.a;
}

int Internal_UIButton_ConsumeClick(void* uiButton)
{
    if (!uiButton) return 0;
    UIButtonComponent* btn = static_cast<UIButtonComponent*>(uiButton);
    int clicked = btn->wasClicked ? 1 : 0;
    btn->wasClicked = false;   // read-clears
    return clicked;
}

int Internal_UIButton_IsHovered(void* uiButton)
{
    if (!uiButton) return 0;
    return static_cast<UIButtonComponent*>(uiButton)->hovered ? 1 : 0;
}

void Internal_UIButton_SetLabel(void* uiButton, void* label)
{
    if (!uiButton || !label) return;
    static_cast<UIButtonComponent*>(uiButton)->label =
        MonoRuntime::GetStringFromMono((MonoString*)label);
}

void* Internal_Object_Instantiate(void* gameObject)
{
    Scene* scene = Ditto::RuntimeContext::CurrentScene();
    if (!gameObject || !scene || !scene->rootGameObject) return nullptr;
    GameObject* source = static_cast<GameObject*>(gameObject);
    auto clone = std::make_unique<GameObject>(source);
    clone->name = source->name + " (Clone)";
    GameObject* result = clone.get();
    scene->rootGameObject->AddChild(std::move(clone));
    scene->gameObjects.push_back(result);
    scene->MarkDirty();
    return result;
}

void Internal_Object_Destroy(void* gameObject)
{
    Scene* scene = Ditto::RuntimeContext::CurrentScene();
    if (!gameObject || !scene || !scene->rootGameObject) return;
    GameObject* obj = static_cast<GameObject*>(gameObject);
    if (obj == scene->rootGameObject.get()) return;
    GameObject* parent = obj->parent;
    if (!parent) return;
    scene->UnregisterSubtree(obj);
    parent->DetachChild(obj);
    scene->MarkDirty();
}

void Internal_AudioSource_Play(void* audioSource)
{
    if (!audioSource) return;
    static_cast<AudioSourceComponent*>(audioSource)->Play();
}

void Internal_AudioSource_Stop(void* audioSource)
{
    if (!audioSource) return;
    static_cast<AudioSourceComponent*>(audioSource)->Stop();
}

float Internal_AudioSource_GetVolume(void* audioSource)
{
    if (!audioSource) return 1.0f;
    return static_cast<AudioSourceComponent*>(audioSource)->volume;
}

void Internal_AudioSource_SetVolume(void* audioSource, float volume)
{
    if (!audioSource) return;
    AudioSourceComponent* audio = static_cast<AudioSourceComponent*>(audioSource);
    audio->volume = volume;
#ifndef DITTO_HEADLESS_TESTS
    if (audio->soundHandle != 0) AudioEngine::SetVolume(audio->soundHandle, volume);
#endif
}

int Internal_AudioSource_GetLoop(void* audioSource)
{
    if (!audioSource) return 0;
    return static_cast<AudioSourceComponent*>(audioSource)->loop ? 1 : 0;
}

void Internal_AudioSource_SetLoop(void* audioSource, int loop)
{
    if (!audioSource) return;
    static_cast<AudioSourceComponent*>(audioSource)->loop = loop != 0;
}

int Internal_AudioSource_IsPlaying(void* audioSource)
{
    if (!audioSource) return 0;
#ifndef DITTO_HEADLESS_TESTS
    AudioSourceComponent* audio = static_cast<AudioSourceComponent*>(audioSource);
    return (audio->soundHandle != 0 && AudioEngine::IsPlaying(audio->soundHandle)) ? 1 : 0;
#else
    return 0;
#endif
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
    (void)renderer;
    // Shape type is no longer a meaningful int — file mesh only.
    return 0;
}

void Internal_Renderer_SetShapeType(void* renderer, int type)
{
    // Shape type is deprecated: the renderer is now exclusively file-mesh driven.
    // Kept as a no-op so existing C# scripts that call it still compile.
    (void)renderer;
    (void)type;
}

void Internal_SpriteRenderer_GetColor(void* spriteRenderer, float* outColor)
{
    if (!spriteRenderer || !outColor) return;

    SpriteRendererComponent* sr = static_cast<SpriteRendererComponent*>(spriteRenderer);
    outColor[0] = sr->color.r;
    outColor[1] = sr->color.g;
    outColor[2] = sr->color.b;
    outColor[3] = sr->color.a;
}

void Internal_SpriteRenderer_SetColor(void* spriteRenderer, float r, float g, float b, float a)
{
    if (!spriteRenderer) return;

    SpriteRendererComponent* sr = static_cast<SpriteRendererComponent*>(spriteRenderer);
    sr->color = glm::vec4(r, g, b, a);
}

void* Internal_SpriteRenderer_GetSprite(void* spriteRenderer)
{
    if (!spriteRenderer) return nullptr;
    return MonoRuntime::CreateString(static_cast<SpriteRendererComponent*>(spriteRenderer)->spritePath);
}

void Internal_SpriteRenderer_SetSprite(void* spriteRenderer, void* spritePath)
{
    if (!spriteRenderer || !spritePath) return;
    static_cast<SpriteRendererComponent*>(spriteRenderer)->spritePath =
        MonoRuntime::GetStringFromMono((MonoString*)spritePath);
}

void* Internal_Camera_GetMainCamera()
{
    return FindMainCameraComponent(Ditto::RuntimeContext::CurrentScene());
}

int Internal_Camera_GetProjectionType(void* camera)
{
    if (!camera) return 0;
    return static_cast<int>(static_cast<CameraComponent*>(camera)->projectionType);
}

void Internal_Camera_SetProjectionType(void* camera, int projectionType)
{
    if (!camera) return;
    CameraComponent* c = static_cast<CameraComponent*>(camera);
    c->projectionType = projectionType == 1
        ? Camera::ProjectionType::Orthographic
        : Camera::ProjectionType::Perspective;
}

float Internal_Camera_GetFieldOfView(void* camera)
{
    if (!camera) return 45.0f;
    return static_cast<CameraComponent*>(camera)->fieldOfView;
}

void Internal_Camera_SetFieldOfView(void* camera, float fieldOfView)
{
    if (!camera) return;
    static_cast<CameraComponent*>(camera)->fieldOfView =
        std::clamp(fieldOfView, 1.0f, 179.0f);
}

float Internal_Camera_GetOrthographicSize(void* camera)
{
    if (!camera) return 5.0f;
    return static_cast<CameraComponent*>(camera)->orthographicSize;
}

void Internal_Camera_SetOrthographicSize(void* camera, float orthographicSize)
{
    if (!camera) return;
    static_cast<CameraComponent*>(camera)->orthographicSize =
        glm::max(0.0001f, orthographicSize);
}

float Internal_Camera_GetNearClipPlane(void* camera)
{
    if (!camera) return 0.1f;
    return static_cast<CameraComponent*>(camera)->nearClipPlane;
}

void Internal_Camera_SetNearClipPlane(void* camera, float nearClipPlane)
{
    if (!camera) return;
    CameraComponent* c = static_cast<CameraComponent*>(camera);
    c->nearClipPlane = glm::max(0.0001f, nearClipPlane);
    c->farClipPlane = glm::max(c->nearClipPlane + 0.0001f, c->farClipPlane);
}

float Internal_Camera_GetFarClipPlane(void* camera)
{
    if (!camera) return 100.0f;
    return static_cast<CameraComponent*>(camera)->farClipPlane;
}

void Internal_Camera_SetFarClipPlane(void* camera, float farClipPlane)
{
    if (!camera) return;
    CameraComponent* c = static_cast<CameraComponent*>(camera);
    c->farClipPlane = glm::max(c->nearClipPlane + 0.0001f, farClipPlane);
}

void Internal_Camera_ScreenPointToRay(void* camera, float x, float y, float* outRay)
{
    if (!outRay) return;
    CameraComponent* c = static_cast<CameraComponent*>(camera);
    Camera runtimeCamera = ToRuntimeCamera(c);
    glm::vec2 viewport = CurrentGameViewportSize();
    Camera::Ray ray = runtimeCamera.ScreenPointToRayFull(glm::vec2(x, y),
        static_cast<int>(viewport.x), static_cast<int>(viewport.y));

    outRay[0] = ray.origin.x;    outRay[1] = ray.origin.y;    outRay[2] = ray.origin.z;
    outRay[3] = ray.direction.x; outRay[4] = ray.direction.y; outRay[5] = ray.direction.z;
}

void Internal_Camera_ScreenToWorldPoint(void* camera, float x, float y, float distance, float* outPoint)
{
    if (!outPoint) return;
    glm::vec3 point = ScreenToWorldPointAtDistance(static_cast<CameraComponent*>(camera),
        glm::vec2(x, y), distance);
    outPoint[0] = point.x;
    outPoint[1] = point.y;
    outPoint[2] = point.z;
}

void Internal_Camera_ScreenToWorldPointOnPlane(void* camera, float x, float y, float worldZ, float* outPoint)
{
    if (!outPoint) return;
    glm::vec3 point = ScreenToWorldPointOnZ(static_cast<CameraComponent*>(camera),
        glm::vec2(x, y), worldZ);
    outPoint[0] = point.x;
    outPoint[1] = point.y;
    outPoint[2] = point.z;
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

int Internal_Rigidbody2D_GetBodyType(void* rigidbody)
{
    if (!rigidbody) return 0;
    return static_cast<int>(static_cast<Rigidbody2DComponent*>(rigidbody)->type);
}

void Internal_Rigidbody2D_SetBodyType(void* rigidbody, int type)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->type = static_cast<Rigidbody2DComponent::Type>(type);
}

float Internal_Rigidbody2D_GetMass(void* rigidbody)
{
    if (!rigidbody) return 1.0f;
    return static_cast<Rigidbody2DComponent*>(rigidbody)->mass;
}

void Internal_Rigidbody2D_SetMass(void* rigidbody, float mass)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->mass = mass;
}

int Internal_Rigidbody2D_GetUseGravity(void* rigidbody)
{
    if (!rigidbody) return 0;
    return static_cast<Rigidbody2DComponent*>(rigidbody)->useGravity ? 1 : 0;
}

void Internal_Rigidbody2D_SetUseGravity(void* rigidbody, int useGravity)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->useGravity = useGravity != 0;
}

float Internal_Rigidbody2D_GetGravityScale(void* rigidbody)
{
    if (!rigidbody) return 1.0f;
    return static_cast<Rigidbody2DComponent*>(rigidbody)->gravityScale;
}

void Internal_Rigidbody2D_SetGravityScale(void* rigidbody, float gravityScale)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->gravityScale = gravityScale;
}

void Internal_Rigidbody2D_GetVelocity(void* rigidbody, float* outVel)
{
    if (!rigidbody || !outVel) return;
    glm::vec2 v = static_cast<Rigidbody2DComponent*>(rigidbody)->velocity;
    outVel[0] = v.x;
    outVel[1] = v.y;
}

void Internal_Rigidbody2D_SetVelocity(void* rigidbody, float x, float y)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->velocity = glm::vec2(x, y);
}

float Internal_Rigidbody2D_GetAngularVelocity(void* rigidbody)
{
    if (!rigidbody) return 0.0f;
    return static_cast<Rigidbody2DComponent*>(rigidbody)->angularVelocity;
}

void Internal_Rigidbody2D_SetAngularVelocity(void* rigidbody, float v)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->angularVelocity = v;
}

void Internal_Rigidbody2D_AddForce(void* rigidbody, float x, float y, int mode)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->AddForce(
        glm::vec2(x, y), static_cast<Rigidbody2DComponent::ForceMode2D>(mode));
}

void Internal_Rigidbody2D_AddTorque(void* rigidbody, float torque, int mode)
{
    if (!rigidbody) return;
    static_cast<Rigidbody2DComponent*>(rigidbody)->AddTorque(
        torque, static_cast<Rigidbody2DComponent::ForceMode2D>(mode));
}

void Internal_Animator_Play(void* animator, void* clipName)
{
    if (!animator) return;
    AnimatorComponent* anim = static_cast<AnimatorComponent*>(animator);
    std::string clip = clipName ? MonoRuntime::GetStringFromMono((MonoString*)clipName) : "";
    anim->Play(clip);
}

void Internal_Animator_Stop(void* animator)
{
    if (!animator) return;
    static_cast<AnimatorComponent*>(animator)->Stop();
}

void Internal_Animator_Pause(void* animator)
{
    if (!animator) return;
    static_cast<AnimatorComponent*>(animator)->Pause();
}

void Internal_Animator_Resume(void* animator)
{
    if (!animator) return;
    static_cast<AnimatorComponent*>(animator)->Resume();
}

float Internal_Animator_GetSpeed(void* animator)
{
    if (!animator) return 1.0f;
    return static_cast<AnimatorComponent*>(animator)->playbackSpeed;
}

void Internal_Animator_SetSpeed(void* animator, float speed)
{
    if (!animator) return;
    static_cast<AnimatorComponent*>(animator)->playbackSpeed = speed;
}

int Internal_Animator_IsPlaying(void* animator)
{
    if (!animator) return 0;
    return static_cast<AnimatorComponent*>(animator)->IsPlaying() ? 1 : 0;
}

void Internal_ParticleSystem_Play(void* particleSystem)
{
    if (!particleSystem) return;
    static_cast<ParticleSystemComponent*>(particleSystem)->Play();
}

void Internal_ParticleSystem_Stop(void* particleSystem)
{
    if (!particleSystem) return;
    static_cast<ParticleSystemComponent*>(particleSystem)->Stop();
}

void Internal_ParticleSystem_Clear(void* particleSystem)
{
    if (!particleSystem) return;
    ParticleSystemComponent* ps = static_cast<ParticleSystemComponent*>(particleSystem);
    ps->Stop();
    ps->particles.clear();
}

int Internal_ParticleSystem_IsPlaying(void* particleSystem)
{
    if (!particleSystem) return 0;
    return static_cast<ParticleSystemComponent*>(particleSystem)->playing ? 1 : 0;
}

}
