#include "VulkanRenderer.h"
#include "../../../Core/IWindow.h"
#include "../../../Core/Logger.h"
#include <algorithm>
#include <vector>
#include <cstdint>

namespace Ditto
{
    
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
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
                && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }

        uint32_t pmCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
        std::vector<VkPresentModeKHR> modes(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, modes.data());
        VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;   
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present = m; break; }

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            int w = 0, h = 0;
            if (m_window)
                m_window->GetFramebufferSize(w, h);
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
        
        int w = 0, h = 0;
        if (m_window)
            m_window->GetFramebufferSize(w, h);
        while (m_window && (w == 0 || h == 0))
        {
            m_window->WaitEvents();
            m_window->GetFramebufferSize(w, h);
        }

        vkDeviceWaitIdle(m_device);
        CleanupSwapchain();
        if (!CreateSwapchain()) return false;
        CreateImageViews();
        if (!CreateDepthResources()) return false;
        CreateFramebuffers();

        
        for (auto s : m_renderFinished) if (s) vkDestroySemaphore(m_device, s, nullptr);
        m_renderFinished.assign(m_swapchainImages.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (auto& s : m_renderFinished) vkCreateSemaphore(m_device, &sci, nullptr, &s);
        return true;
    }

    
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

        m_uboSlot = 0;   

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

    bool VulkanRenderer::NotifyWindowResized(int width, int height)
    {
        if (!m_device || width <= 0 || height <= 0)
            return false;
        if (m_frameActive)
            EndFrame();
        vkDeviceWaitIdle(m_device);
        return RecreateSwapchain();
    }

}
