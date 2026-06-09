#include "VulkanRenderer.h"
#include "../../../Core/Logger.h"
#define GLFW_INCLUDE_VULKAN
#include "../../../../3rdParty/GLFW/glfw3.h"
#include <cstring>
#include <vector>

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

        Logger::Get().Info("[Vulkan] Renderer initialized (instance + surface + device ready).");
    }

    VulkanRenderer::~VulkanRenderer()
    {
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

    // ----------------------------------------------------------------------
    // IRenderer stubs — implemented in later steps (swapchain, pipelines, etc.)
    // ----------------------------------------------------------------------
    void VulkanRenderer::SetViewport(int, int, int, int) {}
    void VulkanRenderer::SetScissor(bool, int, int, int, int) {}
    void VulkanRenderer::Clear(uint32_t, const glm::vec4&) {}
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
