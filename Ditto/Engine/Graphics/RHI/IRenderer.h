#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "../../../3rdParty/GLM/glm.hpp"







namespace Ditto
{
    class IWindow;

    enum class RendererBackend
    {
        OpenGL,
        Vulkan,
        DirectX12,
    };

    
    
    struct MeshHandle          { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct StorageBufferHandle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct PipelineHandle      { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct TextureHandle       { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
    struct RenderTargetHandle  { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

    
    
    struct VertexAttrib
    {
        int location;        
        int componentCount;  
        int offsetFloats;    
    };

    
    
    
    struct FrameUniforms
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 projection{ 1.0f };
        glm::vec3 viewPos{ 0.0f };
        glm::vec3 lightColor{ 1.0f };
        glm::vec3 lightDir{ 0.0f, -1.0f, 0.0f };
        float lightIntensity = 1.0f;
        glm::vec4 time{ 0.0f };        
        glm::vec4 sinTime{ 0.0f };
        glm::vec4 cosTime{ 1.0f };
        glm::vec4 deltaTime{ 0.0f };   
        glm::vec4 screenParams{ 0.0f }; 
    };

    enum ClearFlags : uint32_t { ClearColor = 1u << 0, ClearDepth = 1u << 1 };
    enum class DepthFunc { Less, LessEqual };
    enum class CullMode { Off, Back, Front };

    struct PipelineState
    {
        std::string renderType = "Opaque";
        int renderQueue = 2000; 
        bool depthTest = true;
        bool depthWrite = true;
        DepthFunc depthFunc = DepthFunc::Less;
        bool blend = false;
        bool wireframe = false;
        bool usesSceneResources = true; 
        bool renderToTexture = false;
        CullMode cull = CullMode::Off;
        int vertexStrideFloats = 8;
        std::vector<VertexAttrib> vertexAttributes{ { 0, 3, 0 }, { 1, 3, 3 }, { 2, 2, 6 } };
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        virtual RendererBackend Backend() const = 0;

        
        
        
        
        
        virtual void BeginFrame() {}
        virtual void EndFrame() {}
        virtual bool NotifyWindowResized(int width, int height) { return width > 0 && height > 0; }

        
        
        
        
        
        virtual void ImGuiInit(IWindow* window) {}
        virtual void ImGuiShutdown() {}
        virtual void ImGuiNewFrame() {}
        virtual void ImGuiRenderDrawData(void* drawData) {}

        
        virtual void SetViewport(int x, int y, int w, int h) = 0;
        virtual void SetScissor(bool enabled, int x = 0, int y = 0, int w = 0, int h = 0) = 0;
        virtual void Clear(uint32_t flags, const glm::vec4& color) = 0;
        virtual void SetDepthState(bool enabled, DepthFunc func = DepthFunc::Less) = 0;
        virtual void SetBlendState(bool enabled) = 0;
        virtual void SetWireframe(bool enabled) = 0;
        virtual void SetCullState(bool enabled) = 0;   

        
        virtual MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                                      const std::vector<VertexAttrib>& attribs,
                                      const uint32_t* indices = nullptr, size_t indexCount = 0) = 0;
        virtual void DestroyMesh(MeshHandle) = 0;

        virtual StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) = 0;
        virtual void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) = 0;
        virtual void DestroyStorageBuffer(StorageBufferHandle) = 0;

        
        
        virtual PipelineHandle CreatePipeline(const std::string& hlslSource, const PipelineState& state = {}) = 0;
        virtual void DestroyPipeline(PipelineHandle) = 0;

        virtual TextureHandle CreateTexture(const unsigned char* pixels, int w, int h, int channels) = 0;
        virtual void DestroyTexture(TextureHandle) = 0;
        virtual void* GetImGuiTextureID(TextureHandle) = 0;   
        virtual void BindTexture(int binding, TextureHandle) {}

        virtual RenderTargetHandle CreateRenderTarget(int w, int h) = 0;
        virtual void BeginRenderTarget(RenderTargetHandle) = 0;
        virtual void EndRenderTarget() = 0;
        virtual TextureHandle GetColorTexture(RenderTargetHandle) = 0;
        virtual bool ReadRenderTargetPixels(RenderTargetHandle, std::vector<unsigned char>& rgba) { return false; }
        virtual void DestroyRenderTarget(RenderTargetHandle) = 0;

        
        virtual void BindPipeline(PipelineHandle) = 0;
        virtual void SetFrameUniforms(const FrameUniforms&) = 0;
        virtual void BindStorageBuffer(int binding, StorageBufferHandle) = 0;
        virtual void DrawInstanced(MeshHandle, int instanceCount) = 0;
    };
}
