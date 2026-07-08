#pragma once

#include <concepts>
#include <iosfwd>

struct GameObject;





namespace ComponentIndex
{
    constexpr int Transform    = 1 << 0;
    constexpr int Light        = 1 << 1;
    constexpr int Renderer     = 1 << 2;
    constexpr int Rigidbody    = 1 << 3;
    constexpr int Collider     = 1 << 4;
    constexpr int AudioSource  = 1 << 5;
    constexpr int UIImage      = 1 << 6;
    constexpr int UIText       = 1 << 7;
    constexpr int UIButton     = 1 << 8;
    constexpr int Camera       = 1 << 9;
    constexpr int CSharpScript = 1 << 10;
    constexpr int Rigidbody2D  = 1 << 11;
    constexpr int Collider2D   = 1 << 12;
    constexpr int SpriteRenderer = 1 << 13;
    constexpr int Canvas       = 1 << 14;
    constexpr int RectTransform = 1 << 15;
    constexpr int Animator     = 1 << 16;
    constexpr int ParticleSystem = 1 << 17;
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
