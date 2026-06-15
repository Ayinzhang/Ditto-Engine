#pragma once
#define GLFW_INCLUDE_NONE
#include "../../3rdParty/GLFW/glfw3.h"
#include "../../3rdParty/GLM/glm.hpp"

// Static input state exposed to gameplay (C# scripts query this through
// internal calls). Double-buffered key/mouse state polled once per frame from
// GLFW's cached state -- NewFrame() must run AFTER glfwPollEvents() so `cur`
// holds this frame's state and `prev` last frame's (edge detection).
//
// Mouse position is reported relative to the game viewport: in the editor this
// is the "Game" panel rect (set by Editor::DrawGame each frame); in standalone
// game mode it is the full window.
struct Input
{
    static void Init(GLFWwindow* window);
    // Snapshot prev = cur, then poll fresh state. Call after glfwPollEvents().
    static void NewFrame();

    static bool GetKey(int glfwKey);
    static bool GetKeyDown(int glfwKey);
    static bool GetKeyUp(int glfwKey);

    static bool GetMouseButton(int button);       // 0=left 1=right 2=middle
    static bool GetMouseButtonDown(int button);
    static bool GetMouseButtonUp(int button);

    // Cursor position relative to the game viewport origin (pixels). May be
    // outside [0,size) when the cursor is outside the viewport.
    static glm::vec2 GetMousePosition();
    static glm::vec2 GetGameViewportSize();

    // The screen-space rect of the game viewport in window coordinates.
    // Editor: called every frame by Editor::DrawGame with the Game panel rect.
    // Game mode: Engine::Run sets it to the full window each frame.
    static void SetGameViewport(float x, float y, float w, float h, float contentW = 0.0f, float contentH = 0.0f);

private:
    static GLFWwindow* s_window;
    static bool s_cur[GLFW_KEY_LAST + 1];
    static bool s_prev[GLFW_KEY_LAST + 1];
    static bool s_curMouse[GLFW_MOUSE_BUTTON_LAST + 1];
    static bool s_prevMouse[GLFW_MOUSE_BUTTON_LAST + 1];
    static double s_mouseX, s_mouseY;
    static float s_viewX, s_viewY, s_viewW, s_viewH;
    static float s_contentW, s_contentH;
};
