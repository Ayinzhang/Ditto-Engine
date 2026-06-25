#include "ParticleSystemComponent.h"
#ifndef DITTO_HEADLESS_TESTS
#include "../../3rdParty/ImGui/imgui.h"
#endif
#include "../../3rdParty/GLM/common.hpp"
#include "../../3rdParty/GLM/geometric.hpp"
#include "../Resources/AssetReferenceIO.h"
#include <ostream>
#include <istream>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace
{
    std::mt19937 s_rng{ 1337u };
    std::uniform_real_distribution<float> s_unit{ 0.0f, 1.0f };
    float Rand01() { return s_unit(s_rng); }
}

ParticleSystemComponent::ParticleSystemComponent()
{
    index = ComponentIndex::ParticleSystem;
    particles.resize(maxParticles);
}

ParticleSystemComponent::ParticleSystemComponent(ParticleSystemComponent* other)
    : maxParticles(other->maxParticles), emissionRate(other->emissionRate),
      looping(other->looping), duration(other->duration), playOnAwake(other->playOnAwake),
      startLifetime(other->startLifetime), startSpeed(other->startSpeed),
      startColor(other->startColor), endColor(other->endColor),
      startSize(other->startSize), endSize(other->endSize), materialPath(other->materialPath),
      gravity(other->gravity),
      shape(other->shape), coneAngle(other->coneAngle), sphereRadius(other->sphereRadius)
{
    index = ComponentIndex::ParticleSystem;
    enabled = other->enabled;
    particles.resize(maxParticles);
}

void ParticleSystemComponent::Play()
{
    playing = true;
    paused = false;
    systemTime = 0.0f;
}

void ParticleSystemComponent::Stop()
{
    playing = false;
    paused = false;
    systemTime = 0.0f;
    for (auto& p : particles) p.alive = false;
}

glm::vec3 ParticleSystemComponent::RandomEmissionDir()
{
    switch (shape)
    {
    case Cone:
    {
        float a = glm::radians(coneAngle) * Rand01();
        float phi = Rand01() * 6.2831853f;
        return glm::normalize(glm::vec3(std::sin(a) * std::cos(phi),
                                        std::cos(a),
                                        std::sin(a) * std::sin(phi)));
    }
    case Sphere:
    {
        float theta = Rand01() * 6.2831853f;
        float z = 2.0f * Rand01() - 1.0f;
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return glm::vec3(r * std::cos(theta), r * std::sin(theta), z);
    }
    case Box:
    default:
        return glm::normalize(glm::vec3(2.0f * Rand01() - 1.0f,
                                        2.0f * Rand01() - 1.0f,
                                        2.0f * Rand01() - 1.0f) + glm::vec3(0, 0.001f, 0));
    }
}

void ParticleSystemComponent::SpawnOne(Particle& p)
{
    p.alive = true;
    p.age = 0.0f;
    p.lifetime = startLifetime;
    p.position = glm::vec3(0.0f);
    if (gameObject)
        if (TransformComponent* t = gameObject->GetComponent<TransformComponent>())
            p.position = t->position;
    p.velocity = RandomEmissionDir() * startSpeed;
    p.color = startColor;
    p.size = startSize;
}

void ParticleSystemComponent::Emit(int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle* slot = nullptr;
        for (auto& p : particles)
            if (!p.alive) { slot = &p; break; }
        if (!slot) break;
        SpawnOne(*slot);
    }
}

void ParticleSystemComponent::Update(float deltaTime)
{
    if (!enabled || !playing || paused) return;

    // Resize pool if maxParticles changed in the inspector.
    if (static_cast<int>(particles.size()) != maxParticles && maxParticles > 0)
        particles.resize(maxParticles);

    systemTime += deltaTime;
    bool emitting = looping || systemTime < duration;

    if (emitting && emissionRate > 0.0f)
    {
        emissionAccumulator += deltaTime * emissionRate;
        int toEmit = static_cast<int>(emissionAccumulator);
        if (toEmit > 0)
        {
            Emit(toEmit);
            emissionAccumulator -= static_cast<float>(toEmit);
        }
    }

    for (auto& p : particles)
    {
        if (!p.alive) continue;
        p.age += deltaTime;
        if (p.age >= p.lifetime) { p.alive = false; continue; }
        p.velocity += gravity * deltaTime;
        p.position += p.velocity * deltaTime;
        float t = p.lifetime > 1e-6f ? p.age / p.lifetime : 1.0f;
        p.color = glm::mix(startColor, endColor, t);
        p.size = glm::mix(startSize, endSize, t);
    }
}

void ParticleSystemComponent::OnInspectorGUI()
{
#ifndef DITTO_HEADLESS_TESTS
    ImGui::Checkbox("Enabled", &enabled);

    ImGui::Separator();
    ImGui::TextUnformatted("Emission");
    ImGui::DragInt("Max Particles", &maxParticles, 10.0f, 1, 100000);
    ImGui::DragFloat("Rate", &emissionRate, 0.5f, 0.0f, 5000.0f);
    ImGui::Checkbox("Looping", &looping);
    if (!looping) ImGui::DragFloat("Duration", &duration, 0.1f, 0.1f, 600.0f);
    ImGui::Checkbox("Play On Awake", &playOnAwake);

    ImGui::Separator();
    ImGui::TextUnformatted("Particle");
    ImGui::DragFloat("Lifetime", &startLifetime, 0.05f, 0.05f, 60.0f);
    ImGui::DragFloat("Speed", &startSpeed, 0.05f, 0.0f, 100.0f);
    ImGui::ColorEdit4("Start Color", &startColor.x);
    ImGui::ColorEdit4("End Color", &endColor.x);
    ImGui::DragFloat("Start Size", &startSize, 0.01f, 0.0f, 50.0f);
    ImGui::DragFloat("End Size", &endSize, 0.01f, 0.0f, 50.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Renderer");
    char materialBuffer[512] = {};
    std::snprintf(materialBuffer, sizeof(materialBuffer), "%s", materialPath.c_str());
    if (ImGui::InputText("Material", materialBuffer, sizeof(materialBuffer)))
        materialPath = materialBuffer;

    ImGui::Separator();
    ImGui::TextUnformatted("Shape");
    const char* shapeNames[] = { "Cone", "Sphere", "Box" };
    int shapeIdx = static_cast<int>(shape);
    if (ImGui::Combo("Shape", &shapeIdx, shapeNames, 3))
        shape = static_cast<EmissionShape>(shapeIdx);
    if (shape == Cone)   ImGui::DragFloat("Cone Angle", &coneAngle, 0.5f, 0.0f, 90.0f);
    if (shape == Sphere) ImGui::DragFloat("Sphere Radius", &sphereRadius, 0.05f, 0.0f, 100.0f);

    ImGui::Separator();
    ImGui::TextUnformatted("Forces");
    ImGui::DragFloat3("Gravity", &gravity.x, 0.05f);

    ImGui::Separator();
    if (playing)
    {
        if (ImGui::Button(paused ? "Resume" : "Pause")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("Stop")) Stop();
    }
    else
    {
        if (ImGui::Button("Play")) Play();
    }
    int alive = 0;
    for (const auto& p : particles) if (p.alive) ++alive;
    ImGui::SameLine();
    ImGui::Text("Alive: %d / %d", alive, maxParticles);
#endif
}

void ParticleSystemComponent::Serialize(std::ostream& file) const
{
    file.write(reinterpret_cast<const char*>(&maxParticles), sizeof(maxParticles));
    file.write(reinterpret_cast<const char*>(&emissionRate), sizeof(emissionRate));
    file.write(reinterpret_cast<const char*>(&looping), sizeof(looping));
    file.write(reinterpret_cast<const char*>(&duration), sizeof(duration));
    file.write(reinterpret_cast<const char*>(&playOnAwake), sizeof(playOnAwake));
    file.write(reinterpret_cast<const char*>(&startLifetime), sizeof(startLifetime));
    file.write(reinterpret_cast<const char*>(&startSpeed), sizeof(startSpeed));
    file.write(reinterpret_cast<const char*>(&startColor), sizeof(startColor));
    file.write(reinterpret_cast<const char*>(&endColor), sizeof(endColor));
    file.write(reinterpret_cast<const char*>(&startSize), sizeof(startSize));
    file.write(reinterpret_cast<const char*>(&endSize), sizeof(endSize));
    Ditto::AssetReferenceIO::WriteAssetReference(file, materialPath);
    file.write(reinterpret_cast<const char*>(&gravity), sizeof(gravity));
    int32_t shapeInt = static_cast<int32_t>(shape);
    file.write(reinterpret_cast<const char*>(&shapeInt), sizeof(shapeInt));
    file.write(reinterpret_cast<const char*>(&coneAngle), sizeof(coneAngle));
    file.write(reinterpret_cast<const char*>(&sphereRadius), sizeof(sphereRadius));
}

void ParticleSystemComponent::Deserialize(std::istream& file)
{
    file.read(reinterpret_cast<char*>(&maxParticles), sizeof(maxParticles));
    file.read(reinterpret_cast<char*>(&emissionRate), sizeof(emissionRate));
    file.read(reinterpret_cast<char*>(&looping), sizeof(looping));
    file.read(reinterpret_cast<char*>(&duration), sizeof(duration));
    file.read(reinterpret_cast<char*>(&playOnAwake), sizeof(playOnAwake));
    file.read(reinterpret_cast<char*>(&startLifetime), sizeof(startLifetime));
    file.read(reinterpret_cast<char*>(&startSpeed), sizeof(startSpeed));
    file.read(reinterpret_cast<char*>(&startColor), sizeof(startColor));
    file.read(reinterpret_cast<char*>(&endColor), sizeof(endColor));
    file.read(reinterpret_cast<char*>(&startSize), sizeof(startSize));
    file.read(reinterpret_cast<char*>(&endSize), sizeof(endSize));
    materialPath = Ditto::AssetReferenceIO::ReadAssetReference(file, 17);
    file.read(reinterpret_cast<char*>(&gravity), sizeof(gravity));
    int32_t shapeInt = 0;
    file.read(reinterpret_cast<char*>(&shapeInt), sizeof(shapeInt));
    shape = static_cast<EmissionShape>(shapeInt);
    file.read(reinterpret_cast<char*>(&coneAngle), sizeof(coneAngle));
    file.read(reinterpret_cast<char*>(&sphereRadius), sizeof(sphereRadius));

    if (maxParticles > 0) particles.assign(maxParticles, Particle{});
}
