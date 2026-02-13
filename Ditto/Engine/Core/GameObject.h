#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "../../3rdParty/GLM/glm.hpp"

struct GameObject;
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
    int compMask = 0;
    std::string name;

    // --- 父子关系 ---
    GameObject* parent = nullptr;
    std::vector<GameObject*> children;

    std::vector<Component*> components;
    std::vector<Component*> removeComps;

    GameObject(const std::string _name = "New GameObject");
    GameObject(GameObject* other);          // 深拷贝整棵子树
    ~GameObject();

    // --- 父子操作 ---
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);
    void RemoveFromParent();
    bool IsDescendantOf(GameObject* ancestor) const;

    // --- 组件管理 ---
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

    // --- 编辑器 ---
    void OnInspectorGUI();

    // --- 序列化 ---
    void Serialize(std::ofstream& file) const;
    void Deserialize(std::ifstream& file);
};

// ========== TransformComponent ==========
struct TransformComponent : Component
{
    glm::vec3 position, rotation, scale, forward;
    glm::mat4 model;        // 局部模型矩阵
    mutable glm::mat4 worldModel;   // 缓存的世界矩阵

    mutable bool localDirty;    // 局部矩阵需要重新计算
    mutable bool worldDirty;    // 世界矩阵需要重新计算

    TransformComponent();
    TransformComponent(TransformComponent* other);

    void OnInspectorGUI() override;
    void UpdateTransform();          // 重新计算局部 model（仅当 localDirty）
    void UpdateWorldMatrix() const;  // 重新计算世界矩阵并缓存（仅当 worldDirty）
    glm::mat4 GetWorldModel() const; // 返回缓存的世界矩阵（自动更新）

    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;

private:
    glm::vec3 lastPosition, lastRotation, lastScale; // 用于 Inspector 变化检测
    void MarkChildrenWorldDirty();
};

// ========== LightComponent ==========
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

// ========== RendererComponent ==========
struct RendererComponent : Component
{
    enum Type { Cube, Sphere, Plane };
    Type type;
    glm::vec4 color;

    RendererComponent(Type _type = Cube);
    RendererComponent(RendererComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ofstream& file) const override;
    void Deserialize(std::ifstream& file) override;
};

// ========== RigidbodyComponent ==========
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