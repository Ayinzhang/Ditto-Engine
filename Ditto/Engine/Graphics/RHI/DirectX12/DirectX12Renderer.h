#pragma once

#include "../IRenderer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ditto
{
    class DirectX12Renderer final : public IRenderer
    {
    public:
        explicit DirectX12Renderer(IWindow* window);
        ~DirectX12Renderer() override;

        bool IsValid() const { return m_valid; }
        RendererBackend Backend() const override { return RendererBackend::DirectX12; }

        void BeginFrame() override;
        void EndFrame() override;
        bool NotifyWindowResized(int width, int height) override;

        void ImGuiInit(IWindow* window) override;
        void ImGuiShutdown() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(void* drawData) override;

        void SetViewport(int x, int y, int w, int h) override;
        void SetScissor(bool enabled, int x = 0, int y = 0, int w = 0, int h = 0) override;
        void Clear(uint32_t flags, const glm::vec4& color) override;
        void SetDepthState(bool enabled, DepthFunc func = DepthFunc::Less) override;
        void SetBlendState(bool enabled) override;
        void SetWireframe(bool enabled) override;
        void SetCullState(bool enabled) override;

        MeshHandle CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                              const std::vector<VertexAttrib>& attribs,
                              const uint32_t* indices = nullptr, size_t indexCount = 0) override;
        void DestroyMesh(MeshHandle) override;

        StorageBufferHandle CreateStorageBuffer(size_t sizeBytes, bool dynamic) override;
        void UpdateStorageBuffer(StorageBufferHandle, const void* data, size_t sizeBytes) override;
        void DestroyStorageBuffer(StorageBufferHandle) override;

        PipelineHandle CreatePipeline(const std::string& hlslSource, const PipelineState& state = {}) override;
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
        using ResourcePtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
        using DescriptorHeapPtr = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;

        static constexpr uint32_t kFrameCount = 2;
        static constexpr uint32_t kSrvCapacity = 2048;
        static constexpr uint32_t kSamplerCapacity = 16;
        static constexpr uint32_t kRtvCapacity = 256;
        static constexpr uint32_t kDsvCapacity = 128;
        static constexpr uint32_t kUniformBufferSize = 1024 * 1024;

        struct DxMesh
        {
            ResourcePtr vertexBuffer;
            ResourcePtr indexBuffer;
            ResourcePtr vertexUpload;
            ResourcePtr indexUpload;
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            D3D12_INDEX_BUFFER_VIEW ibv{};
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
        };

        struct DxStorageBuffer
        {
            ResourcePtr resource;
            uint8_t* mapped = nullptr;
            size_t size = 0;
        };

        struct DxPipeline
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
            PipelineState state;
        };

        struct DxTexture
        {
            ResourcePtr resource;
            ResourcePtr upload;
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            int w = 0;
            int h = 0;
        };

        struct DxRenderTarget
        {
            TextureHandle color;
            ResourcePtr depth;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
            D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
            D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            int w = 0;
            int h = 0;
        };

        bool Initialize(IWindow* window);
        bool CreateDeviceAndSwapchain(IWindow* window);
        bool CreateDescriptorHeaps();
        bool CreateFrameResources(int width, int height);
        bool CreateRootSignature();
        bool CreateImGuiResources();
        bool CompileHlsl(const std::string& hlsl, const std::string& entry, const std::string& profile,
                         std::vector<uint8_t>& outBytes, std::string& outError);
        bool CreateUploadBuffer(size_t size, ResourcePtr& outResource, void** outMapped = nullptr);
        bool CreateDefaultBuffer(const void* data, size_t size, ResourcePtr& outResource, ResourcePtr& outUpload);
        bool CreateDepthResource(int width, int height, ResourcePtr& outDepth, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
        void UploadTextureData(DxTexture& texture, const unsigned char* pixels, int w, int h, int channels);
        void ExecuteImmediate(ID3D12GraphicsCommandList* list);
        void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
        void WaitForGpu();
        void ReleaseFrameTargets();
        void ResetCurrentRenderTargetToSwapchain();

        D3D12_CPU_DESCRIPTOR_HANDLE AllocRtv();
        D3D12_CPU_DESCRIPTOR_HANDLE AllocDsv();
        void AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);
        void AllocSampler(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);

        template<typename T>
        uint32_t PushSlot(std::vector<T>& table, const T& value)
        {
            table.push_back(value);
            return static_cast<uint32_t>(table.size());
        }
        template<typename T>
        T* GetSlot(std::vector<T>& table, uint32_t id)
        {
            if (id == 0 || id > table.size()) return nullptr;
            return &table[id - 1];
        }

        bool m_valid = false;
        IWindow* m_window = nullptr;

        Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapchain;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        HANDLE m_fenceEvent = nullptr;
        uint64_t m_fenceValue = 0;

        ResourcePtr m_backbuffers[kFrameCount];
        ResourcePtr m_depthBuffer;
        D3D12_CPU_DESCRIPTOR_HANDLE m_backbufferRtvs[kFrameCount]{};
        D3D12_CPU_DESCRIPTOR_HANDLE m_swapchainDsv{};
        uint32_t m_frameIndex = 0;
        int m_width = 1;
        int m_height = 1;

        DescriptorHeapPtr m_rtvHeap;
        DescriptorHeapPtr m_dsvHeap;
        DescriptorHeapPtr m_srvHeap;
        DescriptorHeapPtr m_samplerHeap;
        uint32_t m_rtvUsed = 0;
        uint32_t m_dsvUsed = 0;
        uint32_t m_srvUsed = 0;
        uint32_t m_samplerUsed = 0;
        uint32_t m_rtvSize = 0;
        uint32_t m_dsvSize = 0;
        uint32_t m_srvSize = 0;
        uint32_t m_samplerSize = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE m_defaultSamplerGpu{};

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_imguiRootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_imguiPipeline;

        ResourcePtr m_uniformBuffer;
        uint8_t* m_uniformMapped = nullptr;
        size_t m_uniformOffset = 0;
        D3D12_GPU_VIRTUAL_ADDRESS m_currentUniformGpu = 0;

        std::vector<DxMesh> m_meshes;
        std::vector<DxStorageBuffer> m_storageBuffers;
        std::vector<DxPipeline> m_pipelines;
        std::vector<DxTexture> m_textures;
        std::vector<DxRenderTarget> m_renderTargets;

        PipelineHandle m_boundPipeline;
        StorageBufferHandle m_boundStorage[2];
        TextureHandle m_boundTextures[4];

        D3D12_CPU_DESCRIPTOR_HANDLE m_currentRtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE m_currentDsv{};
        D3D12_VIEWPORT m_currentViewport{};
        D3D12_RECT m_currentScissor{};
        bool m_frameActive = false;
        bool m_inRenderTarget = false;

        IWindow* m_imguiWindow = nullptr;
        bool m_imguiInit = false;
        TextureHandle m_imguiFontTexture;
        ResourcePtr m_imguiVertexBuffer;
        ResourcePtr m_imguiIndexBuffer;
        int m_imguiVertexCapacity = 0;
        int m_imguiIndexCapacity = 0;
        ResourcePtr m_imguiConstantBuffer;
        uint8_t* m_imguiConstantMapped = nullptr;
    };
}
