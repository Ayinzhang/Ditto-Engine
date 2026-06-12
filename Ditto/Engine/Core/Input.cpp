#include "Input.h"
#include <cstring>

GLFWwindow* Input::s_window = nullptr;
bool Input::s_cur[GLFW_KEY_LAST + 1] = {};
bool Input::s_prev[GLFW_KEY_LAST + 1] = {};
bool Input::s_curMouse[GLFW_MOUSE_BUTTON_LAST + 1] = {};
bool Input::s_prevMouse[GLFW_MOUSE_BUTTON_LAST + 1] = {};
double Input::s_mouseX = 0.0, Input::s_mouseY = 0.0;
float Input::s_viewX = 0.0f, Input::s_viewY = 0.0f;
float Input::s_viewW = 1.0f, Input::s_viewH = 1.0f;

void Input::Init(GLFWwindow* window)
{
    s_window = window;
    std::memset(s_cur, 0, sizeof(s_cur));
    std::memset(s_prev, 0, sizeof(s_prev));
    std::memset(s_curMouse, 0, sizeof(s_curMouse));
    std::memset(s_prevMouse, 0, sizeof(s_prevMouse));
}

void Input::NewFrame()
{
    if (!s_window) return;

    std::memcpy(s_prev, s_cur, sizeof(s_cur));
    std::memcpy(s_prevMouse, s_curMouse, sizeof(s_curMouse));

    // Valid GLFW key codes start at GLFW_KEY_SPACE (32); querying lower values
    // raises GLFW_INVALID_ENUM.
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
        s_cur[key] = glfwGetKey(s_window, key) == GLFW_PRESS;

    for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST; ++b)
        s_curMouse[b] = glfwGetMouseButton(s_window, b) == GLFW_PRESS;

    glfwGetCursorPos(s_window, &s_mouseX, &s_mouseY);
}

bool Input::GetKey(int key)
{
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return s_cur[key];
}

bool Input::GetKeyDown(int key)
{
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return s_cur[key] && !s_prev[key];
}

bool Input::GetKeyUp(int key)
{
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return !s_cur[key] && s_prev[key];
}

bool Input::GetMouseButton(int button)
{
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return s_curMouse[button];
}

bool Input::GetMouseButtonDown(int button)
{
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return s_curMouse[button] && !s_prevMouse[button];
}

bool Input::GetMouseButtonUp(int button)
{
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return !s_curMouse[button] && s_prevMouse[button];
}

glm::vec2 Input::GetMousePosition()
{
    return glm::vec2((float)s_mouseX - s_viewX, (float)s_mouseY - s_viewY);
}

glm::vec2 Input::GetGameViewportSize()
{
    return glm::vec2(s_viewW, s_viewH);
}

void Input::SetGameViewport(float x, float y, float w, float h)
{
    s_viewX = x; s_viewY = y;
    s_viewW = (w > 0.0f) ? w : 1.0f;
    s_viewH = (h > 0.0f) ? h : 1.0f;
}
