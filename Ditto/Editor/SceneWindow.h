#pragma once
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../Engine/Core/GameObject.h"

struct Editor;

enum class ToolMode { Translate, Rotate, Scale };
enum class HandleAxis { None, X, Y, Z, XY, XZ, YZ };

struct ImRect2D { float MinX, MinY, MaxX, MaxY; };
struct ImVec2D { float x, y; ImVec2D() : x(0), y(0) {} ImVec2D(float _x, float _y) : x(_x), y(_y) {} };

class SceneWindow
{
public:
    SceneWindow(Editor* editor);
    ~SceneWindow();
    void Draw();

private:
    Editor* m_editor;
    GameObject* m_selectedObject = nullptr;
    ToolMode m_toolMode = ToolMode::Translate;
    HandleAxis m_highlightedAxis = HandleAxis::None;
    HandleAxis m_draggingAxis = HandleAxis::None;
    bool m_isDragging = false;
    bool m_isRotatingCamera = false;
    bool m_justFinishedDrag = false;  // Flag to skip selection after drag ends
    ImVec2D m_dragStartMousePos;
    ImVec2D m_lastMousePos;
    glm::vec3 m_originalPosition;
    glm::vec3 m_originalRotation;
    glm::vec3 m_originalScale;

    ImRect2D GetCurrentViewportRect();
    void DrawGizmos();
    void DrawTranslateGizmo(const glm::vec3& worldPos, float scale);
    void DrawRotateGizmo(const glm::vec3& worldPos, float scale);
    void DrawScaleGizmo(const glm::vec3& worldPos, float scale);
    void DrawAxisGizmo();
    void HandleMouseInput();
    void HandleCameraMovement();
    void HandleCameraRotation();
    void HandleObjectSelection();
    HandleAxis RaycastGizmos(const ImVec2D& mousePos);
    float DistToRotateRing(const ImVec2D& mousePos, const glm::vec3& worldPos, int axis, float ringRadius);
    ImVec2D WorldToScreen(const glm::vec3& worldPos);
};
