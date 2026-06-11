#pragma once
#include "../IRenderer.h"
#include <vulkan/vulkan.h>
#include "../../../../3rdParty/VMA/vk_mem_alloc.h"
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

        // ---- ImGui ----
        void ImGuiInit(void* window) override;
        void ImGuiShutdown() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(void* drawData) override;

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
        // All buffer/image memory goes through VMA: one allocator sub-allocates
        // big device memory blocks, instead of one vkAllocateMemory per resource
        // (drivers cap total allocations at maxMemoryAllocationCount, often 4096).
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
        std::vector<VkCommandBuffer> m_commandBuffers;   // [kFramesInFlight]
        std::vector<VkSemaphore> m_imageAvailable;       // [kFramesInFlight]
        std::vector<VkSemaphore> m_renderFinished;       // [swapchain image count]
        std::vector<VkFence> m_inFlight;                 // [kFramesInFlight]

        uint32_t m_currentFrame = 0;
        uint32_t m_imageIndex = 0;
        bool m_frameActive = false;   // a command buffer is currently recording
        bool m_ready = false;         // full init succeeded
        glm::vec4 m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

        // ImGui + textures.
        VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
        bool m_imguiInit = false;
        VkSampler m_sampler = VK_NULL_HANDLE;
        struct VkTextureRes
        {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkDescriptorSet descriptor = VK_NULL_HANDLE;   // ImGui texture id
            bool pendingDestroy = false;
        };
        std::vector<VkTextureRes> m_textures;
        std::vector<TextureHandle> m_pendingTextureDestroys;

        VkCommandBuffer BeginSingleTime();
        void EndSingleTime(VkCommandBuffer cmd);

        // ---- Scene rendering (Vk4) ----
        // Depth buffer for the swapchain render pass (recreated with the swapchain).
        VkImage m_depthImage = VK_NULL_HANDLE;
        VmaAllocation m_depthMem = VK_NULL_HANDLE;
        VkImageView m_depthView = VK_NULL_HANDLE;
        VkFormat m_depthFormat{};

        struct VkMeshRes
        {
            VkBuffer vbuf = VK_NULL_HANDLE;
            VmaAllocation vmem = VK_NULL_HANDLE;
            VkBuffer ibuf = VK_NULL_HANDLE;          // optional uint32 index buffer
            VmaAllocation imem = VK_NULL_HANDLE;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;                 // 0 => non-indexed draw
        };
        // Storage buffers are per-frame-in-flight (host-visible, persistently mapped)
        // so a frame's writes don't stomp a buffer the GPU is still reading.
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
            VkDescriptorSetLayout setLayouts[2] = {};   // set0=UBO, set1=2 SSBOs
            VkShaderModule vs = VK_NULL_HANDLE;
            VkShaderModule fs = VK_NULL_HANDLE;
        };
        std::vector<VkMeshRes> m_meshes;
        std::vector<VkStorageRes> m_storage;
        std::vector<VkPipelineRes> m_pipelines;

        // Per-frame-in-flight FrameUniforms UBO ring + a shared set-0 descriptor
        // set per frame (regular, not push -- a layout may have only ONE push set,
        // which we reserve for the storage buffers in set 1).
        VkBuffer m_uboBuf[kFramesInFlight] = {};
        VmaAllocation m_uboMem[kFramesInFlight] = {};
        void* m_uboMapped[kFramesInFlight] = {};
        VkDescriptorSetLayout m_uboSetLayout = VK_NULL_HANDLE;   // shared set-0 layout (dynamic UBO)
        VkDescriptorPool m_uboPool = VK_NULL_HANDLE;
        VkDescriptorSet m_uboSets[kFramesInFlight] = {};
        // The UBO is a ring of slots within each frame: every SetFrameUniforms call
        // (e.g. the Scene then Game viewport) writes its own slot, and the draws that
        // follow bind that slot via a dynamic offset. Avoids one viewport's uniforms
        // overwriting another's within a frame.
        static constexpr uint32_t kUboSlotSize = 256;   // >= sizeof(FrameUniforms), 256-aligned
        static constexpr uint32_t kUboSlots = 16;
        uint32_t m_uboSlot = 0;

        // ---- Offscreen render targets (editor viewports / model previews) ----
        // The color attachment lives in m_textures (so GetImGuiTextureID works and
        // DestroyTexture owns image/view/descriptor); the RT owns depth + framebuffer.
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
        // Offscreen pass: color CLEAR -> SHADER_READ_ONLY (sampled by ImGui), depth CLEAR.
        // Color format matches the swapchain so scene pipelines (created against
        // m_renderPass) stay render-pass compatible.
        VkRenderPass m_rtRenderPass = VK_NULL_HANDLE;
        // Swapchain pass re-begun after an RT pass interrupts it (color LOAD).
        VkRenderPass m_resumePass = VK_NULL_HANDLE;
        bool m_rtActive = false;       // inside a Begin/EndRenderTarget pair
        VkExtent2D m_rtExtent{};
        bool EnsureRenderTargetPasses();

        // Push descriptors (avoids descriptor-pool/set management for scene draws).
        PFN_vkCmdPushDescriptorSetKHR m_pushDescriptor = nullptr;
        bool m_pushDescriptorOK = false;

        // Current-draw state recorded between BindPipeline/BindStorageBuffer/DrawInstanced.
        VkPipelineRes* m_boundPipeline = nullptr;
        StorageBufferHandle m_boundStorage[2];
        TextureHandle m_boundTextures[4];

        // Allocate a buffer through VMA. `hostVisible` buffers are HOST_COHERENT
        // and persistently mapped (pointer returned via `outMapped`); device-local
        // buffers pass hostVisible=false.
        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                          VkBuffer& outBuf, VmaAllocation& outAlloc, void** outMapped = nullptr);
        bool CreateUboDescriptors();
        bool CreateDepthResources();
        void ProcessDeferredDestroys();
        void DestroyDepthResources();
        VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);

        bool m_validation = false;
    };
}
