#pragma once
#include <string>
#include <vector>

struct GameObject;

struct Component
{
	bool enabled = true; int index;
	GameObject* gameObject;
	virtual const char* GetName() = 0;
	virtual void OnInspectorGUI() = 0;
};

struct TransformComponent : Component 
{
    float position[3], rotation[3], scale[3];

    TransformComponent();

    const char* GetName() override { return "Transform"; }
    void OnInspectorGUI() override;
};

struct LightComponent : Component 
{
    float color[3]; float intensity;
    LightComponent();
    const char* GetName() override { return "Dir Light"; }
    void OnInspectorGUI() override;
};

struct RendererComponent : Component 
{
    enum Type { Cube, Sphere, Plane }; Type type; float color[4];
    RendererComponent();

    const char* GetName() override { return "Renderer"; }
    void OnInspectorGUI() override;
};

struct RigidbodyComponent : Component 
{
    enum Type { Static, Dynamic}; Type type; float mass; bool useGravity;

    RigidbodyComponent();

    const char* GetName() override { return "Rigidbody"; }
    void OnInspectorGUI() override;
};

template<typename T>
concept DerivedFromComponent = std::derived_from<T, Component>;

struct GameObject
{
    bool enabled = true;
    int compMask = 0;
    std::string name;
    std::vector<Component*> components;

    GameObject();
    ~GameObject();
    void OnInspectorGUI();
    template<DerivedFromComponent T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        T* newComponent = new T(std::forward<Args>(args)...);
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
        for (auto it = components.begin(); it != components.end(); it++)
        {
            if (*it == component) { delete* it; components.erase(it); compMask -= component->index; break; }
        }
    }
};