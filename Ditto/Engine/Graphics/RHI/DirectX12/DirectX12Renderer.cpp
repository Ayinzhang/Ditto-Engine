#include "DirectX12Renderer.h"

#include "../../../Core/IWindow.h"
#include "../../../Core/Logger.h"
#include "../../../../3rdParty/ImGui/imgui.h"

#include <d3d12sdklayers.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace
{
    static constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

    static size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static std::string FormatHResult(HRESULT hr)
    {
        std::ostringstream ss;
        ss << "0x" << std::uppercase << std::hex << static_cast<unsigned long>(hr);
        return ss.str();
    }

    static bool Succeeded(HRESULT hr, const char* label)
    {
        if (SUCCEEDED(hr)) return true;
        Ditto::Logger::Get().Error(std::string("[DX12] ") + label + " failed, HRESULT=" + FormatHResult(hr));
        return false;
    }

    static std::vector<uint8_t> ReadBytes(const fs::path& path)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        std::streamsize size = f.tellg();
        if (size <= 0) return {};
        f.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(bytes.data()), size);
        return bytes;
    }

    static std::string ReadText(const fs::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static std::string FindDxc()
    {
        char* env = nullptr;
        size_t len = 0;
        if (_dupenv_s(&env, &len, "VULKAN_SDK") == 0 && env)
        {
            std::string p = std::string(env) + "\\Bin\\dxc.exe";
            free(env);
            if (fs::exists(p)) return p;
        }
        const char* candidates[] = {
            "C:\\VulkanSDK\\1.4.350.0\\Bin\\dxc.exe",
            "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x64\\dxc.exe",
        };
        for (const char* c : candidates)
            if (fs::exists(c)) return c;
        return "dxc.exe";
    }

    static D3D12_RESOURCE_DESC BufferDesc(uint64_t size)
    {
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Alignment = 0;
        d.Width = std::max<uint64_t>(1, size);
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
    }

    static D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES p{};
        p.Type = type;
        p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        p.CreationNodeMask = 1;
        p.VisibleNodeMask = 1;
        return p;
    }

    static DXGI_FORMAT FloatFormatForComponents(int count)
    {
        switch (count)
        {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }
}

namespace Ditto
{
    DirectX12Renderer::DirectX12Renderer(IWindow* window)
    {
        m_valid = Initialize(window);
    }

    DirectX12Renderer::~DirectX12Renderer()
    {
        WaitForGpu();
        ImGuiShutdown();
        for (auto& b : m_storageBuffers)
            if (b.resource && b.mapped) b.resource->Unmap(0, nullptr);
        if (m_uniformBuffer && m_uniformMapped) m_uniformBuffer->Unmap(0, nullptr);
        if (m_imguiConstantBuffer && m_imguiConstantMapped) m_imguiConstantBuffer->Unmap(0, nullptr);
        if (m_fenceEvent) CloseHandle(m_fenceEvent);
    }

    bool DirectX12Renderer::Initialize(IWindow* window)
    {
        m_window = window;
        if (!CreateDeviceAndSwapchain(window)) return false;
        if (!CreateDescriptorHeaps()) return false;
        if (!CreateRootSignature()) return false;
        if (!CreateUploadBuffer(kUniformBufferSize, m_uniformBuffer, reinterpret_cast<void**>(&m_uniformMapped))) return false;
        return true;
    }

    bool DirectX12Renderer::CreateDeviceAndSwapchain(IWindow* window)
    {
        void* native = window ? window->GetNativeWindowHandle() : nullptr;
        if (!native)
        {
            Logger::Get().Error("[DX12] native HWND is unavailable");
            return false;
        }
        HWND hwnd = static_cast<HWND>(native);

        UINT factoryFlags = 0;
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        if (!Succeeded(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2")) return false;

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
                break;
        }
        if (!m_device && !Succeeded(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice"))
            return false;

        D3D12_COMMAND_QUEUE_DESC q{};
        q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!Succeeded(m_device->CreateCommandQueue(&q, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue")) return false;

        window->GetFramebufferSize(m_width, m_height);
        m_width = std::max(1, m_width);
        m_height = std::max(1, m_height);

        DXGI_SWAP_CHAIN_DESC1 sc{};
        sc.Width = static_cast<UINT>(m_width);
        sc.Height = static_cast<UINT>(m_height);
        sc.Format = kColorFormat;
        sc.SampleDesc.Count = 1;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount = kFrameCount;
        sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> swap1;
        if (!Succeeded(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sc, nullptr, nullptr, &swap1), "CreateSwapChainForHwnd"))
            return false;
        m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        if (!Succeeded(swap1.As(&m_swapchain), "Query IDXGISwapChain3")) return false;
        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

        if (!Succeeded(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)), "CreateCommandAllocator")) return false;
        if (!Succeeded(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList")) return false;
        m_commandList->Close();

        if (!Succeeded(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence")) return false;
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent) return false;

        return true;
    }

    bool DirectX12Renderer::CreateDescriptorHeaps()
    {
        auto makeHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible, DescriptorHeapPtr& out) -> bool
        {
            D3D12_DESCRIPTOR_HEAP_DESC d{};
            d.Type = type;
            d.NumDescriptors = count;
            d.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            return Succeeded(m_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&out)), "CreateDescriptorHeap");
        };
        if (!makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvCapacity, false, m_rtvHeap)) return false;
        if (!makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvCapacity, false, m_dsvHeap)) return false;
        if (!makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCapacity, true, m_srvHeap)) return false;
        if (!makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerCapacity, true, m_samplerHeap)) return false;

        m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_dsvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_srvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_samplerSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        for (uint32_t i = 0; i < kFrameCount; ++i)
            m_backbufferRtvs[i] = AllocRtv();
        m_swapchainDsv = AllocDsv();

        D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu{};
        AllocSampler(samplerCpu, m_defaultSamplerGpu);
        D3D12_SAMPLER_DESC s{};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        m_device->CreateSampler(&s, samplerCpu);

        return CreateFrameResources(m_width, m_height);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Renderer::AllocRtv()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(m_rtvUsed++) * m_rtvSize;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Renderer::AllocDsv()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(m_dsvUsed++) * m_dsvSize;
        return h;
    }

    void DirectX12Renderer::AllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
    {
        cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(m_srvUsed) * m_srvSize;
        gpu.ptr += static_cast<UINT64>(m_srvUsed) * m_srvSize;
        ++m_srvUsed;
    }

    void DirectX12Renderer::AllocSampler(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
    {
        cpu = m_samplerHeap->GetCPUDescriptorHandleForHeapStart();
        gpu = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(m_samplerUsed) * m_samplerSize;
        gpu.ptr += static_cast<UINT64>(m_samplerUsed) * m_samplerSize;
        ++m_samplerUsed;
    }

    bool DirectX12Renderer::CreateFrameResources(int width, int height)
    {
        ReleaseFrameTargets();
        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (!Succeeded(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i])), "GetBuffer"))
                return false;
            m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, m_backbufferRtvs[i]);
        }
        return CreateDepthResource(width, height, m_depthBuffer, m_swapchainDsv);
    }

    void DirectX12Renderer::ReleaseFrameTargets()
    {
        for (auto& b : m_backbuffers) b.Reset();
        m_depthBuffer.Reset();
    }

    bool DirectX12Renderer::CreateRootSignature()
    {
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 1;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 1;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE texRange{};
        texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texRange.NumDescriptors = 1;
        texRange.BaseShaderRegister = 2;
        texRange.RegisterSpace = 1;
        texRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &texRange;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE samplerRange{};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 3;
        samplerRange.RegisterSpace = 1;
        samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob, err;
        if (!Succeeded(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "D3D12SerializeRootSignature"))
        {
            if (err) Logger::Get().Error(std::string("[DX12] root signature: ") + static_cast<const char*>(err->GetBufferPointer()));
            return false;
        }
        return Succeeded(m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature");
    }

    bool DirectX12Renderer::CompileHlsl(const std::string& hlsl, const std::string& entry, const std::string& profile,
                                        std::vector<uint8_t>& outBytes, std::string& outError)
    {
        static int counter = 0;
        const std::string id = "ditto_dx12_" + std::to_string(counter++);
        fs::path tmp = fs::temp_directory_path();
        fs::path hlslPath = tmp / (id + ".hlsl");
        fs::path csoPath = tmp / (id + ".cso");
        fs::path errPath = tmp / (id + ".err");
        { std::ofstream f(hlslPath, std::ios::binary); f << hlsl; }

        std::string cmd = "\"" + FindDxc() + "\" -T " + profile + " -E " + entry + " -Zpc \"" +
            hlslPath.string() + "\" -Fo \"" + csoPath.string() + "\" 2>\"" + errPath.string() + "\"";
        int rc = std::system(("\"" + cmd + "\"").c_str());
        outBytes = ReadBytes(csoPath);
        outError = ReadText(errPath);

        std::error_code ec;
        fs::remove(hlslPath, ec);
        fs::remove(csoPath, ec);
        fs::remove(errPath, ec);
        return rc == 0 && !outBytes.empty();
    }

    bool DirectX12Renderer::CreateUploadBuffer(size_t size, ResourcePtr& outResource, void** outMapped)
    {
        D3D12_RESOURCE_DESC desc = BufferDesc(size);
        D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
        if (!Succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outResource)), "CreateUploadBuffer"))
            return false;
        if (outMapped)
        {
            D3D12_RANGE range{ 0, 0 };
            if (!Succeeded(outResource->Map(0, &range, outMapped), "Map upload buffer"))
                return false;
        }
        return true;
    }

    bool DirectX12Renderer::CreateDefaultBuffer(const void* data, size_t size, ResourcePtr& outResource, ResourcePtr& outUpload)
    {
        D3D12_RESOURCE_DESC desc = BufferDesc(size);
        D3D12_HEAP_PROPERTIES defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (!Succeeded(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outResource)), "CreateDefaultBuffer"))
            return false;

        void* mapped = nullptr;
        if (!CreateUploadBuffer(size, outUpload, &mapped)) return false;
        std::memcpy(mapped, data, size);
        outUpload->Unmap(0, nullptr);

        auto copy = [&]()
        {
            m_commandList->CopyBufferRegion(outResource.Get(), 0, outUpload.Get(), 0, size);
            Transition(outResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        };

        if (m_frameActive) copy();
        else
        {
            m_commandAllocator->Reset();
            m_commandList->Reset(m_commandAllocator.Get(), nullptr);
            copy();
            ExecuteImmediate(m_commandList.Get());
        }
        return true;
    }

    bool DirectX12Renderer::CreateDepthResource(int width, int height, ResourcePtr& outDepth, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(std::max(1, width));
        desc.Height = static_cast<UINT>(std::max(1, height));
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kDepthFormat;
        clear.DepthStencil.Depth = 1.0f;
        D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (!Succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&outDepth)), "CreateDepthResource"))
            return false;
        D3D12_DEPTH_STENCIL_VIEW_DESC vd{};
        vd.Format = kDepthFormat;
        vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        m_device->CreateDepthStencilView(outDepth.Get(), &vd, dsv);
        return true;
    }

    void DirectX12Renderer::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        if (!resource || before == after) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &b);
    }

    void DirectX12Renderer::ExecuteImmediate(ID3D12GraphicsCommandList* list)
    {
        list->Close();
        ID3D12CommandList* lists[] = { list };
        m_queue->ExecuteCommandLists(1, lists);
        WaitForGpu();
    }

    void DirectX12Renderer::WaitForGpu()
    {
        if (!m_queue || !m_fence) return;
        const uint64_t value = ++m_fenceValue;
        m_queue->Signal(m_fence.Get(), value);
        if (m_fence->GetCompletedValue() < value)
        {
            m_fence->SetEventOnCompletion(value, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    void DirectX12Renderer::BeginFrame()
    {
        if (!m_valid || m_frameActive) return;
        m_commandAllocator->Reset();
        m_commandList->Reset(m_commandAllocator.Get(), nullptr);
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
        m_commandList->SetDescriptorHeaps(2, heaps);

        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
        Transition(m_backbuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_frameActive = true;
        m_uniformOffset = 0;
        ResetCurrentRenderTargetToSwapchain();
    }

    void DirectX12Renderer::EndFrame()
    {
        if (!m_valid || !m_frameActive) return;
        Transition(m_backbuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->Close();
        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_queue->ExecuteCommandLists(1, lists);
        m_swapchain->Present(1, 0);
        WaitForGpu();
        m_frameActive = false;
    }

    bool DirectX12Renderer::NotifyWindowResized(int width, int height)
    {
        if (!m_valid || width <= 0 || height <= 0 || (width == m_width && height == m_height)) return true;
        WaitForGpu();
        ReleaseFrameTargets();
        m_width = width;
        m_height = height;
        if (!Succeeded(m_swapchain->ResizeBuffers(kFrameCount, width, height, kColorFormat, 0), "ResizeBuffers"))
            return false;
        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
        return CreateFrameResources(width, height);
    }

    void DirectX12Renderer::ResetCurrentRenderTargetToSwapchain()
    {
        m_currentRtv = m_backbufferRtvs[m_frameIndex];
        m_currentDsv = m_swapchainDsv;
        SetViewport(0, 0, m_width, m_height);
        SetScissor(false);
        m_commandList->OMSetRenderTargets(1, &m_currentRtv, FALSE, &m_currentDsv);
    }

    void DirectX12Renderer::SetViewport(int x, int y, int w, int h)
    {
        m_currentViewport = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
        if (m_frameActive) m_commandList->RSSetViewports(1, &m_currentViewport);
    }

    void DirectX12Renderer::SetScissor(bool enabled, int x, int y, int w, int h)
    {
        if (enabled) m_currentScissor = { x, y, x + w, y + h };
        else m_currentScissor = { 0, 0, LONG_MAX, LONG_MAX };
        if (m_frameActive) m_commandList->RSSetScissorRects(1, &m_currentScissor);
    }

    void DirectX12Renderer::Clear(uint32_t flags, const glm::vec4& color)
    {
        if (!m_frameActive) return;
        if (flags & ClearColor)
        {
            float c[4] = { color.r, color.g, color.b, color.a };
            m_commandList->ClearRenderTargetView(m_currentRtv, c, 0, nullptr);
        }
        if (flags & ClearDepth)
            m_commandList->ClearDepthStencilView(m_currentDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    void DirectX12Renderer::SetDepthState(bool, DepthFunc) {}
    void DirectX12Renderer::SetBlendState(bool) {}
    void DirectX12Renderer::SetWireframe(bool) {}
    void DirectX12Renderer::SetCullState(bool) {}

    MeshHandle DirectX12Renderer::CreateMesh(const float* vertexData, size_t floatCount, int strideFloats,
                                             const std::vector<VertexAttrib>&,
                                             const uint32_t* indices, size_t indexCount)
    {
        if (!m_valid || !vertexData || floatCount == 0 || strideFloats <= 0) return {};
        DxMesh mesh;
        size_t vbytes = floatCount * sizeof(float);
        if (!CreateDefaultBuffer(vertexData, vbytes, mesh.vertexBuffer, mesh.vertexUpload)) return {};
        mesh.vbv.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
        mesh.vbv.SizeInBytes = static_cast<UINT>(vbytes);
        mesh.vbv.StrideInBytes = static_cast<UINT>(strideFloats * sizeof(float));
        mesh.vertexCount = static_cast<uint32_t>(floatCount / strideFloats);
        if (indices && indexCount > 0)
        {
            size_t ibytes = indexCount * sizeof(uint32_t);
            if (!CreateDefaultBuffer(indices, ibytes, mesh.indexBuffer, mesh.indexUpload)) return {};
            mesh.ibv.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
            mesh.ibv.SizeInBytes = static_cast<UINT>(ibytes);
            mesh.ibv.Format = DXGI_FORMAT_R32_UINT;
            mesh.indexCount = static_cast<uint32_t>(indexCount);
        }
        return MeshHandle{ PushSlot(m_meshes, mesh) };
    }

    void DirectX12Renderer::DestroyMesh(MeshHandle h)
    {
        if (auto* m = GetSlot(m_meshes, h.id)) *m = {};
    }

    StorageBufferHandle DirectX12Renderer::CreateStorageBuffer(size_t sizeBytes, bool)
    {
        DxStorageBuffer b;
        b.size = std::max<size_t>(1, sizeBytes);
        if (!CreateUploadBuffer(b.size, b.resource, reinterpret_cast<void**>(&b.mapped))) return {};
        return StorageBufferHandle{ PushSlot(m_storageBuffers, b) };
    }

    void DirectX12Renderer::UpdateStorageBuffer(StorageBufferHandle h, const void* data, size_t sizeBytes)
    {
        DxStorageBuffer* b = GetSlot(m_storageBuffers, h.id);
        if (!b || !data) return;
        if (sizeBytes > b->size)
        {
            if (b->resource && b->mapped) b->resource->Unmap(0, nullptr);
            b->resource.Reset();
            b->size = AlignUp(sizeBytes, 256);
            if (!CreateUploadBuffer(b->size, b->resource, reinterpret_cast<void**>(&b->mapped))) return;
        }
        std::memcpy(b->mapped, data, sizeBytes);
    }

    void DirectX12Renderer::DestroyStorageBuffer(StorageBufferHandle h)
    {
        DxStorageBuffer* b = GetSlot(m_storageBuffers, h.id);
        if (!b) return;
        if (b->resource && b->mapped) b->resource->Unmap(0, nullptr);
        *b = {};
    }

    PipelineHandle DirectX12Renderer::CreatePipeline(const std::string& hlslSource, const PipelineState& state)
    {
        std::vector<uint8_t> vs, ps;
        std::string err;
        if (!CompileHlsl(hlslSource, "VSMain", "vs_6_0", vs, err) ||
            !CompileHlsl(hlslSource, "PSMain", "ps_6_0", ps, err))
        {
            Logger::Get().Error("[DX12] shader compile failed: " + err);
            return {};
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> input;
        input.reserve(state.vertexAttributes.size());
        for (const VertexAttrib& a : state.vertexAttributes)
        {
            D3D12_INPUT_ELEMENT_DESC e{};
            if (a.location == 0) e.SemanticName = "POSITION";
            else if (a.location == 1) e.SemanticName = "NORMAL";
            else e.SemanticName = "TEXCOORD";
            e.SemanticIndex = a.location >= 2 ? static_cast<UINT>(a.location - 2) : 0;
            e.Format = FloatFormatForComponents(a.componentCount);
            e.InputSlot = 0;
            e.AlignedByteOffset = static_cast<UINT>(a.offsetFloats * sizeof(float));
            e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            input.push_back(e);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS = { vs.data(), vs.size() };
        pso.PS = { ps.data(), ps.size() };
        pso.InputLayout = { input.empty() ? nullptr : input.data(), static_cast<UINT>(input.size()) };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = kColorFormat;
        pso.DSVFormat = kDepthFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = state.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = state.cull == CullMode::Front ? D3D12_CULL_MODE_FRONT :
            state.cull == CullMode::Back ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (state.blend)
        {
            auto& rt = pso.BlendState.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        pso.DepthStencilState.DepthEnable = state.depthTest;
        pso.DepthStencilState.DepthWriteMask = state.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc = state.depthFunc == DepthFunc::LessEqual ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_LESS;
        pso.DepthStencilState.StencilEnable = FALSE;

        DxPipeline p;
        p.state = state;
        ComPtr<ID3D12InfoQueue> infoQueue;
        UINT64 firstMessage = 0;
#if defined(_DEBUG)
        if (SUCCEEDED(m_device.As(&infoQueue)))
            firstMessage = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
#endif
        HRESULT hr = m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&p.pso));
        if (FAILED(hr))
        {
            Logger::Get().Error("[DX12] CreateGraphicsPipelineState failed, HRESULT=" + FormatHResult(hr));
            std::ostringstream layout;
            layout << "[DX12] Pipeline input layout strideFloats=" << state.vertexStrideFloats
                   << " attributes=" << input.size();
            for (const D3D12_INPUT_ELEMENT_DESC& e : input)
            {
                layout << " [" << e.SemanticName << e.SemanticIndex
                       << " fmt=" << static_cast<int>(e.Format)
                       << " offset=" << e.AlignedByteOffset << "]";
            }
            Logger::Get().Error(layout.str());

            static int failedPipelineCounter = 0;
            std::error_code ec;
            fs::path dumpDir = fs::path("TestOutput") / "DX12";
            fs::create_directories(dumpDir, ec);
            fs::path dumpPath = dumpDir / ("failed_pipeline_" + std::to_string(failedPipelineCounter++) + ".hlsl");
            if (std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc); dump)
            {
                dump << hlslSource;
                Logger::Get().Error("[DX12] Failed pipeline shader dumped to " + dumpPath.string());
            }

#if defined(_DEBUG)
            if (infoQueue)
            {
                const UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
                const UINT64 maxMessages = 12;
                const UINT64 begin = messageCount > firstMessage + maxMessages ? messageCount - maxMessages : firstMessage;
                for (UINT64 i = begin; i < messageCount; ++i)
                {
                    SIZE_T messageLength = 0;
                    infoQueue->GetMessage(i, nullptr, &messageLength);
                    if (messageLength == 0) continue;
                    std::vector<char> bytes(messageLength);
                    D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                    if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageLength)) && message->pDescription)
                        Logger::Get().Error(std::string("[DX12][DebugLayer] ") + message->pDescription);
                }
            }
#endif
            return {};
        }
        return PipelineHandle{ PushSlot(m_pipelines, p) };
    }

    void DirectX12Renderer::DestroyPipeline(PipelineHandle h)
    {
        if (auto* p = GetSlot(m_pipelines, h.id)) *p = {};
    }

    TextureHandle DirectX12Renderer::CreateTexture(const unsigned char* pixels, int w, int h, int channels)
    {
        if (!m_valid || w <= 0 || h <= 0) return {};
        DxTexture tex;
        tex.w = w;
        tex.h = h;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = static_cast<UINT64>(w);
        d.Height = static_cast<UINT>(h);
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = kColorFormat;
        d.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (!Succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex.resource)), "CreateTexture"))
            return {};
        tex.state = D3D12_RESOURCE_STATE_COPY_DEST;

        AllocSrv(tex.srvCpu, tex.srvGpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kColorFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(tex.resource.Get(), &srv, tex.srvCpu);

        if (pixels) UploadTextureData(tex, pixels, w, h, channels);
        return TextureHandle{ PushSlot(m_textures, tex) };
    }

    void DirectX12Renderer::UploadTextureData(DxTexture& texture, const unsigned char* pixels, int w, int h, int channels)
    {
        uint64_t uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rows = 0;
        uint64_t rowSize = 0;
        D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
        m_device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowSize, &uploadSize);
        void* mapped = nullptr;
        CreateUploadBuffer(static_cast<size_t>(uploadSize), texture.upload, &mapped);
        uint8_t* dst = static_cast<uint8_t*>(mapped);
        for (int y = 0; y < h; ++y)
        {
            uint8_t* row = dst + layout.Offset + y * layout.Footprint.RowPitch;
            const uint8_t* src = pixels + static_cast<size_t>(y) * w * channels;
            for (int x = 0; x < w; ++x)
            {
                row[x * 4 + 0] = src[x * channels + 0];
                row[x * 4 + 1] = channels > 1 ? src[x * channels + 1] : src[x * channels + 0];
                row[x * 4 + 2] = channels > 2 ? src[x * channels + 2] : src[x * channels + 0];
                row[x * 4 + 3] = channels > 3 ? src[x * channels + 3] : 255;
            }
        }
        texture.upload->Unmap(0, nullptr);

        auto copy = [&]()
        {
            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource = texture.resource.Get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
            srcLoc.pResource = texture.upload.Get();
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint = layout;
            m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
            Transition(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        };
        if (m_frameActive) copy();
        else
        {
            m_commandAllocator->Reset();
            m_commandList->Reset(m_commandAllocator.Get(), nullptr);
            copy();
            ExecuteImmediate(m_commandList.Get());
        }
    }

    void DirectX12Renderer::DestroyTexture(TextureHandle h)
    {
        if (auto* t = GetSlot(m_textures, h.id)) *t = {};
    }

    void* DirectX12Renderer::GetImGuiTextureID(TextureHandle h)
    {
        DxTexture* t = GetSlot(m_textures, h.id);
        return t ? reinterpret_cast<void*>(static_cast<uintptr_t>(t->srvGpu.ptr)) : nullptr;
    }

    void DirectX12Renderer::BindTexture(int binding, TextureHandle h)
    {
        if (binding >= 0 && binding < 4) m_boundTextures[binding] = h;
    }

    RenderTargetHandle DirectX12Renderer::CreateRenderTarget(int w, int h)
    {
        DxTexture tex;
        tex.w = w;
        tex.h = h;
        tex.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = static_cast<UINT64>(w);
        d.Height = static_cast<UINT>(h);
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = kColorFormat;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kColorFormat;
        D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (!Succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
            tex.state, &clear, IID_PPV_ARGS(&tex.resource)), "CreateRenderTargetColor"))
            return {};
        AllocSrv(tex.srvCpu, tex.srvGpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kColorFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(tex.resource.Get(), &srv, tex.srvCpu);

        DxRenderTarget rt;
        rt.color = TextureHandle{ PushSlot(m_textures, tex) };
        rt.rtv = AllocRtv();
        rt.dsv = AllocDsv();
        m_device->CreateRenderTargetView(tex.resource.Get(), nullptr, rt.rtv);
        CreateDepthResource(w, h, rt.depth, rt.dsv);
        rt.w = w;
        rt.h = h;
        return RenderTargetHandle{ PushSlot(m_renderTargets, rt) };
    }

    void DirectX12Renderer::BeginRenderTarget(RenderTargetHandle h)
    {
        DxRenderTarget* rt = GetSlot(m_renderTargets, h.id);
        DxTexture* tex = rt ? GetSlot(m_textures, rt->color.id) : nullptr;
        if (!rt || !tex || !m_frameActive) return;
        Transition(tex->resource.Get(), tex->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        tex->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_currentRtv = rt->rtv;
        m_currentDsv = rt->dsv;
        m_inRenderTarget = true;
        SetViewport(0, 0, rt->w, rt->h);
        SetScissor(false);
        m_commandList->OMSetRenderTargets(1, &m_currentRtv, FALSE, &m_currentDsv);
    }

    void DirectX12Renderer::EndRenderTarget()
    {
        if (!m_inRenderTarget) return;
        for (auto& rt : m_renderTargets)
        {
            DxTexture* tex = GetSlot(m_textures, rt.color.id);
            if (tex && tex->state == D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                Transition(tex->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                tex->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                break;
            }
        }
        m_inRenderTarget = false;
        ResetCurrentRenderTargetToSwapchain();
    }

    TextureHandle DirectX12Renderer::GetColorTexture(RenderTargetHandle h)
    {
        DxRenderTarget* rt = GetSlot(m_renderTargets, h.id);
        return rt ? rt->color : TextureHandle{};
    }

    bool DirectX12Renderer::ReadRenderTargetPixels(RenderTargetHandle h, std::vector<unsigned char>& rgba)
    {
        DxRenderTarget* rt = GetSlot(m_renderTargets, h.id);
        DxTexture* tex = rt ? GetSlot(m_textures, rt->color.id) : nullptr;
        if (!rt || !tex || !tex->resource) return false;
        if (m_frameActive)
        {
            Logger::Get().Error("[DX12] ReadRenderTargetPixels requires the frame to be submitted first");
            return false;
        }

        D3D12_RESOURCE_DESC desc = tex->resource->GetDesc();
        uint64_t readbackSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rows = 0;
        uint64_t rowSize = 0;
        m_device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowSize, &readbackSize);

        ResourcePtr readback;
        D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC buffer = BufferDesc(readbackSize);
        if (!Succeeded(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "CreateReadbackBuffer"))
            return false;

        D3D12_RESOURCE_STATES originalState = tex->state;
        m_commandAllocator->Reset();
        m_commandList->Reset(m_commandAllocator.Get(), nullptr);
        if (originalState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            Transition(tex->resource.Get(), originalState, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = tex->resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = readback.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = layout;
        m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        if (originalState != D3D12_RESOURCE_STATE_COPY_SOURCE)
            Transition(tex->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, originalState);
        ExecuteImmediate(m_commandList.Get());
        tex->state = originalState;

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(readbackSize) };
        if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped))) || !mapped)
            return false;

        rgba.resize(static_cast<size_t>(rt->w) * static_cast<size_t>(rt->h) * 4);
        for (int y = 0; y < rt->h; ++y)
        {
            const uint8_t* src = mapped + layout.Offset + static_cast<size_t>(y) * layout.Footprint.RowPitch;
            uint8_t* dst = rgba.data() + static_cast<size_t>(rt->h - 1 - y) * rt->w * 4;
            std::memcpy(dst, src, static_cast<size_t>(rt->w) * 4);
        }
        D3D12_RANGE writtenRange{ 0, 0 };
        readback->Unmap(0, &writtenRange);
        return true;
    }

    void DirectX12Renderer::DestroyRenderTarget(RenderTargetHandle h)
    {
        if (auto* rt = GetSlot(m_renderTargets, h.id))
        {
            DestroyTexture(rt->color);
            *rt = {};
        }
    }

    void DirectX12Renderer::BindPipeline(PipelineHandle h)
    {
        m_boundPipeline = h;
        DxPipeline* p = GetSlot(m_pipelines, h.id);
        if (!p || !m_frameActive) return;
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
        m_commandList->SetDescriptorHeaps(2, heaps);
        m_commandList->SetPipelineState(p->pso.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void DirectX12Renderer::SetFrameUniforms(const FrameUniforms& u)
    {
        if (!m_uniformMapped) return;
        struct Packed
        {
            glm::mat4 view;
            glm::mat4 projection;
            glm::vec3 viewPos; float p0;
            glm::vec3 lightColor; float p1;
            glm::vec3 lightDir; float lightIntensity;
            glm::vec4 time;
            glm::vec4 sinTime;
            glm::vec4 cosTime;
            glm::vec4 deltaTime;
            glm::vec4 screenParams;
        } data{};
        data.view = u.view;
        data.projection = u.projection;
        data.viewPos = u.viewPos;
        data.lightColor = u.lightColor;
        data.lightDir = u.lightDir;
        data.lightIntensity = u.lightIntensity;
        data.time = u.time;
        data.sinTime = u.sinTime;
        data.cosTime = u.cosTime;
        data.deltaTime = u.deltaTime;
        data.screenParams = u.screenParams;

        m_uniformOffset = AlignUp(m_uniformOffset, 256);
        if (m_uniformOffset + sizeof(Packed) > kUniformBufferSize) m_uniformOffset = 0;
        std::memcpy(m_uniformMapped + m_uniformOffset, &data, sizeof(Packed));
        m_currentUniformGpu = m_uniformBuffer->GetGPUVirtualAddress() + m_uniformOffset;
        m_uniformOffset += AlignUp(sizeof(Packed), 256);
    }

    void DirectX12Renderer::BindStorageBuffer(int binding, StorageBufferHandle h)
    {
        if (binding >= 0 && binding < 2) m_boundStorage[binding] = h;
    }

    void DirectX12Renderer::DrawInstanced(MeshHandle h, int instanceCount)
    {
        if (!m_frameActive || instanceCount <= 0) return;
        DxMesh* mesh = GetSlot(m_meshes, h.id);
        DxPipeline* pipe = GetSlot(m_pipelines, m_boundPipeline.id);
        if (!mesh || !mesh->vertexBuffer || !pipe) return;
        DxStorageBuffer* sb0 = GetSlot(m_storageBuffers, m_boundStorage[0].id);
        DxStorageBuffer* sb1 = GetSlot(m_storageBuffers, m_boundStorage[1].id);
        DxTexture* tex = GetSlot(m_textures, m_boundTextures[2].id);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        m_commandList->SetPipelineState(pipe->pso.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_currentUniformGpu);
        m_commandList->SetGraphicsRootShaderResourceView(1, sb0 && sb0->resource ? sb0->resource->GetGPUVirtualAddress() : 0);
        m_commandList->SetGraphicsRootShaderResourceView(2, sb1 && sb1->resource ? sb1->resource->GetGPUVirtualAddress() : 0);
        if (tex) m_commandList->SetGraphicsRootDescriptorTable(3, tex->srvGpu);
        m_commandList->SetGraphicsRootDescriptorTable(4, m_defaultSamplerGpu);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &mesh->vbv);
        if (mesh->indexCount > 0)
        {
            m_commandList->IASetIndexBuffer(&mesh->ibv);
            m_commandList->DrawIndexedInstanced(mesh->indexCount, instanceCount, 0, 0, 0);
        }
        else
        {
            m_commandList->DrawInstanced(mesh->vertexCount, instanceCount, 0, 0);
        }
    }

    // ---------------- ImGui DX12 renderer ----------------
    void DirectX12Renderer::ImGuiInit(IWindow* window)
    {
        if (m_imguiInit || !m_valid) return;
        if (window) window->ImGuiInitForOther(true);
        m_imguiWindow = window;
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "Ditto_DX12";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        m_imguiFontTexture = CreateTexture(pixels, w, h, 4);
        io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(GetImGuiTextureID(m_imguiFontTexture))));
        CreateImGuiResources();
        CreateUploadBuffer(256, m_imguiConstantBuffer, reinterpret_cast<void**>(&m_imguiConstantMapped));
        m_imguiInit = true;
    }

    void DirectX12Renderer::ImGuiShutdown()
    {
        if (!m_imguiInit) return;
        if (m_window) m_window->ImGuiShutdown();
        ImGui::GetIO().Fonts->SetTexID(nullptr);
        DestroyTexture(m_imguiFontTexture);
        m_imguiFontTexture = {};
        m_imguiPipeline.Reset();
        m_imguiRootSignature.Reset();
        m_imguiVertexBuffer.Reset();
        m_imguiIndexBuffer.Reset();
        m_imguiConstantBuffer.Reset();
        m_imguiConstantMapped = nullptr;
        m_imguiInit = false;
    }

    void DirectX12Renderer::ImGuiNewFrame()
    {
        if (m_imguiWindow) m_imguiWindow->ImGuiNewFrame();
    }

    bool DirectX12Renderer::CreateImGuiResources()
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = 2;
        rs.pParameters = params;
        rs.NumStaticSamplers = 1;
        rs.pStaticSamplers = &sampler;
        rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, err;
        if (!Succeeded(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "ImguiRootSignature"))
            return false;
        if (!Succeeded(m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_imguiRootSignature)), "CreateImguiRootSignature"))
            return false;

        const char* shader = R"(
cbuffer VertexConstants : register(b0) { float4x4 ProjectionMatrix; };
struct VSInput { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct PSInput { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
PSInput VSMain(VSInput i) { PSInput o; o.pos = mul(ProjectionMatrix, float4(i.pos.xy, 0, 1)); o.uv = i.uv; o.col = i.col; return o; }
Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);
float4 PSMain(PSInput i) : SV_Target { return i.col * texture0.Sample(sampler0, i.uv); }
)";
        std::vector<uint8_t> vs, ps;
        std::string compileError;
        if (!CompileHlsl(shader, "VSMain", "vs_6_0", vs, compileError) ||
            !CompileHlsl(shader, "PSMain", "ps_6_0", ps, compileError))
        {
            Logger::Get().Error("[DX12] ImGui shader compile failed: " + compileError);
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC input[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, pos)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ImDrawVert, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(ImDrawVert, col)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_imguiRootSignature.Get();
        pso.VS = { vs.data(), vs.size() };
        pso.PS = { ps.data(), ps.size() };
        pso.InputLayout = { input, _countof(input) };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = kColorFormat;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthEnable = FALSE;
        return Succeeded(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_imguiPipeline)), "CreateImGuiPipeline");
    }

    void DirectX12Renderer::ImGuiRenderDrawData(void* drawDataPtr)
    {
        ImDrawData* drawData = static_cast<ImDrawData*>(drawDataPtr);
        if (!m_frameActive || !drawData || drawData->TotalVtxCount <= 0 || !m_imguiPipeline) return;

        if (drawData->TotalVtxCount > m_imguiVertexCapacity)
        {
            m_imguiVertexCapacity = drawData->TotalVtxCount + 5000;
            CreateUploadBuffer(static_cast<size_t>(m_imguiVertexCapacity) * sizeof(ImDrawVert), m_imguiVertexBuffer, nullptr);
        }
        if (drawData->TotalIdxCount > m_imguiIndexCapacity)
        {
            m_imguiIndexCapacity = drawData->TotalIdxCount + 10000;
            CreateUploadBuffer(static_cast<size_t>(m_imguiIndexCapacity) * sizeof(ImDrawIdx), m_imguiIndexBuffer, nullptr);
        }

        ImDrawVert* vtxDst = nullptr;
        ImDrawIdx* idxDst = nullptr;
        m_imguiVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&vtxDst));
        m_imguiIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&idxDst));
        for (int n = 0; n < drawData->CmdListsCount; ++n)
        {
            const ImDrawList* cl = drawData->CmdLists[n];
            std::memcpy(vtxDst, cl->VtxBuffer.Data, cl->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(idxDst, cl->IdxBuffer.Data, cl->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cl->VtxBuffer.Size;
            idxDst += cl->IdxBuffer.Size;
        }
        m_imguiVertexBuffer->Unmap(0, nullptr);
        m_imguiIndexBuffer->Unmap(0, nullptr);

        float L = drawData->DisplayPos.x;
        float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
        float T = drawData->DisplayPos.y;
        float B = drawData->DisplayPos.y + drawData->DisplaySize.y;
        float mvp[4][4] = {
            { 2.0f / (R - L), 0.0f, 0.0f, 0.0f },
            { 0.0f, 2.0f / (T - B), 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.5f, 0.0f },
            { (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f },
        };
        std::memcpy(m_imguiConstantMapped, mvp, sizeof(mvp));

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = m_imguiVertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = static_cast<UINT>(m_imguiVertexCapacity * sizeof(ImDrawVert));
        vbv.StrideInBytes = sizeof(ImDrawVert);
        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = m_imguiIndexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = static_cast<UINT>(m_imguiIndexCapacity * sizeof(ImDrawIdx));
        ibv.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

        D3D12_VIEWPORT vp{ 0.0f, 0.0f, drawData->DisplaySize.x, drawData->DisplaySize.y, 0.0f, 1.0f };
        m_commandList->RSSetViewports(1, &vp);
        m_commandList->OMSetRenderTargets(1, &m_currentRtv, FALSE, nullptr);
        m_commandList->SetGraphicsRootSignature(m_imguiRootSignature.Get());
        m_commandList->SetPipelineState(m_imguiPipeline.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, heaps);
        m_commandList->SetGraphicsRootConstantBufferView(0, m_imguiConstantBuffer->GetGPUVirtualAddress());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &vbv);
        m_commandList->IASetIndexBuffer(&ibv);

        int globalVtxOffset = 0;
        int globalIdxOffset = 0;
        ImVec2 clipOff = drawData->DisplayPos;
        for (int n = 0; n < drawData->CmdListsCount; ++n)
        {
            const ImDrawList* cl = drawData->CmdLists[n];
            for (int cmdI = 0; cmdI < cl->CmdBuffer.Size; ++cmdI)
            {
                const ImDrawCmd* pcmd = &cl->CmdBuffer[cmdI];
                if (pcmd->UserCallback) { pcmd->UserCallback(cl, pcmd); continue; }
                D3D12_RECT r{
                    static_cast<LONG>(pcmd->ClipRect.x - clipOff.x),
                    static_cast<LONG>(pcmd->ClipRect.y - clipOff.y),
                    static_cast<LONG>(pcmd->ClipRect.z - clipOff.x),
                    static_cast<LONG>(pcmd->ClipRect.w - clipOff.y)
                };
                if (r.right <= r.left || r.bottom <= r.top) continue;
                m_commandList->RSSetScissorRects(1, &r);
                D3D12_GPU_DESCRIPTOR_HANDLE tex{};
                tex.ptr = static_cast<UINT64>(pcmd->GetTexID());
                m_commandList->SetGraphicsRootDescriptorTable(1, tex);
                m_commandList->DrawIndexedInstanced(pcmd->ElemCount, 1,
                    pcmd->IdxOffset + globalIdxOffset,
                    pcmd->VtxOffset + globalVtxOffset, 0);
            }
            globalIdxOffset += cl->IdxBuffer.Size;
            globalVtxOffset += cl->VtxBuffer.Size;
        }
    }
}
