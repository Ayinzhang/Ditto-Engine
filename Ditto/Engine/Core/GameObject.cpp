#include "GameObject.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"

GameObject::GameObject(const std::string name)
{
	this->name = name;
	this->AddComponent<TransformComponent>();
}

GameObject::~GameObject()
{
	for (Component* comp : components) delete comp;
}

void GameObject::OnInspectorGUI()
{
    // GameObject 的通用属性
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    char nameBuffer[256];
    strcpy_s(nameBuffer, name.c_str());
    ImGui::Text("Name"); ImGui::SameLine();
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) name = nameBuffer;

    //ImGui::SameLine();
    //ImGui::Checkbox("Enabled", &enabled);

    ImGui::Separator();

    for (int i = 0; i < components.size(); i++)
    {
        Component* comp = components[i];
        ImGui::PushID(comp);

        // 完全由组件自己绘制
        comp->OnInspectorGUI();

        ImGui::PopID();

        // 组件之间的分隔线
        ImGui::Separator();
    }

    // 添加组件按钮
    if (ImGui::Button("Add Component"))
    {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        if (!(compMask >> 1 & 1) && ImGui::MenuItem("DirLight")) AddComponent<LightComponent>();
        if (!(compMask >> 2 & 1) && ImGui::MenuItem("Renderer")) AddComponent<RendererComponent>();
        if (!(compMask >> 3 & 1) && ImGui::MenuItem("Rigidbody")) AddComponent<RigidbodyComponent>();
        ImGui::EndPopup();
    }
}

TransformComponent::TransformComponent()
{
    index = 1 << 0;
    position[0] = position[1] = position[2] = 0;
    rotation[0] = rotation[1] = rotation[2] = 0;
    scale[0] = scale[1] = scale[2] = 1;

	lastPosition[0] = lastPosition[1] = lastPosition[2] = 0;
	lastRotation[0] = lastRotation[1] = lastRotation[2] = 0;
	lastScale[0] = lastScale[1] = lastScale[2] = 1;
	UpdateTransform();
}

void TransformComponent::OnInspectorGUI() 
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Transform");

    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    ImGui::Text("Position"); ImGui::SameLine();
    ImGui::DragFloat3("##Position", position, 0.1f);
    ImGui::Text("Rotation"); ImGui::SameLine();
    ImGui::DragFloat3("##Rotation", rotation, 0.1f);
    ImGui::Text("Scale   "); ImGui::SameLine();
    ImGui::DragFloat3("##Scale", scale, 0.1f);
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();

	bool changed = false;
    for (int i = 0; i < 3; i++)
    {
        changed |= (lastPosition[i] != position[i]) || (lastRotation[i] != rotation[i]) || (lastScale[i] != scale[i]);
		lastPosition[i] = position[i]; lastRotation[i] = rotation[i]; lastScale[i] = scale[i];
    }
    if (changed) UpdateTransform();
}

void TransformComponent::UpdateTransform()
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(position[0], position[1], position[2]));

    glm::mat4 rotationMat = glm::mat4(1.0f);
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f)); // Yaw
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
    rotationMat = glm::rotate(rotationMat, glm::radians(rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f)); // Roll

    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scale[0], scale[1], scale[2]));

    model = translation * rotationMat * scaleMat;

    glm::mat3 rotationMatrix3 = glm::mat3(rotationMat);
    forward = rotationMatrix3 * glm::vec3(0.0f, 0.0f, -1.0f);
}

LightComponent::LightComponent()
{
    index = 1 << 1; color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; intensity = 1.0f;
}

void LightComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Light");
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    ImGui::Text("Color    "); ImGui::SameLine();
    ImGui::ColorEdit3("##Color", color);
    ImGui::Text("Intensity"); ImGui::SameLine();
    ImGui::DragFloat("##Intensity", &intensity, 0.1f, 0.0f, 100.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

RendererComponent::RendererComponent() 
{
    index = 1 << 2; type = Cube; color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; color[3] = 1.0f;
}

void RendererComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Renderer");

    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Cube", "Sphere", "Plane"};
    int currentType = static_cast<int>(type);
    ImGui::Text("Type "); ImGui::SameLine();
    if (ImGui::Combo("##Type", &currentType, typeNames, 3))
    {
        type = static_cast<Type>(currentType);
    }
    ImGui::Text("Color"); ImGui::SameLine();
    ImGui::ColorEdit4("##Color", color, ImGuiColorEditFlags_AlphaBar);
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
}

RigidbodyComponent::RigidbodyComponent()
{
    index = 1 << 3; type = Dynamic; mass = 1.0f; useGravity = true;
}

void RigidbodyComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Rigidbody");

    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);

    const char* typeNames[] = { "Static", "Dynamic" };
    int currentType = static_cast<int>(type);
    ImGui::Text("Type"); ImGui::SameLine();
    if (ImGui::Combo("##Type", &currentType, typeNames, 2))
    {
        type = static_cast<Type>(currentType);
    }

    if (type == Dynamic)
    {
        ImGui::Text("Use Gravity"); ImGui::SameLine();
        ImGui::Checkbox("##Use Gravity", &useGravity);
        ImGui::Text("Mass"); ImGui::SameLine();
        ImGui::DragFloat("##Mass", &mass, 0.1f, 0.001f, 1000.0f);
    }

    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
}