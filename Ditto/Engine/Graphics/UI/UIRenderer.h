#pragma once
#include <string>
#include <vector>
#include "FontAtlas.h"
#include "../RHI/IRenderer.h"

struct Scene;





struct UIRenderer
{
    
    bool Init(Ditto::IRenderer* rhi);
    void Shutdown();

    
    void Render(Scene* scene, int viewportWidth, int viewportHeight);

    FontAtlas font;

private:
    struct Instance
    {
        glm::vec4 rect;     
        glm::vec4 uvRect;   
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
