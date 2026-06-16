#pragma once
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../3rdParty/ImGui/imgui.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Resources/Resource.h"
#include <memory>
#include <string>
#include <unordered_map>

struct Editor;

enum ToolMode { Translate, Rotate, Scale };
enum class ViewToolMode { Hand, Move, Rotate, Scale, Transform, Rect };

struct ImRect2D { float MinX, MinY, MaxX, MaxY; };
struct ImVec2D { float x, y; ImVec2D() : x(0), y(0) {} ImVec2D(float _x, float _y) : x(_x), y(_y) {} };

struct SceneWindow
{
public:
    SceneWindow(Editor* editor);
    ~SceneWindow();
    void Draw();
    void FrameObject(GameObject* obj);

private:
    Editor* m_editor;
    GameObject* m_selectedObject = nullptr;
    ToolMode m_toolMode = ToolMode::Translate;
    ViewToolMode m_viewToolMode = ViewToolMode::Move;
    bool m_pivotCenter = true;
    bool m_localSpace = true;
    bool m_gridVisible = true;
    bool m_snapEnabled = false;
    bool m_scene2D = false;
    bool m_lightingEnabled = true;
    bool m_audioEnabled = true;
    bool m_effectsEnabled = true;
    bool m_gizmosEnabled = true;
    bool m_toolbarHovered = false;
    bool m_isDragging = false;        // ImGuizmo manipulation in progress
    bool m_isRotatingCamera = false;
    bool m_justFinishedDrag = false;  // Flag to skip selection after drag ends
    bool m_gizmoWasUsing = false;     // edge detection for one-undo-per-drag
    ImVec2D m_lastMousePos;            // last-frame cursor in viewport (for camera rotation)

    ImRect2D GetCurrentViewportRect();
    // ImGuizmo translate/rotate/scale manipulator (replaces the old
    // hand-rolled gizmos). viewMin/viewMax = the viewport rect.
    void DrawImGuizmo(const ImVec2& viewMin, const ImVec2& viewMax);
    void DrawPhysicsMeshGizmos();
    void DrawUIGizmos(const ImVec2& viewMin, const ImVec2& viewMax);
    void DrawCameraGizmos();
    void DrawColliderMeshGizmo(ColliderComponent* collider, const glm::mat4& worldMat);
    void DrawBoxColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    void DrawSphereColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    void DrawConvexMeshColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    MeshData* GetColliderMesh(ColliderComponent* collider);
    void DrawAxisGizmo();
    void DrawSceneToolbar(const ImVec2& viewMin, const ImVec2& viewMax);
    void Apply2DMode(bool enabled);
    void FrameScene2D();
    void HandleCameraMovement();
    void HandleCameraRotation();
    void HandleObjectSelection();
    ColliderComponent* GetSelectedCollider() const;
    ImVec2D WorldToScreen(const glm::vec3& worldPos);
    void DrawWorldLine(const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness);
    void DrawSceneGrid(const ImVec2& viewMin, const ImVec2& viewMax);
    void DrawSceneObjectIcon(GameObject* obj, void* iconTexture, ImU32 tint, float size, const char* tooltip);
    bool IsSelectedOrAncestor(GameObject* obj) const;

    std::unordered_map<std::string, std::unique_ptr<MeshData>> m_physicsMeshCache;
};
