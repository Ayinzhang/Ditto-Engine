#pragma once

#include "IWindow.h"

struct GLFWwindow;

namespace Ditto
{
    class GlfwWindow final : public IWindow
    {
    public:
        GlfwWindow() = default;
        ~GlfwWindow() override;

        bool Create(const WindowDesc& desc) override;
        void Destroy() override;
        bool ShouldClose() const override;
        void PollEvents() override;
        void SetSize(int width, int height) override;
        double TimeSeconds() const override;
        void GetWindowSize(int& width, int& height) const override;
        void GetFramebufferSize(int& width, int& height) const override;

        void MakeContextCurrent() override;
        void SwapBuffers() override;
        void* GetProcAddress(const char* name) const override;

        bool IsKeyPressed(int key) const override;
        bool IsMouseButtonPressed(int button) const override;
        void GetCursorPosition(double& x, double& y) const override;

        void SetCursorCallback(CursorCallback callback) override;
        void SetDropCallback(DropCallback callback) override;

        void WaitEvents() override;
        std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
        int CreateVulkanSurface(VkInstanceHandle instance,
                                const VkAllocationCallbacks* allocator,
                                VkSurfaceHandle* surface) const override;
        void* GetNativeWindowHandle() const override;

        bool ImGuiInitForOpenGL(bool installCallbacks) override;
        bool ImGuiInitForVulkan(bool installCallbacks) override;
        bool ImGuiInitForOther(bool installCallbacks) override;
        void ImGuiNewFrame() override;
        void ImGuiShutdown() override;

    private:
        static void CursorCallbackThunk(GLFWwindow* window, double x, double y);
        static void DropCallbackThunk(GLFWwindow* window, int count, const char** paths);

        GLFWwindow* m_window = nullptr;
        CursorCallback m_cursorCallback;
        DropCallback m_dropCallback;
    };
}
