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
        ImVec2D delta(currentMousePos.x - m_dragStartMousePos.x, currentMousePos.y - m_dragStartMousePos.y);

        switch (m_toolMode)
        {
        case ToolMode::Translate:
        {
            float sensitivity = 0.01f;
            glm::vec3 newPos = m_originalPosition;
            if (m_draggingAxis == HandleAxis::X) newPos.x += delta.x * sensitivity;
            if (m_draggingAxis == HandleAxis::Y) newPos.y -= delta.y * sensitivity;
            if (m_draggingAxis == HandleAxis::Z) newPos.z += delta.x * sensitivity;
            transform->position = newPos;
            break;
        }
        case ToolMode::Rotate:
        {
            float sensitivity = 0.5f;
            glm::vec3 newRot = m_originalRotation;
            if (m_draggingAxis == HandleAxis::X) newRot.x += delta.y * sensitivity;
            if (m_draggingAxis == HandleAxis::Y) newRot.y += delta.x * sensitivity;
            if (m_draggingAxis == HandleAxis::Z) newRot.z += delta.x * sensitivity;
            transform->rotation = newRot;
            break;
        }
        case ToolMode::Scale:
        {
            float sensitivity = 0.01f;
            glm::vec3 newScale = m_originalScale;
            if (m_draggingAxis == HandleAxis::X) newScale.x = std::max(0.1f, m_originalScale.x + delta.x * sensitivity);
            if (m_draggingAxis == HandleAxis::Y) newScale.y = std::max(0.1f, m_originalScale.y - delta.y * sensitivity);
            if (m_draggingAxis == HandleAxis::Z) newScale.z = std::max(0.1f, m_originalScale.z + delta.x * sensitivity);
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
    
    // Debug log
    static int frameCount = 0;
    if (++frameCount % 60 == 0)
    {
        std::cout << "[SceneWindow] windowOpen=" << windowOpen 
                  << " IsWindowFocused=" << ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                  << " IsWindowHovered=" << ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
                  << std::endl;
    }
    
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

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
    {
        // TODO: Implement scene object selection
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
