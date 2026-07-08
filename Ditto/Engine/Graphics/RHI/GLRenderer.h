#pragma once
#include "IRenderer.h"
#include <vector>

namespace Ditto
{
    
    
    
    
    
    
    class GLRenderer : public IRenderer
    {
    public:
        explicit GLRenderer(IWindow* window) : m_window(window) {}
        ~GLRenderer() override;

        RendererBackend Backend() const override { return RendererBackend::OpenGL; }

        void BeginFrame() override {}
        void EndFrame() override;

        void ImGuiInit(IWindow* window) override;
        void ImGuiShutdown() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(void* drawData) override;

        
        void SetViewport(int x, int y, int w, int h) override;
        void SetScissor(bool enabled, int x, int y, int w, int h) override;
        void Clear(uint32_t flags, const glm::vec4& color) override;
        void SetDepthState(bool enabled, DepthFunc func) override;
        void SetBlendState(bool enabled) override;
        void SetWireframe(bool enabled) override;
        void SetCullState(bool enabled) override;

        
        MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                              const std::vector<VertexAttrib>& attribs,
                              const uint32_t* indices, size_t indexCount) override;
        void DestroyMesh(MeshHandle) override;

        StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) override;
        void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) override;
        void DestroyStorageBuffer(StorageBufferHandle) override;

        PipelineHandle CreatePipeline(const std::string& hlslSource, const PipelineState& state) override;
        void DestroyPipeline(PipelineHandle) override;

        TextureHandle CreateTexture(const unsigned char* pixels, int w, int h, int channels) override;
        void DestroyTexture(TextureHandle) override;
        void* GetImGuiTextureID(TextureHandle) override;
        void BindTexture(int binding, TextureHandle) override;

        RenderTargetHandle CreateRenderTarget(int w, int h) override;
        void BeginRenderTarget(RenderTargetHandle) override;
        void EndRenderTarget() override;
        TextureHandle GetColorTexture(RenderTargetHandle) override;
        bool ReadRenderTargetPixels(RenderTargetHandle, std::vector<unsigned char>& rgba) override;
        void DestroyRenderTarget(RenderTargetHandle) override;

        
        void BindPipeline(PipelineHandle) override;
        void SetFrameUniforms(const FrameUniforms&) override;
        void BindStorageBuffer(int binding, StorageBufferHandle) override;
        void DrawInstanced(MeshHandle, int instanceCount) override;

    private:
        
        
        struct GLMesh    { unsigned int vao = 0, vbo = 0, ebo = 0; uint32_t vertexCount = 0, indexCount = 0; };
        struct GLBuffer  { unsigned int ssbo = 0; size_t size = 0; };
        struct GLPipeline{ unsigned int program = 0; PipelineState state; };
        struct GLTexture { unsigned int tex = 0; };
        struct GLRenderTargetRes
        {
            unsigned int fbo = 0, rbo = 0;
            int w = 0, h = 0;
            TextureHandle color;   
        };

        
        
        std::vector<GLMesh>            m_meshes;
        std::vector<GLBuffer>          m_buffers;
        std::vector<GLPipeline>        m_pipelines;
        std::vector<GLTexture>         m_textures;
        std::vector<GLRenderTargetRes> m_renderTargets;

        IWindow* m_window = nullptr;
        IWindow* m_imguiWindow = nullptr;
        unsigned int m_currentProgram = 0;
        unsigned int m_frameUBO = 0;   
        unsigned int m_defaultSampler = 0;

        
        
        int m_savedViewport[4] = { 0, 0, 0, 0 };
        unsigned int m_savedFBO = 0;
    };
}
