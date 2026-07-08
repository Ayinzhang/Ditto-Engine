#pragma once
#include "../IRenderer.h"
#include <vulkan/vulkan.h>
#include "../../../../3rdParty/VMA/vk_mem_alloc.h"
#include <vector>
#include <cstdint>

namespace Ditto
{
    
    
    
    class VulkanRenderer : public IRenderer
    {
    public:
        explicit VulkanRenderer(IWindow* window);
        ~VulkanRenderer() override;

        
        bool IsValid() const { return m_device != VK_NULL_HANDLE; }
        RendererBackend Backend() const override { return RendererBackend::Vulkan; }

        
        void BeginFrame() override;
        void EndFrame() override;
        bool NotifyWindowResized(int width, int height) override;

        
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
        bool CreateInstance();
        bool SetupDebugMessenger();
        bool CreateSurface();
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();

        
        bool CreateSwapchain();
        void CreateImageViews();
        bool CreateRenderPass();
        void CreateFramebuffers();
        bool CreateCommandResources();
        bool CreateSyncObjects();
        void CleanupSwapchain();
        bool RecreateSwapchain();

        static constexpr int kFramesInFlight = 2;

        IWindow* m_window = nullptr;

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        
        
        
        VmaAllocator m_allocator = VK_NULL_HANDLE;
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
        std::vector<VkCommandBuffer> m_commandBuffers;   
        std::vector<VkSemaphore> m_imageAvailable;       
        std::vector<VkSemaphore> m_renderFinished;       
        std::vector<VkFence> m_inFlight;                 

        uint32_t m_currentFrame = 0;
        uint32_t m_imageIndex = 0;
        bool m_frameActive = false;   
        bool m_ready = false;         
        glm::vec4 m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

        
        VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
        bool m_imguiInit = false;
        IWindow* m_imguiWindow = nullptr;
        VkSampler m_sampler = VK_NULL_HANDLE;
        struct VkTextureRes
        {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkDescriptorSet descriptor = VK_NULL_HANDLE;   
            bool pendingDestroy = false;
        };
        std::vector<VkTextureRes> m_textures;
        std::vector<TextureHandle> m_pendingTextureDestroys;

        VkCommandBuffer BeginSingleTime();
        void EndSingleTime(VkCommandBuffer cmd);
        void EnsureSampler();

        
        
        VkImage m_depthImage = VK_NULL_HANDLE;
        VmaAllocation m_depthMem = VK_NULL_HANDLE;
        VkImageView m_depthView = VK_NULL_HANDLE;
        VkFormat m_depthFormat{};

        struct VkMeshRes
        {
            VkBuffer vbuf = VK_NULL_HANDLE;
            VmaAllocation vmem = VK_NULL_HANDLE;
            VkBuffer ibuf = VK_NULL_HANDLE;          
            VmaAllocation imem = VK_NULL_HANDLE;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;                 
        };
        
        
        struct VkStorageRes
        {
            VkBuffer buf[kFramesInFlight] = {};
            VmaAllocation mem[kFramesInFlight] = {};
            void* mapped[kFramesInFlight] = {};
            size_t size = 0;
        };
        struct VkPipelineRes
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            VkDescriptorSetLayout setLayouts[2] = {};   
            VkShaderModule vs = VK_NULL_HANDLE;
            VkShaderModule fs = VK_NULL_HANDLE;
            bool usesSceneResources = true;
        };
        std::vector<VkMeshRes> m_meshes;
        std::vector<VkStorageRes> m_storage;
        std::vector<VkPipelineRes> m_pipelines;

        
        
        
        VkBuffer m_uboBuf[kFramesInFlight] = {};
        VmaAllocation m_uboMem[kFramesInFlight] = {};
        void* m_uboMapped[kFramesInFlight] = {};
        VkDescriptorSetLayout m_uboSetLayout = VK_NULL_HANDLE;   
        VkDescriptorPool m_uboPool = VK_NULL_HANDLE;
        VkDescriptorSet m_uboSets[kFramesInFlight] = {};
        
        
        
        
        static constexpr uint32_t kUboSlotSize = 256;   
        static constexpr uint32_t kUboSlots = 16;
        uint32_t m_uboSlot = 0;

        
        
        
        struct VkRenderTargetRes
        {
            TextureHandle color;
            VkImage depthImage = VK_NULL_HANDLE;
            VmaAllocation depthMem = VK_NULL_HANDLE;
            VkImageView depthView = VK_NULL_HANDLE;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            int w = 0, h = 0;
        };
        std::vector<VkRenderTargetRes> m_renderTargets;
        
        
        
        VkRenderPass m_rtRenderPass = VK_NULL_HANDLE;
        
        VkRenderPass m_resumePass = VK_NULL_HANDLE;
        bool m_rtActive = false;       
        VkExtent2D m_rtExtent{};
        bool EnsureRenderTargetPasses();

        
        PFN_vkCmdPushDescriptorSetKHR m_pushDescriptor = nullptr;
        bool m_pushDescriptorOK = false;

        
        VkPipelineRes* m_boundPipeline = nullptr;
        StorageBufferHandle m_boundStorage[2];
        TextureHandle m_boundTextures[4];

        
        
        
        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                          VkBuffer& outBuf, VmaAllocation& outAlloc, void** outMapped = nullptr);
        bool CreateUboDescriptors();
        bool CreateDepthResources();
        void ProcessDeferredDestroys();
        void WaitGpuIdleForDestroy();
        void DestroyDepthResources();
        VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

        bool m_validation = false;
    };
}
