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
        // `window` is the GLFWwindow* (NO_API) used to create the present surface.
        explicit VulkanRenderer(void* window);
        ~VulkanRenderer() override;

        // True once the instance + device were created successfully.
        bool IsValid() const { return m_device != VK_NULL_HANDLE; }

        // ---- Frame ----
        void BeginFrame() override;
        void EndFrame() override;

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
        bool CreateSurface();
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();

        // Swapchain + frame resources.
        bool CreateSwapchain();
        void CreateImageViews();
        bool CreateRenderPass();
        void CreateFramebuffers();
        bool CreateCommandResources();
        bool CreateSyncObjects();
        void CleanupSwapchain();
        bool RecreateSwapchain();

        static constexpr int kFramesInFlight = 2;

        void* m_window = nullptr;   // GLFWwindow*

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamily = UINT32_MAX;
        uint32_t m_presentQueueFamily = UINT32_MAX;

        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkFormat m_swapchainFormat{};
        VkExtent2D m_swapchainExtent{};
        std::vector<VkImage> m_swapchainImages;
        std::vector<VkImageView> m_swapchainImageViews;
        std::vector<VkFramebuffer> m_framebuffers;
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_commandBuffers;   // [kFramesInFlight]
        std::vector<VkSemaphore> m_imageAvailable;       // [kFramesInFlight]
        std::vector<VkSemaphore> m_renderFinished;       // [swapchain image count]
        std::vector<VkFence> m_inFlight;                 // [kFramesInFlight]

        uint32_t m_currentFrame = 0;
        uint32_t m_imageIndex = 0;
        bool m_frameActive = false;   // a command buffer is currently recording
        bool m_ready = false;         // full init succeeded
        glm::vec4 m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

        bool m_validation = false;
    };
}
