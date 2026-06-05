#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <iosfwd>   // std::ostream / std::istream forward decls (serialize to file OR memory)
#include "../../3rdParty/GLM/glm.hpp"

struct GameObject;
struct Scene;
struct Editor;  // Forward declaration

// Global Editor pointer (for GameObject to access Editor functionality)
extern Editor* g_editor;

struct Component
{
    bool enabled = true;
    int index = 0;
    GameObject* gameObject = nullptr;

    virtual ~Component() = default;
    virtual void OnInspectorGUI() = 0;
    virtual void Serialize(std::ostream& file) const = 0;
    virtual void Deserialize(std::istream& file) = 0;
};

template<typename T>
concept DerivedFromComponent = std::derived_from<T, Component>;

struct GameObject
{
    bool enabled = true;
    bool locked = false;
    int compMask = 0;
    std::string name;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;

    std::vector<Component*> components;
    std::vector<Component*> removeComps;

    GameObject(const std::string _name = "New GameObject");
    explicit GameObject(bool createComponents);
    GameObject(GameObject* other);
    ~GameObject();

    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);
    void RemoveFromParent();
    bool IsDescendantOf(GameObject* ancestor) const;

    template<DerivedFromComponent T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        T* newComp = new T(std::forward<Args>(args)...);
        newComp->gameObject = this;
        components.push_back(newComp);
        compMask |= newComp->index;   // bitwise OR: idempotent if the same component type is added again
        return newComp;
    }

    template<DerivedFromComponent T>
    T* GetComponent() const
    {
        for (Component* comp : components)
            if (T* result = dynamic_cast<T*>(comp))
                return result;
        return nullptr;
    }

    void RemoveComponent(Component* component);
    void ProcessRemovals();
    void OnInspectorGUI();

    void Serialize(std::ostream& file) const;
    void Deserialize(std::istream& file);
};


// Global current scene pointer (used to mark dirty when components are modified)
extern Scene* g_currentScene;

struct TransformComponent : Component
{
    glm::vec3 position, rotation, scale, forward;
    glm::mat4 model; mutable glm::mat4 worldModel;

    mutable bool localDirty, worldDirty;

    TransformComponent();
    TransformComponent(TransformComponent* other);

    void OnInspectorGUI() override;
    void UpdateTransform();
    void UpdateWorldMatrix() const;
    glm::mat4 GetWorldModel() const;

    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    glm::vec3 lastPosition, lastRotation, lastScale;
    void MarkChildrenWorldDirty();
};

struct LightComponent : Component
{
    glm::vec3 color;
    float intensity;

    LightComponent();
    LightComponent(LightComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct RendererComponent : Component
{
    enum Type { Cube, Sphere };
    Type type;
    glm::vec4 color;

    RendererComponent(Type _type = Cube);
    RendererComponent(RendererComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct RigidbodyComponent : Component
{
    enum Type { Static, Dynamic };
    Type type;
    float mass;
    bool useGravity;
    float damp, angularDamp;
    glm::vec3 velocity, angularVelocity;
    glm::mat3 inertia, inverseInertia;

    RigidbodyComponent();
    RigidbodyComponent(RigidbodyComponent* other);
    void OnInspectorGUI() override;
    void CalculateInertia(RendererComponent::Type shapeType, const glm::vec3& scale);
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};