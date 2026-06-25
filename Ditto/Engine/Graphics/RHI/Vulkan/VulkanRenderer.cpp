// VMA implementation lives in this TU (the header is included via VulkanRenderer.h).
#pragma warning(push)
#pragma warning(disable: 4100 4189 4127 4324)   // VMA: unreferenced params/locals, const conditionals, padding
#define VMA_IMPLEMENTATION
#include "../../../../3rdParty/VMA/vk_mem_alloc.h"
#pragma warning(pop)

#include "VulkanRenderer.h"
#include "../../../Core/IWindow.h"
#include "../../../Core/Logger.h"
#include "../../Shaders/ShaderCompiler.h"
#include "../../../../3rdParty/ImGui/imgui.h"
#include "../../../../3rdParty/ImGui/imgui_impl_vulkan.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace Ditto
{
    // ---- Debug messenger plumbing (extension functions are loaded at runtime) ----
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* /*user*/)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            Logger::Get().Error(std::string("[Vulkan] ") + data->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            Logger::Get().Warning(std::string("[Vulkan] ") + data->pMessage);
        return VK_FALSE;
    }

    static bool HasValidationLayer()
    {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        for (const auto& l : layers)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) return true;
        return false;
    }

    VulkanRenderer::VulkanRenderer(IWindow* window)
        : m_window(window)
    {
#ifdef _DEBUG
        m_validation = HasValidationLayer();
#endif
        if (!CreateInstance()) return;
        if (m_validation) SetupDebugMessenger();
        if (!CreateSurface()) return;
        if (!PickPhysicalDevice()) return;
        if (!CreateLogicalDevice()) return;
        if (!CreateSwapchain()) return;
        CreateImageViews();
        if (!CreateRenderPass()) return;
        if (!CreateDepthResources()) return;
        CreateFramebuffers();
        if (!CreateCommandResources()) return;
        if (!CreateSyncObjects()) return;

        // FrameUniforms UBO ring (one per frame in flight), host-visible + mapped.
        // Each frame's buffer holds kUboSlots slots so the Scene and Game viewports
        // can each write their own uniforms within a frame (see SetFrameUniforms).
        constexpr VkDeviceSize kUboBufSize = (VkDeviceSize)kUboSlots * kUboSlotSize;
        for (int i = 0; i < kFramesInFlight; ++i)
            CreateBuffer(kUboBufSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, /*hostVisible=*/true,
                m_uboBuf[i], m_uboMem[i], &m_uboMapped[i]);
        if (!CreateUboDescriptors()) return;

        m_ready = true;
        Logger::Get().Verbose("[Vulkan] Renderer initialized (swapchain + depth + frame resources ready).");
    }

    VulkanRenderer::~VulkanRenderer()
    {
        if (m_device) vkDeviceWaitIdle(m_device);

        CleanupSwapchain();   // framebuffers + image views + swapchain

        if (m_device)
        {
            // Defensive ImGui/texture teardown (normally done by ImGuiShutdown()).
            for (auto& t : m_textures)
            {
                if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
                if (t.image) vmaDestroyImage(m_allocator, t.image, t.memory);
            }
            if (m_sampler)   vkDestroySampler(m_device, m_sampler, nullptr);
            if (m_imguiPool) vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);

            // Scene resources.
            for (auto& m : m_meshes)
            {
                if (m.vbuf) vmaDestroyBuffer(m_allocator, m.vbuf, m.vmem);
                if (m.ibuf) vmaDestroyBuffer(m_allocator, m.ibuf, m.imem);
            }
            for (auto& s : m_storage)
                for (int i = 0; i < kFramesInFlight; ++i)
                    if (s.buf[i]) vmaDestroyBuffer(m_allocator, s.buf[i], s.mem[i]);
            for (auto& p : m_pipelines)
            {
                if (p.pipeline) vkDestroyPipeline(m_device, p.pipeline, nullptr);
                if (p.layout)   vkDestroyPipelineLayout(m_device, p.layout, nullptr);
                if (p.setLayouts[0]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[0], nullptr);
                if (p.setLayouts[1]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[1], nullptr);
                if (p.vs) vkDestroyShaderModule(m_device, p.vs, nullptr);
                if (p.fs) vkDestroyShaderModule(m_device, p.fs, nullptr);
            }
            for (int i = 0; i < kFramesInFlight; ++i)
                if (m_uboBuf[i]) vmaDestroyBuffer(m_allocator, m_uboBuf[i], m_uboMem[i]);
            if (m_uboPool) vkDestroyDescriptorPool(m_device, m_uboPool, nullptr);
            if (m_uboSetLayout) vkDestroyDescriptorSetLayout(m_device, m_uboSetLayout, nullptr);

            // Render targets (their color textures were freed by the texture loop).
            for (auto& rt : m_renderTargets)
            {
                if (rt.framebuffer) vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
                if (rt.depthView)   vkDestroyImageView(m_device, rt.depthView, nullptr);
                if (rt.depthImage)  vmaDestroyImage(m_allocator, rt.depthImage, rt.depthMem);
            }
            if (m_rtRenderPass) vkDestroyRenderPass(m_device, m_rtRenderPass, nullptr);
            if (m_resumePass)   vkDestroyRenderPass(m_device, m_resumePass, nullptr);

            for (auto s : m_renderFinished) if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (auto s : m_imageAvailable) if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (auto f : m_inFlight)       if (f) vkDestroyFence(m_device, f, nullptr);
            if (m_renderPass)  vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        }

        if (m_allocator) vmaDestroyAllocator(m_allocator);
        if (m_device) vkDestroyDevice(m_device, nullptr);
        if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        if (m_debugMessenger)
        {
            auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
            if (destroyFn) destroyFn(m_instance, m_debugMessenger, nullptr);
        }
        if (m_instance) vkDestroyInstance(m_instance, nullptr);
    }

    bool VulkanRenderer::CreateSurface()
    {
        if (!m_window ||
            m_window->CreateVulkanSurface(m_instance, nullptr, &m_surface) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] window surface creation failed");
            return false;
        }
        return true;
    }

    bool VulkanRenderer::CreateInstance()
    {
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "Ditto";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "Ditto-Engine";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_3;

        // Extensions we will need for windowed presentation (surface) + debug.
        std::vector<const char*> extensions =
            m_window ? m_window->GetRequiredVulkanInstanceExtensions() : std::vector<const char*>{};
        if (extensions.empty())
        {
            Logger::Get().Error("[Vulkan] window system did not provide required instance extensions");
            return false;
        }
        if (m_validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        std::vector<const char*> layers;
        if (m_validation) layers.push_back("VK_LAYER_KHRONOS_validation");

        VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (uint32_t)extensions.size();
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = (uint32_t)layers.size();
        ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

        if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateInstance failed");
            return false;
        }
        return true;
    }

    bool VulkanRenderer::SetupDebugMessenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = DebugCallback;

        auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (!createFn) return false;
        return createFn(m_instance, &ci, nullptr, &m_debugMessenger) == VK_SUCCESS;
    }

    bool VulkanRenderer::PickPhysicalDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        if (count == 0) { Logger::Get().Error("[Vulkan] no GPUs with Vulkan support"); return false; }

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

        // For a device, find a graphics queue family and a present-capable family
        // (preferring one family that does both). Returns false if either is missing.
        auto findQueues = [&](VkPhysicalDevice dev, uint32_t& gfx, uint32_t& present) -> bool
        {
            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qprops(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());

            gfx = UINT32_MAX; present = UINT32_MAX;
            for (uint32_t i = 0; i < qCount; ++i)
            {
                bool isGfx = (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                VkBool32 canPresent = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &canPresent);

                if (isGfx && canPresent) { gfx = present = i; return true; }   // unified queue: ideal
                if (isGfx && gfx == UINT32_MAX) gfx = i;
                if (canPresent && present == UINT32_MAX) present = i;
            }
            return gfx != UINT32_MAX && present != UINT32_MAX;
        };

        VkPhysicalDevice fallback = VK_NULL_HANDLE;
        uint32_t fbGfx = UINT32_MAX, fbPresent = UINT32_MAX;
        for (VkPhysicalDevice dev : devices)
        {
            uint32_t gfx, present;
            if (!findQueues(dev, gfx, present)) continue;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_physicalDevice = dev; m_graphicsQueueFamily = gfx; m_presentQueueFamily = present;
                Logger::Get().Verbose(std::string("[Vulkan] GPU: ") + props.deviceName);
                return true;
            }
            if (fallback == VK_NULL_HANDLE) { fallback = dev; fbGfx = gfx; fbPresent = present; }
        }

        if (fallback != VK_NULL_HANDLE)
        {
            m_physicalDevice = fallback; m_graphicsQueueFamily = fbGfx; m_presentQueueFamily = fbPresent;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(fallback, &props);
            Logger::Get().Verbose(std::string("[Vulkan] GPU (fallback): ") + props.deviceName);
            return true;
        }
        Logger::Get().Error("[Vulkan] no device with graphics + present queues");
        return false;
    }

    bool VulkanRenderer::CreateLogicalDevice()
    {
        float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;

        // One queue create-info per unique family (graphics + present may share).
        std::vector<uint32_t> families = { m_graphicsQueueFamily };
        if (m_presentQueueFamily != m_graphicsQueueFamily) families.push_back(m_presentQueueFamily);
        for (uint32_t fam : families)
        {
            VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            qci.queueFamilyIndex = fam;
            qci.queueCount = 1;
            qci.pQueuePriorities = &priority;
            queueInfos.push_back(qci);
        }

        // Enable swapchain + push-descriptor (scene draws push UBO/SSBO descriptors
        // inline, avoiding descriptor-pool management). Push-descriptor is enabled
        // only if the device supports it.
        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        {
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, exts.data());
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
                {
                    deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
                    m_pushDescriptorOK = true;
                    break;
                }
        }
        if (!m_pushDescriptorOK)
        {
            Logger::Get().Warning("[Vulkan] VK_KHR_push_descriptor not supported; falling back to OpenGL.");
            return false;
        }

        VkPhysicalDeviceFeatures features{};
        features.fillModeNonSolid = VK_TRUE;   // wireframe (model preview parity)

        VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        ci.queueCreateInfoCount = (uint32_t)queueInfos.size();
        ci.pQueueCreateInfos = queueInfos.data();
        ci.enabledExtensionCount = (uint32_t)deviceExtensions.size();
        ci.ppEnabledExtensionNames = deviceExtensions.data();
        ci.pEnabledFeatures = &features;

        if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateDevice failed");
            return false;
        }
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);

        if (m_pushDescriptorOK)
            m_pushDescriptor = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(m_device, "vkCmdPushDescriptorSetKHR");

        // VMA allocator: every buffer/image allocation below goes through it.
        VmaAllocatorCreateInfo aci{};
        aci.instance = m_instance;
        aci.physicalDevice = m_physicalDevice;
        aci.device = m_device;
        aci.vulkanApiVersion = VK_API_VERSION_1_3;
        if (vmaCreateAllocator(&aci, &m_allocator) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vmaCreateAllocator failed");
            return false;
        }

        m_depthFormat = VK_FORMAT_D32_SFLOAT;   // universally supported as a depth attachment
        return true;
    }

    // ----------------------------------------------------------------------
    // IRenderer methods. Frame/clear/viewport are live; resource + draw paths
    // are implemented in later steps (pipelines, descriptor sets, etc.).
    // ----------------------------------------------------------------------
    void VulkanRenderer::SetViewport(int x, int y, int w, int h)
    {
        if (!m_frameActive) return;
        // Incoming coords are GL bottom-left origin; convert to Vulkan top-left.
        // Swapchain target: negative-height viewport flips Y to match GL (the
        // presented image must be top-up). Offscreen RT: positive height so the
        // image keeps GL's bottom-up memory order (see BeginRenderTarget).
        const float fbH = (float)(m_rtActive ? m_rtExtent.height : m_swapchainExtent.height);
        const float topY = fbH - (float)y - (float)h;
        VkViewport vp{};
        vp.x = (float)x;
        vp.y = m_rtActive ? topY : topY + (float)h;
        vp.width = (float)w;
        vp.height = m_rtActive ? (float)h : -(float)h;
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &vp);
    }
    void VulkanRenderer::SetScissor(bool enabled, int x, int y, int w, int h)
    {
        if (!m_frameActive) return;
        const VkExtent2D target = m_rtActive ? m_rtExtent : m_swapchainExtent;
        VkRect2D sc;
        if (enabled)
        {
            int topY = (int)target.height - y - h; if (topY < 0) topY = 0;
            sc.offset = { x, topY };
            sc.extent = { (uint32_t)w, (uint32_t)h };
        }
        else sc = VkRect2D{ {0, 0}, target };
        vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &sc);
    }
    void VulkanRenderer::Clear(uint32_t flags, const glm::vec4& color)
    {
        if (flags & ClearColor) m_clearColor = color;   // also feeds the next pass's loadOp clear
        if (!m_frameActive) return;

        // Mid-pass clear of the current target (offscreen RT or swapchain), so
        // GL-style "Clear() after BeginRenderTarget" works on Vulkan too.
        VkClearAttachment atts[2]{};
        uint32_t n = 0;
        if (flags & ClearColor)
        {
            atts[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            atts[n].colorAttachment = 0;
            atts[n].clearValue.color = { { color.r, color.g, color.b, color.a } };
            ++n;
        }
        if (flags & ClearDepth)
        {
            atts[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            atts[n].clearValue.depthStencil = { 1.0f, 0 };
            ++n;
        }
        if (n == 0) return;
        VkClearRect rect{ { {0, 0}, m_rtActive ? m_rtExtent : m_swapchainExtent }, 0, 1 };
        vkCmdClearAttachments(m_commandBuffers[m_currentFrame], n, atts, 1, &rect);
    }
    void VulkanRenderer::SetDepthState(bool, DepthFunc) {}
    void VulkanRenderer::SetBlendState(bool) {}
    void VulkanRenderer::SetWireframe(bool) {}
    void VulkanRenderer::SetCullState(bool) {}

    // ----------------------------- Helpers --------------------------------
    bool VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                                      VkBuffer& outBuf, VmaAllocation& outAlloc, void** outMapped)
    {
        VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = size ? size : 1;
        bci.usage = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        if (hostVisible)
        {
            aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                      | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            // COHERENT required so persistently-mapped writes need no manual flush
            // (matches the previous explicit HOST_VISIBLE|HOST_COHERENT behavior).
            aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &aci, &outBuf, &outAlloc, &info) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vmaCreateBuffer failed");
            return false;
        }
        if (outMapped) *outMapped = info.pMappedData;
        return true;
    }

    bool VulkanRenderer::CreateUboDescriptors()
    {
        // Shared set-0 layout (UBO @ binding 0), used by all pipelines. Regular
        // (not push), so set 1 can be the single allowed push-descriptor set.
        // DYNAMIC so the per-viewport UBO slot is selected via a dynamic offset at
        // bind time (one descriptor set, many slots within the frame's buffer).
        VkDescriptorSetLayoutBinding ub{};
        ub.binding = 0; ub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; ub.descriptorCount = 1;
        ub.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        lc.bindingCount = 1; lc.pBindings = &ub;
        if (vkCreateDescriptorSetLayout(m_device, &lc, nullptr, &m_uboSetLayout) != VK_SUCCESS) return false;

        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kFramesInFlight };
        VkDescriptorPoolCreateInfo pc{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pc.maxSets = kFramesInFlight; pc.poolSizeCount = 1; pc.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(m_device, &pc, nullptr, &m_uboPool) != VK_SUCCESS) return false;

        VkDescriptorSetLayout layouts[kFramesInFlight];
        for (int i = 0; i < kFramesInFlight; ++i) layouts[i] = m_uboSetLayout;
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = m_uboPool; ai.descriptorSetCount = kFramesInFlight; ai.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(m_device, &ai, m_uboSets) != VK_SUCCESS) return false;

        // Point each frame's set at its UBO buffer (contents update via mapped memory).
        // range = one slot; the active slot is chosen by the dynamic offset at bind.
        for (int i = 0; i < kFramesInFlight; ++i)
        {
            VkDescriptorBufferInfo bi{ m_uboBuf[i], 0, kUboSlotSize };
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_uboSets[i]; w.dstBinding = 0; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
        }
        return true;
    }

    bool VulkanRenderer::CreateDepthResources()
    {
        VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 };
        ici.mipLevels = 1; ici.arrayLayers = 1;
        ici.format = m_depthFormat;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;   // full-screen attachment: dedicated is optimal
        if (vmaCreateImage(m_allocator, &ici, &aci, &m_depthImage, &m_depthMem, nullptr) != VK_SUCCESS) return false;

        VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = m_depthImage; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = m_depthFormat;
        vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        return vkCreateImageView(m_device, &vci, nullptr, &m_depthView) == VK_SUCCESS;
    }

    void VulkanRenderer::DestroyDepthResources()
    {
        if (m_depthView)  { vkDestroyImageView(m_device, m_depthView, nullptr); m_depthView = VK_NULL_HANDLE; }
        if (m_depthImage) { vmaDestroyImage(m_allocator, m_depthImage, m_depthMem); m_depthImage = VK_NULL_HANDLE; m_depthMem = VK_NULL_HANDLE; }
    }

    VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv)
    {
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = spirv.size() * sizeof(uint32_t);
        ci.pCode = spirv.data();
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(m_device, &ci, nullptr, &m);
        return m;
    }

    // ----------------------------- Mesh -----------------------------------
    MeshHandle VulkanRenderer::CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                                          const std::vector<VertexAttrib>&, const uint32_t* indices, size_t indexCount)
    {
        if (!vertexData || floatCount == 0 || strideFloats <= 0) return {};

        // Upload one block of data into a fresh device-local buffer via staging.
        auto uploadDeviceLocal = [&](const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkBuffer& outBuf, VmaAllocation& outAlloc)
        {
            VkBuffer staging; VmaAllocation stagingAlloc; void* p = nullptr;
            CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, /*hostVisible=*/true,
                staging, stagingAlloc, &p);
            memcpy(p, data, (size_t)size);

            CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, /*hostVisible=*/false,
                outBuf, outAlloc);

            VkCommandBuffer cmd = BeginSingleTime();
            VkBufferCopy region{}; region.size = size;
            vkCmdCopyBuffer(cmd, staging, outBuf, 1, &region);
            EndSingleTime(cmd);

            vmaDestroyBuffer(m_allocator, staging, stagingAlloc);
        };

        VkMeshRes mesh;
        mesh.vertexCount = (uint32_t)(floatCount / strideFloats);
        uploadDeviceLocal(vertexData, floatCount * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vbuf, mesh.vmem);

        if (indices && indexCount > 0)
        {
            mesh.indexCount = (uint32_t)indexCount;
            uploadDeviceLocal(indices, indexCount * sizeof(uint32_t),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.ibuf, mesh.imem);
        }

        m_meshes.push_back(mesh);
        return MeshHandle{ (uint32_t)m_meshes.size() };
    }

    // Outside a frame, the last kFramesInFlight submissions may still be
    // executing on the GPU -- nothing has waited their fences yet (BeginFrame
    // only ever waits the next frame's, and teardown paths like Scene::~Scene
    // run after the loop with the final frame still pending). Freeing a
    // resource those command buffers reference is invalid (e.g. descriptor
    // sets trip VUID-vkFreeDescriptorSets-pDescriptorSets-00309). Idle the
    // device first; on an already-idle device this returns immediately.
    void VulkanRenderer::WaitGpuIdleForDestroy()
    {
        if (m_device && !m_frameActive) vkDeviceWaitIdle(m_device);
    }

    void VulkanRenderer::DestroyMesh(MeshHandle h)
    {
        if (h.id == 0 || h.id > m_meshes.size()) return;
        WaitGpuIdleForDestroy();
        VkMeshRes& m = m_meshes[h.id - 1];
        if (m.vbuf) vmaDestroyBuffer(m_allocator, m.vbuf, m.vmem);
        if (m.ibuf) vmaDestroyBuffer(m_allocator, m.ibuf, m.imem);
        m = VkMeshRes{};
    }

    // ------------------------- Storage buffers ----------------------------
    StorageBufferHandle VulkanRenderer::CreateStorageBuffer(size_t sizeBytes, bool)
    {
        VkStorageRes s;
        s.size = sizeBytes < 4096 ? 4096 : sizeBytes;   // start with headroom; grow on demand
        for (int i = 0; i < kFramesInFlight; ++i)
            CreateBuffer(s.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, /*hostVisible=*/true,
                s.buf[i], s.mem[i], &s.mapped[i]);
        m_storage.push_back(s);
        return StorageBufferHandle{ (uint32_t)m_storage.size() };
    }

    void VulkanRenderer::UpdateStorageBuffer(StorageBufferHandle h, const void* data, size_t sizeBytes)
    {
        if (h.id == 0 || h.id > m_storage.size() || !data || sizeBytes == 0) return;
        VkStorageRes& s = m_storage[h.id - 1];

        if (sizeBytes > s.size)   // grow (rare: instance count increased)
        {
            vkDeviceWaitIdle(m_device);
            for (int i = 0; i < kFramesInFlight; ++i)
                if (s.buf[i]) vmaDestroyBuffer(m_allocator, s.buf[i], s.mem[i]);
            s.size = sizeBytes;
            for (int i = 0; i < kFramesInFlight; ++i)
                CreateBuffer(s.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, /*hostVisible=*/true,
                    s.buf[i], s.mem[i], &s.mapped[i]);
        }
        memcpy(s.mapped[m_currentFrame], data, sizeBytes);
    }

    void VulkanRenderer::DestroyStorageBuffer(StorageBufferHandle h)
    {
        if (h.id == 0 || h.id > m_storage.size()) return;
        WaitGpuIdleForDestroy();
        VkStorageRes& s = m_storage[h.id - 1];
        for (int i = 0; i < kFramesInFlight; ++i)
            if (s.buf[i]) vmaDestroyBuffer(m_allocator, s.buf[i], s.mem[i]);
        s = VkStorageRes{};
    }

    // ------------------------------ Pipeline ------------------------------
    PipelineHandle VulkanRenderer::CreatePipeline(const std::string& hlslSource, const PipelineState& state)
    {
        if (!m_pushDescriptorOK)
        {
            Logger::Get().Error("[Vulkan] CreatePipeline: push descriptors unavailable");
            return {};
        }
        if (state.renderToTexture && !EnsureRenderTargetPasses())
            return {};
        CompiledShader vs = ShaderCompiler::Compile(hlslSource, ShaderStage::Vertex, "VSMain", false);
        CompiledShader ps = ShaderCompiler::Compile(hlslSource, ShaderStage::Pixel, "PSMain", false);
        if (!vs.ok || !ps.ok) { Logger::Get().Error("[Vulkan] pipeline shader compile failed"); return {}; }

        VkPipelineRes p;
        p.usesSceneResources = state.usesSceneResources;
        p.vs = CreateShaderModule(vs.spirv);
        p.fs = CreateShaderModule(ps.spirv);

        // set 0 = shared UBO layout (regular, bound). set 1 = 2 storage buffers (push,
        // the single allowed push-descriptor set). p.setLayouts[0] stays null (shared,
        // not owned by the pipeline); only set 1 is created/destroyed per pipeline.
        // Every binding is visible to BOTH stages: the engine prelude exposes
        // ModelMatrices/PropertyColors/MainTex to user code in either stage (e.g.
        // PSMain reads PropertyColors via _Color), and a layout that declares
        // fewer stages than the SPIR-V actually uses is invalid
        // (VUID-VkGraphicsPipelineCreateInfo-layout-07988) -> broken draws.
        if (p.usesSceneResources)
        {
            const VkShaderStageFlags vsfs = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutBinding sb[4]{};
            sb[0].binding = 0; sb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sb[0].descriptorCount = 1; sb[0].stageFlags = vsfs;
            sb[1].binding = 1; sb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sb[1].descriptorCount = 1; sb[1].stageFlags = vsfs;
            sb[2].binding = 2; sb[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; sb[2].descriptorCount = 1; sb[2].stageFlags = vsfs;
            sb[3].binding = 3; sb[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; sb[3].descriptorCount = 1; sb[3].stageFlags = vsfs;
            VkDescriptorSetLayoutCreateInfo l1{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            l1.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
            l1.bindingCount = 4; l1.pBindings = sb;
            vkCreateDescriptorSetLayout(m_device, &l1, nullptr, &p.setLayouts[1]);
        }

        VkDescriptorSetLayout layouts[2] = { m_uboSetLayout, p.setLayouts[1] };
        VkPipelineLayoutCreateInfo plc{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plc.setLayoutCount = p.usesSceneResources ? 2u : 1u;
        plc.pSetLayouts = layouts;
        vkCreatePipelineLayout(m_device, &plc, nullptr, &p.layout);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = p.vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = p.fs; stages[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = static_cast<uint32_t>(std::max(1, state.vertexStrideFloats) * sizeof(float));
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::vector<VkVertexInputAttributeDescription> attrs;
        attrs.reserve(state.vertexAttributes.size());
        for (const VertexAttrib& attrib : state.vertexAttributes)
        {
            VkVertexInputAttributeDescription vkAttrib{};
            vkAttrib.location = static_cast<uint32_t>(attrib.location);
            vkAttrib.binding = 0;
            vkAttrib.offset = static_cast<uint32_t>(attrib.offsetFloats * sizeof(float));
            switch (attrib.componentCount)
            {
            case 1: vkAttrib.format = VK_FORMAT_R32_SFLOAT; break;
            case 2: vkAttrib.format = VK_FORMAT_R32G32_SFLOAT; break;
            case 3: vkAttrib.format = VK_FORMAT_R32G32B32_SFLOAT; break;
            default: vkAttrib.format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
            }
            attrs.push_back(vkAttrib);
        }
        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vi.pVertexAttributeDescriptions = attrs.empty() ? nullptr : attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1; vp.scissorCount = 1;   // dynamic

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode = state.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT :
            state.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msi{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        ds.depthTestEnable = state.depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = state.depthFunc == DepthFunc::LessEqual ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = state.blend ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynS{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynS.dynamicStateCount = 2; dynS.pDynamicStates = dyn;

        VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
        gp.pRasterizationState = &rs; gp.pMultisampleState = &msi; gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dynS;
        gp.layout = p.layout; gp.renderPass = state.renderToTexture ? m_rtRenderPass : m_renderPass; gp.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &p.pipeline) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateGraphicsPipelines failed");
            return {};
        }

        m_pipelines.push_back(p);
        return PipelineHandle{ (uint32_t)m_pipelines.size() };
    }

    void VulkanRenderer::DestroyPipeline(PipelineHandle h)
    {
        if (h.id == 0 || h.id > m_pipelines.size()) return;
        WaitGpuIdleForDestroy();
        VkPipelineRes& p = m_pipelines[h.id - 1];
        if (p.pipeline) vkDestroyPipeline(m_device, p.pipeline, nullptr);
        if (p.layout)   vkDestroyPipelineLayout(m_device, p.layout, nullptr);
        if (p.setLayouts[0]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[0], nullptr);
        if (p.setLayouts[1]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[1], nullptr);
        if (p.vs) vkDestroyShaderModule(m_device, p.vs, nullptr);
        if (p.fs) vkDestroyShaderModule(m_device, p.fs, nullptr);
        p = VkPipelineRes{};
    }
    void VulkanRenderer::BindPipeline(PipelineHandle h)
    {
        m_boundPipeline = (h.id && h.id <= m_pipelines.size()) ? &m_pipelines[h.id - 1] : nullptr;
        if (!m_frameActive || !m_boundPipeline || !m_boundPipeline->pipeline) return;

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundPipeline->pipeline);

        // Bind set 0 (this frame's FrameUniforms UBO) at the current ring slot's
        // dynamic offset. SetFrameUniforms (called next) writes that slot and rebinds
        // with the same offset. Set 1 (storage buffers) is pushed per-draw in DrawInstanced.
        const uint32_t dynOffset = m_uboSlot * kUboSlotSize;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundPipeline->layout,
            0, 1, &m_uboSets[m_currentFrame], 1, &dynOffset);
    }

    void VulkanRenderer::SetFrameUniforms(const FrameUniforms& u)
    {
        // std140 mirror matching Scene.hlsl's cbuffer (same bytes as the GL path,
        // GLM column-major; the SPIR-V's matrix decorations make it consistent).
        struct Std140
        {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec3 viewPos;     float _p0;
            glm::vec3 lightColor;  float _p1;
            glm::vec3 lightDir;    float lightIntensity;
            glm::vec4 time;
            glm::vec4 sinTime;
            glm::vec4 cosTime;
            glm::vec4 deltaTime;
            glm::vec4 screenParams;
        } d;
        d.view = u.view; d.projection = u.projection;
        d.viewPos = u.viewPos;        d._p0 = 0.0f;
        d.lightColor = u.lightColor;  d._p1 = 0.0f;
        d.lightDir = u.lightDir;      d.lightIntensity = u.lightIntensity;
        d.time = u.time;
        d.sinTime = u.sinTime;
        d.cosTime = u.cosTime;
        d.deltaTime = u.deltaTime;
        d.screenParams = u.screenParams;

        static_assert(sizeof(Std140) <= kUboSlotSize, "FrameUniforms exceeds UBO slot size");

        // Write this viewport's uniforms into its own ring slot, then point set 0 at
        // that slot. Without per-call slots, a second viewport's SetFrameUniforms would
        // overwrite the first's bytes before the GPU executes either draw (everything
        // is recorded into one command buffer), stretching the earlier viewport.
        const uint32_t dynOffset = m_uboSlot * kUboSlotSize;
        if (m_uboMapped[m_currentFrame])
            memcpy((char*)m_uboMapped[m_currentFrame] + dynOffset, &d, sizeof(d));

        if (m_frameActive && m_boundPipeline && m_boundPipeline->layout)
            vkCmdBindDescriptorSets(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_boundPipeline->layout, 0, 1, &m_uboSets[m_currentFrame], 1, &dynOffset);

        m_uboSlot = (m_uboSlot + 1) % kUboSlots;
    }

    void VulkanRenderer::BindStorageBuffer(int binding, StorageBufferHandle h)
    {
        if (binding >= 0 && binding < 2) m_boundStorage[binding] = h;
    }

    void VulkanRenderer::DrawInstanced(MeshHandle h, int instanceCount)
    {
        static bool s_warnedNoFrame = false;
        static bool s_warnedNoMesh = false;
        static bool s_warnedNoSceneResources = false;
        if (!m_frameActive || !m_boundPipeline || !m_pushDescriptor || instanceCount <= 0)
        {
            if (!s_warnedNoFrame)
            {
                Logger::Get().Warning("[Vulkan] DrawInstanced skipped: no active frame/pipeline or invalid instance count");
                s_warnedNoFrame = true;
            }
            return;
        }
        VkMeshRes* mesh = (h.id && h.id <= m_meshes.size()) ? &m_meshes[h.id - 1] : nullptr;
        if (!mesh || !mesh->vbuf)
        {
            if (!s_warnedNoMesh)
            {
                Logger::Get().Warning("[Vulkan] DrawInstanced skipped: invalid mesh handle");
                s_warnedNoMesh = true;
            }
            return;
        }

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

        if (!m_boundPipeline->usesSceneResources)
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vbuf, &offset);
            if (mesh->indexCount > 0)
            {
                vkCmdBindIndexBuffer(cmd, mesh->ibuf, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, mesh->indexCount, (uint32_t)instanceCount, 0, 0, 0);
            }
            else
            {
                vkCmdDraw(cmd, mesh->vertexCount, (uint32_t)instanceCount, 0, 0);
            }
            return;
        }

        VkStorageRes* model = (m_boundStorage[0].id && m_boundStorage[0].id <= m_storage.size()) ? &m_storage[m_boundStorage[0].id - 1] : nullptr;
        VkStorageRes* color = (m_boundStorage[1].id && m_boundStorage[1].id <= m_storage.size()) ? &m_storage[m_boundStorage[1].id - 1] : nullptr;
        if (!model || !color)
        {
            if (!s_warnedNoSceneResources)
            {
                Logger::Get().Warning("[Vulkan] DrawInstanced skipped: scene resource buffers are not bound");
                s_warnedNoSceneResources = true;
            }
            return;
        }
        VkTextureRes* tex = (m_boundTextures[2].id && m_boundTextures[2].id <= m_textures.size()) ? &m_textures[m_boundTextures[2].id - 1] : nullptr;
        if (!tex || !tex->view || !m_sampler)
        {
            if (!s_warnedNoSceneResources)
            {
                Logger::Get().Warning("[Vulkan] DrawInstanced skipped: material texture is not bound");
                s_warnedNoSceneResources = true;
            }
            return;
        }

        // Push set 1 (this frame's storage buffers + material texture).
        VkDescriptorBufferInfo bi[2] = {
            { model->buf[m_currentFrame], 0, VK_WHOLE_SIZE },
            { color->buf[m_currentFrame], 0, VK_WHOLE_SIZE },
        };
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex->view;
        imageInfo.sampler = m_sampler;
        VkWriteDescriptorSet w[4]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &bi[0];
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &bi[1];
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[2].dstBinding = 2; w[2].descriptorCount = 1; w[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[2].pImageInfo = &imageInfo;
        w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[3].dstBinding = 3; w[3].descriptorCount = 1; w[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; w[3].pImageInfo = &imageInfo;
        m_pushDescriptor(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundPipeline->layout, 1, 4, w);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vbuf, &offset);
        if (mesh->indexCount > 0)
        {
            vkCmdBindIndexBuffer(cmd, mesh->ibuf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount, (uint32_t)instanceCount, 0, 0, 0);
        }
        else
            vkCmdDraw(cmd, mesh->vertexCount, (uint32_t)instanceCount, 0, 0);
    }
}
