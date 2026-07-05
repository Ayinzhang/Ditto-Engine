#include "Input.h"
#include "IWindow.h"
#include "InputKeyCodes.h"
#include <cstring>

Ditto::IWindow* Input::s_window = nullptr;
bool Input::s_cur[Input::kMaxKeyCode + 1] = {};
bool Input::s_prev[Input::kMaxKeyCode + 1] = {};
bool Input::s_curMouse[Input::kMaxMouseButton + 1] = {};
bool Input::s_prevMouse[Input::kMaxMouseButton + 1] = {};
double Input::s_mouseX = 0.0, Input::s_mouseY = 0.0;
float Input::s_viewX = 0.0f, Input::s_viewY = 0.0f;
float Input::s_viewW = 1.0f, Input::s_viewH = 1.0f;
float Input::s_contentW = 1.0f, Input::s_contentH = 1.0f;
float Input::s_axisHorizontal = 0.0f, Input::s_axisVertical = 0.0f;

void Input::Init(Ditto::IWindow* window)
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

    for (int key = kMinKeyCode; key <= kMaxKeyCode; ++key)
        s_cur[key] = s_window->IsKeyPressed(key);

    for (int b = 0; b <= kMaxMouseButton; ++b)
        s_curMouse[b] = s_window->IsMouseButtonPressed(b);

    s_window->GetCursorPosition(s_mouseX, s_mouseY);

    // Advance smoothed virtual axes toward their raw targets (Unity-like ramp).
    // A fixed per-frame step keeps this independent of the gameplay timestep.
    const float kAxisStep = 0.25f;
    auto approach = [&](float current, float target) -> float
    {
        if (current < target) return (current + kAxisStep > target) ? target : current + kAxisStep;
        if (current > target) return (current - kAxisStep < target) ? target : current - kAxisStep;
        return current;
    };
    s_axisHorizontal = approach(s_axisHorizontal, GetAxisRaw("Horizontal"));
    s_axisVertical   = approach(s_axisVertical, GetAxisRaw("Vertical"));
}

bool Input::GetKey(int key)
{
    if (key < kMinKeyCode || key > kMaxKeyCode) return false;
    return s_cur[key];
}

bool Input::GetKeyDown(int key)
{
    if (key < kMinKeyCode || key > kMaxKeyCode) return false;
    return s_cur[key] && !s_prev[key];
}

bool Input::GetKeyUp(int key)
{
    if (key < kMinKeyCode || key > kMaxKeyCode) return false;
    return !s_cur[key] && s_prev[key];
}

bool Input::GetMouseButton(int button)
{
    if (button < 0 || button > kMaxMouseButton) return false;
    return s_curMouse[button];
}

bool Input::GetMouseButtonDown(int button)
{
    if (button < 0 || button > kMaxMouseButton) return false;
    return s_curMouse[button] && !s_prevMouse[button];
}

bool Input::GetMouseButtonUp(int button)
{
    if (button < 0 || button > kMaxMouseButton) return false;
    return !s_curMouse[button] && s_prevMouse[button];
}

glm::vec2 Input::GetMousePosition()
{
    float localX = (float)s_mouseX - s_viewX;
    float localY = (float)s_mouseY - s_viewY;
    if (s_viewW > 0.0f && s_viewH > 0.0f)
    {
        localX *= s_contentW / s_viewW;
        localY *= s_contentH / s_viewH;
    }
    return glm::vec2(localX, localY);
}

glm::vec2 Input::GetGameViewportSize()
{
    return glm::vec2(s_contentW, s_contentH);
}

void Input::SetGameViewport(float x, float y, float w, float h, float contentW, float contentH)
{
    s_viewX = x; s_viewY = y;
    s_viewW = (w > 0.0f) ? w : 1.0f;
    s_viewH = (h > 0.0f) ? h : 1.0f;
    s_contentW = (contentW > 0.0f) ? contentW : s_viewW;
    s_contentH = (contentH > 0.0f) ? contentH : s_viewH;
}

static bool StrEq(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) { if (*a != *b) return false; ++a; ++b; }
    return *a == *b;
}

float Input::GetAxisRaw(const char* axisName)
{
    if (StrEq(axisName, "Horizontal"))
    {
        float v = 0.0f;
        if (GetKey(Ditto::KeyCode::D) || GetKey(Ditto::KeyCode::RightArrow)) v += 1.0f;
        if (GetKey(Ditto::KeyCode::A) || GetKey(Ditto::KeyCode::LeftArrow))  v -= 1.0f;
        return v;
    }
    if (StrEq(axisName, "Vertical"))
    {
        float v = 0.0f;
        if (GetKey(Ditto::KeyCode::W) || GetKey(Ditto::KeyCode::UpArrow))   v += 1.0f;
        if (GetKey(Ditto::KeyCode::S) || GetKey(Ditto::KeyCode::DownArrow)) v -= 1.0f;
        return v;
    }
    return 0.0f;
}

float Input::GetAxis(const char* axisName)
{
    if (StrEq(axisName, "Horizontal")) return s_axisHorizontal;
    if (StrEq(axisName, "Vertical"))   return s_axisVertical;
    return GetAxisRaw(axisName);
}

// Map a named button to its (keyKind, code). keyKind 0 = keyboard, 1 = mouse.
static bool ResolveButton(const char* name, int& kind, int& code)
{
    struct Binding { const char* name; int kind; int code; };
    static const Binding bindings[] = {
        { "Jump",   0, Ditto::KeyCode::Space },
        { "Submit", 0, Ditto::KeyCode::Enter },
        { "Cancel", 0, Ditto::KeyCode::Escape },
        { "Fire1",  1, 0 },   // left mouse
        { "Fire2",  1, 1 },   // right mouse
        { "Fire3",  1, 2 },   // middle mouse
    };
    for (const Binding& b : bindings)
        if (StrEq(name, b.name)) { kind = b.kind; code = b.code; return true; }
    return false;
}

bool Input::GetButton(const char* buttonName)
{
    int kind, code;
    if (!ResolveButton(buttonName, kind, code)) return false;
    return kind == 0 ? GetKey(code) : GetMouseButton(code);
}

bool Input::GetButtonDown(const char* buttonName)
{
    int kind, code;
    if (!ResolveButton(buttonName, kind, code)) return false;
    return kind == 0 ? GetKeyDown(code) : GetMouseButtonDown(code);
}

bool Input::GetButtonUp(const char* buttonName)
{
    int kind, code;
    if (!ResolveButton(buttonName, kind, code)) return false;
    return kind == 0 ? GetKeyUp(code) : GetMouseButtonUp(code);
}
