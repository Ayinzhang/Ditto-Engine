#include "GameObject.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"

GameObject::GameObject(const std::string name)
{
	this->name = name;
	this->AddComponent<TransformComponent>();
}

GameObject::GameObject(GameObject* other)
{
    this->name = other->name;
    for(auto i:other->components)
    {
        if (auto transform = dynamic_cast<TransformComponent*>(i))
            this->AddComponent<TransformComponent>(transform);
        else if (auto light = dynamic_cast<LightComponent*>(i))
            this->AddComponent<LightComponent>(light);
        else if (auto renderer = dynamic_cast<RendererComponent*>(i))
            this->AddComponent<RendererComponent>(renderer);
        else if (auto rigidbody = dynamic_cast<RigidbodyComponent*>(i))
            this->AddComponent<RigidbodyComponent>(rigidbody);
	}
}

GameObject::~GameObject()
{
	for (Component* comp : components) delete comp;
}

void GameObject::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    char nameBuffer[256];
    strcpy_s(nameBuffer, name.c_str());
    ImGui::Text("Name"); ImGui::SameLine();
    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) name = nameBuffer;

    ImGui::Separator();

    for (int i = 0; i < components.size(); i++)
    {
        Component* comp = components[i];
        ImGui::PushID(comp);
        comp->OnInspectorGUI();
        ImGui::PopID();
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

TransformComponent::TransformComponent(TransformComponent* other)
{
    index = 1 << 0;
    for (int i = 0; i < 3; i++)
    {
        position[i] = other->position[i];
        rotation[i] = other->rotation[i];
        scale[i] = other->scale[i];
        lastPosition[i] = other->lastPosition[i];
        lastRotation[i] = other->lastRotation[i];
        lastScale[i] = other->lastScale[i];
    }
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

LightComponent::LightComponent(LightComponent* other)
{
    index = 1 << 1; for (int i = 0; i < 3; i++) color[i] = other->color[i]; intensity = other->intensity;
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

RendererComponent::RendererComponent(Type _type) 
{
    index = 1 << 2; type = _type; color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; color[3] = 1.0f;
}

RendererComponent::RendererComponent(RendererComponent* other)
{
    index = 1 << 2; type = other->type; for (int i = 0; i < 4; i++) color[i] = other->color[i];
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

RigidbodyComponent::RigidbodyComponent(RigidbodyComponent* other)
{
    index = 1 << 3; type = other->type; mass = other->mass; useGravity = other->useGravity;
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