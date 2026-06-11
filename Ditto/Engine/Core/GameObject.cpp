#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Scene.h"
#include "GameObject.h"
#include "Logger.h"
#include "ProjectManager.h"
#include "PathUtils.h"
#include "../Graphics/Shaders/ShaderAsset.h"
#include "../../Editor/Editor.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cctype>

// Component index definitions live in GameObject.h (namespace ComponentIndex)
// so engine-side code can share them. Local aliases keep the switch below terse.
namespace CI = ComponentIndex;

static void WriteString(std::ostream& file, const std::string& str)
{
    uint32_t length = static_cast<uint32_t>(str.length());
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(str.c_str(), length);
}

static std::string ReadString(std::istream& file)
{
    uint32_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    std::string str(length, '\0');
    file.read(&str[0], length);
    return str;
}

// Bracket an ImGui value-editing widget so a whole drag counts as ONE undo
// step, committed only if the value actually changed. Call immediately AFTER
// the widget. Uses the global editor pointer.
static void TrackUndoableEdit()
{
    if (!g_editor) return;
    if (ImGui::IsItemActivated())            g_editor->BeginInspectorEdit();
    if (ImGui::IsItemDeactivatedAfterEdit()) g_editor->EndInspectorEdit();
}

static void DrawComponentSelectionBackground(Component* component)
{
    if (!g_editor || g_editor->selectedComponent != component) return;
    ImVec2 min = ImGui::GetCursorScreenPos();
    ImVec2 max(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f, min.y + ImGui::GetFrameHeight());
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(45, 105, 175, 95), 3.0f);
}

static void SelectComponentOnLastItem(Component* component)
{
    if (g_editor && ImGui::IsItemClicked())
        g_editor->selectedComponent = component;
}

static void SelectComponentArea(Component* component, const ImVec2& start)
{
    if (!g_editor) return;

    ImVec2 end(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f, ImGui::GetCursorScreenPos().y);
    if (end.y < start.y + ImGui::GetFrameHeight())
        end.y = start.y + ImGui::GetFrameHeight();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(start, end, true))
        g_editor->selectedComponent = component;

    if (g_editor->selectedComponent == component)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(start, end, IM_COL32(45, 105, 175, 24), 3.0f);
        drawList->AddRect(start, end, IM_COL32(75, 145, 220, 150), 3.0f);
    }
}

static bool DrawObjectFieldButton(const char* label, void* iconTexture, const std::string& value, const char* popupId,
    std::string* droppedPath = nullptr)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    float rowWidth = ImGui::GetContentRegionAvail().x;
    if (iconTexture)
    {
        ImGui::Image(iconTexture, ImVec2(16.0f, 16.0f), ImVec2(0, 1), ImVec2(1, 0));
        ImGui::SameLine();
        rowWidth -= 16.0f + ImGui::GetStyle().ItemSpacing.x;
    }
    float fieldWidth = std::max(40.0f, rowWidth);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.16f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.23f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.29f, 0.36f, 1.0f));
    bool opened = ImGui::Button((value + "##" + std::string(popupId) + "Field").c_str(), ImVec2(fieldWidth, 0.0f));
    if (droppedPath && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_FILE"))
            *droppedPath = static_cast<const char*>(payload->Data);
        ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleColor(3);
    if (opened)
        ImGui::OpenPopup(popupId);
    return opened;
}

static std::string FileNameFromPath(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static std::string ToAssetRelativePath(const std::string& path)
{
    Project* project = ProjectManager::GetInstance().GetCurrentProject();
    if (!project) return path;

    std::filesystem::path filePath = std::filesystem::absolute(path).lexically_normal();
    std::filesystem::path assetsPath = std::filesystem::absolute(std::filesystem::path(project->path) / "Assets").lexically_normal();
    std::wstring fileW = filePath.wstring();
    std::wstring assetsW = assetsPath.wstring();
    std::replace(fileW.begin(), fileW.end(), L'\\', L'/');
    std::replace(assetsW.begin(), assetsW.end(), L'\\', L'/');

    if (fileW.rfind(assetsW + L"/", 0) == 0)
    {
        std::filesystem::path relative = filePath.lexically_relative(assetsPath);
        return relative.generic_string();
    }
    return path;
}

static std::string LowerExtension(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

static Ditto::ShaderAsset LoadRendererShaderAsset(const std::string& shaderName)
{
    return Ditto::LoadShaderAsset(shaderName.empty() ? RendererComponent::DefaultShaderName : shaderName);
}

GameObject::GameObject(const std::string _name)
{
    name = _name;
    AddComponent<TransformComponent>();
}

GameObject::GameObject(bool createComponents)
{
    name = "New GameObject";
    if (createComponents)
    {
        AddComponent<TransformComponent>();
    }
}

GameObject::GameObject(GameObject* other)
{
    enabled = other->enabled;
    locked = other->locked;
    name = other->name;
    compMask = 0;   // rebuilt by AddComponent below; never copy a possibly-stale mask
    for (const auto& compPtr : other->components)
    {
        Component* comp = compPtr.get();
        if (auto t = dynamic_cast<TransformComponent*>(comp))
            AddComponent<TransformComponent>(t);
        else if (auto l = dynamic_cast<LightComponent*>(comp))
            AddComponent<LightComponent>(l);
        else if (auto r = dynamic_cast<RendererComponent*>(comp))
            AddComponent<RendererComponent>(r);
        else if (auto rb = dynamic_cast<RigidbodyComponent*>(comp))
            AddComponent<RigidbodyComponent>(rb);
        else if (auto col = dynamic_cast<ColliderComponent*>(comp))
            AddComponent<ColliderComponent>(col);
        else if (auto cs = dynamic_cast<CSharpScriptComponent*>(comp))
        {
            // CSharpScriptComponent has no copy-ctor; default-construct then
            // copy its serialized state so duplicated objects keep their script.
            CSharpScriptComponent* newCs = AddComponent<CSharpScriptComponent>();
            newCs->scriptName = cs->scriptName;
            newCs->scriptPath = cs->scriptPath;
            newCs->fields     = cs->fields;
            newCs->enabled    = cs->enabled;
        }
    }
    for (const auto& child : other->children)
    {
        auto newChild = std::make_unique<GameObject>(child.get());
        newChild->parent = this;
        children.push_back(std::move(newChild));
    }
}

// children + components: unique_ptr vectors tear the tree down recursively.
GameObject::~GameObject() = default;

GameObject* GameObject::AddChild(std::unique_ptr<GameObject> child)
{
    if (!child || child.get() == this) return nullptr;
    if (child->IsDescendantOf(this)) return nullptr;
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

void GameObject::AddChild(GameObject* existingChild)
{
    if (!existingChild || existingChild == this) return;
    // Cycle guard must run BEFORE detaching (IsDescendantOf walks parents).
    if (existingChild->IsDescendantOf(this)) return;
    if (!existingChild->parent) return;   // every live non-root object has an owner

    auto owned = existingChild->parent->DetachChild(existingChild);
    if (owned) AddChild(std::move(owned));
}

std::unique_ptr<GameObject> GameObject::DetachChild(GameObject* child)
{
    auto it = std::find_if(children.begin(), children.end(),
        [child](const std::unique_ptr<GameObject>& c) { return c.get() == child; });
    if (it == children.end()) return nullptr;

    auto owned = std::move(*it);
    children.erase(it);
    owned->parent = nullptr;
    return owned;
}

bool GameObject::IsDescendantOf(GameObject* ancestor) const
{
    const GameObject* cur = this;
    while (cur->parent)
    {
        if (cur->parent == ancestor) return true;
        cur = cur->parent;
    }
    return false;
}

void GameObject::RemoveComponent(Component* component)
{
    if (component->gameObject != this) return;
    if (g_editor && g_editor->selectedComponent == component)
        g_editor->selectedComponent = nullptr;
    removeComps.push_back(component);
}

void GameObject::ProcessRemovals()
{
    for (Component* comp : removeComps)
    {
        auto it = std::find_if(components.begin(), components.end(),
            [comp](const std::unique_ptr<Component>& c) { return c.get() == comp; });
        if (it != components.end())
        {
            components.erase(it);
        }
    }
    removeComps.clear();

    compMask = 0;
    for (const auto& comp : components)
        compMask |= comp->index;
}

void GameObject::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    char nameBuffer[256];
    strcpy_s(nameBuffer, sizeof(nameBuffer), name.c_str());
    ImGui::Text("Name"); ImGui::SameLine();
    ImGui::PushID("NameInput");
    if (ImGui::InputText("", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (g_editor) g_editor->PushUndoSnapshot();   // pre-rename state
        name = nameBuffer;
        if (g_currentScene) g_currentScene->MarkDirty();
    }
    ImGui::PopID();
    
    // Lock button (after name)
    ImGui::SameLine();
    extern Editor* g_editor;
    if (g_editor)
    {
        void* lockIcon = locked ? g_editor->GetLockIcon() : g_editor->GetUnlockIcon();
        if (lockIcon)
        {
            float btnSize = 16.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - btnSize - 20);
            if (ImGui::ImageButton("##lock", (void*)(intptr_t)lockIcon, ImVec2(btnSize, btnSize), 
                ImVec2(0, 1), ImVec2(1, 0)))
            {
                locked = !locked;
                g_editor->lockingSelection = locked;
                if (locked) g_editor->activeSelection = this;
            }
        }
    }
    
    ImGui::Separator();

    for (auto& comp : components)
    {
        ImGui::PushID(comp.get());
        ImVec2 componentStart = ImGui::GetCursorScreenPos();
        comp->OnInspectorGUI();
        SelectComponentArea(comp.get(), componentStart);
        ImGui::PopID();
        ImGui::Separator();
    }
    ProcessRemovals();
}

void GameObject::Serialize(std::ostream& file) const
{
    DITTO_LOG_INFO_STREAM("[GameObject::Serialize] Serializing: " << name << ", components: " << components.size() << ", children: " << children.size() );
    
    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    file.write(reinterpret_cast<const char*>(&locked), sizeof(locked));
    WriteString(file, name);
    file.write(reinterpret_cast<const char*>(&compMask), sizeof(compMask));

    uint32_t componentCount = static_cast<uint32_t>(components.size());
    file.write(reinterpret_cast<const char*>(&componentCount), sizeof(componentCount));
    for (const auto& comp : components)
    {
        file.write(reinterpret_cast<const char*>(&comp->index), sizeof(comp->index));
        file.write(reinterpret_cast<const char*>(&comp->enabled), sizeof(comp->enabled));
        comp->Serialize(file);
    }

    uint32_t childCount = static_cast<uint32_t>(children.size());
    file.write(reinterpret_cast<const char*>(&childCount), sizeof(childCount));
    DITTO_LOG_INFO_STREAM("[GameObject::Serialize] Writing childCount: " << childCount << " for " << name );
    for (const auto& child : children)
        child->Serialize(file);
}

void GameObject::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));
    file.read(reinterpret_cast<char*>(&locked), sizeof(locked));
    name = ReadString(file);
    file.read(reinterpret_cast<char*>(&compMask), sizeof(compMask));
    
    DITTO_LOG_INFO_STREAM("[GameObject::Deserialize] Deserializing: " << name );

    components.clear();

    uint32_t componentCount = 0;
    file.read(reinterpret_cast<char*>(&componentCount), sizeof(componentCount));
    DITTO_LOG_INFO_STREAM("[GameObject::Deserialize] Reading componentCount: " << componentCount << " for " << name );
    for (uint32_t i = 0; i < componentCount; i++)
    {
        int index = 0;
        file.read(reinterpret_cast<char*>(&index), sizeof(index));
        bool compEnabled = true;
        file.read(reinterpret_cast<char*>(&compEnabled), sizeof(compEnabled));

        std::unique_ptr<Component> newComp;
        switch (index)
        {
        case CI::Transform:    newComp = std::make_unique<TransformComponent>(); break;
        case CI::Light:        newComp = std::make_unique<LightComponent>(); break;
        case CI::Renderer:     newComp = std::make_unique<RendererComponent>(); break;
        case CI::Rigidbody:    newComp = std::make_unique<RigidbodyComponent>(); break;
        case CI::Collider:     newComp = std::make_unique<ColliderComponent>(); break;
        case CI::CSharpScript: newComp = std::make_unique<CSharpScriptComponent>(); break;
        default: continue;
        }
        if (newComp)
        {
            newComp->index = index;
            newComp->enabled = compEnabled;
            newComp->gameObject = this;
            newComp->Deserialize(file);
            components.push_back(std::move(newComp));
        }
    }

    // Recompute compMask from the components actually rebuilt, so it is
    // self-consistent regardless of what was stored on disk (older files may
    // carry a mask corrupted by the previous '+=' accumulation bug).
    compMask = 0;
    for (const auto& comp : components)
        compMask |= comp->index;

    uint32_t childCount = 0;
    file.read(reinterpret_cast<char*>(&childCount), sizeof(childCount));
    DITTO_LOG_INFO_STREAM("[GameObject::Deserialize] Reading childCount: " << childCount << " for " << name );
    for (uint32_t i = 0; i < childCount; i++)
    {
        auto child = std::make_unique<GameObject>(false);
        child->Deserialize(file);
        child->parent = this;
        children.push_back(std::move(child));
    }
}

TransformComponent::TransformComponent()
    : position(0.0f), rotation(0.0f), scale(1.0f),
    orientation(1.0f, 0.0f, 0.0f, 0.0f), useQuatRotation(false),
    lastPosition(0.0f), lastRotation(0.0f), lastScale(1.0f),
    localDirty(true), worldDirty(true)
{
    index = CI::Transform;
    UpdateTransform();
}

TransformComponent::TransformComponent(TransformComponent* other)
    : position(other->position), rotation(other->rotation), scale(other->scale),
    orientation(1.0f, 0.0f, 0.0f, 0.0f), useQuatRotation(false),
    lastPosition(other->lastPosition), lastRotation(other->lastRotation), lastScale(other->lastScale),
    localDirty(true), worldDirty(true)
{
    index = CI::Transform;
    UpdateTransform();
}

void TransformComponent::SeedOrientationFromEuler()
{
    // Compose in the SAME Y * X * Z order UpdateTransform uses for euler, so a
    // body's visible orientation does not snap when simulation takes over.
    glm::quat qy = glm::angleAxis(glm::radians(rotation.y), glm::vec3(0, 1, 0));
    glm::quat qx = glm::angleAxis(glm::radians(rotation.x), glm::vec3(1, 0, 0));
    glm::quat qz = glm::angleAxis(glm::radians(rotation.z), glm::vec3(0, 0, 1));
    orientation = qy * qx * qz;
    useQuatRotation = true;
    localDirty = true;
}

void TransformComponent::UpdateTransform()
{
    if (!localDirty) return;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);

    glm::mat4 rotationMat;
    if (useQuatRotation)
    {
        // Simulation owns the rotation: build directly from the quaternion.
        rotationMat = glm::mat4_cast(orientation);
    }
    else
    {
        rotationMat = glm::mat4(1.0f);
        rotationMat = glm::rotate(rotationMat, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        rotationMat = glm::rotate(rotationMat, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        rotationMat = glm::rotate(rotationMat, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    }

    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);
    model = translation * rotationMat * scaleMat;
    forward = glm::mat3(rotationMat) * glm::vec3(0, 0, -1);

    localDirty = false; worldDirty = true; MarkChildrenWorldDirty();
}

void TransformComponent::MarkChildrenWorldDirty()
{
    if (!gameObject) return;
    for (const auto& child : gameObject->children)
    {
        if (auto* childTransform = child->GetComponent<TransformComponent>())
        {
            childTransform->worldDirty = true;
            childTransform->MarkChildrenWorldDirty();
        }
    }
}

void TransformComponent::UpdateWorldMatrix() const
{
    if (!worldDirty) return;

    if (!gameObject || !gameObject->parent)
        worldModel = model;
    else
    {
        TransformComponent* parentTransform = gameObject->parent->GetComponent<TransformComponent>();
        if (parentTransform)
        {
            parentTransform->UpdateWorldMatrix();
            worldModel = parentTransform->worldModel * model;
        }
        else
            worldModel = model;
    }
    worldDirty = false;
}

glm::mat4 TransformComponent::GetWorldModel() const
{
    UpdateWorldMatrix();
    return worldModel;
}

void TransformComponent::OnInspectorGUI()
{
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Transform");
    SelectComponentOnLastItem(this);
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    if (ImGui::DragFloat3("##Position", &position.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    if (ImGui::DragFloat3("##Rotation", &rotation.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    if (ImGui::DragFloat3("##Scale", &scale.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();

    if (localDirty)
    {
        lastPosition = position;
        lastRotation = rotation;
        lastScale = scale;
        UpdateTransform();
        
        // Mark scene as modified
        if (g_currentScene) g_currentScene->MarkDirty();
    }
}

void TransformComponent::Serialize(std::ostream& file) const
{
    file.write(reinterpret_cast<const char*>(&position), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&rotation), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&scale), sizeof(glm::vec3));
}

void TransformComponent::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&position), sizeof(glm::vec3));
    file.read(reinterpret_cast<char*>(&rotation), sizeof(glm::vec3));
    file.read(reinterpret_cast<char*>(&scale), sizeof(glm::vec3));
    lastPosition = position;
    lastRotation = rotation;
    lastScale = scale;
    localDirty = true;
    worldDirty = true;
    UpdateTransform();
}

LightComponent::LightComponent() : color(1.0f), intensity(1.0f) { index = CI::Light; }
LightComponent::LightComponent(LightComponent* other) : color(other->color), intensity(other->intensity) { index = CI::Light; }
void LightComponent::OnInspectorGUI()
{
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Light");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    ImGui::Text("Color    "); ImGui::SameLine();
    ImGui::ColorEdit3("##Color", &color.x);
    TrackUndoableEdit();
    ImGui::Text("Intensity"); ImGui::SameLine();
    ImGui::DragFloat("##Intensity", &intensity, 0.1f, 0.0f, 100.0f);
    TrackUndoableEdit();
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}
void LightComponent::Serialize(std::ostream& file) const
{
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&intensity), sizeof(intensity));
}
void LightComponent::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec3));
    file.read(reinterpret_cast<char*>(&intensity), sizeof(intensity));
}

RendererComponent::RendererComponent(Type _type)
    : type(_type), meshSource(BuiltIn), color(1.0f, 1.0f, 1.0f, 1.0f), shaderName(DefaultShaderName)
{
    index = CI::Renderer;
}
RendererComponent::RendererComponent(RendererComponent* other)
    : type(other->type), meshSource(other->meshSource), color(other->color),
    shaderName(other->shaderName), mainTexturePath(other->mainTexturePath), meshPath(other->meshPath)
{
    index = CI::Renderer;
}

bool RendererComponent::UsesFileMesh() const
{
    return meshSource == File && !meshPath.empty();
}

void RendererComponent::OnInspectorGUI()
{
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Renderer");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    std::string meshDisplay;
    if (meshSource == BuiltIn)
        meshDisplay = (type == Sphere) ? "Sphere" : "Cube";
    else
        meshDisplay = meshPath.empty() ? "None (Mesh)" : FileNameFromPath(meshPath);

    std::string droppedMeshPath;
    DrawObjectFieldButton("Mesh", nullptr, meshDisplay, "RendererMeshObjectPopup", &droppedMeshPath);
    if (!droppedMeshPath.empty())
    {
        std::string ext = LowerExtension(droppedMeshPath);
        if (ext == ".obj" || ext == ".fbx" || ext == ".mesh")
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            meshSource = File;
            meshPath = ToAssetRelativePath(droppedMeshPath);
        }
    }
    if (ImGui::BeginPopup("RendererMeshObjectPopup"))
    {
        if (ImGui::MenuItem("Cube", nullptr, meshSource == BuiltIn && type == Cube))
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            meshSource = BuiltIn;
            type = Cube;
        }
        if (ImGui::MenuItem("Sphere", nullptr, meshSource == BuiltIn && type == Sphere))
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            meshSource = BuiltIn;
            type = Sphere;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Use File Mesh", nullptr, meshSource == File))
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            meshSource = File;
        }
        char meshBuf[256];
        strcpy_s(meshBuf, sizeof(meshBuf), meshPath.c_str());
        ImGui::TextUnformatted("Path");
        if (ImGui::InputText("##RendererFileMeshPath", meshBuf, sizeof(meshBuf)))
        {
            meshSource = File;
            meshPath = meshBuf;
        }
        TrackUndoableEdit();
        if (ImGui::SmallButton("Clear Mesh Path"))
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            meshPath.clear();
        }
        ImGui::EndPopup();
    }

    std::string shaderDisplay = shaderName.empty() ? DefaultShaderName : shaderName;
    std::string droppedShaderPath;
    DrawObjectFieldButton("Shader", nullptr, shaderDisplay, "RendererShaderObjectPopup", &droppedShaderPath);
    if (!droppedShaderPath.empty())
    {
        std::string ext = LowerExtension(droppedShaderPath);
        if (ext == ".hlsl" || ext == ".shader" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            shaderName = ToAssetRelativePath(droppedShaderPath);
        }
    }
    if (ImGui::BeginPopup("RendererShaderObjectPopup"))
    {
        if (ImGui::MenuItem(DefaultShaderName, nullptr, shaderDisplay == DefaultShaderName))
        {
            if (g_editor) g_editor->PushUndoSnapshot();
            shaderName = DefaultShaderName;
        }
        ImGui::Separator();
        char shaderBuf[128];
        strcpy_s(shaderBuf, sizeof(shaderBuf), shaderName.c_str());
        ImGui::TextUnformatted("Shader Name");
        if (ImGui::InputText("##RendererShaderName", shaderBuf, sizeof(shaderBuf)))
            shaderName = shaderBuf;
        TrackUndoableEdit();
        ImGui::EndPopup();
    }

    Ditto::ShaderAsset shaderAsset = LoadRendererShaderAsset(shaderName);
    if (shaderAsset.ok && !shaderAsset.properties.empty())
    {
        ImGui::TextUnformatted("Properties");
        ImGui::Indent(12.0f);
        for (const Ditto::ShaderProperty& property : shaderAsset.properties)
        {
            const std::string label = property.displayName.empty() ? property.name : property.displayName;
            if (property.type == Ditto::ShaderPropertyType::Color)
            {
                ImGui::TextUnformatted(label.c_str()); ImGui::SameLine();
                ImGui::ColorEdit4(("##Material" + property.name).c_str(), &color.x, ImGuiColorEditFlags_AlphaBar);
                TrackUndoableEdit();
            }
            else if (property.type == Ditto::ShaderPropertyType::Texture2D)
            {
                std::string droppedTexturePath;
                std::string textureDisplay = mainTexturePath.empty() ? property.textureDefault : FileNameFromPath(mainTexturePath);
                DrawObjectFieldButton(label.c_str(), nullptr, textureDisplay, ("RendererTextureObjectPopup" + property.name).c_str(), &droppedTexturePath);
                if (!droppedTexturePath.empty())
                {
                    std::string ext = LowerExtension(droppedTexturePath);
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
                    {
                        if (g_editor) g_editor->PushUndoSnapshot();
                        mainTexturePath = ToAssetRelativePath(droppedTexturePath);
                    }
                }
                if (ImGui::BeginPopup(("RendererTextureObjectPopup" + property.name).c_str()))
                {
                    char texBuf[256];
                    strcpy_s(texBuf, sizeof(texBuf), mainTexturePath.c_str());
                    ImGui::TextUnformatted("Path");
                    if (ImGui::InputText(("##RendererTexturePath" + property.name).c_str(), texBuf, sizeof(texBuf)))
                        mainTexturePath = texBuf;
                    TrackUndoableEdit();
                    if (ImGui::SmallButton(("Clear##Texture" + property.name).c_str()))
                    {
                        if (g_editor) g_editor->PushUndoSnapshot();
                        mainTexturePath.clear();
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::Unindent(12.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}
void RendererComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    WriteString(file, meshPath);   // scene version 2+
    int32_t meshSourceInt = static_cast<int32_t>(meshSource);
    file.write(reinterpret_cast<const char*>(&meshSourceInt), sizeof(meshSourceInt));
    WriteString(file, shaderName.empty() ? DefaultShaderName : shaderName);
    WriteString(file, mainTexturePath);
}
void RendererComponent::Deserialize(std::istream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    // Only present in scene version >= 2; older files leave meshPath empty.
    if (g_sceneLoadingVersion >= 2)
        meshPath = ReadString(file);
    else
        meshPath.clear();
    if (g_sceneLoadingVersion >= 6)
    {
        int32_t meshSourceInt = 0;
        file.read(reinterpret_cast<char*>(&meshSourceInt), sizeof(meshSourceInt));
        meshSource = static_cast<MeshSource>(meshSourceInt);
        shaderName = ReadString(file);
        if (shaderName.empty()) shaderName = DefaultShaderName;
        if (g_sceneLoadingVersion >= 7)
            mainTexturePath = ReadString(file);
        else
            mainTexturePath.clear();
    }
    else
    {
        meshSource = meshPath.empty() ? BuiltIn : File;
        shaderName = DefaultShaderName;
        mainTexturePath.clear();
    }
}

RigidbodyComponent::RigidbodyComponent()
    : type(Dynamic), mass(1.0f), useGravity(true), damp(0.05f), angularDamp(0.05f),
    velocity(0.0f), angularVelocity(0.0f) {
    index = CI::Rigidbody;
}
RigidbodyComponent::RigidbodyComponent(RigidbodyComponent* other)
    : type(other->type), mass(other->mass), useGravity(other->useGravity),
    damp(0.05f), angularDamp(0.05f), velocity(0.0f), angularVelocity(0.0f) {
    index = CI::Rigidbody;
}
void RigidbodyComponent::OnInspectorGUI()
{
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Rigidbody");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Static", "Dynamic", "Kinematic" };
    int currentType = static_cast<int>(type);
    ImGui::Text("Type"); ImGui::SameLine();
    if (ImGui::Combo("##Type", &currentType, typeNames, 3))
        type = static_cast<Type>(currentType);
    TrackUndoableEdit();
    if (type == Dynamic)
    {
        ImGui::Text("Use Gravity"); ImGui::SameLine();
        ImGui::Checkbox("##Use Gravity", &useGravity);
        TrackUndoableEdit();
        ImGui::Text("Mass "); ImGui::SameLine();
        ImGui::DragFloat("##Mass", &mass, 0.1f, 0.001f, 1000.0f);
        TrackUndoableEdit();
        ImGui::Text("Damp "); ImGui::SameLine();
        ImGui::DragFloat("##Damp", &damp, 0.1f, 0.0f, 1.0f);
        TrackUndoableEdit();
        ImGui::Text("ADamp"); ImGui::SameLine();
        ImGui::DragFloat("##AngularDamp", &angularDamp, 0.1f, 0.0f, 1.0f);
        TrackUndoableEdit();
        ImGui::Text("Velocity "); ImGui::SameLine();
        ImGui::DragFloat3("##Velocity", &velocity.x, 0.1f);
        TrackUndoableEdit();
        ImGui::Text("AVelocity"); ImGui::SameLine();
        ImGui::DragFloat3("##AVelocity", &angularVelocity.x, 0.1f);
        TrackUndoableEdit();
    }
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

void RigidbodyComponent::CalculateInertia(RendererComponent::Type shapeType, const glm::vec3& scale)
{
    inertia = glm::mat3(0.0f);
    switch (shapeType)
    {
    case RendererComponent::Cube:
    {
        float w = scale.x, h = scale.y, d = scale.z;
        float Ixx = (1.0f / 12.0f) * mass * (h * h + d * d);
        float Iyy = (1.0f / 12.0f) * mass * (w * w + d * d);
        float Izz = (1.0f / 12.0f) * mass * (w * w + h * h);
        inertia[0][0] = Ixx; inertia[1][1] = Iyy; inertia[2][2] = Izz;
        break;
    }
    case RendererComponent::Sphere:
    {
        float r = glm::max(scale.x, glm::max(scale.y, scale.z)) * 0.5f;
        float I = (2.0f / 5.0f) * mass * r * r;
        inertia[0][0] = I; inertia[1][1] = I; inertia[2][2] = I;
        break;
    }
    }
    inverseInertia = glm::inverse(inertia);
}

void RigidbodyComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&mass), sizeof(mass));
    file.write(reinterpret_cast<const char*>(&useGravity), sizeof(useGravity));
    file.write(reinterpret_cast<const char*>(&damp), sizeof(damp));
    file.write(reinterpret_cast<const char*>(&angularDamp), sizeof(angularDamp));
}

void RigidbodyComponent::Deserialize(std::istream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(&mass), sizeof(mass));
    file.read(reinterpret_cast<char*>(&useGravity), sizeof(useGravity));
    file.read(reinterpret_cast<char*>(&damp), sizeof(damp));
    file.read(reinterpret_cast<char*>(&angularDamp), sizeof(angularDamp));
}

ColliderComponent::ColliderComponent(Type _type)
    : type(_type), isTrigger(false), biasPosition(0.0f), biasRotation(0.0f), biasScale(1.0f)
{
    index = CI::Collider;
}

ColliderComponent::ColliderComponent(ColliderComponent* other)
    : type(other->type), isTrigger(other->isTrigger),
    biasPosition(other->biasPosition), biasRotation(other->biasRotation), biasScale(other->biasScale),
    meshPath(other->meshPath)
{
    index = CI::Collider;
}

glm::mat4 ColliderComponent::GetBiasMatrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), biasPosition);
    glm::mat4 rotation = glm::mat4(1.0f);
    rotation = glm::rotate(rotation, glm::radians(biasRotation.y), glm::vec3(0, 1, 0));
    rotation = glm::rotate(rotation, glm::radians(biasRotation.x), glm::vec3(1, 0, 0));
    rotation = glm::rotate(rotation, glm::radians(biasRotation.z), glm::vec3(0, 0, 1));
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), biasScale);
    return translation * rotation * scale;
}

void ColliderComponent::OnInspectorGUI()
{
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Collider");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    ImGui::Text("Trigger"); ImGui::SameLine();
    ImGui::Checkbox("##ColliderTrigger", &isTrigger);
    TrackUndoableEdit();

    const char* typeNames[] = { "Box", "Sphere", "Mesh Convex" };
    int currentType = static_cast<int>(type);
    ImGui::Text("Type"); ImGui::SameLine();
    if (ImGui::Combo("##ColliderType", &currentType, typeNames, 3))
        type = static_cast<Type>(currentType);
    TrackUndoableEdit();

    ImGui::Text("Bias Pos"); ImGui::SameLine();
    ImGui::DragFloat3("##ColliderBiasPosition", &biasPosition.x, 0.1f);
    TrackUndoableEdit();
    ImGui::Text("Bias Rot"); ImGui::SameLine();
    ImGui::DragFloat3("##ColliderBiasRotation", &biasRotation.x, 0.1f);
    TrackUndoableEdit();
    ImGui::Text("Bias Scale"); ImGui::SameLine();
    ImGui::DragFloat3("##ColliderBiasScale", &biasScale.x, 0.1f, 0.001f, 1000.0f);
    TrackUndoableEdit();

    if (type == MeshConvex)
    {
        char meshBuf[256];
        strcpy_s(meshBuf, sizeof(meshBuf), meshPath.c_str());
        ImGui::Text("Mesh"); ImGui::SameLine();
        if (ImGui::InputText("##ColliderMeshPath", meshBuf, sizeof(meshBuf)))
            meshPath = meshBuf;
        TrackUndoableEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##ColliderMesh")) meshPath.clear();
        ImGui::TextDisabled("OBJ only; used as convex point cloud");
    }
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
}

void ColliderComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&isTrigger), sizeof(isTrigger));
    file.write(reinterpret_cast<const char*>(&biasPosition), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&biasRotation), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&biasScale), sizeof(glm::vec3));
    WriteString(file, meshPath);
}

void ColliderComponent::Deserialize(std::istream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    if (g_sceneLoadingVersion >= 4)
        file.read(reinterpret_cast<char*>(&isTrigger), sizeof(isTrigger));
    else
        isTrigger = false;
    if (g_sceneLoadingVersion >= 5)
    {
        file.read(reinterpret_cast<char*>(&biasPosition), sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(&biasRotation), sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(&biasScale), sizeof(glm::vec3));
    }
    else
    {
        biasPosition = glm::vec3(0.0f);
        biasRotation = glm::vec3(0.0f);
        biasScale = glm::vec3(1.0f);
    }
    meshPath = ReadString(file);
}

