#include "GlfwWindow.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#ifdef DITTO_ENABLE_VULKAN
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#include "../../3rdParty/GLFW/glfw3.h"
#ifdef _WIN32
#include "../../3rdParty/GLFW/glfw3native.h"
#endif
#include "../../3rdParty/ImGui/imgui_impl_glfw.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Ditto
{
    bool InitializeWindowSystem()
    {
        return glfwInit() == GLFW_TRUE;
    }

    void ShutdownWindowSystem()
    {
        glfwTerminate();
    }

    GlfwWindow::~GlfwWindow()
    {
        Destroy();
    }

    bool GlfwWindow::Create(const WindowDesc& desc)
    {
        Destroy();

        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
        if (desc.backendHint == WindowBackendHint::Vulkan ||
            desc.backendHint == WindowBackendHint::DirectX12)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }
        else
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        }

        m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
        if (!m_window)
            return false;

        glfwSetWindowUserPointer(m_window, this);
        glfwSetCursorPosCallback(m_window, CursorCallbackThunk);
        glfwSetDropCallback(m_window, DropCallbackThunk);
        return true;
    }

    void GlfwWindow::Destroy()
    {
        if (!m_window)
            return;
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    bool GlfwWindow::ShouldClose() const
    {
        return !m_window || glfwWindowShouldClose(m_window) != 0;
    }

    void GlfwWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void GlfwWindow::SetSize(int width, int height)
    {
        if (m_window)
            glfwSetWindowSize(m_window, width, height);
    }

    double GlfwWindow::TimeSeconds() const
    {
        return glfwGetTime();
    }

    void GlfwWindow::GetFramebufferSize(int& width, int& height) const
    {
        width = 0;
        height = 0;
        if (m_window)
            glfwGetFramebufferSize(m_window, &width, &height);
    }

    void GlfwWindow::MakeContextCurrent()
    {
        if (m_window)
            glfwMakeContextCurrent(m_window);
    }

    void GlfwWindow::SwapBuffers()
    {
        if (m_window)
            glfwSwapBuffers(m_window);
    }

    void* GlfwWindow::GetProcAddress(const char* name) const
    {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    }

    bool GlfwWindow::IsKeyPressed(int key) const
    {
        if (!m_window || key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST)
            return false;
        return glfwGetKey(m_window, key) == GLFW_PRESS;
    }

    bool GlfwWindow::IsMouseButtonPressed(int button) const
    {
        if (!m_window || button < 0 || button > GLFW_MOUSE_BUTTON_LAST)
            return false;
        return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
    }

    void GlfwWindow::GetCursorPosition(double& x, double& y) const
    {
        x = 0.0;
        y = 0.0;
        if (m_window)
            glfwGetCursorPos(m_window, &x, &y);
    }

    void GlfwWindow::SetCursorCallback(CursorCallback callback)
    {
        m_cursorCallback = std::move(callback);
    }

    void GlfwWindow::SetDropCallback(DropCallback callback)
    {
        m_dropCallback = std::move(callback);
    }

    void GlfwWindow::WaitEvents()
    {
        glfwWaitEvents();
    }

    std::vector<const char*> GlfwWindow::GetRequiredVulkanInstanceExtensions() const
    {
#ifdef DITTO_ENABLE_VULKAN
        uint32_t count = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&count);
        if (!names || count == 0)
            return {};
        return std::vector<const char*>(names, names + count);
#else
        return {};
#endif
    }

    int GlfwWindow::CreateVulkanSurface(VkInstanceHandle instance,
                                        const VkAllocationCallbacks* allocator,
                                        VkSurfaceHandle* surface) const
    {
#ifdef DITTO_ENABLE_VULKAN
        return glfwCreateWindowSurface(instance, m_window, allocator, surface);
#else
        (void)instance;
        (void)allocator;
        (void)surface;
        return -1;
#endif
    }

    void* GlfwWindow::GetNativeWindowHandle() const
    {
#ifdef _WIN32
        return m_window ? static_cast<void*>(glfwGetWin32Window(m_window)) : nullptr;
#else
        return nullptr;
#endif
    }

    bool GlfwWindow::ImGuiInitForOpenGL(bool installCallbacks)
    {
        return m_window && ImGui_ImplGlfw_InitForOpenGL(m_window, installCallbacks);
    }

    bool GlfwWindow::ImGuiInitForVulkan(bool installCallbacks)
    {
        return m_window && ImGui_ImplGlfw_InitForVulkan(m_window, installCallbacks);
    }

    bool GlfwWindow::ImGuiInitForOther(bool installCallbacks)
    {
        return m_window && ImGui_ImplGlfw_InitForOther(m_window, installCallbacks);
    }

    void GlfwWindow::ImGuiNewFrame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

    void GlfwWindow::ImGuiShutdown()
    {
        ImGui_ImplGlfw_Shutdown();
    }

    void GlfwWindow::CursorCallbackThunk(GLFWwindow* window, double x, double y)
    {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && self->m_cursorCallback)
            self->m_cursorCallback(x, y);
    }

    void GlfwWindow::DropCallbackThunk(GLFWwindow* window, int count, const char** paths)
    {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
        if (!self || !self->m_dropCallback || count <= 0 || !paths)
            return;

        std::vector<std::string> dropped;
        dropped.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            if (paths[i])
                dropped.emplace_back(paths[i]);
        self->m_dropCallback(dropped);
    }
}
