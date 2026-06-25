#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <iosfwd>   // std::ostream / std::istream forward decls (serialize to file OR memory)
#include <cstdint>
#include <utility>
#include "Component.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"
#include "../Graphics/Camera.h"

struct GameObject;
struct Scene;
struct Editor;  // Forward declaration

struct GameObject
{
    bool enabled = true;
    bool locked = false;
    int compMask = 0;
    std::string name;
    std::string prefabSourcePath;
    std::string prefabSourceGuid;

    // Single-ownership tree: each GameObject owns its children; `parent` is a
    // non-owning back-pointer.
    GameObject* parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> children;

    // Owned components; `removeComps` is a non-owning pending-removal list of
    // markers into `components`, resolved by ProcessRemovals().
    std::vector<std::unique_ptr<Component>> components;
    std::vector<Component*> removeComps;

    GameObject(const std::string& _name = "New GameObject");
    GameObject(const char* _name);
    explicit GameObject(bool createComponents);
    GameObject(GameObject* other);
    ~GameObject();

    // Adopt a new (not-yet-owned) object; returns a raw observer to it.
    GameObject* AddChild(std::unique_ptr<GameObject> child);
    // Reparent an object currently owned elsewhere in the tree (editor
    // drag-drop). Self/cycle guards as before; no-op if they fail.
    void AddChild(GameObject* existingChild);
    // Unlink `child` from this->children and hand its ownership to the caller
    // (nulls child->parent). Returns nullptr if not found.
    std::unique_ptr<GameObject> DetachChild(GameObject* child);
    bool IsDescendantOf(GameObject* ancestor) const;

    template<DerivedFromComponent T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* newComp = owned.get();
        newComp->gameObject = this;
        components.push_back(std::move(owned));
        compMask |= newComp->index;   // bitwise OR: idempotent if the same component type is added again
        return newComp;               // raw observer; the GameObject owns the component
    }

    template<DerivedFromComponent T>
    T* GetComponent() const
    {
        // Fast reject: if the component bit isn't set in compMask, the object
        // cannot own a T, so skip the dynamic_cast scan entirely. Components
        // expose their bit as `T::TypeBit`; types without one fall back to the
        // full scan.
        if constexpr (requires { T::TypeBit; })
            if ((compMask & T::TypeBit) == 0) return nullptr;

        for (const auto& comp : components)
            if (T* result = dynamic_cast<T*>(comp.get()))
                return result;
        return nullptr;
    }

    template<DerivedFromComponent T>
    std::vector<T*> GetComponents() const
    {
        std::vector<T*> results;
        if constexpr (requires { T::TypeBit; })
            if ((compMask & T::TypeBit) == 0) return results;

        for (const auto& comp : components)
            if (T* result = dynamic_cast<T*>(comp.get()))
                results.push_back(result);
        return results;
    }

    void RemoveComponent(Component* component);
    void ProcessRemovals();
    void OnInspectorGUI();

    void Serialize(std::ostream& file) const;
    void Deserialize(std::istream& file);
};

struct TransformComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Transform;
    glm::vec3 position, rotation, scale, forward;
    glm::mat4 model; mutable glm::mat4 worldModel;

    mutable bool localDirty, worldDirty;

    // Runtime rotation state for physics. `rotation` (euler degrees) stays the
    // authored + serialized representation; during simulation the physics
    // integrator advances `orientation` (a quaternion) instead -- correct
    // angular composition, no gimbal lock. When `useQuatRotation` is set,
    // UpdateTransform builds the model from the quaternion; otherwise it uses
    // euler exactly as before. The quaternion is NOT serialized.
    glm::quat orientation;
    bool useQuatRotation;

    TransformComponent();
    TransformComponent(TransformComponent* other);

    void OnInspectorGUI() override;
    void UpdateTransform();
    void UpdateWorldMatrix() const;
    glm::mat4 GetWorldModel() const;
    void SetTRS(const glm::vec3& newPosition, const glm::vec3& newRotation, const glm::vec3& newScale);

    // Seed `orientation` from the current euler `rotation` (matching the same
    // Y*X*Z order UpdateTransform uses) and switch to quaternion mode. Called
    // lazily by the physics integrator when a body first simulates.
    void SeedOrientationFromEuler();

    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    glm::vec3 lastPosition, lastRotation, lastScale;
    void MarkChildrenWorldDirty();
};

struct LightComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Light;
    enum Type { Directional, Point, Spot, Area };
    Type type = Directional;
    glm::vec3 color;
    float intensity;
    float range = 10.0f;
    float spotAngle = 30.0f;
    float indirectMultiplier = 1.0f;
    bool castShadows = false;

    LightComponent();
    LightComponent(LightComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct CameraComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Camera;
    enum ClearFlags { Skybox, SolidColor, DepthOnly, DontClear };
    bool mainCamera = true;
    ClearFlags clearFlags = SolidColor;
    Camera::ProjectionType projectionType = Camera::ProjectionType::Perspective;
    float fieldOfView = 45.0f;
    float orthographicSize = 5.0f;
    float nearClipPlane = 0.1f;
    float farClipPlane = 100.0f;
    glm::vec4 backgroundColor{ 0.1f, 0.1f, 0.1f, 1.0f };
    glm::vec4 viewportRect{ 0.0f, 0.0f, 1.0f, 1.0f };
    float depth = 0.0f;
    bool occlusionCulling = true;
    bool allowHDR = true;
    bool allowMSAA = true;

    CameraComponent();
    CameraComponent(CameraComponent* other);
    Camera ToCamera(const TransformComponent* transform) const;
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct RendererComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Renderer;

    enum ShadowCastingMode { ShadowsOff, ShadowsOn, TwoSided, ShadowsOnly };

    glm::vec4 color;
    std::string materialPath;
    std::string mainTexturePath;
    ShadowCastingMode shadowCastingMode = ShadowsOn;
    bool receiveShadows = true;
    bool staticShadowCaster = false;
    bool contributeGI = false;
    int lightProbeUsage = 1;
    int reflectionProbeUsage = 1;
    int motionVectors = 1;
    bool dynamicOcclusion = true;
    uint32_t renderingLayerMask = 1;

    // Mesh path (project-relative path). Required - must be loaded from Assets.
    std::string meshPath;

    RendererComponent();
    RendererComponent(RendererComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct SpriteRendererComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::SpriteRenderer;
    enum DrawMode { Simple, Sliced, Tiled };
    enum MaskInteraction { None, VisibleInsideMask, VisibleOutsideMask };
    enum SpriteSortPoint { Center, Pivot };

    glm::vec4 color{ 1.0f };
    std::string spritePath;  // Required - must be loaded from Assets
    std::string materialPath;

    bool flipX = false;
    bool flipY = false;
    DrawMode drawMode = Simple;
    glm::vec2 size{ 1.0f, 1.0f };
    MaskInteraction maskInteraction = None;
    int sortingLayer = 0;
    int sortingOrder = 0;
    SpriteSortPoint spriteSortPoint = Center;

    SpriteRendererComponent();
    SpriteRendererComponent(SpriteRendererComponent* other);
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
    static constexpr int TypeBit = ComponentIndex::Rigidbody;
    enum Type { Static, Dynamic, Kinematic };
    Type type;
    float mass;
    bool useGravity;
    float damp, angularDamp;
    bool isKinematic = false;
    int interpolate = 0;       // None, Interpolate, Extrapolate
    int collisionDetection = 0; // Discrete, Continuous, Continuous Dynamic, Continuous Speculative
    bool freezePosition[3] = { false, false, false };
    bool freezeRotation[3] = { false, false, false };
    glm::vec3 velocity, angularVelocity;
    glm::mat3 inertia, inverseInertia;

    RigidbodyComponent();
    RigidbodyComponent(RigidbodyComponent* other);
    void OnInspectorGUI() override;
    void CalculateInertia(const glm::vec3& scale);
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct ColliderComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Collider;
    enum Type { Box, Sphere, MeshConvex };
    Type type;
    bool isTrigger;
    bool providesContacts = false;
    glm::vec3 biasPosition;
    glm::vec3 biasRotation;
    glm::vec3 biasScale;
    std::string meshPath;

    ColliderComponent(Type _type = Box);
    ColliderComponent(ColliderComponent* other);
    glm::mat4 GetBiasMatrix() const;
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct Rigidbody2DComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Rigidbody2D;
    enum Type { Static, Dynamic, Kinematic };
    enum ForceMode2D { Force, Impulse };

    Type type = Dynamic;
    std::string materialPath;
    bool simulated = true;
    bool useAutoMass = false;
    float mass = 1.0f;
    bool useGravity = true;
    float gravityScale = 1.0f;
    float linearDamping = 0.02f;
    float angularDamping = 0.02f;
    int collisionDetection = 0; // Discrete, Continuous
    int sleepingMode = 0;       // Never Sleep, Start Awake, Start Asleep
    int interpolate = 0;        // None, Interpolate, Extrapolate
    bool freezePositionX = false;
    bool freezePositionY = false;
    bool freezeRotation = false;
    glm::vec2 velocity{ 0.0f };
    float angularVelocity = 0.0f;

    Rigidbody2DComponent();
    Rigidbody2DComponent(Rigidbody2DComponent* other);
    void AddForce(const glm::vec2& force, ForceMode2D mode = Force);
    void AddTorque(float torque, ForceMode2D mode = Force);
    void ClearAccumulators();
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

    glm::vec2 forceAccum{ 0.0f };
    float torqueAccum = 0.0f;
};

struct Collider2DComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Collider2D;
    enum Type { Box, Circle };

    Type type = Box;
    bool isTrigger = false;
    bool usedByEffector = false;
    bool usedByComposite = false;
    glm::vec2 offset{ 0.0f };
    glm::vec2 size{ 1.0f, 1.0f };
    float radius = 0.5f;
    float restitution = 0.2f;
    float friction = 0.6f;

    Collider2DComponent(Type _type = Box);
    Collider2DComponent(Collider2DComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct AudioSourceComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::AudioSource;
    std::string clipPath;        // project-relative audio file (wav/mp3/ogg/flac)
    std::string outputPath;
    bool mute = false;
    bool bypassEffects = false;
    bool bypassListenerEffects = false;
    bool bypassReverbZones = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnAwake = true;
    int priority = 128;
    float stereoPan = 0.0f;
    float spatialBlend = 0.0f;
    float reverbZoneMix = 1.0f;

    // Runtime-only: handle of the currently playing sound (AudioEngine).
    uint32_t soundHandle = 0;

    AudioSourceComponent();
    AudioSourceComponent(AudioSourceComponent* other);
    void Play();
    void Stop();
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

// ---- In-game UI (screen-space) ----

enum class UIAnchor : int
{
    TopLeft = 0, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
};

// Normalized anchor position inside the viewport ((0,0)=top-left .. (1,1)=
// bottom-right). Also used as the element pivot so e.g. a BottomRight element
// grows up-left from the corner.
inline glm::vec2 UIAnchorFactor(UIAnchor anchor)
{
    const int i = static_cast<int>(anchor);
    return glm::vec2(static_cast<float>(i % 3) * 0.5f, static_cast<float>(i / 3) * 0.5f);
}

// Screen rect (x, y, w, h) in pixels, top-left origin, for an anchored
// element. Shared by the UI renderer and the button hit test.
inline glm::vec4 ComputeUIRect(UIAnchor anchor, const glm::vec2& offset,
    const glm::vec2& size, float viewW, float viewH)
{
    glm::vec2 f = UIAnchorFactor(anchor);
    glm::vec2 base = glm::vec2(viewW, viewH) * f;
    glm::vec2 pos = base + offset - size * f;
    return glm::vec4(pos, size);
}

struct CanvasComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Canvas;
    enum RenderMode { ScreenSpaceOverlay, ScreenSpaceCamera, WorldSpace };

    RenderMode renderMode = ScreenSpaceOverlay;
    bool pixelPerfect = false;
    float planeDistance = 100.0f;
    int sortingOrder = 0;

    CanvasComponent();
    CanvasComponent(CanvasComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct RectTransformComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::RectTransform;
    UIAnchor anchor = UIAnchor::TopLeft;
    glm::vec2 anchoredPosition{ 0.0f };
    glm::vec2 sizeDelta{ 100.0f, 100.0f };
    glm::vec2 pivot{ 0.5f, 0.5f };

    RectTransformComponent();
    RectTransformComponent(RectTransformComponent* other);
    glm::vec4 ComputeRect(float viewW, float viewH) const;
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct UIImageComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::UIImage;
    enum Type { Simple, Sliced, Tiled, Filled };
    UIAnchor anchor = UIAnchor::TopLeft;
    glm::vec2 offset{ 0.0f };
    glm::vec2 size{ 100.0f, 100.0f };
    glm::vec4 color{ 1.0f };
    std::string texturePath;   // empty = solid color (white texture)
    Type type = Simple;
    bool raycastTarget = true;
    bool maskable = true;

    UIImageComponent();
    UIImageComponent(UIImageComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct UITextComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::UIText;
    UIAnchor anchor = UIAnchor::TopLeft;
    glm::vec2 offset{ 0.0f };
    float fontSize = 24.0f;
    glm::vec4 color{ 1.0f };
    std::string text = "Text";
    std::string fontPath;
    int fontStyle = 0; // Normal, Bold, Italic, Bold And Italic
    int alignment = 0; // Left, Center, Right
    bool raycastTarget = true;
    bool maskable = true;

    UITextComponent();
    UITextComponent(UITextComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};

struct UIButtonComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::UIButton;
    UIAnchor anchor = UIAnchor::TopLeft;
    glm::vec2 offset{ 0.0f };
    glm::vec2 size{ 160.0f, 40.0f };
    glm::vec4 color{ 0.25f, 0.25f, 0.25f, 1.0f };
    glm::vec4 hoverColor{ 0.35f, 0.35f, 0.35f, 1.0f };
    glm::vec4 pressedColor{ 0.15f, 0.15f, 0.15f, 1.0f };
    std::string label = "Button";
    float fontSize = 20.0f;
    glm::vec4 labelColor{ 1.0f };
    bool interactable = true;
    int transition = 1; // None, Color Tint
    glm::vec4 disabledColor{ 0.5f, 0.5f, 0.5f, 0.5f };
    float colorMultiplier = 1.0f;
    float fadeDuration = 0.1f;

    // Runtime-only interaction state (driven by Engine in Play mode).
    bool hovered = false;
    bool pressed = false;
    bool wasClicked = false;   // sticky until read by a script (read-clears)

    UIButtonComponent();
    UIButtonComponent(UIButtonComponent* other);
    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;
};
