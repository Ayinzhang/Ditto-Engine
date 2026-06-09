#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "../../../3rdParty/GLM/glm.hpp"

// ---------------------------------------------------------------------------
// Ditto Render Hardware Interface (RHI)
//
// A thin abstraction over the graphics API so engine/editor code never calls
// gl*/vk* directly. GLRenderer implements this with OpenGL today; a future
// VulkanRenderer (or a library backend) can implement the same interface
// without touching Scene/Engine/Editor code.
//
// Phase 0 goal: behavior identical to the previous direct-OpenGL path.
// ---------------------------------------------------------------------------
namespace Ditto
{
    // Opaque resource handles. id == 0 means invalid. The value indexes into a
    // backend-private resource table, so a Vulkan backend can reuse the tickets.
    struct MeshHandle          { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct StorageBufferHandle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct PipelineHandle      { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct TextureHandle       { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct RenderTargetHandle  { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

    // One vertex attribute, described in floats. Captured explicitly because a
    // Vulkan backend needs it verbatim for VkVertexInputAttributeDescription.
    struct VertexAttrib
    {
        int location;        // shader attribute location
        int componentCount;  // e.g. 3 for a vec3
        int offsetFloats;    // offset within one vertex, in floats
    };

    // Fixed per-frame uniform set for the scene shader. Modeled as an explicit
    // block (NOT immediate-mode by-name setters) so a Vulkan backend can back it
    // with a UBO / push constants without changing any call site.
    struct FrameUniforms
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 projection{ 1.0f };
        glm::vec3 viewPos{ 0.0f };
        glm::vec3 lightColor{ 1.0f };
        glm::vec3 lightDir{ 0.0f, -1.0f, 0.0f };
        float lightIntensity = 1.0f;
    };

    enum ClearFlags : uint32_t { ClearColor = 1u << 0, ClearDepth = 1u << 1 };
    enum class DepthFunc { Less, LessEqual };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // ---- Frame lifecycle ----
        // GL: BeginFrame is a no-op, EndFrame swaps buffers. Vulkan: BeginFrame
        // acquires a swapchain image + begins the command buffer/render pass,
        // EndFrame ends + submits + presents. Default no-ops so a backend can
        // opt in incrementally.
        virtual void BeginFrame() {}
        virtual void EndFrame() {}

        // ---- Dear ImGui backend (platform + renderer), implemented per-backend.
        // ImGuiInit sets up the GLFW platform + the renderer backend; ImGuiNewFrame
        // runs both backends' NewFrame; ImGuiRenderDrawData records the draw data
        // (into the current command buffer on Vulkan). The caller still owns
        // ImGui::CreateContext/NewFrame/Render/DestroyContext.
        virtual void ImGuiInit(void* window) {}
        virtual void ImGuiShutdown() {}
        virtual void ImGuiNewFrame() {}
        virtual void ImGuiRenderDrawData(void* drawData) {}

        // ---- State commands (stateless; safe to call mid-ImGui-frame) ----
        virtual void SetViewport(int x, int y, int w, int h) = 0;
        virtual void SetScissor(bool enabled, int x = 0, int y = 0, int w = 0, int h = 0) = 0;
        virtual void Clear(uint32_t flags, const glm::vec4& color) = 0;
        virtual void SetDepthState(bool enabled, DepthFunc func = DepthFunc::Less) = 0;
        virtual void SetBlendState(bool enabled) = 0;
        virtual void SetWireframe(bool enabled) = 0;
        virtual void SetCullState(bool enabled) = 0;   // back-face culling when enabled

        // ---- Resources (the renderer owns the underlying GPU objects) ----
        virtual MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                                      const std::vector<VertexAttrib>& attribs,
                                      const uint32_t* indices = nullptr, size_t indexCount = 0) = 0;
        virtual void DestroyMesh(MeshHandle) = 0;

        virtual StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) = 0;
        virtual void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) = 0;
        virtual void DestroyStorageBuffer(StorageBufferHandle) = 0;

        // One combined HLSL source with `VSMain` + `PSMain` entry points. Compiled
        // (HLSL->SPIR-V->per-backend) by the shared ShaderCompiler.
        virtual PipelineHandle CreatePipeline(const std::string& hlslSource) = 0;
        virtual void DestroyPipeline(PipelineHandle) = 0;

        virtual TextureHandle CreateTexture(const unsigned char* pixels, int w, int h, int channels) = 0;
        virtual void DestroyTexture(TextureHandle) = 0;
        virtual void* GetImGuiTextureID(TextureHandle) = 0;   // ImTextureID (void*); backend-specific

        virtual RenderTargetHandle CreateRenderTarget(int w, int h) = 0;
        virtual void BeginRenderTarget(RenderTargetHandle) = 0;
        virtual void EndRenderTarget() = 0;
        virtual TextureHandle GetColorTexture(RenderTargetHandle) = 0;
        virtual void DestroyRenderTarget(RenderTargetHandle) = 0;

        // ---- Draw ----
        virtual void BindPipeline(PipelineHandle) = 0;
        virtual void SetFrameUniforms(const FrameUniforms&) = 0;
        virtual void BindStorageBuffer(int binding, StorageBufferHandle) = 0;
        virtual void DrawInstanced(MeshHandle, int instanceCount) = 0;
    };
}
