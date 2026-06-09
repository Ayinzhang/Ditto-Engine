#pragma once
#include "../IRenderer.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Ditto
{
    // Vulkan implementation of the RHI. Skeleton stage: creates an instance,
    // picks a physical device, and creates a logical device + graphics queue.
    // Swapchain / pipelines / resources are filled in by later steps; the
    // IRenderer methods are stubbed until then. Engine still uses GLRenderer;
    // this only needs to compile + link against the Vulkan loader for now.
    class VulkanRenderer : public IRenderer
    {
    public:
        VulkanRenderer();
        ~VulkanRenderer() override;

        // True once the instance + device were created successfully.
        bool IsValid() const { return m_device != VK_NULL_HANDLE; }

        // ---- State ----
        void SetViewport(int x, int y, int w, int h) override;
        void SetScissor(bool enabled, int x, int y, int w, int h) override;
        void Clear(uint32_t flags, const glm::vec4& color) override;
        void SetDepthState(bool enabled, DepthFunc func) override;
        void SetBlendState(bool enabled) override;
        void SetWireframe(bool enabled) override;
        void SetCullState(bool enabled) override;

        // ---- Resources ----
        MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                              const std::vector<VertexAttrib>& attribs,
                              const uint32_t* indices, size_t indexCount) override;
        void DestroyMesh(MeshHandle) override;
        StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) override;
        void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) override;
        void DestroyStorageBuffer(StorageBufferHandle) override;
        PipelineHandle CreatePipeline(const std::string& vertexSrc, const std::string& fragmentSrc) override;
        void DestroyPipeline(PipelineHandle) override;
        TextureHandle CreateTexture(const unsigned char* pixels, int w, int h, int channels) override;
        void DestroyTexture(TextureHandle) override;
        void* GetImGuiTextureID(TextureHandle) override;
        RenderTargetHandle CreateRenderTarget(int w, int h) override;
        void BeginRenderTarget(RenderTargetHandle) override;
        void EndRenderTarget() override;
        TextureHandle GetColorTexture(RenderTargetHandle) override;
        void DestroyRenderTarget(RenderTargetHandle) override;

        // ---- Draw ----
        void BindPipeline(PipelineHandle) override;
        void SetFrameUniforms(const FrameUniforms&) override;
        void BindStorageBuffer(int binding, StorageBufferHandle) override;
        void DrawInstanced(MeshHandle, int instanceCount) override;

    private:
        bool CreateInstance();
        bool SetupDebugMessenger();
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamily = UINT32_MAX;

        bool m_validation = false;
    };
}
