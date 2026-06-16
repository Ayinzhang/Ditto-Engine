#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SceneWindow.h"
#include "../Engine/Core/Logger.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/Input.h"
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
#include <functional>

SceneWindow::SceneWindow(Editor* editor) : m_editor(editor)
{
}

SceneWindow::~SceneWindow()
{
}

void SceneWindow::FrameObject(GameObject* obj)
{
    if (!obj || !m_editor || !m_editor->engine || !m_editor->engine->sceneCamera) return;
    TransformComponent* transform = obj->GetComponent<TransformComponent>();
    if (!transform) return;

    Camera* camera = m_editor->engine->sceneCamera.get();
    glm::vec3 target = glm::vec3(transform->GetWorldModel()[3]);
    float radius = 2.5f;
    if (RendererComponent* renderer = obj->GetComponent<RendererComponent>())
    {
        if (renderer->type == RendererComponent::Sphere) radius = 2.5f;
        else radius = 2.0f;
    }
    if (SpriteRendererComponent* sprite = obj->GetComponent<SpriteRendererComponent>())
        radius = sprite->drawMode == SpriteRendererComponent::Simple ? 2.0f : glm::max(sprite->size.x, sprite->size.y) * 1.5f;

    if (camera->projectionType == Camera::ProjectionType::Orthographic || m_scene2D)
    {
        camera->projectionType = Camera::ProjectionType::Orthographic;
        camera->orthographicSize = glm::max(1.0f, radius);
        camera->position = glm::vec3(target.x, target.y, 10.0f);
        camera->nearClipPlane = 0.01f;
        camera->farClipPlane = 1000.0f;
        camera->LookAt(target);
    }
    else
    {
        glm::vec3 dir = glm::length(camera->forward) > 0.001f ? camera->forward : glm::vec3(0.0f, -0.6f, -0.8f);
        camera->position = target - glm::normalize(dir) * glm::max(4.0f, radius * 3.0f);
        camera->nearClipPlane = 0.01f;
        camera->farClipPlane = 1000.0f;
        camera->LookAt(target);
    }
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
    if (!m_editor || !m_editor->engine || !m_editor->activeSelection) return;

    if (auto* selectedCollider = dynamic_cast<ColliderComponent*>(m_editor->selectedComponent))
    {
        GameObject* obj = selectedCollider->gameObject;
        TransformComponent* transform = obj ? obj->GetComponent<TransformComponent>() : nullptr;
        if (obj && IsSelectedOrAncestor(obj) && transform && selectedCollider->enabled)
            DrawColliderMeshGizmo(selectedCollider, transform->GetWorldModel());
        return;
    }

    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;
        if (!IsSelectedOrAncestor(obj))
        {
            for (const auto& child : obj->children)
                visit(child.get());
            return;
        }

        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (!transform) return;

        glm::mat4 worldMat = transform->GetWorldModel();
        for (ColliderComponent* collider : obj->GetComponents<ColliderComponent>())
            if (collider && collider->enabled)
                DrawColliderMeshGizmo(collider, worldMat);

        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(m_editor->engine && m_editor->engine->scene ? m_editor->engine->scene->rootGameObject.get() : m_editor->activeSelection);
}

bool SceneWindow::IsSelectedOrAncestor(GameObject* obj) const
{
    if (!m_editor || !obj || !m_editor->activeSelection) return false;

    GameObject* current = obj;
    while (current)
    {
        if (current == m_editor->activeSelection) return true;
        current = current->parent;
    }
    return false;
}

void SceneWindow::DrawSceneObjectIcon(GameObject* obj, void* iconTexture, ImU32 tint, float size, const char* tooltip)
{
    if (!obj || !iconTexture) return;
    TransformComponent* transform = obj->GetComponent<TransformComponent>();
    if (!transform) return;

    ImVec2D screen = WorldToScreen(glm::vec3(transform->GetWorldModel()[3]));
    if (screen.x < -500.0f || screen.y < -500.0f) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 min(screen.x - size * 0.5f, screen.y - size * 0.5f);
    ImVec2 max(screen.x + size * 0.5f, screen.y + size * 0.5f);
    drawList->AddImage((ImTextureID)iconTexture, min, max, ImVec2(0, 1), ImVec2(1, 0), tint);

    if (tooltip && ImGui::IsMouseHoveringRect(min, max))
        ImGui::SetTooltip("%s", tooltip);
}

void SceneWindow::DrawUIGizmos(const ImVec2& viewMin, const ImVec2& viewMax)
{
    if (!m_editor || !m_editor->engine || !m_editor->engine->scene || !m_gizmosEnabled) return;
    if (!m_editor->activeSelection) return;

    glm::vec2 gameSize = Input::GetGameViewportSize();
    const float w = gameSize.x > 0.0f ? gameSize.x : (viewMax.x - viewMin.x);
    const float h = gameSize.y > 0.0f ? gameSize.y : (viewMax.y - viewMin.y);
    if (w <= 0.0f || h <= 0.0f) return;
    const float sceneW = viewMax.x - viewMin.x;
    const float sceneH = viewMax.y - viewMin.y;
    const float scale = glm::min(sceneW / w, sceneH / h);
    const ImVec2 canvasMin(viewMin.x + (sceneW - w * scale) * 0.5f,
        viewMin.y + (sceneH - h * scale) * 0.5f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;
        if (!IsSelectedOrAncestor(obj))
        {
            for (const auto& child : obj->children)
                visit(child.get());
            return;
        }

        RectTransformComponent* rect = obj->GetComponent<RectTransformComponent>();
        bool isUI = rect && (obj->GetComponent<CanvasComponent>() || obj->GetComponent<UIImageComponent>() ||
            obj->GetComponent<UITextComponent>() || obj->GetComponent<UIButtonComponent>());
        if (isUI)
        {
            glm::vec4 r = rect->ComputeRect(w, h);
            ImU32 color = obj->GetComponent<CanvasComponent>()
                ? IM_COL32(255, 255, 255, 135)
                : (obj == m_editor->activeSelection ? IM_COL32(255, 220, 80, 230) : IM_COL32(90, 170, 255, 150));
            ImVec2 a(canvasMin.x + r.x * scale, canvasMin.y + r.y * scale);
            ImVec2 b(canvasMin.x + (r.x + r.z) * scale, canvasMin.y + (r.y + r.w) * scale);
            drawList->AddRect(a, b, color, 0.0f, 0, obj == m_editor->activeSelection ? 2.0f : 1.0f);
            if (obj == m_editor->activeSelection)
            {
                drawList->AddCircleFilled(a, 3.0f, color);
                drawList->AddCircleFilled(ImVec2(b.x, a.y), 3.0f, color);
                drawList->AddCircleFilled(b, 3.0f, color);
                drawList->AddCircleFilled(ImVec2(a.x, b.y), 3.0f, color);
            }
        }
        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(m_editor->engine->scene->rootGameObject.get());
}

void SceneWindow::DrawCameraGizmos()
{
    if (!m_editor || !m_editor->engine || !m_editor->engine->scene || !m_gizmosEnabled) return;
    void* cameraIcon = m_editor->GetCameraIcon();
    void* gameObjectIcon = m_editor->GetGameObjectIcon();

    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;
        CameraComponent* camera = obj->GetComponent<CameraComponent>();
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (transform && !camera && glm::length(glm::vec3(transform->GetWorldModel()[3])) < 0.0001f)
            DrawSceneObjectIcon(obj, gameObjectIcon, IM_COL32(255, 255, 255, IsSelectedOrAncestor(obj) ? 235 : 120), 18.0f, obj->name.c_str());

        if (camera && camera->enabled && transform)
        {
            DrawSceneObjectIcon(obj, cameraIcon, IM_COL32(255, 255, 255, IsSelectedOrAncestor(obj) ? 255 : 150), 22.0f, obj->name.c_str());

            if (!IsSelectedOrAncestor(obj))
            {
                for (const auto& child : obj->children)
                    visit(child.get());
                return;
            }

            Camera c = camera->ToCamera(transform);
            const glm::vec3 origin = c.position;
            const glm::vec3 f = glm::normalize(c.forward);
            const glm::vec3 r = glm::normalize(c.right);
            const glm::vec3 u = glm::normalize(c.up);
            const float distance = camera->projectionType == Camera::ProjectionType::Orthographic
                ? glm::min(camera->farClipPlane, 8.0f)
                : 3.0f;
            const float aspect = 16.0f / 9.0f;
            float halfH = 1.0f;
            float halfW = aspect;
            if (camera->projectionType == Camera::ProjectionType::Orthographic)
            {
                halfH = glm::max(0.01f, camera->orthographicSize);
                halfW = halfH * aspect;
            }
            else
            {
                halfH = tanf(glm::radians(camera->fieldOfView) * 0.5f) * distance;
                halfW = halfH * aspect;
            }

            glm::vec3 center = origin + f * distance;
            glm::vec3 corners[4] = {
                center - r * halfW - u * halfH,
                center + r * halfW - u * halfH,
                center + r * halfW + u * halfH,
                center - r * halfW + u * halfH,
            };
            ImU32 color = obj == m_editor->activeSelection ? IM_COL32(255, 220, 80, 230) : IM_COL32(255, 255, 255, 135);
            for (int i = 0; i < 4; ++i)
            {
                DrawWorldLine(corners[i], corners[(i + 1) % 4], color, 1.0f);
                if (camera->projectionType == Camera::ProjectionType::Perspective)
                    DrawWorldLine(origin, corners[i], color, 1.0f);
            }
            if (camera->projectionType == Camera::ProjectionType::Orthographic)
            {
                glm::vec3 nearCenter = origin + f * camera->nearClipPlane;
                for (int i = 0; i < 4; ++i)
                {
                    glm::vec3 nearCorner = nearCenter + (corners[i] - center);
                    DrawWorldLine(nearCorner, corners[i], color, 1.0f);
                }
            }
        }
        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(m_editor->engine->scene->rootGameObject.get());
}

void SceneWindow::HandleCameraMovement()
{
    if (!m_editor->isSceneActive)
        return;

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (!camera)
        return;

    float keySpeed = camera->projectionType == Camera::ProjectionType::Orthographic
        ? camera->orthographicSize * 0.015f
        : 0.05f;

    if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
        camera->position += camera->up * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
        camera->position -= camera->up * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
        camera->position -= camera->right * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
        camera->position += camera->right * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_PageUp))
        camera->position += camera->forward * keySpeed;
    if (ImGui::IsKeyDown(ImGuiKey_PageDown))
        camera->position -= camera->forward * keySpeed;

    float wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsWindowHovered() && !m_toolbarHovered && fabsf(wheel) > 0.001f)
    {
        if (camera->projectionType == Camera::ProjectionType::Orthographic)
            camera->orthographicSize = glm::clamp(camera->orthographicSize * (wheel > 0.0f ? 0.88f : 1.14f), 0.05f, 10000.0f);
        else
            camera->position += camera->forward * (wheel * 0.8f);
    }
}

void SceneWindow::DrawSceneGrid(const ImVec2& viewMin, const ImVec2& viewMax)
{
    if (!m_gridVisible || !m_editor || !m_editor->engine || !m_editor->engine->sceneCamera) return;

    Camera* camera = m_editor->engine->sceneCamera.get();
    const float w = viewMax.x - viewMin.x;
    const float h = viewMax.y - viewMin.y;
    if (!camera || w <= 0.0f || h <= 0.0f) return;

    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix(w / h);
    glm::mat4 grid = glm::mat4(1.0f);
    if (m_scene2D)
        grid = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewMin.x, viewMin.y, w, h);
    ImGuizmo::SetOrthographic(camera->projectionType == Camera::ProjectionType::Orthographic);
    ImGuizmo::DrawGridCustomColor(glm::value_ptr(view), glm::value_ptr(proj), glm::value_ptr(grid),
        100.0f, 1.0f, 10,
        IM_COL32(100, 100, 100, 85),
        IM_COL32(70, 70, 70, 55),
        IM_COL32(135, 135, 135, 120));
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
    if (m_viewToolMode == ViewToolMode::Hand)
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
    ImGuizmo::SetGizmoSizeClipSpace(0.22f);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (m_viewToolMode == ViewToolMode::Transform)
        op = ImGuizmo::UNIVERSAL;
    else if (m_toolMode == ToolMode::Rotate)
        op = ImGuizmo::ROTATE;
    else if (m_toolMode == ToolMode::Scale)
        op = ImGuizmo::SCALE;
    else
        op = ImGuizmo::TRANSLATE;

    glm::mat4 model = transform->GetWorldModel();

    bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
        op, m_localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(model));

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
        m_viewToolMode = ViewToolMode::Move;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        m_toolMode = ToolMode::Rotate;
        m_viewToolMode = ViewToolMode::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        m_toolMode = ToolMode::Scale;
        m_viewToolMode = ViewToolMode::Scale;
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

    DrawSceneGrid(min, max);

    if (m_gizmosEnabled)
    {
        DrawPhysicsMeshGizmos();
        DrawCameraGizmos();
        DrawUIGizmos(min, max);
        DrawImGuizmo(min, max);
    }
    HandleCameraRotation();
    if (m_gizmosEnabled)
        DrawAxisGizmo();
    DrawSceneToolbar(min, max);

    // Only handle object selection when not dragging the gizmo, not hovering
    // it, not rotating the camera, and not just finished a drag.
    if (ImGui::IsWindowHovered() && !m_toolbarHovered && m_viewToolMode != ViewToolMode::Hand &&
        ImGui::IsMouseClicked(0) && !m_isRotatingCamera &&
        !m_isDragging && !m_justFinishedDrag && !ImGuizmo::IsOver())
    {
        HandleObjectSelection();
    }
    
    // Reset the flag after this frame
    m_justFinishedDrag = false;

    ImGui::End();
    ImGui::PopStyleColor();
}

void SceneWindow::Apply2DMode(bool enabled)
{
    m_scene2D = enabled;
    if (!m_editor || !m_editor->engine || !m_editor->engine->sceneCamera) return;

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (enabled)
    {
        camera->projectionType = Camera::ProjectionType::Orthographic;
        camera->orthographicSize = 6.0f;
        camera->nearClipPlane = 0.01f;
        camera->farClipPlane = 1000.0f;
        camera->position = glm::vec3(0.0f, 0.0f, 10.0f);
        camera->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        if (m_editor->activeSelection)
            FrameObject(m_editor->activeSelection);
        else
            FrameScene2D();
    }
    else
    {
        camera->projectionType = Camera::ProjectionType::Perspective;
        camera->nearClipPlane = 0.01f;
        camera->farClipPlane = 1000.0f;
        camera->position = glm::vec3(0.0f, 10.0f, 10.0f);
        camera->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void SceneWindow::FrameScene2D()
{
    if (!m_editor || !m_editor->engine || !m_editor->engine->scene || !m_editor->engine->sceneCamera) return;

    glm::vec2 minPt(FLT_MAX);
    glm::vec2 maxPt(-FLT_MAX);
    bool any = false;
    std::function<void(GameObject*)> visit = [&](GameObject* obj)
    {
        if (!obj || !obj->enabled) return;
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (transform && (obj->GetComponent<RendererComponent>() || obj->GetComponent<SpriteRendererComponent>()))
        {
            glm::vec3 p = glm::vec3(transform->GetWorldModel()[3]);
            glm::vec2 half(0.5f);
            if (SpriteRendererComponent* sprite = obj->GetComponent<SpriteRendererComponent>())
            {
                half = sprite->drawMode == SpriteRendererComponent::Simple
                    ? glm::vec2(0.5f)
                    : glm::max(sprite->size, glm::vec2(0.5f)) * 0.5f;
            }
            glm::vec2 scale(glm::length(glm::vec3(transform->GetWorldModel()[0])),
                glm::length(glm::vec3(transform->GetWorldModel()[1])));
            half *= glm::max(scale, glm::vec2(0.01f));
            minPt = glm::min(minPt, glm::vec2(p) - half);
            maxPt = glm::max(maxPt, glm::vec2(p) + half);
            any = true;
        }
        for (const auto& child : obj->children)
            visit(child.get());
    };
    visit(m_editor->engine->scene->rootGameObject.get());

    Camera* camera = m_editor->engine->sceneCamera.get();
    if (!any)
    {
        camera->orthographicSize = 6.0f;
        camera->position = glm::vec3(0.0f, 0.0f, 10.0f);
        camera->LookAt(glm::vec3(0.0f));
        return;
    }

    glm::vec2 center = (minPt + maxPt) * 0.5f;
    glm::vec2 extents = glm::max((maxPt - minPt) * 0.5f, glm::vec2(1.0f));
    camera->orthographicSize = glm::max(1.0f, extents.y * 1.35f);
    camera->position = glm::vec3(center.x, center.y, 10.0f);
    camera->LookAt(glm::vec3(center.x, center.y, 0.0f));
}

void SceneWindow::DrawSceneToolbar(const ImVec2& viewMin, const ImVec2& viewMax)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = viewMax.x - viewMin.x;
    const bool compact = width < 560.0f;
    const float iconSize = compact ? 14.0f : 18.0f;
    const float height = compact ? 24.0f : 26.0f;
    ImVec2 pos(viewMin.x + 8.0f, viewMin.y + 6.0f);
    ImVec2 size(viewMax.x - viewMin.x - 16.0f, height);
    ImVec2 mouse = ImGui::GetMousePos();
    m_toolbarHovered = mouse.x >= pos.x && mouse.x <= pos.x + size.x &&
        mouse.y >= pos.y && mouse.y <= pos.y + size.y;
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(37, 37, 37, 235), 3.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 6.0f, pos.y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compact ? ImVec2(4.0f, 2.0f) : ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, compact ? ImVec2(2.0f, 0.0f) : ImVec2(4.0f, 0.0f));

    auto iconButton = [&](const char* id, SceneToolbarIcon icon, bool active, const char* tooltip) -> bool
    {
        void* tex = m_editor ? m_editor->GetSceneIcon(icon) : nullptr;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.50f, 1.0f));
        bool clicked = false;
        if (tex)
            clicked = ImGui::ImageButton(id, (ImTextureID)tex, ImVec2(iconSize, iconSize),
                ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
        else
            clicked = ImGui::Button(id, ImVec2(30.0f, 22.0f));
        if (active)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    };

    if (iconButton("##ScenePivot", m_pivotCenter ? SceneToolbarIcon::Center : SceneToolbarIcon::Pivot,
        m_pivotCenter, m_pivotCenter ? "Center" : "Pivot"))
        m_pivotCenter = !m_pivotCenter;
    ImGui::SameLine();
    if (iconButton("##SceneSpace", m_localSpace ? SceneToolbarIcon::Local : SceneToolbarIcon::Global,
        m_localSpace, m_localSpace ? "Local" : "Global"))
        m_localSpace = !m_localSpace;

    ImGui::SameLine();
    if (iconButton("##SceneGrid", SceneToolbarIcon::Grid, m_gridVisible, "Grid"))
        m_gridVisible = !m_gridVisible;
    ImGui::SameLine();
    if (iconButton("##SceneSnap", SceneToolbarIcon::Tools, m_snapEnabled, "Snap"))
        m_snapEnabled = !m_snapEnabled;

    float rightStart = viewMax.x - (compact ? 190.0f : 320.0f);
    if (!compact && ImGui::GetCursorScreenPos().x < rightStart)
        ImGui::SameLine(rightStart - viewMin.x);
    else
        ImGui::SameLine();

    bool new2D = m_scene2D;
    if (iconButton("##Scene2D", SceneToolbarIcon::View2D, new2D, "2D Mode"))
        new2D = !new2D;
    if (new2D != m_scene2D)
        Apply2DMode(new2D);

    ImGui::SameLine();
    if (iconButton("##SceneLighting", m_lightingEnabled ? SceneToolbarIcon::Lighting : SceneToolbarIcon::LightingOff,
        m_lightingEnabled, "Scene Lighting"))
        m_lightingEnabled = !m_lightingEnabled;
    ImGui::SameLine();
    if (iconButton("##SceneAudio", m_audioEnabled ? SceneToolbarIcon::Audio : SceneToolbarIcon::AudioOff,
        m_audioEnabled, "Audio"))
        m_audioEnabled = !m_audioEnabled;
    ImGui::SameLine();
    if (iconButton("##SceneFx", SceneToolbarIcon::Fx, m_effectsEnabled, "Effects"))
        m_effectsEnabled = !m_effectsEnabled;
    ImGui::SameLine();
    if (iconButton("##SceneGizmos", SceneToolbarIcon::Visibility, m_gizmosEnabled, "Gizmos"))
        m_gizmosEnabled = !m_gizmosEnabled;

    ImGui::PopStyleVar(2);

    const float sideButton = compact ? 24.0f : 28.0f;
    const float sideIcon = compact ? 15.0f : 18.0f;
    const float sidePad = 5.0f;
    ImVec2 sidePos(viewMin.x + 8.0f, viewMin.y + height + 12.0f);
    ImVec2 sideSize(sideButton + sidePad * 2.0f, sideButton * 6.0f + sidePad * 2.0f);
    m_toolbarHovered = m_toolbarHovered ||
        (mouse.x >= sidePos.x && mouse.x <= sidePos.x + sideSize.x &&
         mouse.y >= sidePos.y && mouse.y <= sidePos.y + sideSize.y);
    drawList->AddRectFilled(sidePos, ImVec2(sidePos.x + sideSize.x, sidePos.y + sideSize.y), IM_COL32(37, 37, 37, 235), 3.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    auto sideToolButton = [&](const char* id, SceneToolbarIcon icon, ViewToolMode mode, const char* tooltip) -> bool
    {
        void* tex = m_editor ? m_editor->GetSceneIcon(icon) : nullptr;
        const bool active = m_viewToolMode == mode;
        ImGui::SetCursorScreenPos(ImVec2(sidePos.x + sidePad, sidePos.y + sidePad + static_cast<float>(static_cast<int>(mode)) * sideButton));
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.50f, 1.0f));
        bool clicked = false;
        if (tex)
            clicked = ImGui::ImageButton(id, (ImTextureID)tex, ImVec2(sideIcon, sideIcon),
                ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
        else
            clicked = ImGui::Button(id, ImVec2(sideButton - 6.0f, sideButton - 6.0f));
        if (active)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    };

    if (sideToolButton("##SceneHandTool", SceneToolbarIcon::Hand, ViewToolMode::Hand, "View Tool"))
        m_viewToolMode = ViewToolMode::Hand;
    if (sideToolButton("##SceneMoveTool", SceneToolbarIcon::Move, ViewToolMode::Move, "Move Tool"))
    {
        m_viewToolMode = ViewToolMode::Move;
        m_toolMode = ToolMode::Translate;
    }
    if (sideToolButton("##SceneRotateTool", SceneToolbarIcon::Rotate, ViewToolMode::Rotate, "Rotate Tool"))
    {
        m_viewToolMode = ViewToolMode::Rotate;
        m_toolMode = ToolMode::Rotate;
    }
    if (sideToolButton("##SceneScaleTool", SceneToolbarIcon::Scale, ViewToolMode::Scale, "Scale Tool"))
    {
        m_viewToolMode = ViewToolMode::Scale;
        m_toolMode = ToolMode::Scale;
    }
    if (sideToolButton("##SceneTransformTool", SceneToolbarIcon::Transform, ViewToolMode::Transform, "Transform Tool"))
    {
        m_viewToolMode = ViewToolMode::Transform;
        m_toolMode = ToolMode::Translate;
    }
    if (sideToolButton("##SceneRectTool", SceneToolbarIcon::Rect, ViewToolMode::Rect, "Rect Tool"))
    {
        m_viewToolMode = ViewToolMode::Rect;
        m_toolMode = ToolMode::Translate;
    }

    ImGui::PopStyleVar(2);
}

void SceneWindow::DrawAxisGizmo()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    
    // Position in top-right corner with some padding
    float gizmoSize = 110.0f;
    float padding = 20.0f;
    float topOffset = 42.0f;
    ImVec2 center(windowPos.x + windowSize.x - gizmoSize * 0.5f - padding, windowPos.y + gizmoSize * 0.5f + padding + topOffset);
    
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

        if (m_scene2D || camera->projectionType == Camera::ProjectionType::Orthographic)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const float viewportHeight = window ? glm::max(1.0f, window->InnerRect.Max.y - window->InnerRect.Min.y) : 1.0f;
            const float unitsPerPixel = (camera->orthographicSize * 2.0f) / viewportHeight;
            camera->position -= camera->right * (deltaX * unitsPerPixel);
            camera->position += camera->up * (deltaY * unitsPerPixel);
            m_lastMousePos = currentMousePos;
            return;
        }
        
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

