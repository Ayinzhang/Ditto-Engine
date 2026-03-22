#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "../../3rdParty/GLM/glm.hpp"

struct GameObject;
struct Scene;
struct Editor;  // 前向声明

// 全局 Editor 指针（用于 GameObject 访问 Editor 功能）
extern Editor* g_editor;

struct Component
{
    bool enabled = true;
    int index = 0;
    GameObject* gameObject = nullptr;

    virtual ~Component() = default;
    virtual void OnInspectorGUI() = 0;
    virtual void Serialize(std::ofstream& file) const = 0;
    virtual void Deserialize(std::ifstream& file) = 0;
};

template<typename T>
concept DerivedFromComponent = std::derived_from<T, Component>;

struct GameObject
{
    bool enabled = true;
    bool locked = false;  // 锁定状态
    int compMask = 0;
    std::string name;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;

    std::vector<Component*> components;
    std::vector<Component*> removeComps;

    GameObject(const std::string _name = "New GameObject");
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
        compMask += newComp->index;
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

    void Serialize(std::ofstream& file) const;
    void Deserialize(std::ifstream& file);
};


// 全局当前场景指针（用于组件修改时标记 dirty）
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

    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;

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
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};

struct RendererComponent : Component
{
    enum Type { Cube, Sphere };
    Type type;
    glm::vec4 color;

    RendererComponent(Type _type = Cube);
    RendererComponent(RendererComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
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
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};