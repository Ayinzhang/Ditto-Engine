#include "GameObject.h"
#include "../../3rdParty/ImGui/imgui.h"

GameObject::GameObject()
{
	this->name = "New GameObject";
	this->AddComponent<TransformComponent>();
}

GameObject::~GameObject()
{
	for (Component* comp : components) delete comp;
}

void GameObject::OnInspectorGUI()
{
    // GameObject 的通用属性
    char nameBuffer[256];
    strcpy_s(nameBuffer, name.c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) name = nameBuffer;

    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &enabled);

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
}

void TransformComponent::OnInspectorGUI() 
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted("Transform");

    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    ImGui::DragFloat3("Position", position, 0.1f);
    ImGui::DragFloat3("Rotation", rotation, 0.1f);
    ImGui::DragFloat3("Scale", scale, 0.1f);
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
}

LightComponent::LightComponent()
{
    index = 1 << 1; color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; intensity = 1.0f;
}

void LightComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted(GetName());
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    ImGui::ColorEdit3("Color", color);
    ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 100.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
}

RendererComponent::RendererComponent() 
{
    index = 1 << 2; color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; color[3] = 1.0f;
}

void RendererComponent::OnInspectorGUI()
{
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted(GetName());

    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Cube", "Sphere", "Plane"};
    int currentType = static_cast<int>(type);
    if (ImGui::Combo("Type", &currentType, typeNames, 3))
    {
        type = static_cast<Type>(currentType);
    }
    ImGui::ColorEdit4("Color", color, ImGuiColorEditFlags_AlphaBar);
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
    ImGui::TextUnformatted(GetName());

    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);

    const char* typeNames[] = { "Static", "Dynamic" };
    int currentType = static_cast<int>(type);
    if (ImGui::Combo("Type", &currentType, typeNames, 2))
    {
        type = static_cast<Type>(currentType);
    }

    if (type == Dynamic)
    {
        ImGui::Checkbox("Use Gravity", &useGravity);
        ImGui::DragFloat("Mass", &mass, 0.1f, 0.001f, 1000.0f);
    }

    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
}