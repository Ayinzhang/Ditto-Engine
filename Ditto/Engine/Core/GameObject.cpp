#include "GameObject.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include <algorithm>
#include <fstream>

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

    if (ImGui::Button("Add Component")) ImGui::OpenPopup("AddComponentPopup");

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        if (!(compMask >> 1 & 1) && ImGui::MenuItem("DirLight")) AddComponent<LightComponent>();
        if (!(compMask >> 2 & 1) && ImGui::MenuItem("Renderer")) AddComponent<RendererComponent>();
        if (!(compMask >> 3 & 1) && ImGui::MenuItem("Rigidbody")) AddComponent<RigidbodyComponent>();
        ImGui::EndPopup();
    }
}

void GameObject::Serialize(std::ofstream& file) const
{
    file.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));

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
}

void GameObject::Deserialize(std::ifstream& file)
{
    file.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));

    name = ReadString(file);

    file.read(reinterpret_cast<char*>(&compMask), sizeof(compMask));

    uint32_t componentCount = 0;
    file.read(reinterpret_cast<char*>(&componentCount), sizeof(componentCount));

    for (Component* comp : components) delete comp;
    components.clear();

    for (uint32_t i = 0; i < componentCount; i++)
    {
        int index = 0;
        file.read(reinterpret_cast<char*>(&index), sizeof(index));

        bool compEnabled = true;
        file.read(reinterpret_cast<char*>(&compEnabled), sizeof(compEnabled));

        Component* newComp = nullptr;

        switch (index)
        {
        case 1 << 0: newComp = new TransformComponent(); break;
        case 1 << 1: newComp = new LightComponent(); break;
        case 1 << 2: newComp = new RendererComponent(); break;
        case 1 << 3: newComp = new RigidbodyComponent(); break;
        default: continue;
        }

        if (newComp)
        {
            newComp->index = index; newComp->enabled = compEnabled;
            newComp->gameObject = this; newComp->Deserialize(file);

            components.push_back(newComp);
        }
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

void TransformComponent::Serialize(std::ofstream& file) const
{
    file.write(reinterpret_cast<const char*>(position), sizeof(float) * 3);
    file.write(reinterpret_cast<const char*>(rotation), sizeof(float) * 3);
    file.write(reinterpret_cast<const char*>(scale), sizeof(float) * 3);
}

void TransformComponent::Deserialize(std::ifstream& file)
{
    file.read(reinterpret_cast<char*>(position), sizeof(float) * 3);
    file.read(reinterpret_cast<char*>(rotation), sizeof(float) * 3);
    file.read(reinterpret_cast<char*>(scale), sizeof(float) * 3);

    for (int i = 0; i < 3; i++) { lastPosition[i] = position[i]; lastRotation[i] = rotation[i]; lastScale[i] = scale[i]; }

    UpdateTransform();
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

void LightComponent::Serialize(std::ofstream& file) const
{
    file.write(reinterpret_cast<const char*>(color), sizeof(float) * 3);
    file.write(reinterpret_cast<const char*>(&intensity), sizeof(intensity));
}

void LightComponent::Deserialize(std::ifstream& file)
{
    file.read(reinterpret_cast<char*>(color), sizeof(float) * 3);
    file.read(reinterpret_cast<char*>(&intensity), sizeof(intensity));
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

void RendererComponent::Serialize(std::ofstream& file) const
{
    int32_t typeInt = static_cast<int32_t>(type);
    file.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    file.write(reinterpret_cast<const char*>(color), sizeof(float) * 4);
}

void RendererComponent::Deserialize(std::ifstream& file)
{
    int32_t typeInt = 0;
    file.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt));
    type = static_cast<Type>(typeInt);
    file.read(reinterpret_cast<char*>(color), sizeof(float) * 4);
}

RigidbodyComponent::RigidbodyComponent()
{
    index = 1 << 3; type = Dynamic; mass = 1.0f; useGravity = true; 
	velocity = angularVelocity = glm::vec3(0); damp = angularDamp = 0.05f;
}

RigidbodyComponent::RigidbodyComponent(RigidbodyComponent* other)
{
    index = 1 << 3; type = other->type; mass = other->mass; useGravity = other->useGravity;
    velocity = angularVelocity = glm::vec3(0); damp = angularDamp = 0.05f;
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
        ImGui::Text("Mass "); ImGui::SameLine();
        ImGui::DragFloat("##Mass", &mass, 0.1f, 0.001f, 1000.0f);
        ImGui::Text("Damp "); ImGui::SameLine();
        ImGui::DragFloat("##Damp", &damp, 0.1f, 0.0f, 1.0f);
        ImGui::Text("ADamp"); ImGui::SameLine();
        ImGui::DragFloat("##AngularDamp", &angularDamp, 0.1f, 0.0f, 1.0f);

        ImGui::Text("Velocity "); ImGui::SameLine();
        ImGui::Text("X: %.3f", velocity.x); ImGui::SameLine();
        ImGui::Text("Y: %.3f", velocity.y); ImGui::SameLine();
        ImGui::Text("Z: %.3f", velocity.z);

        ImGui::Text("AVelocity"); ImGui::SameLine();
        ImGui::Text("X: %.3f", angularVelocity.x); ImGui::SameLine();
        ImGui::Text("Y: %.3f", angularVelocity.y); ImGui::SameLine();
        ImGui::Text("Z: %.3f", angularVelocity.z);
    }

    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();
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