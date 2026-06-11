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
enum HandleAxis { None, X, Y, Z, XY, XZ, YZ };

struct ImRect2D { float MinX, MinY, MaxX, MaxY; };
struct ImVec2D { float x, y; ImVec2D() : x(0), y(0) {} ImVec2D(float _x, float _y) : x(_x), y(_y) {} };

struct SceneWindow
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

    // Rotation drag state. We track the grabbed point's parametric position on
    // the ring and advance it by projecting each frame's mouse motion onto the
    // ring's *screen-space tangent* (the Unity/Unreal approach). This stays
    // responsive from any view -- including edge-on rings, where ray->plane
    // intersection degenerates -- and the direction sign comes from the
    // projection itself, so no axis ever rotates backwards.
    float m_rotateRingAngle = 0.0f;   // parametric angle of grab point on ring (radians)
    float m_rotateTotalAngle = 0.0f;  // accumulated rotation applied (radians)
    ImVec2D m_rotateLastMouse;        // mouse pos last frame, for incremental delta

    ImRect2D GetCurrentViewportRect();
    void DrawGizmos();
    void DrawPhysicsMeshGizmos();
    void DrawColliderMeshGizmo(ColliderComponent* collider, const glm::mat4& worldMat);
    void DrawBoxColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    void DrawSphereColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    void DrawConvexMeshColliderGizmo(const glm::mat4& worldMat, MeshData* mesh);
    MeshData* GetColliderMesh(ColliderComponent* collider);
    void DrawTranslateGizmo(const glm::vec3& worldPos, float scale);
    void DrawRotateGizmo(const glm::vec3& worldPos, float scale);
    void DrawScaleGizmo(const glm::vec3& worldPos, float scale);
    void DrawAxisGizmo();
    void HandleMouseInput();
    void HandleCameraMovement();
    void HandleCameraRotation();
    void HandleObjectSelection();
    HandleAxis RaycastGizmos(const ImVec2D& mousePos);
    ColliderComponent* GetSelectedCollider() const;
    glm::vec3 GetGizmoWorldPosition() const;
    float DistToRotateRing(const ImVec2D& mousePos, const glm::vec3& worldPos, int axis, float ringRadius);
    ImVec2D WorldToScreen(const glm::vec3& worldPos);
    void DrawWorldLine(const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness);

    std::unordered_map<std::string, std::unique_ptr<MeshData>> m_physicsMeshCache;

    // Right-handed in-plane basis (u, v) for a rotation axis, so (u, v, axis)
    // is right-handed and +parametric-angle is a CCW turn about +axis.
    void RingBasis(const glm::vec3& axis, glm::vec3& outU, glm::vec3& outV);
    // Parametric angle on the ring whose screen projection is closest to the
    // cursor (where the user grabbed). Robust even when the ring is edge-on.
    float ClosestRingAngle(const ImVec2D& mousePos, const glm::vec3& center,
                           const glm::vec3& axis, float radius);
};
