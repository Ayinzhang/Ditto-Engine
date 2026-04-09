#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Scene.h"
#include "GameObject.h"
#include "../../Editor/Editor.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include <fstream>
#include <algorithm>

// 组件索引定义
constexpr int TRANSFORM_INDEX = 1 << 0;
constexpr int LIGHT_INDEX = 1 << 1;
constexpr int RENDERER_INDEX = 1 << 2;
constexpr int RIGIDBODY_INDEX = 1 << 3;
constexpr int CSHARP_SCRIPT_INDEX = 1 << 10;

static void WriteString(std::ofstream& file, const std::string& str)
{
    uint32_t length = static_cast<uint32_t>(str.length());
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(str.c_str(), length);
}

static std::string ReadString(std::ifstream& file)
{
    uint32_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    std::string str(length, '\0');
    file.read(&str[0], length);
    return str;
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
    compMask = other->compMask;
    for (Component* comp : other->components)
    {
        if (auto t = dynamic_cast<TransformComponent*>(comp))
            AddComponent<TransformComponent>(t);
        else if (auto l = dynamic_cast<LightComponent*>(comp))
            AddComponent<LightComponent>(l);
        else if (auto r = dynamic_cast<RendererComponent*>(comp))
            AddComponent<RendererComponent>(r);
        else if (auto rb = dynamic_cast<RigidbodyComponent*>(comp))
            AddComponent<RigidbodyComponent>(rb);
    }
    for (GameObject* child : other->children)
    {
        GameObject* newChild = new GameObject(child);
        newChild->parent = this;
        children.push_back(newChild);
    }
}

GameObject::~GameObject()
{
    for (GameObject* child : children)
    {
        child->parent = nullptr;
        delete child;
    }
    children.clear();
    for (Component* comp : components) delete comp;
}

void GameObject::AddChild(GameObject* child)
{
    if (!child || child == this) return;
    if (child->IsDescendantOf(this)) return;
    if (child->parent) child->RemoveFromParent();
    child->parent = this;
    children.push_back(child);
}

void GameObject::RemoveChild(GameObject* child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        children.erase(it);
        child->parent = nullptr;
    }
}

void GameObject::RemoveFromParent()
{
    if (parent) parent->RemoveChild(this);
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
    removeComps.push_back(component);
}

void GameObject::ProcessRemovals()
{
    for (Component* comp : removeComps)
    {
        auto it = std::find(components.begin(), components.end(), comp);
        if (it != components.end())
        {
            delete* it;
            components.erase(it);
            compMask &= ~comp->index;  // 使用位清除而不是减法
        }
    }
    removeComps.clear();
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
        name = nameBuffer;
        if (g_currentScene) g_currentScene->MarkDirty();
    }
    ImGui::PopID();
    
    // 锁定按钮（在名称后面）
    ImGui::SameLine();
    extern Editor* g_editor;
    if (g_editor)
    {
        unsigned int lockIcon = locked ? g_editor->GetLockIcon() : g_editor->GetUnlockIcon();
        if (lockIcon)
        {
            float btnSize = 16.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - btnSize - 20);
            if (ImGui::ImageButton("##lock", (void*)(intptr_t)lockIcon, ImVec2(btnSize, btnSize), 
                ImVec2(0, 1), ImVec2(1, 0)))
            {
                locked = !locked;
                g_editor->lockingSelection = locked;
                // 锁定时也更新 activeSelection，保持 Hierarchy 高亮
                if (locked) g_editor->activeSelection = this;
            }
        }
    }
    
    ImGui::Separator();

    for (auto comp : components)
    {
        ImGui::PushID(comp);
        comp->OnInspectorGUI();
        ImGui::PopID();
        ImGui::Separator();
    }
    ProcessRemovals();
}

void GameObject::Serialize(std::ofstream& file) const
{
    std::cout << "[GameObject::Serialize] Serializing: " << name << ", components: " << components.size() << ", children: " << children.size() << std::endl;
    
    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    file.write(reinterpret_cast<const char*>(&locked), sizeof(locked));
    WriteString(file, name);
    file.write(reinterpret_cast<const char*>(&compMask), sizeof(compMask));

    uint32_t componentCount = static_cast<uint32_t>(components.size());
    file.write(reinterpret_cast<const char*>(&componentCount), sizeof(componentCount));
    for (Component* comp : components)
    {
        file.write(reinterpret_cast<const char*>(&comp->index), sizeof(comp->index));
        file.write(reinterpret_cast<const char*>(&comp->enabled), sizeof(comp->enabled));
        comp->Serialize(file);
    }

    uint32_t childCount = static_cast<uint32_t>(children.size());
    file.write(reinterpret_cast<const char*>(&childCount), sizeof(childCount));
    std::cout << "[GameObject::Serialize] Writing childCount: " << childCount << " for " << name << std::endl;
    for (GameObject* child : children)
        child->Serialize(file);
}

void GameObject::Deserialize(std::ifstream& file)
{
    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));
    file.read(reinterpret_cast<char*>(&locked), sizeof(locked));
    name = ReadString(file);
    file.read(reinterpret_cast<char*>(&compMask), sizeof(compMask));
    
    std::cout << "[GameObject::Deserialize] Deserializing: " << name << std::endl;

    for (Component* comp : components) delete comp;
    components.clear();

    uint32_t componentCount = 0;
    file.read(reinterpret_cast<char*>(&componentCount), sizeof(componentCount));
    std::cout << "[GameObject::Deserialize] Reading componentCount: " << componentCount << " for " << name << std::endl;
    for (uint32_t i = 0; i < componentCount; i++)
    {
        int index = 0;
        file.read(reinterpret_cast<char*>(&index), sizeof(index));
        bool compEnabled = true;
        file.read(reinterpret_cast<char*>(&compEnabled), sizeof(compEnabled));

        Component* newComp = nullptr;
        switch (index)
        {
        case TRANSFORM_INDEX: newComp = new TransformComponent(); break;
        case LIGHT_INDEX: newComp = new LightComponent(); break;
        case RENDERER_INDEX: newComp = new RendererComponent(); break;
        case RIGIDBODY_INDEX: newComp = new RigidbodyComponent(); break;
        case CSHARP_SCRIPT_INDEX: newComp = new CSharpScriptComponent(); break;
        default: continue;
        }
        if (newComp)
        {
            newComp->index = index;
            newComp->enabled = compEnabled;
            newComp->gameObject = this;
            newComp->Deserialize(file);
            components.push_back(newComp);
        }
    }

    uint32_t childCount = 0;
    file.read(reinterpret_cast<char*>(&childCount), sizeof(childCount));
    std::cout << "[GameObject::Deserialize] Reading childCount: " << childCount << " for " << name << std::endl;
    for (uint32_t i = 0; i < childCount; i++)
    {
        GameObject* child = new GameObject(false);  // 不自动添加组件
        child->Deserialize(file);
        child->parent = this;
        children.push_back(child);
    }
}

TransformComponent::TransformComponent()
    : position(0.0f), rotation(0.0f), scale(1.0f),
    lastPosition(0.0f), lastRotation(0.0f), lastScale(1.0f),
    localDirty(true), worldDirty(true)
{
    index = 1 << 0;
    UpdateTransform();
}

TransformComponent::TransformComponent(TransformComponent* other)
    : position(other->position), rotation(other->rotation), scale(other->scale),
    lastPosition(other->lastPosition), lastRotation(other->lastRotation), lastScale(other->lastScale),
    localDirty(true), worldDirty(true)
{
    index = 1 << 0;
    UpdateTransform();
}

void TransformComponent::UpdateTransform()
{
    if (!localDirty) return;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotationMat = glm::mat4(1.0f);
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);
    model = translation * rotationMat * scaleMat;
    forward = glm::mat3(rotationMat) * glm::vec3(0, 0, -1);

    localDirty = false; worldDirty = true; MarkChildrenWorldDirty();
}

void TransformComponent::MarkChildrenWorldDirty()
{
    if (!gameObject) return;
    for (GameObject* child : gameObject->children)
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
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Transform");
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    if (ImGui::DragFloat3("##Position", &position.x, 0.1f)) localDirty = true;
    if (ImGui::DragFloat3("##Rotation", &rotation.x, 0.1f)) localDirty = true;
    if (ImGui::DragFloat3("##Scale", &scale.x, 0.1f)) localDirty = true;
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();

    if (localDirty)
    {
        lastPosition = position;
        lastRotation = rotation;
        lastScale = scale;
        UpdateTransform();
        
        // 标记场景已修改
        if (g_currentScene) g_currentScene->MarkDirty();
    }
}

void TransformComponent::Serialize(std::ofstream& file) const
{
    file.write(reinterpret_cast<const char*>(&position), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&rotation), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&scale), sizeof(glm::vec3));
}

void TransformComponent::Deserialize(std::ifstream& file)
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

LightComponent::LightComponent() : color(1.0f), intensity(1.0f) { index = 1 << 1; }
LightComponent::LightComponent(LightComponent* other) : color(other->color), intensity(other->intensity) { index = 1 << 1; }
void LightComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Light");
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    ImGui::Text("Color    "); ImGui::SameLine();
    ImGui::ColorEdit3("##Color", &color.x);
    ImGui::Text("Intensity"); ImGui::SameLine();
    ImGui::DragFloat("##Intensity", &intensity, 0.1f, 0.0f, 100.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}
void LightComponent::Serialize(std::ofstream& file) const
{
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&intensity), sizeof(intensity));
}
void LightComponent::Deserialize(std::ifstream& file)
{
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec3));
    file.read(reinterpret_cast<char*>(&intensity), sizeof(intensity));
}

RendererComponent::RendererComponent(Type _type) : type(_type), color(1.0f, 1.0f, 1.0f, 1.0f) { index = 1 << 2; }
RendererComponent::RendererComponent(RendererComponent* other) : type(other->type), color(other->color) { index = 1 << 2; }
void RendererComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Renderer");
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Cube", "Sphere", "Plane" };
    int currentType = static_cast<int>(type);
    ImGui::Text("Type "); ImGui::SameLine();
    if (ImGui::Combo("##Type", &currentType, typeNames, 3))
        type = static_cast<Type>(currentType);
    ImGui::Text("Color"); ImGui::SameLine();
    ImGui::ColorEdit4("##Color", &color.x, ImGuiColorEditFlags_AlphaBar);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}
void RendererComponent::Serialize(std::ofstream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
}
void RendererComponent::Deserialize(std::ifstream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
}

RigidbodyComponent::RigidbodyComponent()
    : type(Dynamic), mass(1.0f), useGravity(true), damp(0.05f), angularDamp(0.05f),
    velocity(0.0f), angularVelocity(0.0f) {
    index = 1 << 3;
}
RigidbodyComponent::RigidbodyComponent(RigidbodyComponent* other)
    : type(other->type), mass(other->mass), useGravity(other->useGravity),
    damp(0.05f), angularDamp(0.05f), velocity(0.0f), angularVelocity(0.0f) {
    index = 1 << 3;
}
void RigidbodyComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Rigidbody");
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Static", "Dynamic" };
    int currentType = static_cast<int>(type);
    ImGui::Text("Type"); ImGui::SameLine();
    if (ImGui::Combo("##Type", &currentType, typeNames, 2))
        type = static_cast<Type>(currentType);
    if (type == Dynamic)
    {
        ImGui::Text("Use Gravity"); ImGui::SameLine();
        ImGui::Checkbox("##Use Gravity", &useGravity);
        ImGui::Text("Mass "); ImGui::SameLine();
        ImGui::DragFloat("##Mass", &mass, 0.1f, 0.001f, 1000.0f);
        ImGui::Text("Damp "); ImGui::SameLine();
        ImGui::DragFloat("##Damp", &damp, 0.1f, 0.0f, 1.0f);
        ImGui::Text("ADamp"); ImGui::SameLine();
        ImGui::DragFloat("##AngularDamp", &angularDamp, 0.1f, 0.0f, 1.0f);
        ImGui::Text("Velocity "); ImGui::SameLine();
        ImGui::DragFloat3("##Velocity", &velocity.x, 0.1f);
        ImGui::Text("AVelocity"); ImGui::SameLine();
        ImGui::DragFloat3("##AVelocity", &angularVelocity.x, 0.1f);
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

void RigidbodyComponent::Serialize(std::ofstream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&mass), sizeof(mass));
    file.write(reinterpret_cast<const char*>(&useGravity), sizeof(useGravity));
    file.write(reinterpret_cast<const char*>(&damp), sizeof(damp));
    file.write(reinterpret_cast<const char*>(&angularDamp), sizeof(angularDamp));
}

void RigidbodyComponent::Deserialize(std::ifstream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(&mass), sizeof(mass));
    file.read(reinterpret_cast<char*>(&useGravity), sizeof(useGravity));
    file.read(reinterpret_cast<char*>(&damp), sizeof(damp));
    file.read(reinterpret_cast<char*>(&angularDamp), sizeof(angularDamp));
}