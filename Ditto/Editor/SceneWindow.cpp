#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SceneWindow.h"
#include "../Engine/Core/Logger.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/PathUtils.h"
#include "../Engine/Graphics/Camera.h"

#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../3rdParty/GLM/gtc/type_ptr.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "../3rdParty/GLM/gtx/euler_angles.hpp"
#include "../3rdParty/GLFW/glfw3.h"
#include "../3rdParty/ImGui/imgui.h"
#include "../3rdParty/ImGuizmo/ImGuizmo.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <cfloat>

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
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImVec2D(0, 0);
    ImVec2 viewportMin = window->InnerRect.Min;
    ImVec2 viewportSize(
        window->InnerRect.Max.x - window->InnerRect.Min.x,
        window->InnerRect.Max.y - window->InnerRect.Min.y);
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        return ImVec2D(0, 0);

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (!camera) return ImVec2D(0, 0);

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix(viewportSize.x / viewportSize.y);

    glm::vec4 clipPos = proj * view * glm::vec4(worldPos, 1.0f);
    if (clipPos.w <= 0) return ImVec2D(-1000, -1000);

    glm::vec3 ndcPos(clipPos.x / clipPos.w, clipPos.y / clipPos.w, clipPos.z / clipPos.w);

    float screenX = (ndcPos.x + 1.0f) * 0.5f * viewportSize.x + viewportMin.x;
    float screenY = (1.0f - ndcPos.y) * 0.5f * viewportSize.y + viewportMin.y;

    return ImVec2D(screenX, screenY);
}

void SceneWindow::DrawWorldLine(const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness)
{
    ImVec2D pa = WorldToScreen(a);
    ImVec2D pb = WorldToScreen(b);
    if (pa.x < -500 || pb.x < -500) return;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pa.x, pa.y), ImVec2(pb.x, pb.y), color, thickness);
}

MeshData* SceneWindow::GetColliderMesh(ColliderComponent* collider)
{
    if (!collider || !m_editor || !m_editor->engine || !m_editor->engine->resource) return nullptr;

    Resource* resource = m_editor->engine->resource.get();
    switch (collider->type)
    {
    case ColliderComponent::Box:
        return resource->cubeMesh.get();
    case ColliderComponent::Sphere:
        return resource->sphereMesh.get();
    case ColliderComponent::MeshConvex:
    {
        std::string meshPath;
        if (RendererComponent* renderer = collider->gameObject ? collider->gameObject->GetComponent<RendererComponent>() : nullptr)
            meshPath = renderer->meshPath;
        if (meshPath.empty())
            meshPath = collider->meshPath;
        if (meshPath.empty()) return resource->cubeMesh.get();

        std::filesystem::path resolved = meshPath;
        if (!std::filesystem::exists(resolved))
            resolved = PathUtils::ResolveAsset(meshPath);
        if (resolved.extension() != ".obj") return resource->cubeMesh.get();

        std::string key = resolved.string();
        auto it = m_physicsMeshCache.find(key);
        if (it == m_physicsMeshCache.end())
            it = m_physicsMeshCache.emplace(key, std::make_unique<MeshData>(key, false)).first;
        return it->second.get();
    }
    }
    return nullptr;
}

void SceneWindow::DrawColliderMeshGizmo(ColliderComponent* collider, const glm::mat4& worldMat)
{
    MeshData* mesh = GetColliderMesh(collider);
    if (!mesh || mesh->vertices.empty()) return;

    glm::mat4 colliderWorld = worldMat * collider->GetBiasMatrix();

    switch (collider->type)
    {
    case ColliderComponent::Box:
        DrawBoxColliderGizmo(colliderWorld, mesh);
        break;
    case ColliderComponent::Sphere:
        DrawSphereColliderGizmo(colliderWorld, mesh);
        break;
    case ColliderComponent::MeshConvex:
        DrawConvexMeshColliderGizmo(colliderWorld, mesh);
        break;
    }
}

void SceneWindow::DrawBoxColliderGizmo(const glm::mat4& worldMat, MeshData* mesh)
{
    const ImU32 green = IM_COL32(80, 255, 120, 220);
    const float thickness = 1.0f;
    glm::vec3 mn = mesh ? mesh->aabbMin : glm::vec3(-0.5f);
    glm::vec3 mx = mesh ? mesh->aabbMax : glm::vec3(0.5f);

    glm::vec3 p[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
    };
    for (glm::vec3& v : p) v = glm::vec3(worldMat * glm::vec4(v, 1.0f));

    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
    };
    for (const auto& edge : edges)
        DrawWorldLine(p[edge[0]], p[edge[1]], green, thickness);
}

void SceneWindow::DrawSphereColliderGizmo(const glm::mat4& worldMat, MeshData* mesh)
{
    const ImU32 green = IM_COL32(80, 255, 120, 220);
    const float thickness = 1.0f;
    glm::vec3 mn = mesh ? mesh->aabbMin : glm::vec3(-0.5f);
    glm::vec3 mx = mesh ? mesh->aabbMax : glm::vec3(0.5f);
    glm::vec3 diameter = mx - mn;
    float radius = std::max(diameter.x, std::max(diameter.y, diameter.z)) * 0.5f;
    glm::vec3 center = (mn + mx) * 0.5f;

    auto drawCircle = [&](int plane)
    {
        const int segments = 64;
        glm::vec3 prev;
        for (int i = 0; i <= segments; ++i)
        {
            float a = (float)i / (float)segments * 6.2831853f;
            glm::vec3 p = center;
            if (plane == 0) p += glm::vec3(0.0f, cosf(a) * radius, sinf(a) * radius);
            if (plane == 1) p += glm::vec3(cosf(a) * radius, 0.0f, sinf(a) * radius);
            if (plane == 2) p += glm::vec3(cosf(a) * radius, sinf(a) * radius, 0.0f);
            p = glm::vec3(worldMat * glm::vec4(p, 1.0f));
            if (i > 0) DrawWorldLine(prev, p, green, thickness);
            prev = p;
        }
    };
    drawCircle(0);
    drawCircle(1);
    drawCircle(2);
}

void SceneWindow::DrawConvexMeshColliderGizmo(const glm::mat4& worldMat, MeshData* mesh)
{
    const ImU32 green = IM_COL32(80, 255, 120, 220);
    const float thickness = 1.0f;
    if (!mesh || mesh->vertices.empty()) return;

    auto support = [&](const glm::vec3& dir) {
        float bestDot = -FLT_MAX;
        glm::vec3 best(0.0f);
        for (const glm::vec3& v : mesh->vertices)
        {
            float d = glm::dot(v, dir);
            if (d > bestDot) { bestDot = d; best = v; }
        }
        return glm::vec3(worldMat * glm::vec4(best, 1.0f));
    };

    auto drawSupportLoop = [&](const glm::vec3& u, const glm::vec3& v)
    {
        const int segments = 48;
        glm::vec3 prev;
        for (int i = 0; i <= segments; ++i)
        {
            float a = (float)i / (float)segments * 6.2831853f;
            glm::vec3 p = support(glm::normalize(cosf(a) * u + sinf(a) * v));
            if (i > 0) DrawWorldLine(prev, p, green, thickness);
            prev = p;
        }
    };
    drawSupportLoop(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    drawSupportLoop(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
    drawSupportLoop(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
}

void SceneWindow::DrawPhysicsMeshGizmos()
{
    if (!m_editor || !m_editor->activeSelection) return;

    GameObject* selectedObject = m_editor->activeSelection;
    TransformComponent* transform = selectedObject->GetComponent<TransformComponent>();
    if (!transform) return;

    glm::mat4 worldMat = transform->GetWorldModel();
    if (auto* selectedCollider = dynamic_cast<ColliderComponent*>(m_editor->selectedComponent))
    {
        if (selectedCollider->gameObject == selectedObject && selectedCollider->enabled)
            DrawColliderMeshGizmo(selectedCollider, worldMat);
        return;
    }

    for (ColliderComponent* collider : selectedObject->GetComponents<ColliderComponent>())
        if (collider && collider->enabled)
            DrawColliderMeshGizmo(collider, worldMat);

    if ((selectedObject->compMask & ComponentIndex::Collider) == 0)
    {
        RendererComponent* renderer = selectedObject->GetComponent<RendererComponent>();
        RigidbodyComponent* rigidbody = selectedObject->GetComponent<RigidbodyComponent>();
        if (renderer && renderer->enabled && rigidbody && m_editor->engine->resource)
        {
            ColliderComponent implicitCollider(
                renderer->type == RendererComponent::Sphere ? ColliderComponent::Sphere : ColliderComponent::Box);
            implicitCollider.gameObject = selectedObject;
            if (renderer->type == RendererComponent::Quad)
                implicitCollider.biasScale.z = 0.01f;
            DrawColliderMeshGizmo(&implicitCollider, worldMat);
        }
    }
}

void SceneWindow::HandleCameraMovement()
{
    if (!m_editor->isSceneActive)
        return;

    Camera* camera = m_editor->engine->sceneCamera.get();
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

// ImGuizmo translate/rotate/scale manipulator for the selected object,
// replacing the old hand-rolled gizmos. Edit mode only.
void SceneWindow::DrawImGuizmo(const ImVec2& viewMin, const ImVec2& viewMax)
{
    m_selectedObject = m_editor->activeSelection;
    m_isDragging = false;

    if (!m_selectedObject || !m_editor->engine || m_editor->engine->state != Engine::Edit)
    {
        m_gizmoWasUsing = false;
        return;
    }
    TransformComponent* transform = m_selectedObject->GetComponent<TransformComponent>();
    if (!transform) { m_gizmoWasUsing = false; return; }

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (!camera) return;

    const float w = viewMax.x - viewMin.x;
    const float h = viewMax.y - viewMin.y;
    if (w <= 0.0f || h <= 0.0f) return;

    // MUST match RenderSceneToTexture's projection exactly or the gizmo drifts.
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix(w / h);

    ImGuizmo::SetOrthographic(camera->projectionType == Camera::ProjectionType::Orthographic);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewMin.x, viewMin.y, w, h);
    ImGuizmo::SetGizmoSizeClipSpace(0.14f);

    ImGuizmo::OPERATION op =
        m_toolMode == ToolMode::Rotate ? ImGuizmo::ROTATE :
        m_toolMode == ToolMode::Scale ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;

    glm::mat4 model = transform->GetWorldModel();

    bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
        op, ImGuizmo::LOCAL, glm::value_ptr(model));

    // One undo snapshot per drag (rising edge of IsUsing).
    bool usingNow = ImGuizmo::IsUsing();
    if (usingNow && !m_gizmoWasUsing)
        m_editor->PushUndoSnapshot();
    if (m_gizmoWasUsing && !usingNow)
        m_justFinishedDrag = true;   // suppress click-selection this frame
    m_gizmoWasUsing = usingNow;
    m_isDragging = usingNow;

    if (manipulated)
    {
        // Manipulate edits the WORLD matrix; convert back to local space for
        // parented objects before decomposing.
        glm::mat4 local = model;
        if (m_selectedObject->parent)
        {
            if (TransformComponent* pt = m_selectedObject->parent->GetComponent<TransformComponent>())
                local = glm::inverse(pt->GetWorldModel()) * model;
        }

        glm::vec3 t(local[3]);
        glm::vec3 s(glm::length(glm::vec3(local[0])),
                    glm::length(glm::vec3(local[1])),
                    glm::length(glm::vec3(local[2])));

        // Normalize the rotation part, then extract euler in the engine's
        // Y*X*Z order (matches TransformComponent::UpdateTransform).
        glm::mat4 rotMat(1.0f);
        if (s.x > 1e-6f) rotMat[0] = glm::vec4(glm::vec3(local[0]) / s.x, 0.0f);
        if (s.y > 1e-6f) rotMat[1] = glm::vec4(glm::vec3(local[1]) / s.y, 0.0f);
        if (s.z > 1e-6f) rotMat[2] = glm::vec4(glm::vec3(local[2]) / s.z, 0.0f);
        float ry = 0.0f, rx = 0.0f, rz = 0.0f;
        glm::extractEulerAngleYXZ(rotMat, ry, rx, rz);

        transform->position = t;
        transform->rotation = glm::degrees(glm::vec3(rx, ry, rz));
        transform->scale = s;
        transform->useQuatRotation = false;
        transform->localDirty = true;
        transform->UpdateTransform();

        if (m_editor->engine->scene) m_editor->engine->scene->MarkDirty();
    }
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

    // Scene renders into an offscreen target, displayed as an ImGui image
    // (flipped V: both backends produce GL bottom-up memory order).
    void* sceneTex = m_editor->engine->RenderSceneToTexture(
        (int)(max.x - min.x), (int)(max.y - min.y), false);
    if (sceneTex)
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)sceneTex, min, max, ImVec2(0, 1), ImVec2(1, 0));

    DrawPhysicsMeshGizmos();
    DrawImGuizmo(min, max);
    HandleCameraRotation();
    DrawAxisGizmo();

    // Only handle object selection when not dragging the gizmo, not hovering
    // it, not rotating the camera, and not just finished a drag.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !m_isRotatingCamera &&
        !m_isDragging && !m_justFinishedDrag && !ImGuizmo::IsOver())
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
    float gizmoSize = 110.0f;
    float padding = 20.0f;
    ImVec2 center(windowPos.x + windowSize.x - gizmoSize * 0.5f - padding, windowPos.y + gizmoSize * 0.5f + padding);
    
    Camera* camera = m_editor->engine->sceneCamera.get();
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
    float lineThickness = 4.0f;
    
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
        drawList->AddCircleFilled(endPoint, 8.0f, axis.color, 16);
        
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
    
    Camera* camera = m_editor->engine->sceneCamera.get();
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
        
        camera->LookAt(camera->position + newForward);
        
        m_lastMousePos = currentMousePos;
    }
}

void SceneWindow::HandleObjectSelection()
{
    if (!m_editor->engine->scene)
        return;

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (!camera)
        return;

    // Get mouse position relative to window
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    // Calculate mouse position in viewport coordinates
    glm::vec2 mouseViewportPos(mousePos.x - windowPos.x, mousePos.y - windowPos.y);

    // Perform raycast
    GameObject* hitObject = m_editor->engine->scene->RaycastGameObjects(
        mouseViewportPos, *camera, (int)windowSize.x, (int)windowSize.y);

    if (hitObject)
    {
        // Select the object
        m_editor->activeSelection = hitObject;
        m_editor->selectedObject = hitObject;
        m_editor->selectedComponent = nullptr;
        m_editor->selectedFile.Clear();
        DITTO_LOG_INFO_STREAM("[SceneWindow] Selected object: " << hitObject->name );
    }
    else
    {
        // Deselect if clicked on empty space
        m_editor->activeSelection = nullptr;
        m_editor->selectedObject = nullptr;
        m_editor->selectedComponent = nullptr;
        DITTO_LOG_INFO_STREAM("[SceneWindow] Deselected object" );
    }
}

