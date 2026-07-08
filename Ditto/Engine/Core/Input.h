#pragma once
#include "../../3rdParty/GLM/glm.hpp"

namespace Ditto { class IWindow; }









struct Input
{
    static void Init(Ditto::IWindow* window);
    
    static void NewFrame();

    static bool GetKey(int key);
    static bool GetKeyDown(int key);
    static bool GetKeyUp(int key);

    static bool GetMouseButton(int button);       
    static bool GetMouseButtonDown(int button);
    static bool GetMouseButtonUp(int button);

    
    
    
    static float GetAxis(const char* axisName);
    static float GetAxisRaw(const char* axisName);

    
    
    static bool GetButton(const char* buttonName);
    static bool GetButtonDown(const char* buttonName);
    static bool GetButtonUp(const char* buttonName);

    
    
    static glm::vec2 GetMousePosition();
    static glm::vec2 GetRawMousePosition();
    static glm::vec2 GetGameViewportSize();
    static glm::vec4 GetGameViewportRect();
    static bool IsMouseInsideGameViewport();

    
    
    
    static void SetGameViewport(float x, float y, float w, float h, float contentW = 0.0f, float contentH = 0.0f);

private:
    static constexpr int kMinKeyCode = 0;
    static constexpr int kMaxKeyCode = 512;
    static constexpr int kMaxMouseButton = 8;

    static Ditto::IWindow* s_window;
    static bool s_cur[kMaxKeyCode + 1];
    static bool s_prev[kMaxKeyCode + 1];
    static bool s_curMouse[kMaxMouseButton + 1];
    static bool s_prevMouse[kMaxMouseButton + 1];
    static double s_mouseX, s_mouseY;
    static float s_viewX, s_viewY, s_viewW, s_viewH;
    static float s_contentW, s_contentH;

    
    
    static float s_axisHorizontal, s_axisVertical;
};
