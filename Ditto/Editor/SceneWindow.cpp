#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SceneWindow.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Graphics/Camera.h"

#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../3rdParty/GLFW/glfw3.h"
#include "../3rdParty/ImGui/imgui.h"
#include <iostream>
#include <cmath>

SceneWindow::SceneWindow(Editor* editor) : m_editor(editor)
{
}

SceneWindow::~SceneWindow()
{
}

ImRect2D SceneWindow::GetCurrentViewportRect()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImRect2D(0, 0, 0, 0);
    ImVec2 min = window->InnerRect.Min;
    ImVec2 max = window->InnerRect.Max;
    return ImRect2D(min.x, min.y, max.x, max.y);
}

ImVec2D SceneWindow::WorldToScreen(const glm::vec3& worldPos)
{
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    Camera* camera = m_editor->engine->sceneCamera;
    if (!camera) return ImVec2D(0, 0);

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), windowSize.x / windowSize.y, 0.1f, 100.0f);

    glm::vec4 clipPos = proj * view * glm::vec4(worldPos, 1.0f);
    if (clipPos.w <= 0) return ImVec2D(-1000, -1000);

    glm::vec3 ndcPos(clipPos.x / clipPos.w, clipPos.y / clipPos.w, clipPos.z / clipPos.w);

    float screenX = (ndcPos.x + 1.0f) * 0.5f * windowSize.x + windowPos.x;
    float screenY = (1.0f - ndcPos.y) * 0.5f * windowSize.y + windowPos.y;

    return ImVec2D(screenX, screenY);
}

float SceneWindow::DistToRotateRing(const ImVec2D& mousePos, const glm::vec3& worldPos, int axis, float ringRadius)
{
    float minDist = 10000.0f;
    int samples = 32;
    
    for (int i = 0; i < samples; i++)
    {
        float angle = (float)i / (float)samples * 2.0f * 3.14159265f;
        float ca = cosf(angle);
        float sa = sinf(angle);
        
        glm::vec3 ringPoint;
        if (axis == 0)
            ringPoint = worldPos + glm::vec3(0, ca * ringRadius, sa * ringRadius);
        else if (axis == 1)
            ringPoint = worldPos + glm::vec3(sa * ringRadius, 0, ca * ringRadius);
        else
            ringPoint = worldPos + glm::vec3(ca * ringRadius, sa * ringRadius, 0);
        
        ImVec2D screenPoint = WorldToScreen(ringPoint);
        float dx = mousePos.x - screenPoint.x;
        float dy = mousePos.y - screenPoint.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < minDist)
            minDist = dist;
    }
    
    return minDist;
}

HandleAxis SceneWindow::RaycastGizmos(const ImVec2D& mousePos)
{
    if (!m_selectedObject)
        return HandleAxis::None;

    TransformComponent* transform = m_selectedObject->GetComponent<TransformComponent>();
    if (!transform)
        return HandleAxis::None;

    ImVec2D screenCenter = WorldToScreen(transform->position);
    if (screenCenter.x < -500) return HandleAxis::None;

    const float threshold = 8.0f;

    if (m_toolMode == ToolMode::Rotate)
    {
        float ringRadius = 1.0f;
        
        float distX = DistToRotateRing(mousePos, transform->position, 0, ringRadius);
        float distY = DistToRotateRing(mousePos, transform->position, 1, ringRadius);
        float distZ = DistToRotateRing(mousePos, transform->position, 2, ringRadius);
        
        if (distX < threshold && distX <= distY && distX <= distZ) return HandleAxis::X;
        if (distY < threshold && distY <= distX && distY <= distZ) return HandleAxis::Y;
        if (distZ < threshold && distZ <= distX && distZ <= distY) return HandleAxis::Z;
    }
    else
    {
        float axisLength = 1.5f;
        ImVec2D xEnd = WorldToScreen(transform->position + glm::vec3(axisLength, 0, 0));
        ImVec2D yEnd = WorldToScreen(transform->position + glm::vec3(0, axisLength, 0));
        ImVec2D zEnd = WorldToScreen(transform->position + glm::vec3(0, 0, axisLength));

        auto distToLine = [](ImVec2D p, ImVec2D a, ImVec2D b) -> float {
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1.0f) return 10000.0f;
            float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / (len * len);
            t = std::max(0.0f, std::min(1.0f, t));
            float projX = a.x + t * dx;
            float projY = a.y + t * dy;
            return std::sqrt((p.x - projX) * (p.x - projX) + (p.y - projY) * (p.y - projY));
        };

        float distX = distToLine(mousePos, screenCenter, xEnd);
        float distY = distToLine(mousePos, screenCenter, yEnd);
        float distZ = distToLine(mousePos, screenCenter, zEnd);

        if (distX < threshold && distX <= distY && distX <= distZ) return HandleAxis::X;
        if (distY < threshold && distY <= distX && distY <= distZ) return HandleAxis::Y;
        if (distZ < threshold && distZ <= distX && distZ <= distY) return HandleAxis::Z;
    }

    return HandleAxis::None;
}

void SceneWindow::DrawTranslateGizmo(const glm::vec3& worldPos, float scale)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImU32 xColor = IM_COL32(255, 50, 50, 255);
    ImU32 yColor = IM_COL32(50, 255, 50, 255);
    ImU32 zColor = IM_COL32(50, 50, 255, 255);

    ImVec2D center = WorldToScreen(worldPos);
    if (center.x < -500) return;

    float axisLength = 1.5f;
    ImVec2D xEnd = WorldToScreen(worldPos + glm::vec3(axisLength, 0, 0));
    ImVec2D yEnd = WorldToScreen(worldPos + glm::vec3(0, axisLength, 0));
    ImVec2D zEnd = WorldToScreen(worldPos + glm::vec3(0, 0, axisLength));

    ImU32 xCol = (m_highlightedAxis == HandleAxis::X) ? IM_COL32(255, 200, 0, 255) : xColor;
    ImU32 yCol = (m_highlightedAxis == HandleAxis::Y) ? IM_COL32(255, 200, 0, 255) : yColor;
    ImU32 zCol = (m_highlightedAxis == HandleAxis::Z) ? IM_COL32(255, 200, 0, 255) : zColor;

    drawList->AddLine(ImVec2(center.x, center.y), ImVec2(xEnd.x, xEnd.y), xCol, 3.0f);
    drawList->AddLine(ImVec2(center.x, center.y), ImVec2(yEnd.x, yEnd.y), yCol, 3.0f);
    drawList->AddLine(ImVec2(center.x, center.y), ImVec2(zEnd.x, zEnd.y), zCol, 3.0f);

    float arrowSize = 12.0f;
    ImVec2 dirX(xEnd.x - center.x, xEnd.y - center.y);
    float lenX = std::sqrt(dirX.x * dirX.x + dirX.y * dirX.y);
    if (lenX > 0) {
        dirX.x /= lenX; dirX.y /= lenX;
        ImVec2 perpX(-dirX.y, dirX.x);
        drawList->AddTriangleFilled(
            ImVec2(xEnd.x, xEnd.y),
            ImVec2(xEnd.x - dirX.x * arrowSize + perpX.x * arrowSize * 0.4f, xEnd.y - dirX.y * arrowSize + perpX.y * arrowSize * 0.4f),
            ImVec2(xEnd.x - dirX.x * arrowSize - perpX.x * arrowSize * 0.4f, xEnd.y - dirX.y * arrowSize - perpX.y * arrowSize * 0.4f),
            xCol);
    }

    ImVec2 dirY(yEnd.x - center.x, yEnd.y - center.y);
    float lenY = std::sqrt(dirY.x * dirY.x + dirY.y * dirY.y);
    if (lenY > 0) {
        dirY.x /= lenY; dirY.y /= lenY;
        ImVec2 perpY(-dirY.y, dirY.x);
        drawList->AddTriangleFilled(
            ImVec2(yEnd.x, yEnd.y),
            ImVec2(yEnd.x - dirY.x * arrowSize + perpY.x * arrowSize * 0.4f, yEnd.y - dirY.y * arrowSize + perpY.y * arrowSize * 0.4f),
            ImVec2(yEnd.x - dirY.x * arrowSize - perpY.x * arrowSize * 0.4f, yEnd.y - dirY.y * arrowSize - perpY.y * arrowSize * 0.4f),
            yCol);
    }

    ImVec2 dirZ(zEnd.x - center.x, zEnd.y - center.y);
    float lenZ = std::sqrt(dirZ.x * dirZ.x + dirZ.y * dirZ.y);
    if (lenZ > 0) {
        dirZ.x /= lenZ; dirZ.y /= lenZ;
        ImVec2 perpZ(-dirZ.y, dirZ.x);
        drawList->AddTriangleFilled(
            ImVec2(zEnd.x, zEnd.y),
            ImVec2(zEnd.x - dirZ.x * arrowSize + perpZ.x * arrowSize * 0.4f, zEnd.y - dirZ.y * arrowSize + perpZ.y * arrowSize * 0.4f),
            ImVec2(zEnd.x - dirZ.x * arrowSize - perpZ.x * arrowSize * 0.4f, zEnd.y - dirZ.y * arrowSize - perpZ.y * arrowSize * 0.4f),
            zCol);
    }
}

void SceneWindow::DrawRotateGizmo(const glm::vec3& worldPos, float scale)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImU32 xColor = IM_COL32(255, 50, 50, 255);
    ImU32 yColor = IM_COL32(50, 255, 50, 255);
    ImU32 zColor = IM_COL32(50, 50, 255, 255);

    ImVec2D center = WorldToScreen(worldPos);
    if (center.x < -500) return;

    float radius = 1.0f;
    int segments = 64;

    ImU32 xCol = (m_highlightedAxis == HandleAxis::X) ? IM_COL32(255, 200, 0, 255) : xColor;
    ImU32 yCol = (m_highlightedAxis == HandleAxis::Y) ? IM_COL32(255, 200, 0, 255) : yColor;
    ImU32 zCol = (m_highlightedAxis == HandleAxis::Z) ? IM_COL32(255, 200, 0, 255) : zColor;

    Camera* cam = m_editor->engine->sceneCamera;
    glm::vec3 camDir = cam ? glm::normalize(worldPos - cam->position) : glm::vec3(0, 0, -1);

    // X axis ring (YZ plane) - red, axis=(1,0,0)
    {
        ImVec2 prevPoint;
        bool firstPoint = true;
        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
            float ca = cosf(angle);
            float sa = sinf(angle);
            // YZ plane: X=0, Y=cos, Z=sin
            glm::vec3 ringPoint = worldPos + glm::vec3(0, ca * radius, sa * radius);
            glm::vec3 pointNormal = glm::vec3(0, ca, sa);
            float d = glm::dot(pointNormal, camDir);
            // Draw back-facing half (d < 0)
            if (d < 0)
            {
                ImVec2D screenPoint = WorldToScreen(ringPoint);
                if (!firstPoint)
                    drawList->AddLine(prevPoint, ImVec2(screenPoint.x, screenPoint.y), xCol, 2.5f);
                prevPoint = ImVec2(screenPoint.x, screenPoint.y);
                firstPoint = false;
            }
            else
            {
                firstPoint = true;
            }
        }
    }

    // Y axis ring (XZ plane) - green, axis=(0,1,0)
    {
        ImVec2 prevPoint;
        bool firstPoint = true;
        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
            float ca = cosf(angle);
            float sa = sinf(angle);
            // XZ plane: X=cos, Y=0, Z=sin
            glm::vec3 ringPoint = worldPos + glm::vec3(ca * radius, 0, sa * radius);
            glm::vec3 pointNormal = glm::vec3(ca, 0, sa);
            float d = glm::dot(pointNormal, camDir);
            // Draw back-facing half (d < 0)
            if (d < 0)
            {
                ImVec2D screenPoint = WorldToScreen(ringPoint);
                if (!firstPoint)
                    drawList->AddLine(prevPoint, ImVec2(screenPoint.x, screenPoint.y), yCol, 2.5f);
                prevPoint = ImVec2(screenPoint.x, screenPoint.y);
                firstPoint = false;
            }
            else
            {
                firstPoint = true;
            }
        }
    }
    
    // Z axis ring (XY plane) - blue, axis=(0,0,1)
    {
        ImVec2 prevPoint;
        bool firstPoint = true;
        for (int i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
            float ca = cosf(angle);
            float sa = sinf(angle);
            // XY plane: X=cos, Y=sin, Z=0
            glm::vec3 ringPoint = worldPos + glm::vec3(ca * radius, sa * radius, 0);
            glm::vec3 pointNormal = glm::vec3(ca, sa, 0);
            float d = glm::dot(pointNormal, camDir);
            // Draw back-facing half (d < 0)
            if (d < 0)
            {
                ImVec2D screenPoint = WorldToScreen(ringPoint);
                if (!firstPoint)
                    drawList->AddLine(prevPoint, ImVec2(screenPoint.x, screenPoint.y), zCol, 2.5f);
                prevPoint = ImVec2(screenPoint.x, screenPoint.y);
                firstPoint = false;
            }
            else
            {
                firstPoint = true;
            }
        }
    }
}

void SceneWindow::DrawScaleGizmo(const glm::vec3& worldPos, float scale)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImU32 xColor = IM_COL32(255, 50, 50, 255);
    ImU32 yColor = IM_COL32(50, 255, 50, 255);
    ImU32 zColor = IM_COL32(50, 50, 255, 255);

    ImVec2D center = WorldToScreen(worldPos);
    if (center.x < -500) return;

    float axisLength = 1.5f;
    ImVec2D xEnd = WorldToScreen(worldPos + glm::vec3(axisLength, 0, 0));
    ImVec2D yEnd = WorldToScreen(worldPos + glm::vec3(0, axisLength, 0));
    ImVec2D zEnd = WorldToScreen(worldPos + glm::vec3(0, 0, axisLength));

    ImU32 xCol = (m_highlightedAxis == HandleAxis::X) ? IM_COL32(255, 200, 0, 255) : xColor;
    ImU32 yCol = (m_highlightedAxis == HandleAxis::Y) ? IM_COL32(255, 200, 0, 255) : yColor;
    ImU32 zCol = (m_highlightedAxis == HandleAxis::Z) ? IM_COL32(255, 200, 0, 255) : zColor;

    ImVec2 centerImGui(center.x, center.y);
    ImVec2 xEndImGui(xEnd.x, xEnd.y);
    ImVec2 yEndImGui(yEnd.x, yEnd.y);
    ImVec2 zEndImGui(zEnd.x, zEnd.y);

    drawList->AddLine(centerImGui, xEndImGui, xCol, 3.0f);
    drawList->AddLine(centerImGui, yEndImGui, yCol, 3.0f);
    drawList->AddLine(centerImGui, zEndImGui, zCol, 3.0f);

    float boxSize = 8.0f;
    drawList->AddRectFilled(ImVec2(xEnd.x - boxSize/2, xEnd.y - boxSize/2), ImVec2(xEnd.x + boxSize/2, xEnd.y + boxSize/2), xCol);
    drawList->AddRectFilled(ImVec2(yEnd.x - boxSize/2, yEnd.y - boxSize/2), ImVec2(yEnd.x + boxSize/2, yEnd.y + boxSize/2), yCol);
    drawList->AddRectFilled(ImVec2(zEnd.x - boxSize/2, zEnd.y - boxSize/2), ImVec2(zEnd.x + boxSize/2, zEnd.y + boxSize/2), zCol);
}

void SceneWindow::DrawGizmos()
{
    m_selectedObject = m_editor->activeSelection;
    if (!m_selectedObject)
        return;

    TransformComponent* transform = m_selectedObject->GetComponent<TransformComponent>();
    if (!transform)
        return;

    ImVec2 mousePosImGui = ImGui::GetMousePos();
    ImVec2D mousePos(mousePosImGui.x, mousePosImGui.y);
    m_highlightedAxis = RaycastGizmos(mousePos);

    float gizmoScale = 2.0f;

    switch (m_toolMode)
    {
    case ToolMode::Translate:
        DrawTranslateGizmo(transform->position, gizmoScale);
        break;
    case ToolMode::Rotate:
        DrawRotateGizmo(transform->position, gizmoScale);
        break;
    case ToolMode::Scale:
        DrawScaleGizmo(transform->position, gizmoScale);
        break;
    }
}

void SceneWindow::HandleMouseInput()
{
    if (!m_selectedObject)
        return;

    TransformComponent* transform = m_selectedObject->GetComponent<TransformComponent>();
    if (!transform)
        return;

    if (ImGui::IsMouseClicked(0) && m_highlightedAxis != HandleAxis::None)
    {
        m_isDragging = true;
        m_draggingAxis = m_highlightedAxis;
        m_dragStartMousePos = ImVec2D(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
        m_originalPosition = transform->position;
        m_originalRotation = transform->rotation;
        m_originalScale = transform->scale;
    }

    if (m_isDragging && ImGui::IsMouseDown(0))
    {
        ImVec2D currentMousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
        
        // Calculate screen-space axis direction for more accurate dragging
        Camera* camera = m_editor->engine->sceneCamera;
        glm::vec3 worldPos = transform->position;
        ImVec2D screenPos = WorldToScreen(worldPos);
        
        // Get screen-space axis directions
        glm::vec3 axisDir(0);
        if (m_draggingAxis == HandleAxis::X) axisDir = glm::vec3(1, 0, 0);
        else if (m_draggingAxis == HandleAxis::Y) axisDir = glm::vec3(0, 1, 0);
        else if (m_draggingAxis == HandleAxis::Z) axisDir = glm::vec3(0, 0, 1);
        
        ImVec2D screenAxisEnd = WorldToScreen(worldPos + axisDir * 0.5f);
        ImVec2D screenAxisDir(screenAxisEnd.x - screenPos.x, screenAxisEnd.y - screenPos.y);
        
        // Normalize screen axis direction
        float axisLen = std::sqrt(screenAxisDir.x * screenAxisDir.x + screenAxisDir.y * screenAxisDir.y);
        if (axisLen > 0.001f)
        {
            screenAxisDir.x /= axisLen;
            screenAxisDir.y /= axisLen;
        }
        else
        {
            // Fallback if axis is perpendicular to view
            screenAxisDir.x = (m_draggingAxis == HandleAxis::X) ? 1 : 0;
            screenAxisDir.y = (m_draggingAxis == HandleAxis::Y) ? -1 : 0;
        }
        
        // Calculate mouse movement projected onto screen axis
        ImVec2D mouseDelta(currentMousePos.x - m_dragStartMousePos.x, currentMousePos.y - m_dragStartMousePos.y);
        float projectedDelta = mouseDelta.x * screenAxisDir.x + mouseDelta.y * screenAxisDir.y;
        
        // Calculate perpendicular movement for Z axis fallback
        float perpDelta = mouseDelta.x;
        
        switch (m_toolMode)
        {
        case ToolMode::Translate:
        {
            float sensitivity = 0.02f;
            glm::vec3 newPos = m_originalPosition;
            if (m_draggingAxis == HandleAxis::X) newPos.x += projectedDelta * sensitivity;
            if (m_draggingAxis == HandleAxis::Y) newPos.y += projectedDelta * sensitivity;
            if (m_draggingAxis == HandleAxis::Z) newPos.z += projectedDelta * sensitivity;
            transform->position = newPos;
            break;
        }
        case ToolMode::Rotate:
        {
            float sensitivity = 1.0f;
            glm::vec3 newRot = m_originalRotation;
            if (m_draggingAxis == HandleAxis::X) newRot.x += projectedDelta * sensitivity;
            if (m_draggingAxis == HandleAxis::Y) newRot.y += projectedDelta * sensitivity;
            if (m_draggingAxis == HandleAxis::Z) newRot.z += projectedDelta * sensitivity;
            transform->rotation = newRot;
            break;
        }
        case ToolMode::Scale:
        {
            float sensitivity = 0.02f;
            glm::vec3 newScale = m_originalScale;
            if (m_draggingAxis == HandleAxis::X) newScale.x = std::max(0.1f, m_originalScale.x + projectedDelta * sensitivity);
            if (m_draggingAxis == HandleAxis::Y) newScale.y = std::max(0.1f, m_originalScale.y + projectedDelta * sensitivity);
            if (m_draggingAxis == HandleAxis::Z) newScale.z = std::max(0.1f, m_originalScale.z + projectedDelta * sensitivity);
            transform->scale = newScale;
            break;
        }
        }
        
        // Mark transform as modified to ensure Inspector updates
        transform->localDirty = true;
        transform->UpdateTransform();
    }

    if (ImGui::IsMouseReleased(0))
    {
        if (m_isDragging)
        {
            m_justFinishedDrag = true;  // Mark that we just finished a drag operation
        }
        m_isDragging = false;
        m_draggingAxis = HandleAxis::None;
    }
}

void SceneWindow::HandleCameraMovement()
{
    if (!m_editor->isSceneActive)
        return;

    Camera* camera = m_editor->engine->sceneCamera;
    if (!camera)
        return;

    float keySpeed = 0.01f;

    if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
        camera->position += camera->forward * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
        camera->position -= camera->forward * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
        camera->position -= camera->right * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
        camera->position += camera->right * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_PageUp))
        camera->position += camera->up * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_PageDown))
        camera->position -= camera->up * keySpeed;
}

void SceneWindow::Draw()
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(0.0f);

    bool windowOpen = ImGui::Begin("Scene", nullptr);
    
    // Set isSceneActive based on window focus/hover
    m_editor->isSceneActive = windowOpen && (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows));
    
    if (!windowOpen)
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // Tool switching shortcuts
    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        m_toolMode = ToolMode::Translate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        m_toolMode = ToolMode::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        m_toolMode = ToolMode::Scale;
    }

    HandleCameraMovement();

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
    ImVec2 min = window->InnerRect.Min;
    ImVec2 max = window->InnerRect.Max;

    ImGui::GetWindowDrawList()->PushClipRect(min, max, true);
    m_editor->engine->RenderSceneToViewport(ImRect(min, max), false);
    ImGui::GetWindowDrawList()->PopClipRect();

    DrawGizmos();
    HandleMouseInput();
    HandleCameraRotation();
    DrawAxisGizmo();

    // Only handle object selection when not dragging Gizmo, not rotating camera, and not just finished drag
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !m_isRotatingCamera && !m_isDragging && !m_justFinishedDrag)
    {
        HandleObjectSelection();
    }
    
    // Reset the flag after this frame
    m_justFinishedDrag = false;

    ImGui::End();
    ImGui::PopStyleColor();
}

void SceneWindow::DrawAxisGizmo()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    
    // Position in top-right corner with some padding
    float gizmoSize = 80.0f;
    float padding = 20.0f;
    ImVec2 center(windowPos.x + windowSize.x - gizmoSize * 0.5f - padding, windowPos.y + gizmoSize * 0.5f + padding);
    
    Camera* camera = m_editor->engine->sceneCamera;
    if (!camera) return;
    
    // Get camera's basis vectors (right, up, forward)
    glm::vec3 camRight = camera->right;
    glm::vec3 camUp = camera->up;
    glm::vec3 camForward = camera->forward;
    
    // World axes
    glm::vec3 worldX(1, 0, 0);
    glm::vec3 worldY(0, 1, 0);
    glm::vec3 worldZ(0, 0, 1);
    
    // Project world axis to camera view space
    auto worldToView = [&](const glm::vec3& worldAxis) -> glm::vec3 {
        return glm::vec3(
            glm::dot(worldAxis, camRight),
            glm::dot(worldAxis, camUp),
            glm::dot(worldAxis, -camForward) // Negative because camera looks down negative Z
        );
    };
    
    glm::vec3 viewX = worldToView(worldX);
    glm::vec3 viewY = worldToView(worldY);
    glm::vec3 viewZ = worldToView(worldZ);
    
    // Axis colors (Unity style)
    ImU32 xColor = IM_COL32(220, 50, 50, 255);
    ImU32 yColor = IM_COL32(50, 220, 50, 255);
    ImU32 zColor = IM_COL32(50, 100, 220, 255);
    
    // Axis length
    float axisLength = gizmoSize * 0.35f;
    float lineThickness = 3.0f;
    
    // Store axis info for proper depth sorting
    struct AxisInfo {
        glm::vec3 viewDir;
        ImU32 color;
        const char* label;
        float zDepth;
    };
    
    AxisInfo axes[3] = {
        { viewX, xColor, "X", viewX.z },
        { viewY, yColor, "Y", viewY.z },
        { viewZ, zColor, "Z", viewZ.z }
    };
    
    // Sort by Z depth (draw back axes first)
    // Simple bubble sort for 3 elements
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2 - i; j++) {
            if (axes[j].zDepth > axes[j + 1].zDepth) {
                AxisInfo temp = axes[j];
                axes[j] = axes[j + 1];
                axes[j + 1] = temp;
            }
        }
    }
    
    // Draw axes from back to front
    for (int i = 0; i < 3; i++) {
        const AxisInfo& axis = axes[i];
        
        // Project to 2D (X right, Y up in screen space)
        ImVec2 endPoint(
            center.x + axis.viewDir.x * axisLength,
            center.y - axis.viewDir.y * axisLength
        );
        
        // Draw line from center to end
        drawList->AddLine(center, endPoint, axis.color, lineThickness);
        
        // Draw label at end of axis
        ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        ImVec2 textPos(
            endPoint.x - textSize.x * 0.5f,
            endPoint.y - textSize.y * 0.5f
        );
        
        // Draw text background for better visibility
        drawList->AddRectFilled(
            ImVec2(textPos.x - 2, textPos.y - 2),
            ImVec2(textPos.x + textSize.x + 2, textPos.y + textSize.y + 2),
            IM_COL32(0, 0, 0, 180)
        );
        drawList->AddText(textPos, axis.color, axis.label);
    }
}

void SceneWindow::HandleCameraRotation()
{
    if (!m_editor->isSceneActive)
        return;
    
    Camera* camera = m_editor->engine->sceneCamera;
    if (!camera)
        return;
    
    ImVec2 mousePos = ImGui::GetMousePos();
    
    // Start right-click drag
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(1))
    {
        m_isRotatingCamera = true;
        m_lastMousePos = ImVec2D(mousePos.x, mousePos.y);
    }
    
    // End right-click drag
    if (ImGui::IsMouseReleased(1))
    {
        m_isRotatingCamera = false;
    }
    
    // Handle rotation while dragging
    if (m_isRotatingCamera && ImGui::IsMouseDown(1))
    {
        ImVec2D currentMousePos(mousePos.x, mousePos.y);
        float deltaX = currentMousePos.x - m_lastMousePos.x;
        float deltaY = currentMousePos.y - m_lastMousePos.y;
        
        float sensitivity = 0.005f;
        
        // Rotate around Y axis (yaw) based on horizontal mouse movement
        float yaw = -deltaX * sensitivity;
        // Rotate around X axis (pitch) based on vertical mouse movement
        float pitch = -deltaY * sensitivity;
        
        // Create rotation quaternions
        glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
        glm::quat pitchRotation = glm::angleAxis(pitch, camera->right);
        
        // Apply rotations to forward vector
        glm::vec3 newForward = glm::normalize(yawRotation * pitchRotation * camera->forward);
        
        // Update camera
        camera->forward = newForward;
        camera->right = glm::normalize(glm::cross(camera->forward, glm::vec3(0, 1, 0)));
        camera->up = glm::normalize(glm::cross(camera->right, camera->forward));
        
        m_lastMousePos = currentMousePos;
    }
}

void SceneWindow::HandleObjectSelection()
{
    if (!m_editor->engine->scene)
        return;

    Camera* camera = m_editor->engine->sceneCamera;
    if (!camera)
        return;

    // Get mouse position relative to window
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    // Calculate mouse position in viewport coordinates
    glm::vec2 mouseViewportPos(mousePos.x - windowPos.x, mousePos.y - windowPos.y);

    // Get view and projection matrices
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), windowSize.x / windowSize.y, 0.1f, 100.0f);

    // Perform raycast
    GameObject* hitObject = m_editor->engine->scene->RaycastGameObjects(
        mouseViewportPos, view, projection, (int)windowSize.x, (int)windowSize.y);

    if (hitObject)
    {
        // Select the object
        m_editor->activeSelection = hitObject;
        m_editor->selectedObject = hitObject;
        m_editor->selectedFile.Clear();
        std::cout << "[SceneWindow] Selected object: " << hitObject->name << std::endl;
    }
    else
    {
        // Deselect if clicked on empty space
        m_editor->activeSelection = nullptr;
        m_editor->selectedObject = nullptr;
        std::cout << "[SceneWindow] Deselected object" << std::endl;
    }
}
