#pragma once
#include <string>
#include <vector>
#include "../../3rdParty/GLM/glm.hpp"

struct GameObject;
struct Component
{
    bool enabled = true; int index;
    GameObject* gameObject;

    virtual void OnInspectorGUI() = 0;
    virtual void Serialize(std::ofstream& file) const = 0;
    virtual void Deserialize(std::ifstream& file) = 0;
};
template<typename T>
concept DerivedFromComponent = std::derived_from<T, Component>;

struct GameObject
{
    bool enabled = true;
    int compMask = 0;
    std::string name;
    std::vector<Component*> components, removeComps;

    GameObject(const std::string name = "New GameObject");
    GameObject(GameObject* other);
    ~GameObject();
    void OnInspectorGUI();
    void Serialize(std::ofstream& file) const;
    void Deserialize(std::ifstream& file);

    template<DerivedFromComponent T, typename... Args>
    T* AddComponent(Args&&... args)
    {
		T* newComponent = new T(std::forward<Args>(args)...); newComponent->gameObject = this;
        components.push_back(newComponent); compMask += newComponent->index;
        return newComponent;
    }
    template<DerivedFromComponent T>
    T* GetComponent()
    {
        for (Component* comp : components)
        {
            T* result = dynamic_cast<T*>(comp);
            if (result != nullptr) return result;
        }
        return nullptr;
    }
    void RemoveComponent(Component* component)
    {
        if (component->gameObject != this) return;
        removeComps.push_back(component);
    }
    void ProcessRemovals() 
    {
        for (Component* comp : removeComps) 
            for (auto it = components.begin(); it != components.end(); it++)
                if (*it == comp) { delete* it; components.erase(it); compMask -= comp->index; break; }
        removeComps.clear();
    }
};

struct TransformComponent : Component 
{
    glm::vec3 position, rotation, scale, forward; glm::mat4 model;

    TransformComponent();
    TransformComponent(TransformComponent* other);

    void OnInspectorGUI() override;
    void UpdateTransform();
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
private:
    glm::vec3 lastPosition, lastRotation, lastScale;
};

struct LightComponent : Component 
{
    glm::vec3 color; float intensity;
    LightComponent();
	LightComponent(LightComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};

struct RendererComponent : Component 
{
    enum Type { Cube, Sphere, Plane }; Type type; glm::vec4 color;
    RendererComponent(Type _type = Cube);
	RendererComponent(RendererComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};

struct RigidbodyComponent : Component 
{
    enum Type { Static, Dynamic}; Type type; float mass; bool useGravity;
	glm::vec3 velocity, angularVelocity; float damp, angularDamp;

    RigidbodyComponent();
	RigidbodyComponent(RigidbodyComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};