#pragma once
#include "../Core/GameObject.h"
#include <vector>

// ParticleSystemComponent: Unity-style CPU-simulated particle system. The
// component owns and simulates a pool of particles; the Scene reads the live
// particle list each frame and renders them as camera-facing billboarded quads
// through the existing GPU-instancing batch path (no bespoke GL code here).
struct Particle
{
    glm::vec3 position{ 0.0f };
    glm::vec3 velocity{ 0.0f };
    glm::vec4 color{ 1.0f };
    float size = 1.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    bool alive = false;
};

struct ParticleSystemComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::ParticleSystem;

    enum EmissionShape { Cone = 0, Sphere = 1, Box = 2 };

    // Emission.
    int maxParticles = 1000;
    float emissionRate = 20.0f;   // particles per second
    bool looping = true;
    float duration = 5.0f;        // system lifetime when not looping
    bool playOnAwake = true;

    // Per-particle start values.
    float startLifetime = 3.0f;
    float startSpeed = 3.0f;
    glm::vec4 startColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 endColor{ 1.0f, 1.0f, 1.0f, 0.0f };
    float startSize = 0.2f;
    float endSize = 0.0f;

    // Forces.
    glm::vec3 gravity{ 0.0f, -2.0f, 0.0f };

    // Shape.
    EmissionShape shape = Cone;
    float coneAngle = 25.0f;      // degrees, half-angle
    float sphereRadius = 1.0f;

    // Runtime state (not all serialized).
    std::vector<Particle> particles;
    float emissionAccumulator = 0.0f;
    float systemTime = 0.0f;
    bool playing = false;
    bool paused = false;

    ParticleSystemComponent();
    ParticleSystemComponent(ParticleSystemComponent* other);

    // Playback control (Unity-like API).
    void Play();
    void Stop();
    void Pause() { paused = true; }
    void Resume() { paused = false; }
    bool IsPlaying() const { return playing && !paused; }

    // Simulate one frame. Called by the engine in Play mode.
    void Update(float deltaTime);
    // Spawn `count` new particles immediately.
    void Emit(int count);

    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    void SpawnOne(Particle& p);
    glm::vec3 RandomEmissionDir();
};
