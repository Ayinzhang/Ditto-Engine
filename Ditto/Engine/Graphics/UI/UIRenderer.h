#pragma once
#include <string>
#include <vector>
#include "FontAtlas.h"
#include "../RHI/IRenderer.h"

struct Scene;

// Screen-space UI pass: collects UIImage/UIText/UIButton components from the
// scene and draws them as instanced quads (one draw per texture) at the end of
// Scene::Render. Works in both the editor's offscreen viewports and the
// standalone game-mode backbuffer (no ImGui involved).
struct UIRenderer
{
    // Lazy init on first Render call; safe to call repeatedly.
    bool Init(Ditto::IRenderer* rhi);
    void Shutdown();

    // Draw all UI components found under the scene's root. viewport w/h in pixels.
    void Render(Scene* scene, int viewportWidth, int viewportHeight);

    FontAtlas font;

private:
    struct Instance
    {
        glm::vec4 rect;     // x, y, w, h (pixels, top-left origin)
        glm::vec4 uvRect;   // u0, v0, u1, v1
        glm::vec4 color;
    };

    void Flush(const std::vector<Instance>& instances, Ditto::TextureHandle texture);
    void AppendText(std::vector<Instance>& out, const std::string& text, float fontSize,
        const glm::vec2& topLeft, const glm::vec4& color) const;

    Ditto::IRenderer* renderer = nullptr;
    Ditto::PipelineHandle pipeline;
    Ditto::MeshHandle quadMesh;
    Ditto::StorageBufferHandle rectsSSBO, extrasSSBO;
    Ditto::TextureHandle fontTexture;
    bool initialized = false;
    bool initFailed = false;
};
