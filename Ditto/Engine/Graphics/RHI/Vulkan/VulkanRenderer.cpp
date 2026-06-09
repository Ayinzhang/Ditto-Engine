#include "VulkanRenderer.h"
#include "../../../Core/Logger.h"
#define GLFW_INCLUDE_VULKAN
#include "../../../../3rdParty/GLFW/glfw3.h"
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
        CreateFramebuffers();
        if (!CreateCommandResources()) return;
        if (!CreateSyncObjects()) return;

        m_ready = true;
        Logger::Get().Info("[Vulkan] Renderer initialized (swapchain + frame resources ready).");
    }

    VulkanRenderer::~VulkanRenderer()
    {
        if (m_device) vkDeviceWaitIdle(m_device);

        CleanupSwapchain();   // framebuffers + image views + swapchain

        if (m_device)
        {
            for (auto s : m_renderFinished) if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (auto s : m_imageAvailable) if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (auto f : m_inFlight)       if (f) vkDestroyFence(m_device, f, nullptr);
            if (m_renderPass)  vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        }

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

        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkPhysicalDeviceFeatures features{};
        features.fillModeNonSolid = VK_TRUE;   // wireframe (model preview parity)

        VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        ci.queueCreateInfoCount = (uint32_t)queueInfos.size();
        ci.pQueueCreateInfos = queueInfos.data();
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = deviceExtensions;
        ci.pEnabledFeatures = &features;

        if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS)
        {
            Logger::Get().Error("[Vulkan] vkCreateDevice failed");
            return false;
        }
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
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
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
        for (const auto& f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }

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
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
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
            VkImageView attachments[] = { m_swapchainImageViews[i] };
            VkFramebufferCreateInfo ci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            ci.renderPass = m_renderPass;
            ci.attachmentCount = 1;
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

        VkResult acq = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
            m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) { Logger::Get().Error("[Vulkan] acquire failed"); return; }

        vkResetFences(m_device, 1, &m_inFlight[m_currentFrame]);

        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);

        VkClearValue clear{};
        clear.color = { { m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a } };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = m_renderPass;
        rp.framebuffer = m_framebuffers[m_imageIndex];
        rp.renderArea.extent = m_swapchainExtent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{ 0.0f, 0.0f, (float)m_swapchainExtent.width, (float)m_swapchainExtent.height, 0.0f, 1.0f };
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
        VkViewport vp{ (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f };
        vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &vp);
    }
    void VulkanRenderer::SetScissor(bool enabled, int x, int y, int w, int h)
    {
        if (!m_frameActive) return;
        VkRect2D sc = enabled ? VkRect2D{ {x, y}, {(uint32_t)w, (uint32_t)h} }
                              : VkRect2D{ {0, 0}, m_swapchainExtent };
        vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &sc);
    }
    void VulkanRenderer::Clear(uint32_t, const glm::vec4& color) { m_clearColor = color; }
    void VulkanRenderer::SetDepthState(bool, DepthFunc) {}
    void VulkanRenderer::SetBlendState(bool) {}
    void VulkanRenderer::SetWireframe(bool) {}
    void VulkanRenderer::SetCullState(bool) {}

    MeshHandle VulkanRenderer::CreateMesh(const float*, size_t, int, const std::vector<VertexAttrib>&, const uint32_t*, size_t) { return {}; }
    void VulkanRenderer::DestroyMesh(MeshHandle) {}
    StorageBufferHandle VulkanRenderer::CreateStorageBuffer(size_t, bool) { return {}; }
    void VulkanRenderer::UpdateStorageBuffer(StorageBufferHandle, const void*, size_t) {}
    void VulkanRenderer::DestroyStorageBuffer(StorageBufferHandle) {}
    PipelineHandle VulkanRenderer::CreatePipeline(const std::string&, const std::string&) { return {}; }
    void VulkanRenderer::DestroyPipeline(PipelineHandle) {}
    TextureHandle VulkanRenderer::CreateTexture(const unsigned char*, int, int, int) { return {}; }
    void VulkanRenderer::DestroyTexture(TextureHandle) {}
    void* VulkanRenderer::GetImGuiTextureID(TextureHandle) { return nullptr; }
    RenderTargetHandle VulkanRenderer::CreateRenderTarget(int, int) { return {}; }
    void VulkanRenderer::BeginRenderTarget(RenderTargetHandle) {}
    void VulkanRenderer::EndRenderTarget() {}
    TextureHandle VulkanRenderer::GetColorTexture(RenderTargetHandle) { return {}; }
    void VulkanRenderer::DestroyRenderTarget(RenderTargetHandle) {}

    void VulkanRenderer::BindPipeline(PipelineHandle) {}
    void VulkanRenderer::SetFrameUniforms(const FrameUniforms&) {}
    void VulkanRenderer::BindStorageBuffer(int, StorageBufferHandle) {}
    void VulkanRenderer::DrawInstanced(MeshHandle, int) {}
}
