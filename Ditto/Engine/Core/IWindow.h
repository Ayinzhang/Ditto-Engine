#pragma once

#include <functional>
#include <string>
#include <vector>

struct VkAllocationCallbacks;
struct VkInstance_T;
struct VkSurfaceKHR_T;

namespace Ditto
{
    using VkInstanceHandle = VkInstance_T*;
    using VkSurfaceHandle = VkSurfaceKHR_T*;

    enum class WindowBackendHint
    {
        OpenGL,
        Vulkan,
        DirectX12,
    };

    struct WindowDesc
    {
        int width = 1200;
        int height = 900;
        std::string title = "Ditto";
        WindowBackendHint backendHint = WindowBackendHint::OpenGL;
        bool visible = true;
    };

    class IWindow
    {
    public:
        using CursorCallback = std::function<void(double x, double y)>;
        using DropCallback = std::function<void(const std::vector<std::string>& paths)>;

        virtual ~IWindow() = default;

        virtual bool Create(const WindowDesc& desc) = 0;
        virtual void Destroy() = 0;
        virtual bool ShouldClose() const = 0;
        virtual void PollEvents() = 0;
        virtual void SetSize(int width, int height) = 0;
        virtual double TimeSeconds() const = 0;
        virtual void GetFramebufferSize(int& width, int& height) const = 0;

        virtual void MakeContextCurrent() = 0;
        virtual void SwapBuffers() = 0;
        virtual void* GetProcAddress(const char* name) const = 0;

        virtual bool IsKeyPressed(int key) const = 0;
        virtual bool IsMouseButtonPressed(int button) const = 0;
        virtual void GetCursorPosition(double& x, double& y) const = 0;

        virtual void SetCursorCallback(CursorCallback callback) = 0;
        virtual void SetDropCallback(DropCallback callback) = 0;

        virtual void WaitEvents() = 0;
        virtual std::vector<const char*> GetRequiredVulkanInstanceExtensions() const = 0;
        virtual int CreateVulkanSurface(VkInstanceHandle instance,
                                        const VkAllocationCallbacks* allocator,
                                        VkSurfaceHandle* surface) const = 0;
        virtual void* GetNativeWindowHandle() const = 0;

        virtual bool ImGuiInitForOpenGL(bool installCallbacks) = 0;
        virtual bool ImGuiInitForVulkan(bool installCallbacks) = 0;
        virtual bool ImGuiInitForOther(bool installCallbacks) = 0;
        virtual void ImGuiNewFrame() = 0;
        virtual void ImGuiShutdown() = 0;
    };

    bool InitializeWindowSystem();
    void ShutdownWindowSystem();
}
