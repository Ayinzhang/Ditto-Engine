// VMA implementation lives in this TU (the header is included via VulkanRenderer.h).
#pragma warning(push)
#pragma warning(disable: 4100 4189 4127 4324)   // VMA: unreferenced params/locals, const conditionals, padding
#define VMA_IMPLEMENTATION
#include "../../../../3rdParty/VMA/vk_mem_alloc.h"
#pragma warning(pop)

#include "VulkanRenderer.h"
#include "../../../Core/Logger.h"
#include "../../Shaders/ShaderCompiler.h"
#define GLFW_INCLUDE_VULKAN
#include "../../../../3rdParty/GLFW/glfw3.h"
#include "../../../../3rdParty/ImGui/imgui.h"
#include "../../../../3rdParty/ImGui/imgui_impl_glfw.h"
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

    VulkanRenderer::VulkanRenderer(void* window)
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
        Logger::Get().Info("[Vulkan] Renderer initialized (swapchain + depth + frame resources ready).");
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
        if (glfwCreateWindowSurface(m_instance, static_cast<GLFWwindow*>(m_window), nullptr, &m_surface) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] glfwCreateWindowSurface failed (is GLFW built with Vulkan support?)");
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
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            "VK_KHR_win32_surface",
        };
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
                Logger::Get().Info(std::string("[Vulkan] GPU: ") + props.deviceName);
                return true;
            }
            if (fallback == VK_NULL_HANDLE) { fallback = dev; fbGfx = gfx; fbPresent = present; }
        }

        if (fallback != VK_NULL_HANDLE)
        {
            m_physicalDevice = fallback; m_graphicsQueueFamily = fbGfx; m_presentQueueFamily = fbPresent;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(fallback, &props);
            Logger::Get().Info(std::string("[Vulkan] GPU (fallback): ") + props.deviceName);
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

    // ----------------------------- Swapchain ------------------------------
    bool VulkanRenderer::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, formats.data());
        // Prefer a UNORM (non-sRGB) surface format: the engine and ImGui write raw
        // color values with no gamma handling, exactly like the GL backend (no
        // GL_FRAMEBUFFER_SRGB). An *_SRGB swapchain re-encodes those values on
        // store, washing the whole editor out to grey. Same policy as ImGui's own
        // Vulkan example.
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
        for (const auto& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
                && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }

        uint32_t pmCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
        std::vector<VkPresentModeKHR> modes(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, modes.data());
        VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;   // always available (vsync)
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present = m; break; }

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            int w = 0, h = 0; glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h);
            extent.width  = std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width);
            extent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        ci.surface = m_surface;
        ci.minImageCount = imageCount;
        ci.imageFormat = chosen.format;
        ci.imageColorSpace = chosen.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t fams[] = { m_graphicsQueueFamily, m_presentQueueFamily };
        if (m_graphicsQueueFamily != m_presentQueueFamily)
        {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = fams;
        }
        else ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = present;
        ci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateSwapchainKHR failed");
            return false;
        }
        m_swapchainFormat = chosen.format;
        m_swapchainExtent = extent;

        uint32_t count = 0;
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
        m_swapchainImages.resize(count);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapchainImages.data());
        return true;
    }

    void VulkanRenderer::CreateImageViews()
    {
        m_swapchainImageViews.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); ++i)
        {
            VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            ci.image = m_swapchainImages[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = m_swapchainFormat;
            ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.levelCount = 1;
            ci.subresourceRange.layerCount = 1;
            vkCreateImageView(m_device, &ci, nullptr, &m_swapchainImageViews[i]);
        }
    }

    bool VulkanRenderer::CreateRenderPass()
    {
        VkAttachmentDescription color{};
        color.format = m_swapchainFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        // Depth attachment (cleared each frame, not stored).
        VkAttachmentDescription depth{};
        depth.format = m_depthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[] = { color, depth };
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 2;
        ci.pAttachments = attachments;
        ci.subpassCount = 1;
        ci.pSubpasses = &subpass;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;

        if (vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateRenderPass failed");
            return false;
        }
        return true;
    }

    void VulkanRenderer::CreateFramebuffers()
    {
        m_framebuffers.resize(m_swapchainImageViews.size());
        for (size_t i = 0; i < m_swapchainImageViews.size(); ++i)
        {
            VkImageView attachments[] = { m_swapchainImageViews[i], m_depthView };
            VkFramebufferCreateInfo ci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            ci.renderPass = m_renderPass;
            ci.attachmentCount = 2;
            ci.pAttachments = attachments;
            ci.width = m_swapchainExtent.width;
            ci.height = m_swapchainExtent.height;
            ci.layers = 1;
            vkCreateFramebuffer(m_device, &ci, nullptr, &m_framebuffers[i]);
        }
    }

    bool VulkanRenderer::CreateCommandResources()
    {
        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_graphicsQueueFamily;
        if (vkCreateCommandPool(m_device, &pci, nullptr, &m_commandPool) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateCommandPool failed");
            return false;
        }
        m_commandBuffers.resize(kFramesInFlight);
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = m_commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = kFramesInFlight;
        if (vkAllocateCommandBuffers(m_device, &ai, m_commandBuffers.data()) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkAllocateCommandBuffers failed");
            return false;
        }
        return true;
    }

    bool VulkanRenderer::CreateSyncObjects()
    {
        m_imageAvailable.resize(kFramesInFlight);
        m_inFlight.resize(kFramesInFlight);
        m_renderFinished.resize(m_swapchainImages.size());

        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < kFramesInFlight; ++i)
            if (vkCreateSemaphore(m_device, &sci, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
                vkCreateFence(m_device, &fci, nullptr, &m_inFlight[i]) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] sync object creation failed");
                return false;
            }
        // Per-swapchain-image render-finished semaphores (avoids reuse-while-pending).
        for (size_t i = 0; i < m_renderFinished.size(); ++i)
            if (vkCreateSemaphore(m_device, &sci, nullptr, &m_renderFinished[i]) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] sync object creation failed");
                return false;
            }
        return true;
    }

    void VulkanRenderer::CleanupSwapchain()
    {
        if (!m_device) return;
        DestroyDepthResources();
        for (auto fb : m_framebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
        m_framebuffers.clear();
        for (auto iv : m_swapchainImageViews) if (iv) vkDestroyImageView(m_device, iv, nullptr);
        m_swapchainImageViews.clear();
        if (m_swapchain) { vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
    }

    bool VulkanRenderer::RecreateSwapchain()
    {
        // Pause while minimized (zero-size framebuffer).
        int w = 0, h = 0; glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h);
        while (w == 0 || h == 0) { glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h); glfwWaitEvents(); }

        vkDeviceWaitIdle(m_device);
        CleanupSwapchain();
        if (!CreateSwapchain()) return false;
        CreateImageViews();
        if (!CreateDepthResources()) return false;
        CreateFramebuffers();

        // Image count may change → rebuild per-image render-finished semaphores.
        for (auto s : m_renderFinished) if (s) vkDestroySemaphore(m_device, s, nullptr);
        m_renderFinished.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& s : m_renderFinished) vkCreateSemaphore(m_device, &sci, nullptr, &s);
        return true;
    }

    // ------------------------------- Frame --------------------------------
    void VulkanRenderer::BeginFrame()
    {
        if (!m_ready) return;

        vkWaitForFences(m_device, 1, &m_inFlight[m_currentFrame], VK_TRUE, UINT64_MAX);
        ProcessDeferredDestroys();

        VkResult acq = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
            m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) { Logger::Get().Error("[Vulkan] acquire failed"); return; }

        vkResetFences(m_device, 1, &m_inFlight[m_currentFrame]);

        m_uboSlot = 0;   // restart the per-frame UBO ring (one slot per viewport)

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);

        VkClearValue clears[2]{};
        clears[0].color = { { m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a } };
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = m_renderPass;
        rp.framebuffer = m_framebuffers[m_imageIndex];
        rp.renderArea.extent = m_swapchainExtent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        // Negative-height viewport flips Y so Vulkan matches the GL Y-up convention.
        VkViewport vp{ 0.0f, (float)m_swapchainExtent.height,
                       (float)m_swapchainExtent.width, -(float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D sc{ {0, 0}, m_swapchainExtent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        m_frameActive = true;
    }

    void VulkanRenderer::EndFrame()
    {
        if (!m_ready || !m_frameActive) return;
        m_frameActive = false;

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkSemaphore waitSem[] = { m_imageAvailable[m_currentFrame] };
        VkPipelineStageFlags waitStage[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSem[] = { m_renderFinished[m_imageIndex] };

        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = waitSem;
        si.pWaitDstStageMask = waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = signalSem;
        vkQueueSubmit(m_graphicsQueue, 1, &si, m_inFlight[m_currentFrame]);

        VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = signalSem;
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain;
        pi.pImageIndices = &m_imageIndex;
        VkResult pres = vkQueuePresentKHR(m_presentQueue, &pi);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) RecreateSwapchain();

        m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
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

    void VulkanRenderer::DestroyMesh(MeshHandle h)
    {
        if (h.id == 0 || h.id > m_meshes.size()) return;
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
        VkStorageRes& s = m_storage[h.id - 1];
        for (int i = 0; i < kFramesInFlight; ++i)
            if (s.buf[i]) vmaDestroyBuffer(m_allocator, s.buf[i], s.mem[i]);
        s = VkStorageRes{};
    }

    // ------------------------------ Pipeline ------------------------------
    PipelineHandle VulkanRenderer::CreatePipeline(const std::string& hlslSource)
    {
        if (!m_pushDescriptorOK)
        {
            Logger::Get().Error("[Vulkan] CreatePipeline: push descriptors unavailable");
            return {};
        }
        CompiledShader vs = ShaderCompiler::Compile(hlslSource, ShaderStage::Vertex, "VSMain", false);
        CompiledShader ps = ShaderCompiler::Compile(hlslSource, ShaderStage::Pixel, "PSMain", false);
        if (!vs.ok || !ps.ok) { Logger::Get().Error("[Vulkan] pipeline shader compile failed"); return {}; }

        VkPipelineRes p;
        p.vs = CreateShaderModule(vs.spirv);
        p.fs = CreateShaderModule(ps.spirv);

        // set 0 = shared UBO layout (regular, bound). set 1 = 2 storage buffers (push,
        // the single allowed push-descriptor set). p.setLayouts[0] stays null (shared,
        // not owned by the pipeline); only set 1 is created/destroyed per pipeline.
        VkDescriptorSetLayoutBinding sb[4]{};
        sb[0].binding = 0; sb[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sb[0].descriptorCount = 1; sb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        sb[1].binding = 1; sb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; sb[1].descriptorCount = 1; sb[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        sb[2].binding = 2; sb[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; sb[2].descriptorCount = 1; sb[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        sb[3].binding = 3; sb[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; sb[3].descriptorCount = 1; sb[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo l1{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        l1.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        l1.bindingCount = 4; l1.pBindings = sb;
        vkCreateDescriptorSetLayout(m_device, &l1, nullptr, &p.setLayouts[1]);

        VkDescriptorSetLayout layouts[2] = { m_uboSetLayout, p.setLayouts[1] };
        VkPipelineLayoutCreateInfo plc{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plc.setLayoutCount = 2; plc.pSetLayouts = layouts;
        vkCreatePipelineLayout(m_device, &plc, nullptr, &p.layout);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = p.vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = p.fs; stages[1].pName = "main";

        // Vertex input: interleaved pos(vec3)+normal(vec3)+uv(vec2), stride 32 (matches the
        // engine's base/custom meshes).
        VkVertexInputBindingDescription bind{}; bind.binding = 0; bind.stride = 8 * sizeof(float); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 3 * sizeof(float);
        attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = 6 * sizeof(float);
        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1; vp.scissorCount = 1;   // dynamic

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msi{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        msi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
        gp.layout = p.layout; gp.renderPass = m_renderPass; gp.subpass = 0;

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
        VkPipelineRes& p = m_pipelines[h.id - 1];
        if (p.pipeline) vkDestroyPipeline(m_device, p.pipeline, nullptr);
        if (p.layout)   vkDestroyPipelineLayout(m_device, p.layout, nullptr);
        if (p.setLayouts[0]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[0], nullptr);
        if (p.setLayouts[1]) vkDestroyDescriptorSetLayout(m_device, p.setLayouts[1], nullptr);
        if (p.vs) vkDestroyShaderModule(m_device, p.vs, nullptr);
        if (p.fs) vkDestroyShaderModule(m_device, p.fs, nullptr);
        p = VkPipelineRes{};
    }
    // ------------------------------- ImGui --------------------------------
    void VulkanRenderer::ImGuiInit(void* window)
    {
        if (!m_ready || m_imguiInit) return;

        VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 } };
        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = 256;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(m_device, &pci, nullptr, &m_imguiPool);

        VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 1.0f;
        vkCreateSampler(m_device, &sci, nullptr, &m_sampler);

        ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window), true);

        ImGui_ImplVulkan_InitInfo init{};
        init.ApiVersion = VK_API_VERSION_1_3;
        init.Instance = m_instance;
        init.PhysicalDevice = m_physicalDevice;
        init.Device = m_device;
        init.QueueFamily = m_graphicsQueueFamily;
        init.Queue = m_graphicsQueue;
        init.DescriptorPool = m_imguiPool;
        init.MinImageCount = (uint32_t)m_swapchainImages.size();
        init.ImageCount = (uint32_t)m_swapchainImages.size();
        init.PipelineInfoMain.RenderPass = m_renderPass;
        init.PipelineInfoMain.Subpass = 0;
        init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&init);

        m_imguiInit = true;
    }

    void VulkanRenderer::ImGuiShutdown()
    {
        if (!m_imguiInit) return;
        vkDeviceWaitIdle(m_device);

        for (auto& t : m_textures)
        {
            if (t.descriptor) ImGui_ImplVulkan_RemoveTexture(t.descriptor);
            if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
            if (t.image) vmaDestroyImage(m_allocator, t.image, t.memory);
        }
        m_textures.clear();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        if (m_sampler)   { vkDestroySampler(m_device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
        if (m_imguiPool) { vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr); m_imguiPool = VK_NULL_HANDLE; }
        m_imguiInit = false;
    }

    void VulkanRenderer::ImGuiNewFrame()
    {
        if (!m_imguiInit) return;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void VulkanRenderer::ImGuiRenderDrawData(void* drawData)
    {
        // Records into the current command buffer, which BeginFrame put inside the
        // swapchain render pass.
        if (!m_imguiInit || !m_frameActive) return;
        ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(drawData), m_commandBuffers[m_currentFrame]);
    }

    // ------------------------------ Textures ------------------------------
    VkCommandBuffer VulkanRenderer::BeginSingleTime()
    {
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = m_commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(m_device, &ai, &cmd);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void VulkanRenderer::EndSingleTime(VkCommandBuffer cmd)
    {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }

    TextureHandle VulkanRenderer::CreateTexture(const unsigned char* pixels, int w, int h, int /*channels*/)
    {
        if (!m_imguiInit || !pixels || w <= 0 || h <= 0) return {};   // need ImGui pool for AddTexture
        const VkDeviceSize size = (VkDeviceSize)w * h * 4;            // Engine uploads RGBA8

        // Staging buffer.
        VkBuffer staging; VmaAllocation stagingAlloc; void* mapped = nullptr;
        CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, /*hostVisible=*/true,
            staging, stagingAlloc, &mapped);
        memcpy(mapped, pixels, (size_t)size);

        // Device-local image.
        VkTextureRes t;
        VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
        ici.mipLevels = 1; ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(m_allocator, &ici, &aci, &t.image, &t.memory, nullptr);

        // Upload: UNDEFINED -> TRANSFER_DST -> copy -> SHADER_READ_ONLY.
        VkCommandBuffer cmd = BeginSingleTime();
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        EndSingleTime(cmd);

        vmaDestroyBuffer(m_allocator, staging, stagingAlloc);

        VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = t.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(m_device, &vci, nullptr, &t.view);

        t.descriptor = ImGui_ImplVulkan_AddTexture(m_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_textures.push_back(t);
        return TextureHandle{ (uint32_t)m_textures.size() };
    }

    void VulkanRenderer::DestroyTexture(TextureHandle h)
    {
        if (h.id == 0 || h.id > m_textures.size()) return;
        VkTextureRes& t = m_textures[h.id - 1];
        if (m_frameActive)
        {
            if (!t.pendingDestroy)
            {
                t.pendingDestroy = true;
                m_pendingTextureDestroys.push_back(h);
            }
            return;
        }
        if (t.descriptor) ImGui_ImplVulkan_RemoveTexture(t.descriptor);
        if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
        if (t.image) vmaDestroyImage(m_allocator, t.image, t.memory);
        t = VkTextureRes{};
    }

    void VulkanRenderer::ProcessDeferredDestroys()
    {
        if (m_pendingTextureDestroys.empty()) return;
        std::vector<TextureHandle> pending;
        pending.swap(m_pendingTextureDestroys);
        for (TextureHandle h : pending)
        {
            if (h.id == 0 || h.id > m_textures.size()) continue;
            VkTextureRes& t = m_textures[h.id - 1];
            t.pendingDestroy = false;
            DestroyTexture(h);
        }
    }

    void* VulkanRenderer::GetImGuiTextureID(TextureHandle h)
    {
        if (h.id == 0 || h.id > m_textures.size()) return nullptr;
        return (void*)m_textures[h.id - 1].descriptor;   // VkDescriptorSet as ImTextureID
    }

    void VulkanRenderer::BindTexture(int binding, TextureHandle h)
    {
        if (binding >= 0 && binding < 4)
            m_boundTextures[binding] = h;
    }

    // --------------------------- Render targets ---------------------------
    bool VulkanRenderer::EnsureRenderTargetPasses()
    {
        if (m_rtRenderPass && m_resumePass) return true;

        // Offscreen pass: clear color+depth, end with color ready for sampling.
        // The EXTERNAL dependencies order this frame's RT write against the
        // previous frame's ImGui sampling (WAR) and this frame's (RAW).
        {
            VkAttachmentDescription color{};
            color.format = m_swapchainFormat;
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentDescription depth{};
            depth.format = m_depthFormat;
            depth.samples = VK_SAMPLE_COUNT_1_BIT;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;

            VkSubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.srcAccessMask = 0;
            dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkAttachmentDescription atts[] = { color, depth };
            VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            ci.attachmentCount = 2; ci.pAttachments = atts;
            ci.subpassCount = 1; ci.pSubpasses = &subpass;
            ci.dependencyCount = 1; ci.pDependencies = &dep;
            if (vkCreateRenderPass(m_device, &ci, nullptr, &m_rtRenderPass) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] RT render pass creation failed");
                return false;
            }
        }

        // Resume pass: identical attachments to the swapchain pass but LOADing the
        // already-rendered color, so an interrupted frame keeps its contents.
        // (Compatible with m_renderPass framebuffers: same formats/samples.)
        {
            VkAttachmentDescription color{};
            color.format = m_swapchainFormat;
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;   // layout left by the ended pass
            color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentDescription depth{};
            depth.format = m_depthFormat;
            depth.samples = VK_SAMPLE_COUNT_1_BIT;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // prior pass didn't store depth
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;

            VkSubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.srcAccessMask = 0;
            dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkAttachmentDescription atts[] = { color, depth };
            VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            ci.attachmentCount = 2; ci.pAttachments = atts;
            ci.subpassCount = 1; ci.pSubpasses = &subpass;
            ci.dependencyCount = 1; ci.pDependencies = &dep;
            if (vkCreateRenderPass(m_device, &ci, nullptr, &m_resumePass) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] resume render pass creation failed");
                return false;
            }
        }
        return true;
    }

    RenderTargetHandle VulkanRenderer::CreateRenderTarget(int w, int h)
    {
        // Needs the ImGui pool/sampler so the color texture is ImGui-displayable.
        if (!m_ready || !m_imguiInit || w <= 0 || h <= 0) return {};
        if (!EnsureRenderTargetPasses()) return {};

        VkRenderTargetRes rt;
        rt.w = w; rt.h = h;

        // Color attachment (also sampled by ImGui). Registered as a texture so
        // GetImGuiTextureID / DestroyTexture handle it like any other texture.
        VkTextureRes color;
        {
            VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
            ici.mipLevels = 1; ici.arrayLayers = 1;
            ici.format = m_swapchainFormat;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ici.samples = VK_SAMPLE_COUNT_1_BIT;
            ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            aci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            if (vmaCreateImage(m_allocator, &ici, &aci, &color.image, &color.memory, nullptr) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] RT color image creation failed");
                return {};
            }

            VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vci.image = color.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = m_swapchainFormat;
            vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCreateImageView(m_device, &vci, nullptr, &color.view);

            color.descriptor = ImGui_ImplVulkan_AddTexture(m_sampler, color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        m_textures.push_back(color);
        rt.color = TextureHandle{ (uint32_t)m_textures.size() };

        // Depth attachment.
        {
            VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
            ici.mipLevels = 1; ici.arrayLayers = 1;
            ici.format = m_depthFormat;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            ici.samples = VK_SAMPLE_COUNT_1_BIT;
            ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            aci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            if (vmaCreateImage(m_allocator, &ici, &aci, &rt.depthImage, &rt.depthMem, nullptr) != VK_SUCCESS)
            {
                Logger::Get().Error("[Vulkan] RT depth image creation failed");
                DestroyTexture(rt.color);
                return {};
            }

            VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vci.image = rt.depthImage; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = m_depthFormat;
            vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            vkCreateImageView(m_device, &vci, nullptr, &rt.depthView);
        }

        VkImageView attachments[] = { m_textures[rt.color.id - 1].view, rt.depthView };
        VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = m_rtRenderPass;
        fci.attachmentCount = 2;
        fci.pAttachments = attachments;
        fci.width = (uint32_t)w; fci.height = (uint32_t)h; fci.layers = 1;
        if (vkCreateFramebuffer(m_device, &fci, nullptr, &rt.framebuffer) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] RT framebuffer creation failed");
            vkDestroyImageView(m_device, rt.depthView, nullptr);
            vmaDestroyImage(m_allocator, rt.depthImage, rt.depthMem);
            DestroyTexture(rt.color);
            return {};
        }

        m_renderTargets.push_back(rt);
        return RenderTargetHandle{ (uint32_t)m_renderTargets.size() };
    }

    void VulkanRenderer::BeginRenderTarget(RenderTargetHandle h)
    {
        if (!m_frameActive || m_rtActive) return;
        if (h.id == 0 || h.id > m_renderTargets.size()) return;
        VkRenderTargetRes& rt = m_renderTargets[h.id - 1];
        if (!rt.framebuffer) return;

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

        // Suspend the swapchain pass; EndRenderTarget resumes it with m_resumePass.
        vkCmdEndRenderPass(cmd);

        VkClearValue clears[2]{};
        clears[0].color = { { m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a } };
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = m_rtRenderPass;
        rp.framebuffer = rt.framebuffer;
        rp.renderArea.extent = { (uint32_t)rt.w, (uint32_t)rt.h };
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        m_rtActive = true;
        m_rtExtent = { (uint32_t)rt.w, (uint32_t)rt.h };

        // POSITIVE-height viewport (no Y flip): leaves the image in GL's
        // bottom-up memory order, so editor code can use the same flipped UVs
        // (0,1)-(1,0) it already uses for GL render targets.
        VkViewport vp{ 0.0f, 0.0f, (float)rt.w, (float)rt.h, 0.0f, 1.0f };
        VkRect2D sc{ {0, 0}, m_rtExtent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    void VulkanRenderer::EndRenderTarget()
    {
        if (!m_frameActive || !m_rtActive) return;
        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

        vkCmdEndRenderPass(cmd);   // color transitions to SHADER_READ_ONLY
        m_rtActive = false;

        // Resume the swapchain pass, keeping its existing color contents.
        VkClearValue clears[2]{};
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = m_resumePass;
        rp.framebuffer = m_framebuffers[m_imageIndex];
        rp.renderArea.extent = m_swapchainExtent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        // Restore the full-swapchain flipped viewport/scissor (as in BeginFrame).
        VkViewport vp{ 0.0f, (float)m_swapchainExtent.height,
                       (float)m_swapchainExtent.width, -(float)m_swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D sc{ {0, 0}, m_swapchainExtent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    TextureHandle VulkanRenderer::GetColorTexture(RenderTargetHandle h)
    {
        if (h.id == 0 || h.id > m_renderTargets.size()) return {};
        return m_renderTargets[h.id - 1].color;
    }

    void VulkanRenderer::DestroyRenderTarget(RenderTargetHandle h)
    {
        if (h.id == 0 || h.id > m_renderTargets.size()) return;
        VkRenderTargetRes& rt = m_renderTargets[h.id - 1];
        if (!rt.framebuffer && !rt.depthImage && !rt.color) return;

        // Editor-only path (viewport resize): wait out any frame still sampling
        // this target before tearing it down.
        vkDeviceWaitIdle(m_device);

        if (rt.framebuffer) vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
        if (rt.depthView)   vkDestroyImageView(m_device, rt.depthView, nullptr);
        if (rt.depthImage)  vmaDestroyImage(m_allocator, rt.depthImage, rt.depthMem);
        DestroyTexture(rt.color);   // frees color image/view + ImGui descriptor + slot
        rt = VkRenderTargetRes{};
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
        if (!m_frameActive || !m_boundPipeline || !m_pushDescriptor || instanceCount <= 0) return;
        VkMeshRes* mesh = (h.id && h.id <= m_meshes.size()) ? &m_meshes[h.id - 1] : nullptr;
        if (!mesh || !mesh->vbuf) return;

        VkStorageRes* model = (m_boundStorage[0].id && m_boundStorage[0].id <= m_storage.size()) ? &m_storage[m_boundStorage[0].id - 1] : nullptr;
        VkStorageRes* color = (m_boundStorage[1].id && m_boundStorage[1].id <= m_storage.size()) ? &m_storage[m_boundStorage[1].id - 1] : nullptr;
        if (!model || !color) return;
        VkTextureRes* tex = (m_boundTextures[2].id && m_boundTextures[2].id <= m_textures.size()) ? &m_textures[m_boundTextures[2].id - 1] : nullptr;
        if (!tex || !tex->view || !m_sampler) return;

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

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
