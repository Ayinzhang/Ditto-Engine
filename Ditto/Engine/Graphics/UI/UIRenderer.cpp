#include "UIRenderer.h"
#include "../../Core/Scene.h"
#include "../../Core/GameObject.h"
#include "../../Core/PathUtils.h"
#include "../../Core/Logger.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

namespace fs = std::filesystem;

static std::string ReadTextFileUI(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Engine font: prefer a project/engine-shipped TTF, fall back to Windows fonts.
static std::string FindDefaultFontPath()
{
    fs::path assets = PathUtils::ResolveAsset("Fonts");
    std::error_code ec;
    if (fs::exists(assets, ec))
    {
        for (const auto& entry : fs::directory_iterator(assets, ec))
        {
            auto ext = entry.path().extension().string();
            if (ext == ".ttf" || ext == ".TTF" || ext == ".otf")
                return entry.path().string();
        }
    }
    for (const char* sys : { "C:\\Windows\\Fonts\\arial.ttf",
                             "C:\\Windows\\Fonts\\segoeui.ttf",
                             "C:\\Windows\\Fonts\\consola.ttf" })
    {
        if (fs::exists(sys, ec)) return sys;
    }
    return {};
}

bool UIRenderer::Init(Ditto::IRenderer* rhi)
{
    if (initialized) return true;
    if (initFailed || !rhi) return false;
    renderer = rhi;

    // Unit quad (0,0)-(1,1), two triangles, position-only (vec2 @ location 0).
    const float quadVerts[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f,
    };
    quadMesh = renderer->CreateMesh(quadVerts, 12, 2, { { 0, 2, 0 } });

    fs::path shaderPath = PathUtils::ResolveAsset("Shaders/UI.hlsl");
    std::string hlsl = ReadTextFileUI(shaderPath.string());
    if (hlsl.empty())
    {
        DITTO_LOG_ERROR_STREAM("[UIRenderer] UI shader not found: " << shaderPath.string());
        initFailed = true;
        return false;
    }

    Ditto::PipelineState state;
    state.renderType = "Transparent";
    state.renderQueue = 4000;          // overlay: after everything
    state.depthTest = false;
    state.depthWrite = false;
    state.blend = true;
    state.cull = Ditto::CullMode::Off;
    pipeline = renderer->CreatePipeline(hlsl, state);
    if (!pipeline)
    {
        DITTO_LOG_ERROR("[UIRenderer] Failed to create UI pipeline");
        initFailed = true;
        return false;
    }

    rectsSSBO = renderer->CreateStorageBuffer(sizeof(glm::vec4) * 256, true);
    uvsSSBO = renderer->CreateStorageBuffer(sizeof(glm::vec4) * 256, true);
    colorsSSBO = renderer->CreateStorageBuffer(sizeof(glm::vec4) * 256, true);

    std::string fontPath = FindDefaultFontPath();
    if (!fontPath.empty() && font.Load(fontPath))
    {
        fontTexture = renderer->CreateTexture(font.Pixels().data(),
            FontAtlas::AtlasSize, FontAtlas::AtlasSize, 4);
    }
    else
    {
        DITTO_LOG_WARN("[UIRenderer] No usable font found; UI text will not render");
    }

    initialized = true;
    return true;
}

void UIRenderer::Shutdown()
{
    if (!renderer) return;
    if (quadMesh) renderer->DestroyMesh(quadMesh);
    if (pipeline) renderer->DestroyPipeline(pipeline);
    if (rectsSSBO) renderer->DestroyStorageBuffer(rectsSSBO);
    if (uvsSSBO) renderer->DestroyStorageBuffer(uvsSSBO);
    if (colorsSSBO) renderer->DestroyStorageBuffer(colorsSSBO);
    if (fontTexture) renderer->DestroyTexture(fontTexture);
    quadMesh = {}; pipeline = {}; rectsSSBO = {}; uvsSSBO = {}; colorsSSBO = {};
    fontTexture = {};
    initialized = false;
}

void UIRenderer::AppendText(std::vector<Instance>& out, const std::string& text,
    float fontSize, const glm::vec2& topLeft, const glm::vec4& color) const
{
    for (const FontAtlas::GlyphQuad& g : font.LayoutText(text, fontSize))
    {
        Instance inst;
        inst.rect = glm::vec4(topLeft.x + g.rect.x, topLeft.y + g.rect.y, g.rect.z, g.rect.w);
        inst.uvRect = g.uvRect;
        inst.color = color;
        out.push_back(inst);
    }
}

void UIRenderer::Flush(const std::vector<Instance>& instances, Ditto::TextureHandle texture)
{
    if (instances.empty() || !texture) return;

    std::vector<glm::vec4> rects, uvs, colors;
    rects.reserve(instances.size());
    uvs.reserve(instances.size());
    colors.reserve(instances.size());
    for (const Instance& inst : instances)
    {
        rects.push_back(inst.rect);
        uvs.push_back(inst.uvRect);
        colors.push_back(inst.color);
    }

    const size_t bytes = sizeof(glm::vec4) * instances.size();
    renderer->UpdateStorageBuffer(rectsSSBO, rects.data(), bytes);
    renderer->UpdateStorageBuffer(uvsSSBO, uvs.data(), bytes);
    renderer->UpdateStorageBuffer(colorsSSBO, colors.data(), bytes);

    renderer->BindTexture(2, texture);
    renderer->BindStorageBuffer(0, rectsSSBO);
    renderer->BindStorageBuffer(1, uvsSSBO);
    renderer->BindStorageBuffer(2, colorsSSBO);
    renderer->DrawInstanced(quadMesh, static_cast<int>(instances.size()));
}

void UIRenderer::Render(Scene* scene, int viewportWidth, int viewportHeight)
{
    if (!scene || !scene->rootGameObject) return;
    if (!Init(scene->renderer)) return;

    const float w = static_cast<float>(viewportWidth);
    const float h = static_cast<float>(viewportHeight);

    // Solid-color quads (buttons + untextured images) batch on the white
    // texture; textured images batch per texture; text batches on the font
    // atlas and is drawn LAST so labels sit on top of button backgrounds.
    std::vector<Instance> whiteBatch;
    std::map<uint32_t, std::pair<Ditto::TextureHandle, std::vector<Instance>>> texturedBatches;
    std::vector<Instance> textBatch;

    // Image UVs are V-flipped because material textures load with
    // stbi_set_flip_vertically_on_load(true) (image top stored at v=1).
    const glm::vec4 kImageUV(0.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 kWhiteUV(0.0f, 0.0f, 1.0f, 1.0f);

    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;

        for (const auto& comp : obj->components)
        {
            if (!comp || !comp->enabled) continue;

            if (comp->index == ComponentIndex::UIImage)
            {
                auto* img = static_cast<UIImageComponent*>(comp.get());
                Instance inst;
                inst.rect = ComputeUIRect(img->anchor, img->offset, img->size, w, h);
                inst.color = img->color;
                if (img->texturePath.empty())
                {
                    inst.uvRect = kWhiteUV;
                    whiteBatch.push_back(inst);
                }
                else
                {
                    Ditto::TextureHandle tex = scene->GetOrCreateMaterialTexture(img->texturePath);
                    inst.uvRect = kImageUV;
                    auto& bucket = texturedBatches[tex.id];
                    bucket.first = tex;
                    bucket.second.push_back(inst);
                }
            }
            else if (comp->index == ComponentIndex::UIButton)
            {
                auto* btn = static_cast<UIButtonComponent*>(comp.get());
                Instance inst;
                inst.rect = ComputeUIRect(btn->anchor, btn->offset, btn->size, w, h);
                inst.uvRect = kWhiteUV;
                inst.color = btn->pressed ? btn->pressedColor
                    : btn->hovered ? btn->hoverColor : btn->color;
                whiteBatch.push_back(inst);

                if (!btn->label.empty() && font.IsLoaded())
                {
                    glm::vec2 textSize;
                    font.LayoutText(btn->label, btn->fontSize, &textSize);
                    glm::vec2 topLeft(
                        inst.rect.x + (inst.rect.z - textSize.x) * 0.5f,
                        inst.rect.y + (inst.rect.w - textSize.y) * 0.5f);
                    AppendText(textBatch, btn->label, btn->fontSize, topLeft, btn->labelColor);
                }
            }
            else if (comp->index == ComponentIndex::UIText)
            {
                auto* txt = static_cast<UITextComponent*>(comp.get());
                if (txt->text.empty() || !font.IsLoaded()) continue;
                glm::vec2 textSize;
                font.LayoutText(txt->text, txt->fontSize, &textSize);
                glm::vec4 rect = ComputeUIRect(txt->anchor, txt->offset, textSize, w, h);
                AppendText(textBatch, txt->text, txt->fontSize, glm::vec2(rect.x, rect.y), txt->color);
            }
        }

        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(scene->rootGameObject.get());

    if (whiteBatch.empty() && texturedBatches.empty() && textBatch.empty()) return;

    // Frame uniforms: the UI shader only reads screenParams.
    Ditto::FrameUniforms fu;
    fu.screenParams = glm::vec4(glm::max(1.0f, w), glm::max(1.0f, h),
        1.0f + 1.0f / glm::max(1.0f, w), 1.0f + 1.0f / glm::max(1.0f, h));

    renderer->BindPipeline(pipeline);   // applies no-depth + alpha blend state
    renderer->SetFrameUniforms(fu);

    Flush(whiteBatch, scene->GetOrCreateMaterialTexture(""));
    for (auto& [id, bucket] : texturedBatches)
        Flush(bucket.second, bucket.first);
    Flush(textBatch, fontTexture);
}
