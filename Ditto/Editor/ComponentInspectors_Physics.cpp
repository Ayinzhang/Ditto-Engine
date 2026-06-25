#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../Engine/Physics/PhysicsMaterial2DAsset.h"
#include "../3rdParty/ImGui/imgui.h"

#include <algorithm>
#include <cstring>

namespace
{
    Editor* CurrentEditor()
    {
        return Ditto::RuntimeContext::CurrentEditor();
    }

    Scene* CurrentScene()
    {
        return Ditto::RuntimeContext::CurrentScene();
    }
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
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
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
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
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
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
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
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
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

#endif



