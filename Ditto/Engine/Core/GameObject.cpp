#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Scene.h"
#include "GameObject.h"
#include "Logger.h"
#include "CSharpScript.h"
#include "../Animation/AnimatorComponent.h"
#include "../Graphics/ParticleSystemComponent.h"
#include "../Resources/AssetPath.h"
#ifndef DITTO_HEADLESS_TESTS
#include "../Graphics/Materials/MaterialAsset.h"
#include "../Audio/AudioEngine.h"
#include "../Physics/PhysicsMaterial2DAsset.h"
#include "../../Editor/Editor.h"
#include "../../Editor/ComponentInspectorWidgets.h"
#include "../../3rdParty/ImGui/imgui.h"
#endif
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <iterator>

// Component index definitions live in GameObject.h (namespace ComponentIndex)
// so engine-side code can share them. Local aliases keep the switch below terse.
namespace CI = ComponentIndex;

static void WriteString(std::ostream& file, const std::string& str)
{
    uint32_t length = static_cast<uint32_t>(str.length());
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(str.c_str(), length);
}

static void WriteAssetPathString(std::ostream& file, const std::string& path)
{
    WriteString(file, Ditto::AssetPath::NormalizeAssetKey(path));
}

static std::string ReadString(std::istream& file)
{
    uint32_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    std::string str(length, '\0');
    file.read(&str[0], length);
    return str;
}

#ifdef DITTO_HEADLESS_TESTS
Editor* g_editor = nullptr;
#endif
GameObject::GameObject(const std::string& _name)
{
    name = _name;
    AddComponent<TransformComponent>();
}

GameObject::GameObject(const char* _name)
    : GameObject(std::string(_name ? _name : "New GameObject"))
{
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
        else if (auto c = dynamic_cast<CameraComponent*>(comp))
            AddComponent<CameraComponent>(c);
        else if (auto r = dynamic_cast<RendererComponent*>(comp))
            AddComponent<RendererComponent>(r);
        else if (auto sr = dynamic_cast<SpriteRendererComponent*>(comp))
            AddComponent<SpriteRendererComponent>(sr);
        else if (auto rb = dynamic_cast<RigidbodyComponent*>(comp))
            AddComponent<RigidbodyComponent>(rb);
        else if (auto col = dynamic_cast<ColliderComponent*>(comp))
            AddComponent<ColliderComponent>(col);
        else if (auto rb2d = dynamic_cast<Rigidbody2DComponent*>(comp))
            AddComponent<Rigidbody2DComponent>(rb2d);
        else if (auto col2d = dynamic_cast<Collider2DComponent*>(comp))
            AddComponent<Collider2DComponent>(col2d);
        else if (auto canvas = dynamic_cast<CanvasComponent*>(comp))
            AddComponent<CanvasComponent>(canvas);
        else if (auto rect = dynamic_cast<RectTransformComponent*>(comp))
            AddComponent<RectTransformComponent>(rect);
        else if (auto anim = dynamic_cast<AnimatorComponent*>(comp))
            AddComponent<AnimatorComponent>(anim);
        else if (auto ps = dynamic_cast<ParticleSystemComponent*>(comp))
            AddComponent<ParticleSystemComponent>(ps);
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
    if (this->IsDescendantOf(child.get())) return nullptr;
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

void GameObject::AddChild(GameObject* existingChild)
{
    if (!existingChild || existingChild == this) return;
    // Cycle guard must run BEFORE detaching (IsDescendantOf walks parents).
    if (this->IsDescendantOf(existingChild)) return;
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
#ifndef DITTO_HEADLESS_TESTS
    if (g_editor && g_editor->selectedComponent == component)
        g_editor->selectedComponent = nullptr;
#endif
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
#ifdef DITTO_HEADLESS_TESTS
    ProcessRemovals();
#else
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
#endif
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
        case CI::Camera:       newComp = std::make_unique<CameraComponent>(); break;
        case CI::Renderer:     newComp = std::make_unique<RendererComponent>(); break;
        case CI::SpriteRenderer: newComp = std::make_unique<SpriteRendererComponent>(); break;
        case CI::Rigidbody:    newComp = std::make_unique<RigidbodyComponent>(); break;
        case CI::Collider:     newComp = std::make_unique<ColliderComponent>(); break;
        case CI::AudioSource:  newComp = std::make_unique<AudioSourceComponent>(); break;
        case CI::UIImage:      newComp = std::make_unique<UIImageComponent>(); break;
        case CI::UIText:       newComp = std::make_unique<UITextComponent>(); break;
        case CI::UIButton:     newComp = std::make_unique<UIButtonComponent>(); break;
        case CI::CSharpScript: newComp = std::make_unique<CSharpScriptComponent>(); break;
        case CI::Rigidbody2D:  newComp = std::make_unique<Rigidbody2DComponent>(); break;
        case CI::Collider2D:   newComp = std::make_unique<Collider2DComponent>(); break;
        case CI::Canvas:       newComp = std::make_unique<CanvasComponent>(); break;
        case CI::RectTransform: newComp = std::make_unique<RectTransformComponent>(); break;
        case CI::Animator:     newComp = std::make_unique<AnimatorComponent>(); break;
        case CI::ParticleSystem: newComp = std::make_unique<ParticleSystemComponent>(); break;
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
#ifdef DITTO_HEADLESS_TESTS
    UpdateTransform();
#else
    if (!DrawComponentHeader(this, "Transform", false)) return;

    ImGui::Indent(20.0f);
    UnityLabel("Position");
    if (ImGui::DragFloat3("##TransformPosition", &position.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    UnityLabel("Rotation");
    if (ImGui::DragFloat3("##TransformRotation", &rotation.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    UnityLabel("Scale");
    if (ImGui::DragFloat3("##TransformScale", &scale.x, 0.1f)) localDirty = true;
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
#endif
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
LightComponent::LightComponent(LightComponent* other)
    : type(other->type), color(other->color), intensity(other->intensity),
    range(other->range), spotAngle(other->spotAngle), indirectMultiplier(other->indirectMultiplier),
    castShadows(other->castShadows)
{
    index = CI::Light;
}
void LightComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    if (!DrawComponentHeader(this, "Light")) return;
    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Directional", "Point", "Spot", "Area" };
    int typeIndex = static_cast<int>(type);
    if (UnityCombo("Type", &typeIndex, typeNames, 4, "##LightType"))
        type = static_cast<Type>(typeIndex);
    UnityColor3("Color", &color, "##LightColor");
    const char* modeNames[] = { "Realtime" };
    int mode = 0;
    UnityCombo("Mode", &mode, modeNames, 1, "##LightMode");
    UnityDragFloat("Intensity", &intensity, "##LightIntensity", 0.1f, 0.0f, 100.0f);
    if (type == Point || type == Spot)
        UnityDragFloat("Range", &range, "##LightRange", 0.1f, 0.0f, 10000.0f);
    if (type == Spot)
        UnityDragFloat("Spot Angle", &spotAngle, "##LightSpotAngle", 0.1f, 1.0f, 179.0f);
    UnityDragFloat("Indirect Multiplier", &indirectMultiplier, "##LightIndirectMultiplier", 0.1f, 0.0f, 100.0f);
    const char* shadowNames[] = { "No Shadows", "Hard Shadows" };
    int shadow = castShadows ? 1 : 0;
    if (UnityCombo("Shadow Type", &shadow, shadowNames, 2, "##LightShadowType"))
        castShadows = shadow != 0;
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}
void LightComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&intensity), sizeof(intensity));
    file.write(reinterpret_cast<const char*>(&range), sizeof(range));
    file.write(reinterpret_cast<const char*>(&spotAngle), sizeof(spotAngle));
    file.write(reinterpret_cast<const char*>(&indirectMultiplier), sizeof(indirectMultiplier));
    file.write(reinterpret_cast<const char*>(&castShadows), sizeof(castShadows));
}
void LightComponent::Deserialize(std::istream& file)
{
    if (g_sceneLoadingVersion >= 13)
    {
        int32_t typeInt = 0;
        file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
        type = static_cast<Type>(typeInt);
        file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(&intensity), sizeof(intensity));
        file.read(reinterpret_cast<char*>(&range), sizeof(range));
        file.read(reinterpret_cast<char*>(&spotAngle), sizeof(spotAngle));
        file.read(reinterpret_cast<char*>(&indirectMultiplier), sizeof(indirectMultiplier));
        file.read(reinterpret_cast<char*>(&castShadows), sizeof(castShadows));
    }
    else
    {
        type = Directional;
        file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(&intensity), sizeof(intensity));
        range = 10.0f;
        spotAngle = 30.0f;
        indirectMultiplier = 1.0f;
        castShadows = false;
    }
}

CameraComponent::CameraComponent()
{
    index = CI::Camera;
}

CameraComponent::CameraComponent(CameraComponent* other)
    : mainCamera(other->mainCamera), clearFlags(other->clearFlags), projectionType(other->projectionType),
    fieldOfView(other->fieldOfView), orthographicSize(other->orthographicSize),
    nearClipPlane(other->nearClipPlane), farClipPlane(other->farClipPlane),
    backgroundColor(other->backgroundColor), viewportRect(other->viewportRect), depth(other->depth),
    occlusionCulling(other->occlusionCulling), allowHDR(other->allowHDR), allowMSAA(other->allowMSAA)
{
    index = CI::Camera;
}

Camera CameraComponent::ToCamera(const TransformComponent* transform) const
{
    glm::vec3 position(0.0f);
    glm::vec3 forward(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);

    if (transform)
    {
        glm::mat4 world = transform->GetWorldModel();
        position = glm::vec3(world[3]);
        glm::vec3 worldForward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 worldUp = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::length(worldForward) > 0.0001f)
            forward = glm::normalize(worldForward);
        if (glm::length(worldUp) > 0.0001f)
            cameraUp = glm::normalize(worldUp);
    }

    Camera camera(position, position + forward, cameraUp);
    camera.projectionType = projectionType;
    camera.fieldOfView = fieldOfView;
    camera.orthographicSize = orthographicSize;
    camera.nearClipPlane = nearClipPlane;
    camera.farClipPlane = farClipPlane;
    camera.backgroundColor = backgroundColor;
    return camera;
}

void CameraComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    if (!DrawComponentHeader(this, "Camera")) return;

    ImGui::Indent(20.0f);
    UnityCheckbox("Main Camera", &mainCamera, "##CameraMain");
    const char* clearNames[] = { "Skybox", "Solid Color", "Depth Only", "Don't Clear" };
    int clearIndex = static_cast<int>(clearFlags);
    if (UnityCombo("Clear Flags", &clearIndex, clearNames, 4, "##CameraClearFlags"))
        clearFlags = static_cast<ClearFlags>(clearIndex);
    if (clearFlags == SolidColor)
        UnityColor4("Background", &backgroundColor, "##CameraBackground");

    const char* projectionNames[] = { "Perspective", "Orthographic" };
    int projection = projectionType == Camera::ProjectionType::Orthographic ? 1 : 0;
    if (UnityCombo("Projection", &projection, projectionNames, 2, "##CameraProjection"))
        projectionType = projection == 1 ? Camera::ProjectionType::Orthographic : Camera::ProjectionType::Perspective;

    if (projectionType == Camera::ProjectionType::Perspective)
    {
        UnityLabel("Field of View");
        if (ImGui::DragFloat("##CameraFOV", &fieldOfView, 0.1f, 1.0f, 179.0f))
            fieldOfView = std::clamp(fieldOfView, 1.0f, 179.0f);
        TrackUndoableEdit();
    }
    else
    {
        UnityLabel("Size");
        if (ImGui::DragFloat("##CameraOrthoSize", &orthographicSize, 0.1f, 0.0001f, 10000.0f))
            orthographicSize = std::max(0.0001f, orthographicSize);
        TrackUndoableEdit();
    }

    ImGui::TextUnformatted("Clipping Planes");
    ImGui::Indent(12.0f);
    UnityLabel("Near");
    if (ImGui::DragFloat("##CameraNear", &nearClipPlane, 0.01f, 0.0001f, farClipPlane - 0.0001f))
        nearClipPlane = std::max(0.0001f, nearClipPlane);
    TrackUndoableEdit();
    UnityLabel("Far");
    if (ImGui::DragFloat("##CameraFar", &farClipPlane, 0.1f, nearClipPlane + 0.0001f, 100000.0f))
        farClipPlane = std::max(nearClipPlane + 0.0001f, farClipPlane);
    TrackUndoableEdit();
    ImGui::Unindent(12.0f);

    UnityLabel("Viewport Rect");
    ImGui::DragFloat4("##CameraViewportRect", &viewportRect.x, 0.01f, 0.0f, 1.0f);
    TrackUndoableEdit();
    UnityDragFloat("Depth", &depth, "##CameraDepth", 1.0f, -100.0f, 100.0f);
    const char* renderingPath[] = { "Use Graphics Settings" };
    int renderingPathIndex = 0;
    UnityCombo("Rendering Path", &renderingPathIndex, renderingPath, 1, "##CameraRenderingPath");
    const char* targetTexture[] = { "None (Render Texture)" };
    int targetTextureIndex = 0;
    UnityCombo("Target Texture", &targetTextureIndex, targetTexture, 1, "##CameraTargetTexture");
    UnityCheckbox("Occlusion Culling", &occlusionCulling, "##CameraOcclusionCulling");
    UnityCheckbox("HDR", &allowHDR, "##CameraHDR");
    UnityCheckbox("MSAA", &allowMSAA, "##CameraMSAA");

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void CameraComponent::Serialize(std::ostream& file) const
{
    int32_t clearFlagsInt = static_cast<int32_t>(clearFlags);
    file.write(reinterpret_cast<const char*>(&clearFlagsInt), sizeof(clearFlagsInt));
    file.write(reinterpret_cast<const char*>(&mainCamera), sizeof(mainCamera));
    int32_t projection = static_cast<int32_t>(projectionType);
    file.write(reinterpret_cast<const char*>(&projection), sizeof(projection));
    file.write(reinterpret_cast<const char*>(&fieldOfView), sizeof(fieldOfView));
    file.write(reinterpret_cast<const char*>(&orthographicSize), sizeof(orthographicSize));
    file.write(reinterpret_cast<const char*>(&nearClipPlane), sizeof(nearClipPlane));
    file.write(reinterpret_cast<const char*>(&farClipPlane), sizeof(farClipPlane));
    file.write(reinterpret_cast<const char*>(&backgroundColor), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&viewportRect), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&depth), sizeof(depth));
    file.write(reinterpret_cast<const char*>(&occlusionCulling), sizeof(occlusionCulling));
    file.write(reinterpret_cast<const char*>(&allowHDR), sizeof(allowHDR));
    file.write(reinterpret_cast<const char*>(&allowMSAA), sizeof(allowMSAA));
}

void CameraComponent::Deserialize(std::istream& file)
{
    if (g_sceneLoadingVersion >= 13)
    {
        int32_t clearFlagsInt = 1;
        file.read(reinterpret_cast<char*>(&clearFlagsInt), sizeof(clearFlagsInt));
        clearFlags = static_cast<ClearFlags>(clearFlagsInt);
    }
    else
    {
        clearFlags = SolidColor;
    }
    file.read(reinterpret_cast<char*>(&mainCamera), sizeof(mainCamera));
    int32_t projection = 0;
    file.read(reinterpret_cast<char*>(&projection), sizeof(projection));
    projectionType = projection == 1 ? Camera::ProjectionType::Orthographic : Camera::ProjectionType::Perspective;
    file.read(reinterpret_cast<char*>(&fieldOfView), sizeof(fieldOfView));
    file.read(reinterpret_cast<char*>(&orthographicSize), sizeof(orthographicSize));
    file.read(reinterpret_cast<char*>(&nearClipPlane), sizeof(nearClipPlane));
    file.read(reinterpret_cast<char*>(&farClipPlane), sizeof(farClipPlane));
    file.read(reinterpret_cast<char*>(&backgroundColor), sizeof(glm::vec4));
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&viewportRect), sizeof(glm::vec4));
        file.read(reinterpret_cast<char*>(&depth), sizeof(depth));
        file.read(reinterpret_cast<char*>(&occlusionCulling), sizeof(occlusionCulling));
        file.read(reinterpret_cast<char*>(&allowHDR), sizeof(allowHDR));
        file.read(reinterpret_cast<char*>(&allowMSAA), sizeof(allowMSAA));
    }
    else
    {
        viewportRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        depth = 0.0f;
        occlusionCulling = true;
        allowHDR = true;
        allowMSAA = true;
    }
}

RendererComponent::RendererComponent()
    : color(1.0f, 1.0f, 1.0f, 1.0f), shaderName(DefaultShaderName)
{
    index = CI::Renderer;
}
RendererComponent::RendererComponent(RendererComponent* other)
    : color(other->color),
    materialPath(other->materialPath), shaderName(other->shaderName),
    mainTexturePath(other->mainTexturePath), shadowCastingMode(other->shadowCastingMode),
    receiveShadows(other->receiveShadows), staticShadowCaster(other->staticShadowCaster),
    contributeGI(other->contributeGI), lightProbeUsage(other->lightProbeUsage),
    reflectionProbeUsage(other->reflectionProbeUsage), motionVectors(other->motionVectors),
    dynamicOcclusion(other->dynamicOcclusion), renderingLayerMask(other->renderingLayerMask),
    meshPath(other->meshPath)
{
    index = CI::Renderer;
}

void RendererComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::TextUnformatted("Mesh Filter");
    SelectComponentOnLastItem(this);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Mesh", meshPath, "RendererMeshObjectPopup",
        "Select Mesh", { ".obj", ".fbx", ".mesh" }, "None (Mesh)");

    ImGui::Unindent(20.0f);
    ImGui::Separator();

    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Mesh Renderer");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    ImGui::TextUnformatted("Materials");
    ImGui::Indent(12.0f);

    DrawAssetObjectField("Element 0", materialPath, "RendererMaterialObjectPopup",
        "Select Material", { ".mat" }, "Default-Material");

    if (!materialPath.empty() && !Ditto::LoadMaterialAsset(materialPath).ok)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Material load failed");

    ImGui::Unindent(12.0f);
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* shadowNames[] = { "Off", "On", "Two Sided", "Shadows Only" };
        int shadowIndex = static_cast<int>(shadowCastingMode);
        if (UnityCombo("Cast Shadows", &shadowIndex, shadowNames, 4, "##RendererCastShadows"))
            shadowCastingMode = static_cast<ShadowCastingMode>(shadowIndex);
        UnityCheckbox("Receive Shadows", &receiveShadows, "##RendererReceiveShadows");
        UnityCheckbox("Static Shadow Caster", &staticShadowCaster, "##RendererStaticShadowCaster");
        UnityCheckbox("Contribute Global Illumination", &contributeGI, "##RendererContributeGI");
    }
    if (ImGui::CollapsingHeader("Probes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* lightProbeNames[] = { "Off", "Blend Probes", "Use Proxy Volume", "Custom Provided" };
        UnityCombo("Light Probes", &lightProbeUsage, lightProbeNames, 4, "##RendererLightProbes");
        const char* reflectionProbeNames[] = { "Off", "Blend Probes", "Blend Probes And Skybox", "Simple" };
        UnityCombo("Reflection Probes", &reflectionProbeUsage, reflectionProbeNames, 4, "##RendererReflectionProbes");
        const char* anchorNames[] = { "None (Transform)" };
        int anchor = 0;
        UnityCombo("Anchor Override", &anchor, anchorNames, 1, "##RendererAnchorOverride");
    }
    if (ImGui::CollapsingHeader("Additional Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* motionNames[] = { "Camera Motion Only", "Per Object Motion", "Force No Motion" };
        UnityCombo("Motion Vectors", &motionVectors, motionNames, 3, "##RendererMotionVectors");
        UnityCheckbox("Dynamic Occlusion", &dynamicOcclusion, "##RendererDynamicOcclusion");
        int layerMask = static_cast<int>(renderingLayerMask);
        UnityLabel("Rendering Layer Mask");
        if (ImGui::InputInt("##RendererLayerMask", &layerMask))
            renderingLayerMask = static_cast<uint32_t>(std::max(0, layerMask));
        TrackUndoableEdit();
    }
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}
void RendererComponent::Serialize(std::ostream& file) const
{
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    WriteAssetPathString(file, meshPath);
    WriteString(file, shaderName.empty() ? DefaultShaderName : shaderName);
    WriteAssetPathString(file, mainTexturePath);
    WriteAssetPathString(file, materialPath);
    int32_t shadowModeInt = static_cast<int32_t>(shadowCastingMode);
    file.write(reinterpret_cast<const char*>(&shadowModeInt), sizeof(shadowModeInt));
    file.write(reinterpret_cast<const char*>(&receiveShadows), sizeof(receiveShadows));
    file.write(reinterpret_cast<const char*>(&staticShadowCaster), sizeof(staticShadowCaster));
    file.write(reinterpret_cast<const char*>(&contributeGI), sizeof(contributeGI));
    file.write(reinterpret_cast<const char*>(&lightProbeUsage), sizeof(lightProbeUsage));
    file.write(reinterpret_cast<const char*>(&reflectionProbeUsage), sizeof(reflectionProbeUsage));
    file.write(reinterpret_cast<const char*>(&motionVectors), sizeof(motionVectors));
    file.write(reinterpret_cast<const char*>(&dynamicOcclusion), sizeof(dynamicOcclusion));
    file.write(reinterpret_cast<const char*>(&renderingLayerMask), sizeof(renderingLayerMask));
}
void RendererComponent::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    meshPath = ReadString(file);
    shaderName = ReadString(file);
    if (shaderName.empty()) shaderName = DefaultShaderName;
    mainTexturePath = ReadString(file);
    materialPath = ReadString(file);
    int32_t shadowModeInt = 0;
    file.read(reinterpret_cast<char*>(&shadowModeInt), sizeof(shadowModeInt));
    shadowCastingMode = static_cast<ShadowCastingMode>(shadowModeInt);
    file.read(reinterpret_cast<char*>(&receiveShadows), sizeof(receiveShadows));
    file.read(reinterpret_cast<char*>(&staticShadowCaster), sizeof(staticShadowCaster));
    file.read(reinterpret_cast<char*>(&contributeGI), sizeof(contributeGI));
    file.read(reinterpret_cast<char*>(&lightProbeUsage), sizeof(lightProbeUsage));
    file.read(reinterpret_cast<char*>(&reflectionProbeUsage), sizeof(reflectionProbeUsage));
    file.read(reinterpret_cast<char*>(&motionVectors), sizeof(motionVectors));
    file.read(reinterpret_cast<char*>(&dynamicOcclusion), sizeof(dynamicOcclusion));
    file.read(reinterpret_cast<char*>(&renderingLayerMask), sizeof(renderingLayerMask));
}

SpriteRendererComponent::SpriteRendererComponent()
{
    index = CI::SpriteRenderer;
}

SpriteRendererComponent::SpriteRendererComponent(SpriteRendererComponent* other)
    : color(other->color), spritePath(other->spritePath), materialPath(other->materialPath),
    shaderName(other->shaderName), flipX(other->flipX), flipY(other->flipY), drawMode(other->drawMode),
    size(other->size), maskInteraction(other->maskInteraction), sortingLayer(other->sortingLayer),
    sortingOrder(other->sortingOrder), spriteSortPoint(other->spriteSortPoint)
{
    index = CI::SpriteRenderer;
}

void SpriteRendererComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Sprite Renderer");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Sprite", spritePath, "SpriteRendererSpriteObjectPopup",
        "Select Sprite", { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" }, "None (Sprite)");

    UnityColor4("Color", &color, "##SpriteRendererColor");
    UnityLabel("Flip");
    ImGui::Checkbox("X##SpriteRendererFlipX", &flipX);
    ImGui::SameLine();
    ImGui::Checkbox("Y##SpriteRendererFlipY", &flipY);
    TrackUndoableEdit();
    const char* drawModeNames[] = { "Simple", "Sliced", "Tiled" };
    int drawModeIndex = static_cast<int>(drawMode);
    if (UnityCombo("Draw Mode", &drawModeIndex, drawModeNames, 3, "##SpriteRendererDrawMode"))
        drawMode = static_cast<DrawMode>(drawModeIndex);
    if (drawMode != Simple)
        UnityDragFloat2("Size", &size, "##SpriteRendererSize", 0.05f, 0.0f, 10000.0f);

    ImGui::TextUnformatted("Materials");
    ImGui::Indent(12.0f);

    DrawAssetObjectField("Element 0", materialPath, "SpriteRendererMaterialObjectPopup",
        "Select Material", { ".mat" }, "Default-Sprite-Material");

    if (!materialPath.empty() && !Ditto::LoadMaterialAsset(materialPath).ok)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Material load failed");

    ImGui::Unindent(12.0f);
    const char* maskNames[] = { "None", "Visible Inside Mask", "Visible Outside Mask" };
    int maskIndex = static_cast<int>(maskInteraction);
    if (UnityCombo("Mask Interaction", &maskIndex, maskNames, 3, "##SpriteRendererMaskInteraction"))
        maskInteraction = static_cast<MaskInteraction>(maskIndex);
    const char* sortingLayerNames[] = { "Default" };
    UnityCombo("Sorting Layer", &sortingLayer, sortingLayerNames, 1, "##SpriteRendererSortingLayer");
    UnityLabel("Order in Layer");
    ImGui::DragInt("##SpriteRendererSortingOrder", &sortingOrder, 1.0f, -32768, 32767);
    TrackUndoableEdit();
    const char* sortPointNames[] = { "Center", "Pivot" };
    int sortPointIndex = static_cast<int>(spriteSortPoint);
    if (UnityCombo("Sprite Sort Point", &sortPointIndex, sortPointNames, 2, "##SpriteRendererSortPoint"))
        spriteSortPoint = static_cast<SpriteSortPoint>(sortPointIndex);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void SpriteRendererComponent::Serialize(std::ostream& file) const
{
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    WriteAssetPathString(file, spritePath);
    WriteAssetPathString(file, materialPath);
    WriteString(file, shaderName.empty() ? DefaultShaderName : shaderName);
    file.write(reinterpret_cast<const char*>(&flipX), sizeof(flipX));
    file.write(reinterpret_cast<const char*>(&flipY), sizeof(flipY));
    int32_t drawModeInt = static_cast<int32_t>(drawMode);
    file.write(reinterpret_cast<const char*>(&drawModeInt), sizeof(drawModeInt));
    file.write(reinterpret_cast<const char*>(&size), sizeof(glm::vec2));
    int32_t maskInteractionInt = static_cast<int32_t>(maskInteraction);
    file.write(reinterpret_cast<const char*>(&maskInteractionInt), sizeof(maskInteractionInt));
    file.write(reinterpret_cast<const char*>(&sortingLayer), sizeof(sortingLayer));
    file.write(reinterpret_cast<const char*>(&sortingOrder), sizeof(sortingOrder));
    int32_t sortPointInt = static_cast<int32_t>(spriteSortPoint);
    file.write(reinterpret_cast<const char*>(&sortPointInt), sizeof(sortPointInt));
}

void SpriteRendererComponent::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    spritePath = ReadString(file);
    materialPath = ReadString(file);
    shaderName = ReadString(file);
    if (shaderName.empty()) shaderName = DefaultShaderName;
    file.read(reinterpret_cast<char*>(&flipX), sizeof(flipX));
    file.read(reinterpret_cast<char*>(&flipY), sizeof(flipY));
    int32_t drawModeInt = 0;
    file.read(reinterpret_cast<char*>(&drawModeInt), sizeof(drawModeInt));
    drawMode = static_cast<DrawMode>(drawModeInt);
    file.read(reinterpret_cast<char*>(&size), sizeof(glm::vec2));
    int32_t maskInteractionInt = 0;
    file.read(reinterpret_cast<char*>(&maskInteractionInt), sizeof(maskInteractionInt));
    maskInteraction = static_cast<MaskInteraction>(maskInteractionInt);
    file.read(reinterpret_cast<char*>(&sortingLayer), sizeof(sortingLayer));
    file.read(reinterpret_cast<char*>(&sortingOrder), sizeof(sortingOrder));
    int32_t sortPointInt = 0;
    file.read(reinterpret_cast<char*>(&sortPointInt), sizeof(sortPointInt));
    spriteSortPoint = static_cast<SpriteSortPoint>(sortPointInt);
}

RigidbodyComponent::RigidbodyComponent()
    : type(Dynamic), mass(1.0f), useGravity(true), damp(0.05f), angularDamp(0.05f),
    velocity(0.0f), angularVelocity(0.0f) {
    index = CI::Rigidbody;
}
RigidbodyComponent::RigidbodyComponent(RigidbodyComponent* other)
    : type(other->type), mass(other->mass), useGravity(other->useGravity),
    damp(other->damp), angularDamp(other->angularDamp), isKinematic(other->isKinematic),
    interpolate(other->interpolate), collisionDetection(other->collisionDetection),
    velocity(0.0f), angularVelocity(0.0f) {
    std::copy(std::begin(other->freezePosition), std::end(other->freezePosition), std::begin(freezePosition));
    std::copy(std::begin(other->freezeRotation), std::end(other->freezeRotation), std::begin(freezeRotation));
    index = CI::Rigidbody;
}
void RigidbodyComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
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
    if (UnityCombo("Type", &currentType, typeNames, 3, "##RigidbodyType"))
    {
        type = static_cast<Type>(currentType);
        isKinematic = type == Kinematic;
    }
    UnityDragFloat("Mass", &mass, "##RigidbodyMass", 0.1f, 0.001f, 100000.0f);
    UnityDragFloat("Drag", &damp, "##RigidbodyDrag", 0.01f, 0.0f, 100.0f);
    UnityDragFloat("Angular Drag", &angularDamp, "##RigidbodyAngularDrag", 0.01f, 0.0f, 100.0f);
    UnityCheckbox("Use Gravity", &useGravity, "##RigidbodyUseGravity");
    bool kinematic = isKinematic || type == Kinematic;
    UnityCheckbox("Is Kinematic", &kinematic, "##RigidbodyIsKinematic");
    isKinematic = kinematic;
    if (isKinematic) type = Kinematic;
    else if (type == Kinematic) type = Dynamic;
    const char* interpolateNames[] = { "None", "Interpolate", "Extrapolate" };
    UnityCombo("Interpolate", &interpolate, interpolateNames, 3, "##RigidbodyInterpolate");
    const char* collisionNames[] = { "Discrete", "Continuous", "Continuous Dynamic", "Continuous Speculative" };
    UnityCombo("Collision Detection", &collisionDetection, collisionNames, 4, "##RigidbodyCollisionDetection");
    ImGui::TextUnformatted("Constraints");
    ImGui::Indent(12.0f);
    UnityLabel("Freeze Position");
    ImGui::Checkbox("X##RigidbodyFreezePositionX", &freezePosition[0]); ImGui::SameLine();
    ImGui::Checkbox("Y##RigidbodyFreezePositionY", &freezePosition[1]); ImGui::SameLine();
    ImGui::Checkbox("Z##RigidbodyFreezePositionZ", &freezePosition[2]); TrackUndoableEdit();
    UnityLabel("Freeze Rotation");
    ImGui::Checkbox("X##RigidbodyFreezeRotationX", &freezeRotation[0]); ImGui::SameLine();
    ImGui::Checkbox("Y##RigidbodyFreezeRotationY", &freezeRotation[1]); ImGui::SameLine();
    ImGui::Checkbox("Z##RigidbodyFreezeRotationZ", &freezeRotation[2]); TrackUndoableEdit();
    ImGui::Unindent(12.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void RigidbodyComponent::CalculateInertia(const glm::vec3& scale)
{
    // Default to a box inertia tensor. Without a built-in shape hint we
    // assume the bounding box of the (scaled) mesh.
    float w = scale.x, h = scale.y, d = scale.z;
    float Ixx = (1.0f / 12.0f) * mass * (h * h + d * d);
    float Iyy = (1.0f / 12.0f) * mass * (w * w + d * d);
    float Izz = (1.0f / 12.0f) * mass * (w * w + h * h);
    inertia = glm::mat3(0.0f);
    inertia[0][0] = Ixx; inertia[1][1] = Iyy; inertia[2][2] = Izz;
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
    file.write(reinterpret_cast<const char*>(&isKinematic), sizeof(isKinematic));
    file.write(reinterpret_cast<const char*>(&interpolate), sizeof(interpolate));
    file.write(reinterpret_cast<const char*>(&collisionDetection), sizeof(collisionDetection));
    file.write(reinterpret_cast<const char*>(freezePosition), sizeof(freezePosition));
    file.write(reinterpret_cast<const char*>(freezeRotation), sizeof(freezeRotation));
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
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&isKinematic), sizeof(isKinematic));
        file.read(reinterpret_cast<char*>(&interpolate), sizeof(interpolate));
        file.read(reinterpret_cast<char*>(&collisionDetection), sizeof(collisionDetection));
        file.read(reinterpret_cast<char*>(freezePosition), sizeof(freezePosition));
        file.read(reinterpret_cast<char*>(freezeRotation), sizeof(freezeRotation));
        if (isKinematic) type = Kinematic;
    }
    else
    {
        isKinematic = type == Kinematic;
        interpolate = 0;
        collisionDetection = 0;
        std::fill(std::begin(freezePosition), std::end(freezePosition), false);
        std::fill(std::begin(freezeRotation), std::end(freezeRotation), false);
    }
}

ColliderComponent::ColliderComponent(Type _type)
    : type(_type), isTrigger(false), biasPosition(0.0f), biasRotation(0.0f), biasScale(1.0f)
{
    index = CI::Collider;
}

ColliderComponent::ColliderComponent(ColliderComponent* other)
    : type(other->type), isTrigger(other->isTrigger), providesContacts(other->providesContacts),
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
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    const char* colliderTitle = type == Sphere ? "Sphere Collider" : type == MeshConvex ? "Mesh Collider" : "Box Collider";
    ImGui::SameLine(); ImGui::TextUnformatted(colliderTitle);
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    UnityCheckbox("Is Trigger", &isTrigger, "##ColliderTrigger");
    UnityCheckbox("Provides Contacts", &providesContacts, "##ColliderProvidesContacts");
    const char* typeNames[] = { "Box", "Sphere", "Mesh" };
    int currentType = static_cast<int>(type);
    if (UnityCombo("Type", &currentType, typeNames, 3, "##ColliderType"))
        type = static_cast<Type>(currentType);
    UnityDragFloat3("Center", &biasPosition, "##ColliderCenter", 0.05f);

    if (type == MeshConvex)
    {
        DrawAssetObjectField("Mesh", meshPath, "ColliderMeshObjectPopup",
            "Select Mesh", { ".obj", ".fbx", ".mesh" }, "None (Mesh)");
        bool convex = true;
        UnityCheckbox("Convex", &convex, "##ColliderConvex");
    }
    else if (type == Sphere)
    {
        UnityDragFloat("Radius", &biasScale.x, "##SphereColliderRadius", 0.05f, 0.001f, 10000.0f);
        biasScale.y = biasScale.x;
        biasScale.z = biasScale.x;
    }
    else
    {
        UnityDragFloat3("Size", &biasScale, "##BoxColliderSize", 0.05f, 0.001f, 10000.0f);
    }
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
#endif
}

void ColliderComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&isTrigger), sizeof(isTrigger));
    file.write(reinterpret_cast<const char*>(&providesContacts), sizeof(providesContacts));
    file.write(reinterpret_cast<const char*>(&biasPosition), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&biasRotation), sizeof(glm::vec3));
    file.write(reinterpret_cast<const char*>(&biasScale), sizeof(glm::vec3));
    WriteAssetPathString(file, meshPath);
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
    if (g_sceneLoadingVersion >= 13)
        file.read(reinterpret_cast<char*>(&providesContacts), sizeof(providesContacts));
    else
        providesContacts = false;
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

Rigidbody2DComponent::Rigidbody2DComponent()
{
    index = CI::Rigidbody2D;
}

Rigidbody2DComponent::Rigidbody2DComponent(Rigidbody2DComponent* other)
    : type(other->type), materialPath(other->materialPath), simulated(other->simulated),
    useAutoMass(other->useAutoMass), mass(other->mass), useGravity(other->useGravity),
    gravityScale(other->gravityScale), linearDamping(other->linearDamping),
    angularDamping(other->angularDamping), collisionDetection(other->collisionDetection),
    sleepingMode(other->sleepingMode), interpolate(other->interpolate),
    freezePositionX(other->freezePositionX), freezePositionY(other->freezePositionY),
    freezeRotation(other->freezeRotation), velocity(other->velocity),
    angularVelocity(other->angularVelocity)
{
    index = CI::Rigidbody2D;
}

void Rigidbody2DComponent::AddForce(const glm::vec2& force, ForceMode2D mode)
{
    if (type != Dynamic) return;
    if (mode == Impulse)
        velocity += force / glm::max(0.0001f, mass);
    else
        forceAccum += force;
}

void Rigidbody2DComponent::AddTorque(float torque, ForceMode2D mode)
{
    if (type != Dynamic) return;
    if (mode == Impulse)
        angularVelocity += torque / glm::max(0.0001f, mass);
    else
        torqueAccum += torque;
}

void Rigidbody2DComponent::ClearAccumulators()
{
    forceAccum = glm::vec2(0.0f);
    torqueAccum = 0.0f;
}

void Rigidbody2DComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Rigidbody 2D");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Static", "Dynamic", "Kinematic" };
    int currentType = static_cast<int>(type);
    if (UnityCombo("Body Type", &currentType, typeNames, 3, "##Rigidbody2DType"))
        type = static_cast<Type>(currentType);

    if (type == Dynamic)
    {
        DrawAssetObjectField("Material", materialPath, "Rigidbody2DMaterialObjectPopup",
            "Select Physics Material 2D", { ".physmat2d" }, "None (Physics Material 2D)");
        if (!materialPath.empty() && !Ditto::LoadPhysicsMaterial2DAsset(materialPath).ok)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Physics material 2D load failed");
        UnityCheckbox("Simulated", &simulated, "##Rigidbody2DSimulated");
        UnityCheckbox("Use Auto Mass", &useAutoMass, "##Rigidbody2DUseAutoMass");
        UnityDragFloat("Mass", &mass, "##Rigidbody2DMass", 0.1f, 0.001f, 100000.0f);
        UnityDragFloat("Linear Damping", &linearDamping, "##Rigidbody2DLinearDamping", 0.01f, 0.0f, 100.0f);
        UnityDragFloat("Angular Damping", &angularDamping, "##Rigidbody2DAngularDamping", 0.01f, 0.0f, 100.0f);
        UnityDragFloat("Gravity Scale", &gravityScale, "##Rigidbody2DGravityScale", 0.05f, -100.0f, 100.0f);
        const char* collisionNames[] = { "Discrete", "Continuous" };
        UnityCombo("Collision Detection", &collisionDetection, collisionNames, 2, "##Rigidbody2DCollisionDetection");
        const char* sleepingNames[] = { "Never Sleep", "Start Awake", "Start Asleep" };
        UnityCombo("Sleeping Mode", &sleepingMode, sleepingNames, 3, "##Rigidbody2DSleepingMode");
        const char* interpolateNames[] = { "None", "Interpolate", "Extrapolate" };
        UnityCombo("Interpolate", &interpolate, interpolateNames, 3, "##Rigidbody2DInterpolate");
    }
    ImGui::TextUnformatted("Constraints");
    ImGui::Indent(12.0f);
    UnityLabel("Freeze Position");
    ImGui::Checkbox("X##Rigidbody2DFreezePositionX", &freezePositionX); ImGui::SameLine();
    ImGui::Checkbox("Y##Rigidbody2DFreezePositionY", &freezePositionY); TrackUndoableEdit();
    UnityCheckbox("Freeze Rotation", &freezeRotation, "##Rigidbody2DFreezeRotation");
    ImGui::Unindent(12.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void Rigidbody2DComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    WriteAssetPathString(file, materialPath);
    file.write(reinterpret_cast<const char*>(&simulated), sizeof(simulated));
    file.write(reinterpret_cast<const char*>(&useAutoMass), sizeof(useAutoMass));
    file.write(reinterpret_cast<const char*>(&mass), sizeof(mass));
    file.write(reinterpret_cast<const char*>(&useGravity), sizeof(useGravity));
    file.write(reinterpret_cast<const char*>(&gravityScale), sizeof(gravityScale));
    file.write(reinterpret_cast<const char*>(&linearDamping), sizeof(linearDamping));
    file.write(reinterpret_cast<const char*>(&angularDamping), sizeof(angularDamping));
    file.write(reinterpret_cast<const char*>(&collisionDetection), sizeof(collisionDetection));
    file.write(reinterpret_cast<const char*>(&sleepingMode), sizeof(sleepingMode));
    file.write(reinterpret_cast<const char*>(&interpolate), sizeof(interpolate));
    file.write(reinterpret_cast<const char*>(&freezePositionX), sizeof(freezePositionX));
    file.write(reinterpret_cast<const char*>(&freezePositionY), sizeof(freezePositionY));
    file.write(reinterpret_cast<const char*>(&freezeRotation), sizeof(freezeRotation));
    file.write(reinterpret_cast<const char*>(&velocity), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&angularVelocity), sizeof(angularVelocity));
}

void Rigidbody2DComponent::Deserialize(std::istream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    if (g_sceneLoadingVersion >= 13)
    {
        materialPath = ReadString(file);
        file.read(reinterpret_cast<char*>(&simulated), sizeof(simulated));
        file.read(reinterpret_cast<char*>(&useAutoMass), sizeof(useAutoMass));
    }
    else
    {
        materialPath.clear();
        simulated = true;
        useAutoMass = false;
    }
    file.read(reinterpret_cast<char*>(&mass), sizeof(mass));
    file.read(reinterpret_cast<char*>(&useGravity), sizeof(useGravity));
    file.read(reinterpret_cast<char*>(&gravityScale), sizeof(gravityScale));
    file.read(reinterpret_cast<char*>(&linearDamping), sizeof(linearDamping));
    file.read(reinterpret_cast<char*>(&angularDamping), sizeof(angularDamping));
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&collisionDetection), sizeof(collisionDetection));
        file.read(reinterpret_cast<char*>(&sleepingMode), sizeof(sleepingMode));
        file.read(reinterpret_cast<char*>(&interpolate), sizeof(interpolate));
        file.read(reinterpret_cast<char*>(&freezePositionX), sizeof(freezePositionX));
        file.read(reinterpret_cast<char*>(&freezePositionY), sizeof(freezePositionY));
        file.read(reinterpret_cast<char*>(&freezeRotation), sizeof(freezeRotation));
    }
    else
    {
        collisionDetection = 0;
        sleepingMode = 0;
        interpolate = 0;
        freezePositionX = false;
        freezePositionY = false;
        freezeRotation = false;
    }
    file.read(reinterpret_cast<char*>(&velocity), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&angularVelocity), sizeof(angularVelocity));
}

Collider2DComponent::Collider2DComponent(Type _type)
    : type(_type)
{
    index = CI::Collider2D;
}

Collider2DComponent::Collider2DComponent(Collider2DComponent* other)
    : type(other->type), isTrigger(other->isTrigger), usedByEffector(other->usedByEffector),
    usedByComposite(other->usedByComposite), offset(other->offset),
    size(other->size), radius(other->radius), restitution(other->restitution),
    friction(other->friction)
{
    index = CI::Collider2D;
}

void Collider2DComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted(type == Circle ? "Circle Collider 2D" : "Box Collider 2D");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    UnityCheckbox("Is Trigger", &isTrigger, "##Collider2DTrigger");
    UnityCheckbox("Used By Effector", &usedByEffector, "##Collider2DUsedByEffector");
    UnityCheckbox("Used By Composite", &usedByComposite, "##Collider2DUsedByComposite");

    const char* typeNames[] = { "Box", "Circle" };
    int currentType = static_cast<int>(type);
    if (UnityCombo("Type", &currentType, typeNames, 2, "##Collider2DType"))
        type = static_cast<Type>(currentType);

    UnityDragFloat2("Offset", &offset, "##Collider2DOffset", 0.05f);

    if (type == Box)
        UnityDragFloat2("Size", &size, "##Collider2DSize", 0.05f, 0.001f, 10000.0f);
    else
        UnityDragFloat("Radius", &radius, "##Collider2DRadius", 0.05f, 0.001f, 10000.0f);

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void Collider2DComponent::Serialize(std::ostream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&isTrigger), sizeof(isTrigger));
    file.write(reinterpret_cast<const char*>(&usedByEffector), sizeof(usedByEffector));
    file.write(reinterpret_cast<const char*>(&usedByComposite), sizeof(usedByComposite));
    file.write(reinterpret_cast<const char*>(&offset), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&size), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&radius), sizeof(radius));
    file.write(reinterpret_cast<const char*>(&restitution), sizeof(restitution));
    file.write(reinterpret_cast<const char*>(&friction), sizeof(friction));
}

void Collider2DComponent::Deserialize(std::istream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(&isTrigger), sizeof(isTrigger));
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&usedByEffector), sizeof(usedByEffector));
        file.read(reinterpret_cast<char*>(&usedByComposite), sizeof(usedByComposite));
    }
    else
    {
        usedByEffector = false;
        usedByComposite = false;
    }
    file.read(reinterpret_cast<char*>(&offset), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&size), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&radius), sizeof(radius));
    file.read(reinterpret_cast<char*>(&restitution), sizeof(restitution));
    file.read(reinterpret_cast<char*>(&friction), sizeof(friction));
}

AudioSourceComponent::AudioSourceComponent()
{
    index = CI::AudioSource;
}

AudioSourceComponent::AudioSourceComponent(AudioSourceComponent* other)
    : clipPath(other->clipPath), outputPath(other->outputPath), mute(other->mute),
    bypassEffects(other->bypassEffects), bypassListenerEffects(other->bypassListenerEffects),
    bypassReverbZones(other->bypassReverbZones), volume(other->volume), pitch(other->pitch),
    loop(other->loop), playOnAwake(other->playOnAwake), priority(other->priority),
    stereoPan(other->stereoPan), spatialBlend(other->spatialBlend), reverbZoneMix(other->reverbZoneMix)
{
    index = CI::AudioSource;
}

void AudioSourceComponent::Play()
{
#ifndef DITTO_HEADLESS_TESTS
    if (clipPath.empty()) return;
    Stop();
    std::filesystem::path resolved = Ditto::AssetPath::ResolveAssetPath(clipPath);
    soundHandle = AudioEngine::Play(resolved.string(), volume, loop);
#endif
}

void AudioSourceComponent::Stop()
{
#ifndef DITTO_HEADLESS_TESTS
    if (soundHandle != 0)
    {
        AudioEngine::Stop(soundHandle);
        soundHandle = 0;
    }
#endif
}

void AudioSourceComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Audio Source");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);

    DrawAssetObjectField("Audio Clip", clipPath, "AudioClipObjectPopup",
        "Select Audio Clip", { ".wav", ".mp3", ".ogg", ".flac" }, "None (Audio Clip)");

    UnityLabel("Output");
    ImGui::TextDisabled("None (Audio Mixer Group)");
    UnityCheckbox("Mute", &mute, "##AudioMute");
    UnityCheckbox("Bypass Effects", &bypassEffects, "##AudioBypassEffects");
    UnityCheckbox("Bypass Listener Effects", &bypassListenerEffects, "##AudioBypassListenerEffects");
    UnityCheckbox("Bypass Reverb Zones", &bypassReverbZones, "##AudioBypassReverbZones");
    UnityCheckbox("Play On Awake", &playOnAwake, "##AudioPlayOnAwake");
    UnityCheckbox("Loop", &loop, "##AudioLoop");
    UnityLabel("Priority");
    ImGui::DragInt("##AudioPriority", &priority, 1.0f, 0, 256);
    priority = std::clamp(priority, 0, 256);
    TrackUndoableEdit();

    UnityLabel("Volume");
    if (ImGui::SliderFloat("##AudioVolume", &volume, 0.0f, 1.0f))
    {
        if (soundHandle != 0) AudioEngine::SetVolume(soundHandle, volume);
    }
    TrackUndoableEdit();
    UnityDragFloat("Pitch", &pitch, "##AudioPitch", 0.01f, -3.0f, 3.0f);
    UnityDragFloat("Stereo Pan", &stereoPan, "##AudioStereoPan", 0.01f, -1.0f, 1.0f);
    UnityDragFloat("Spatial Blend", &spatialBlend, "##AudioSpatialBlend", 0.01f, 0.0f, 1.0f);
    UnityDragFloat("Reverb Zone Mix", &reverbZoneMix, "##AudioReverbZoneMix", 0.01f, 0.0f, 1.1f);

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void AudioSourceComponent::Serialize(std::ostream& file) const
{
    WriteAssetPathString(file, clipPath);
    WriteString(file, outputPath);
    file.write(reinterpret_cast<const char*>(&mute), sizeof(mute));
    file.write(reinterpret_cast<const char*>(&bypassEffects), sizeof(bypassEffects));
    file.write(reinterpret_cast<const char*>(&bypassListenerEffects), sizeof(bypassListenerEffects));
    file.write(reinterpret_cast<const char*>(&bypassReverbZones), sizeof(bypassReverbZones));
    file.write(reinterpret_cast<const char*>(&volume), sizeof(volume));
    file.write(reinterpret_cast<const char*>(&pitch), sizeof(pitch));
    file.write(reinterpret_cast<const char*>(&loop), sizeof(loop));
    file.write(reinterpret_cast<const char*>(&playOnAwake), sizeof(playOnAwake));
    file.write(reinterpret_cast<const char*>(&priority), sizeof(priority));
    file.write(reinterpret_cast<const char*>(&stereoPan), sizeof(stereoPan));
    file.write(reinterpret_cast<const char*>(&spatialBlend), sizeof(spatialBlend));
    file.write(reinterpret_cast<const char*>(&reverbZoneMix), sizeof(reverbZoneMix));
}

void AudioSourceComponent::Deserialize(std::istream& file)
{
    clipPath = ReadString(file);
    if (g_sceneLoadingVersion >= 13)
    {
        outputPath = ReadString(file);
        file.read(reinterpret_cast<char*>(&mute), sizeof(mute));
        file.read(reinterpret_cast<char*>(&bypassEffects), sizeof(bypassEffects));
        file.read(reinterpret_cast<char*>(&bypassListenerEffects), sizeof(bypassListenerEffects));
        file.read(reinterpret_cast<char*>(&bypassReverbZones), sizeof(bypassReverbZones));
    }
    else
    {
        outputPath.clear();
        mute = false;
        bypassEffects = false;
        bypassListenerEffects = false;
        bypassReverbZones = false;
    }
    file.read(reinterpret_cast<char*>(&volume), sizeof(volume));
    if (g_sceneLoadingVersion >= 13)
        file.read(reinterpret_cast<char*>(&pitch), sizeof(pitch));
    else
        pitch = 1.0f;
    file.read(reinterpret_cast<char*>(&loop), sizeof(loop));
    file.read(reinterpret_cast<char*>(&playOnAwake), sizeof(playOnAwake));
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&priority), sizeof(priority));
        file.read(reinterpret_cast<char*>(&stereoPan), sizeof(stereoPan));
        file.read(reinterpret_cast<char*>(&spatialBlend), sizeof(spatialBlend));
        file.read(reinterpret_cast<char*>(&reverbZoneMix), sizeof(reverbZoneMix));
    }
    else
    {
        priority = 128;
        stereoPan = 0.0f;
        spatialBlend = 0.0f;
        reverbZoneMix = 1.0f;
    }
}

// ---- UI components ----

CanvasComponent::CanvasComponent()
{
    index = CI::Canvas;
}

CanvasComponent::CanvasComponent(CanvasComponent* other)
    : renderMode(other->renderMode), pixelPerfect(other->pixelPerfect),
    planeDistance(other->planeDistance), sortingOrder(other->sortingOrder)
{
    index = CI::Canvas;
}

void CanvasComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Canvas");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    const char* renderModeNames[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
    int mode = static_cast<int>(renderMode);
    if (UnityCombo("Render Mode", &mode, renderModeNames, 3, "##CanvasRenderMode"))
        renderMode = static_cast<RenderMode>(mode);
    UnityCheckbox("Pixel Perfect", &pixelPerfect, "##CanvasPixelPerfect");
    UnityDragFloat("Plane Distance", &planeDistance, "##CanvasPlaneDistance", 1.0f, 0.0f, 100000.0f);
    UnityLabel("Sorting Order");
    ImGui::DragInt("##CanvasSortingOrder", &sortingOrder, 1.0f, -32768, 32767);
    TrackUndoableEdit();
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void CanvasComponent::Serialize(std::ostream& file) const
{
    int32_t mode = static_cast<int32_t>(renderMode);
    file.write(reinterpret_cast<const char*>(&mode), sizeof(mode));
    file.write(reinterpret_cast<const char*>(&pixelPerfect), sizeof(pixelPerfect));
    file.write(reinterpret_cast<const char*>(&planeDistance), sizeof(planeDistance));
    file.write(reinterpret_cast<const char*>(&sortingOrder), sizeof(sortingOrder));
}

void CanvasComponent::Deserialize(std::istream& file)
{
    int32_t mode = 0;
    file.read(reinterpret_cast<char*>(&mode), sizeof(mode));
    renderMode = static_cast<RenderMode>(mode);
    file.read(reinterpret_cast<char*>(&pixelPerfect), sizeof(pixelPerfect));
    file.read(reinterpret_cast<char*>(&planeDistance), sizeof(planeDistance));
    file.read(reinterpret_cast<char*>(&sortingOrder), sizeof(sortingOrder));
}

RectTransformComponent::RectTransformComponent()
{
    index = CI::RectTransform;
}

RectTransformComponent::RectTransformComponent(RectTransformComponent* other)
    : anchor(other->anchor), anchoredPosition(other->anchoredPosition),
    sizeDelta(other->sizeDelta), pivot(other->pivot)
{
    index = CI::RectTransform;
}

glm::vec4 RectTransformComponent::ComputeRect(float viewW, float viewH) const
{
    glm::vec2 f = UIAnchorFactor(anchor);
    glm::vec2 base = glm::vec2(viewW, viewH) * f;
    glm::vec2 pos = base + anchoredPosition - sizeDelta * pivot;
    return glm::vec4(pos, sizeDelta);
}

void RectTransformComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Rect Transform");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    DrawUIAnchorCombo(anchor); TrackUndoableEdit();
    UnityDragFloat2("Pos", &anchoredPosition, "##RectTransformPos", 1.0f);
    UnityDragFloat2("Width Height", &sizeDelta, "##RectTransformSize", 1.0f, 0.0f, 8192.0f);
    UnityDragFloat2("Pivot", &pivot, "##RectTransformPivot", 0.01f, 0.0f, 1.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void RectTransformComponent::Serialize(std::ostream& file) const
{
    int32_t anchorInt = static_cast<int32_t>(anchor);
    file.write(reinterpret_cast<const char*>(&anchorInt), sizeof(anchorInt));
    file.write(reinterpret_cast<const char*>(&anchoredPosition), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&sizeDelta), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&pivot), sizeof(glm::vec2));
}

void RectTransformComponent::Deserialize(std::istream& file)
{
    int32_t anchorInt = 0;
    file.read(reinterpret_cast<char*>(&anchorInt), sizeof(anchorInt));
    anchor = static_cast<UIAnchor>(anchorInt);
    file.read(reinterpret_cast<char*>(&anchoredPosition), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&sizeDelta), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&pivot), sizeof(glm::vec2));
}

UIImageComponent::UIImageComponent() { index = CI::UIImage; }
UIImageComponent::UIImageComponent(UIImageComponent* other)
    : anchor(other->anchor), offset(other->offset), size(other->size),
    color(other->color), texturePath(other->texturePath), type(other->type),
    raycastTarget(other->raycastTarget), maskable(other->maskable)
{
    index = CI::UIImage;
}

void UIImageComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Image");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Source Image", texturePath, "UIImageTextureObjectPopup",
        "Select Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" }, "None (Texture2D)");
    UnityColor4("Color", &color, "##UIImgColor");
    const char* imageTypeNames[] = { "Simple", "Sliced", "Tiled", "Filled" };
    int imageTypeIndex = static_cast<int>(type);
    if (UnityCombo("Image Type", &imageTypeIndex, imageTypeNames, 4, "##UIImageType"))
        type = static_cast<Type>(imageTypeIndex);
    UnityCheckbox("Raycast Target", &raycastTarget, "##UIImageRaycastTarget");
    UnityCheckbox("Maskable", &maskable, "##UIImageMaskable");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UIImgOffset", 1.0f);
        UnityDragFloat2("Size", &size, "##UIImgSize", 1.0f, 0.0f, 8192.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UIImageComponent::Serialize(std::ostream& file) const
{
    int32_t anchorInt = static_cast<int32_t>(anchor);
    file.write(reinterpret_cast<const char*>(&anchorInt), sizeof(anchorInt));
    file.write(reinterpret_cast<const char*>(&offset), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&size), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    WriteAssetPathString(file, texturePath);
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(&raycastTarget), sizeof(raycastTarget));
    file.write(reinterpret_cast<const char*>(&maskable), sizeof(maskable));
}

void UIImageComponent::Deserialize(std::istream& file)
{
    int32_t anchorInt = 0;
    file.read(reinterpret_cast<char*>(&anchorInt), sizeof(anchorInt));
    anchor = static_cast<UIAnchor>(anchorInt);
    file.read(reinterpret_cast<char*>(&offset), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&size), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    texturePath = ReadString(file);
    if (g_sceneLoadingVersion >= 13)
    {
        int32_t typeInt = 0;
        file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
        type = static_cast<Type>(typeInt);
        file.read(reinterpret_cast<char*>(&raycastTarget), sizeof(raycastTarget));
        file.read(reinterpret_cast<char*>(&maskable), sizeof(maskable));
    }
    else
    {
        type = Simple;
        raycastTarget = true;
        maskable = true;
    }
}

UITextComponent::UITextComponent() { index = CI::UIText; }
UITextComponent::UITextComponent(UITextComponent* other)
    : anchor(other->anchor), offset(other->offset), fontSize(other->fontSize),
    color(other->color), text(other->text), fontPath(other->fontPath),
    fontStyle(other->fontStyle), alignment(other->alignment),
    raycastTarget(other->raycastTarget), maskable(other->maskable)
{
    index = CI::UIText;
}

void UITextComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Text");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    static char textBuf[1024];
    strcpy_s(textBuf, sizeof(textBuf), text.c_str());
    UnityLabel("Text");
    if (ImGui::InputTextMultiline("##UIText", textBuf, sizeof(textBuf),
            ImVec2(-1, ImGui::GetTextLineHeight() * 3)))
        text = textBuf;
    TrackUndoableEdit();

    DrawAssetObjectField("Font", fontPath, "UITextFontObjectPopup",
        "Select Font", { ".ttf", ".otf" }, "Default Font");
    const char* fontStyleNames[] = { "Normal", "Bold", "Italic", "Bold And Italic" };
    UnityCombo("Font Style", &fontStyle, fontStyleNames, 4, "##UITextFontStyle");
    UnityDragFloat("Font Size", &fontSize, "##UITextSize", 0.5f, 4.0f, 256.0f);
    const char* alignmentNames[] = { "Left", "Center", "Right" };
    UnityCombo("Alignment", &alignment, alignmentNames, 3, "##UITextAlignment");
    UnityColor4("Color", &color, "##UITextColor");
    UnityCheckbox("Raycast Target", &raycastTarget, "##UITextRaycastTarget");
    UnityCheckbox("Maskable", &maskable, "##UITextMaskable");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UITextOffset", 1.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UITextComponent::Serialize(std::ostream& file) const
{
    int32_t anchorInt = static_cast<int32_t>(anchor);
    file.write(reinterpret_cast<const char*>(&anchorInt), sizeof(anchorInt));
    file.write(reinterpret_cast<const char*>(&offset), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&fontSize), sizeof(fontSize));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    WriteString(file, text);
    WriteAssetPathString(file, fontPath);
    file.write(reinterpret_cast<const char*>(&fontStyle), sizeof(fontStyle));
    file.write(reinterpret_cast<const char*>(&alignment), sizeof(alignment));
    file.write(reinterpret_cast<const char*>(&raycastTarget), sizeof(raycastTarget));
    file.write(reinterpret_cast<const char*>(&maskable), sizeof(maskable));
}

void UITextComponent::Deserialize(std::istream& file)
{
    int32_t anchorInt = 0;
    file.read(reinterpret_cast<char*>(&anchorInt), sizeof(anchorInt));
    anchor = static_cast<UIAnchor>(anchorInt);
    file.read(reinterpret_cast<char*>(&offset), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&fontSize), sizeof(fontSize));
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    text = ReadString(file);
    if (g_sceneLoadingVersion >= 13)
    {
        fontPath = ReadString(file);
        file.read(reinterpret_cast<char*>(&fontStyle), sizeof(fontStyle));
        file.read(reinterpret_cast<char*>(&alignment), sizeof(alignment));
        file.read(reinterpret_cast<char*>(&raycastTarget), sizeof(raycastTarget));
        file.read(reinterpret_cast<char*>(&maskable), sizeof(maskable));
    }
    else
    {
        fontPath.clear();
        fontStyle = 0;
        alignment = 0;
        raycastTarget = true;
        maskable = true;
    }
}

UIButtonComponent::UIButtonComponent() { index = CI::UIButton; }
UIButtonComponent::UIButtonComponent(UIButtonComponent* other)
    : anchor(other->anchor), offset(other->offset), size(other->size),
    color(other->color), hoverColor(other->hoverColor), pressedColor(other->pressedColor),
    label(other->label), fontSize(other->fontSize), labelColor(other->labelColor),
    interactable(other->interactable), transition(other->transition),
    disabledColor(other->disabledColor), colorMultiplier(other->colorMultiplier),
    fadeDuration(other->fadeDuration)
{
    index = CI::UIButton;
}

void UIButtonComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Button");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (g_editor) g_editor->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    static char labelBuf[256];
    strcpy_s(labelBuf, sizeof(labelBuf), label.c_str());
    UnityCheckbox("Interactable", &interactable, "##UIButtonInteractable");
    const char* transitionNames[] = { "None", "Color Tint" };
    UnityCombo("Transition", &transition, transitionNames, 2, "##UIButtonTransition");
    UnityColor4("Normal Color", &color, "##UIBtnColor");
    UnityColor4("Highlighted Color", &hoverColor, "##UIBtnHover");
    UnityColor4("Pressed Color", &pressedColor, "##UIBtnPressed");
    UnityColor4("Disabled Color", &disabledColor, "##UIBtnDisabled");
    UnityDragFloat("Color Multiplier", &colorMultiplier, "##UIBtnColorMultiplier", 0.01f, 0.0f, 10.0f);
    UnityDragFloat("Fade Duration", &fadeDuration, "##UIBtnFadeDuration", 0.01f, 0.0f, 10.0f);

    UnityLabel("Text");
    if (ImGui::InputText("##UIBtnLabel", labelBuf, sizeof(labelBuf)))
        label = labelBuf;
    TrackUndoableEdit();
    UnityDragFloat("Font Size", &fontSize, "##UIBtnFontSize", 0.5f, 4.0f, 256.0f);
    UnityColor4("Text Color", &labelColor, "##UIBtnLabelColor");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UIBtnOffset", 1.0f);
        UnityDragFloat2("Size", &size, "##UIBtnSize", 1.0f, 0.0f, 8192.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UIButtonComponent::Serialize(std::ostream& file) const
{
    int32_t anchorInt = static_cast<int32_t>(anchor);
    file.write(reinterpret_cast<const char*>(&anchorInt), sizeof(anchorInt));
    file.write(reinterpret_cast<const char*>(&offset), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&size), sizeof(glm::vec2));
    file.write(reinterpret_cast<const char*>(&color), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&hoverColor), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&pressedColor), sizeof(glm::vec4));
    WriteString(file, label);
    file.write(reinterpret_cast<const char*>(&fontSize), sizeof(fontSize));
    file.write(reinterpret_cast<const char*>(&labelColor), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&interactable), sizeof(interactable));
    file.write(reinterpret_cast<const char*>(&transition), sizeof(transition));
    file.write(reinterpret_cast<const char*>(&disabledColor), sizeof(glm::vec4));
    file.write(reinterpret_cast<const char*>(&colorMultiplier), sizeof(colorMultiplier));
    file.write(reinterpret_cast<const char*>(&fadeDuration), sizeof(fadeDuration));
}

void UIButtonComponent::Deserialize(std::istream& file)
{
    int32_t anchorInt = 0;
    file.read(reinterpret_cast<char*>(&anchorInt), sizeof(anchorInt));
    anchor = static_cast<UIAnchor>(anchorInt);
    file.read(reinterpret_cast<char*>(&offset), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&size), sizeof(glm::vec2));
    file.read(reinterpret_cast<char*>(&color), sizeof(glm::vec4));
    file.read(reinterpret_cast<char*>(&hoverColor), sizeof(glm::vec4));
    file.read(reinterpret_cast<char*>(&pressedColor), sizeof(glm::vec4));
    label = ReadString(file);
    file.read(reinterpret_cast<char*>(&fontSize), sizeof(fontSize));
    file.read(reinterpret_cast<char*>(&labelColor), sizeof(glm::vec4));
    if (g_sceneLoadingVersion >= 13)
    {
        file.read(reinterpret_cast<char*>(&interactable), sizeof(interactable));
        file.read(reinterpret_cast<char*>(&transition), sizeof(transition));
        file.read(reinterpret_cast<char*>(&disabledColor), sizeof(glm::vec4));
        file.read(reinterpret_cast<char*>(&colorMultiplier), sizeof(colorMultiplier));
        file.read(reinterpret_cast<char*>(&fadeDuration), sizeof(fadeDuration));
    }
    else
    {
        interactable = true;
        transition = 1;
        disabledColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);
        colorMultiplier = 1.0f;
        fadeDuration = 0.1f;
    }
}

