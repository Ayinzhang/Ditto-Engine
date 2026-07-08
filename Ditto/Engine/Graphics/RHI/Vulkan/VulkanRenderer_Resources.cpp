#include "VulkanRenderer.h"
#include "../../../Core/IWindow.h"
#include "../../../Core/Logger.h"
#include "../../../../3rdParty/ImGui/imgui.h"
#include "../../../../3rdParty/ImGui/imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Ditto
{
    
    void VulkanRenderer::ImGuiInit(IWindow* window)
    {
        if (!m_ready || m_imguiInit) return;

        VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 } };
        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = 256;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(m_device, &pci, nullptr, &m_imguiPool);

        EnsureSampler();

        if (window)
        {
            window->ImGuiInitForVulkan(true);
            m_imguiWindow = window;
        }

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
        if (m_imguiWindow)
        {
            m_imguiWindow->ImGuiShutdown();
            m_imguiWindow = nullptr;
        }
        if (m_sampler)   { vkDestroySampler(m_device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
        if (m_imguiPool) { vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr); m_imguiPool = VK_NULL_HANDLE; }
        m_imguiInit = false;
    }

    void VulkanRenderer::ImGuiNewFrame()
    {
        if (!m_imguiInit) return;
        ImGui_ImplVulkan_NewFrame();
        if (m_imguiWindow)
            m_imguiWindow->ImGuiNewFrame();
    }

    void VulkanRenderer::ImGuiRenderDrawData(void* drawData)
    {
        
        
        if (!m_imguiInit || !m_frameActive) return;
        ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(drawData), m_commandBuffers[m_currentFrame]);
    }

    
    
    
    
    
    void VulkanRenderer::EnsureSampler()
    {
        if (m_sampler) return;
        VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 1.0f;
        vkCreateSampler(m_device, &sci, nullptr, &m_sampler);
    }

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

    TextureHandle VulkanRenderer::CreateTexture(const unsigned char* pixels, int w, int h, int )
    {
        if (!m_ready || !pixels || w <= 0 || h <= 0) return {};
        EnsureSampler();
        const VkDeviceSize size = (VkDeviceSize)w * h * 4;            

        
        VkBuffer staging; VmaAllocation stagingAlloc; void* mapped = nullptr;
        CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
            staging, stagingAlloc, &mapped);
        memcpy(mapped, pixels, (size_t)size);

        
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

        
        
        
        if (m_imguiInit)
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
        WaitGpuIdleForDestroy();
        if (t.descriptor) ImGui_ImplVulkan_RemoveTexture(t.descriptor);
        if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
        if (t.image) vmaDestroyImage(m_allocator, t.image, t.memory);
        t = VkTextureRes{};
    }

    void VulkanRenderer::ProcessDeferredDestroys()
    {
        if (m_pendingTextureDestroys.empty()) return;

        
        
        
        
        
        
        
        
        
        
        
        
        
        vkDeviceWaitIdle(m_device);
        for (auto cb : m_commandBuffers)
            if (cb) vkResetCommandBuffer(cb, 0);

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
        VkTextureRes& t = m_textures[h.id - 1];
        
        if (!t.descriptor && m_imguiInit && t.view && m_sampler)
            t.descriptor = ImGui_ImplVulkan_AddTexture(m_sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return (void*)t.descriptor;   
    }

    void VulkanRenderer::BindTexture(int binding, TextureHandle h)
    {
        if (binding >= 0 && binding < 4)
            m_boundTextures[binding] = h;
    }

    
    bool VulkanRenderer::EnsureRenderTargetPasses()
    {
        if (m_rtRenderPass && m_resumePass) return true;

        
        
        
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

        
        
        
        {
            VkAttachmentDescription color{};
            color.format = m_swapchainFormat;
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;   
            color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentDescription depth{};
            depth.format = m_depthFormat;
            depth.samples = VK_SAMPLE_COUNT_1_BIT;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   
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
        if (!m_ready || w <= 0 || h <= 0) return {};
        EnsureSampler();
        if (!EnsureRenderTargetPasses()) return {};

        VkRenderTargetRes rt;
        rt.w = w; rt.h = h;

        
        
        VkTextureRes color;
        {
            VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
            ici.mipLevels = 1; ici.arrayLayers = 1;
            ici.format = m_swapchainFormat;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

            if (m_imguiInit)
                color.descriptor = ImGui_ImplVulkan_AddTexture(m_sampler, color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        m_textures.push_back(color);
        rt.color = TextureHandle{ (uint32_t)m_textures.size() };

        
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

        
        
        
        VkViewport vp{ 0.0f, 0.0f, (float)rt.w, (float)rt.h, 0.0f, 1.0f };
        VkRect2D sc{ {0, 0}, m_rtExtent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    void VulkanRenderer::EndRenderTarget()
    {
        if (!m_frameActive || !m_rtActive) return;
        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

        vkCmdEndRenderPass(cmd);   
        m_rtActive = false;

        
        VkClearValue clears[2]{};
        clears[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = m_resumePass;
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
    }

    TextureHandle VulkanRenderer::GetColorTexture(RenderTargetHandle h)
    {
        if (h.id == 0 || h.id > m_renderTargets.size()) return {};
        return m_renderTargets[h.id - 1].color;
    }

    bool VulkanRenderer::ReadRenderTargetPixels(RenderTargetHandle h, std::vector<unsigned char>& rgba)
    {
        if (!m_ready || m_frameActive || m_rtActive) return false;
        if (h.id == 0 || h.id > m_renderTargets.size()) return false;
        const VkRenderTargetRes& rt = m_renderTargets[h.id - 1];
        if (!rt.color || rt.color.id > m_textures.size() || rt.w <= 0 || rt.h <= 0) return false;
        const VkTextureRes& color = m_textures[rt.color.id - 1];
        if (!color.image) return false;

        vkDeviceWaitIdle(m_device);

        const VkDeviceSize size = static_cast<VkDeviceSize>(rt.w) * rt.h * 4;
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = nullptr;
        void* mapped = nullptr;
        if (!CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
            staging, stagingAlloc, &mapped))
        {
            return false;
        }

        VkCommandBuffer cmd = BeginSingleTime();

        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = color.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { static_cast<uint32_t>(rt.w), static_cast<uint32_t>(rt.h), 1 };
        vkCmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);

        EndSingleTime(cmd);

        rgba.resize(static_cast<size_t>(size));
        std::memcpy(rgba.data(), mapped, static_cast<size_t>(size));
        vmaDestroyBuffer(m_allocator, staging, stagingAlloc);
        return true;
    }

    void VulkanRenderer::DestroyRenderTarget(RenderTargetHandle h)
    {
        if (h.id == 0 || h.id > m_renderTargets.size()) return;
        VkRenderTargetRes& rt = m_renderTargets[h.id - 1];
        if (!rt.framebuffer && !rt.depthImage && !rt.color) return;

        
        
        
        
        
        
        
        
        vkDeviceWaitIdle(m_device);
        if (!m_frameActive)
            for (auto cb : m_commandBuffers)
                if (cb) vkResetCommandBuffer(cb, 0);

        if (rt.framebuffer) vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
        if (rt.depthView)   vkDestroyImageView(m_device, rt.depthView, nullptr);
        if (rt.depthImage)  vmaDestroyImage(m_allocator, rt.depthImage, rt.depthMem);
        DestroyTexture(rt.color);   
        rt = VkRenderTargetRes{};
    }

}
