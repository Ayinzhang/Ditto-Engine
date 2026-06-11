#pragma once
#include "IRenderer.h"
#include <vector>

namespace Ditto
{
    // OpenGL implementation of the RHI. Sole owner of every GL object it creates;
    // callers hold opaque handles only and must release them via Destroy*.
    //
    // Lifetime: a GLRenderer must outlive everything that holds its handles
    // (Scene, the editor windows) and must be destroyed while the GL context is
    // still current (i.e. before glfwDestroyWindow/glfwTerminate).
    class GLRenderer : public IRenderer
    {
    public:
        // `window` is the GLFWwindow* (kept as void* so this header needs no GLFW
        // include); used by EndFrame to swap buffers.
        explicit GLRenderer(void* window) : m_window(window) {}
        ~GLRenderer() override;

        void BeginFrame() override {}
        void EndFrame() override;   // glfwSwapBuffers

        void ImGuiInit(void* window) override;
        void ImGuiShutdown() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(void* drawData) override;

        // State
        void SetViewport(int x, int y, int w, int h) override;
        void SetScissor(bool enabled, int x, int y, int w, int h) override;
        void Clear(uint32_t flags, const glm::vec4& color) override;
        void SetDepthState(bool enabled, DepthFunc func) override;
        void SetBlendState(bool enabled) override;
        void SetWireframe(bool enabled) override;
        void SetCullState(bool enabled) override;

        // Resources
        MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                              const std::vector<VertexAttrib>& attribs,
                              const uint32_t* indices, size_t indexCount) override;
        void DestroyMesh(MeshHandle) override;

        StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) override;
        void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) override;
        void DestroyStorageBuffer(StorageBufferHandle) override;

        PipelineHandle CreatePipeline(const std::string& hlslSource) override;
        void DestroyPipeline(PipelineHandle) override;

        TextureHandle CreateTexture(const unsigned char* pixels, int w, int h, int channels) override;
        void DestroyTexture(TextureHandle) override;
        void* GetImGuiTextureID(TextureHandle) override;
        void BindTexture(int binding, TextureHandle) override;

        RenderTargetHandle CreateRenderTarget(int w, int h) override;
        void BeginRenderTarget(RenderTargetHandle) override;
        void EndRenderTarget() override;
        TextureHandle GetColorTexture(RenderTargetHandle) override;
        void DestroyRenderTarget(RenderTargetHandle) override;

        // Draw
        void BindPipeline(PipelineHandle) override;
        void SetFrameUniforms(const FrameUniforms&) override;
        void BindStorageBuffer(int binding, StorageBufferHandle) override;
        void DrawInstanced(MeshHandle, int instanceCount) override;

    private:
        // GL object types kept as plain unsigned int so this header needs no
        // GLAD include. id 0 = released/empty slot.
        struct GLMesh    { unsigned int vao = 0, vbo = 0, ebo = 0; uint32_t vertexCount = 0, indexCount = 0; };
        struct GLBuffer  { unsigned int ssbo = 0; size_t size = 0; };
        struct GLPipeline{ unsigned int program = 0; };
        struct GLTexture { unsigned int tex = 0; };
        struct GLRenderTargetRes
        {
            unsigned int fbo = 0, rbo = 0;
            int w = 0, h = 0;
            TextureHandle color;   // non-owning view into m_textures (freed by DestroyRenderTarget)
        };

        // Resource tables. Handle id == index + 1. Slots are not reused on
        // destroy (a handful of preview reloads at most), so growth is bounded.
        std::vector<GLMesh>            m_meshes;
        std::vector<GLBuffer>          m_buffers;
        std::vector<GLPipeline>        m_pipelines;
        std::vector<GLTexture>         m_textures;
        std::vector<GLRenderTargetRes> m_renderTargets;

        void* m_window = nullptr;   // GLFWwindow* for buffer swap
        unsigned int m_currentProgram = 0;
        unsigned int m_frameUBO = 0;   // FrameUniforms std140 UBO (binding 0)

        // Saved default-framebuffer state across BeginRenderTarget/EndRenderTarget
        // (single level; the preview never nests render targets).
        int m_savedViewport[4] = { 0, 0, 0, 0 };
        unsigned int m_savedFBO = 0;
    };
}
