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

// Component type identifiers. Each component carries one of these in its
// `index` field; `GameObject::compMask` is the bitwise-OR of the indices of
// the components it owns. Kept as distinct bit flags so a mask can answer
// "does this object have component X?" with a single AND. These are the single
// source of truth shared by component constructors, scene (de)serialization,
// and engine-side iteration -- do not hardcode the raw `1 << n` values.
namespace ComponentIndex
{
    constexpr int Transform    = 1 << 0;
    constexpr int Light        = 1 << 1;
    constexpr int Renderer     = 1 << 2;
    constexpr int Rigidbody    = 1 << 3;
    constexpr int CSharpScript = 1 << 10;
}

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
    // Static    : never moves, infinite mass (level geometry).
    // Dynamic   : fully simulated -- receives gravity/impulses, its world pose
    //             is owned by the solver and is NOT driven by the Transform
    //             hierarchy (so a Dynamic child does not follow a moving parent).
    // Kinematic : infinite mass like Static, but its pose IS driven by the
    //             Transform/script/parent hierarchy. It pushes Dynamic bodies
    //             (moving platforms, parent-driven children) without being
    //             pushed back, and is not affected by gravity. This is the type
    //             to use for "child follows the parent" without double gravity.
    // NOTE: enum values are serialized as int32; only append new values at the
    // end to keep old scene files loading correctly (Static=0, Dynamic=1).
    enum Type { Static, Dynamic, Kinematic };
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