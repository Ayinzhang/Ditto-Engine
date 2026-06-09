#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "InspectorWindow.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/ProjectManager.h"

// Defined below; the model preview routes all GPU work through the RHI.
static Ditto::IRenderer* PreviewRenderer(Editor* editor);
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/ext/matrix_transform.hpp"

namespace fs = std::filesystem;

InspectorWindow::InspectorWindow(Editor* editor)
    : m_editor(editor)
{
}

InspectorWindow::~InspectorWindow()
{
    CleanupModelPreview();
}

InspectorWindow::ModelInfo InspectorWindow::LoadModelInfo(const std::string& path)
{
    ModelInfo info;
    std::ifstream file(path);
    if (!file.is_open()) return info;

    std::string line;
    int vertexCount = 0;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") vertexCount++;
        else if (prefix == "f") info.faceCount++;
    }

    info.vertexCount = vertexCount;
    info.loaded = true;
    return info;
}

void InspectorWindow::Draw()
{
    ImGui::Begin("Inspector");

    // Set current object to display (if Inspector is locked, show locked object)
    m_currentObject = nullptr;
    if (m_editor)
    {
        if (m_editor->selectedFile.IsValid())
        {
            // When showing file info, there is no associated GameObject
            m_currentObject = nullptr;
        }
        else
        {
            // If Inspector is locked (lockingSelection = true), show locked object
            // Otherwise show currently selected object
            if (m_editor->lockingSelection && m_editor->selectedObject && m_editor->selectedObject->locked)
            {
                // Found locked object - need to traverse scene
                // Simplified: use selectedObject because when locked it is the locked object
                m_currentObject = m_editor->selectedObject;
            }
            else
            {
                m_currentObject = m_editor->selectedObject;
            }
        }
    }

    // Handle script drag-drop (drag-drop is not affected by lock)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CS_SCRIPT"))
        {
            const char* scriptPath = (const char*)payload->Data;
            std::cout << "[Inspector] Received script: " << scriptPath << std::endl;
            
            // Use m_currentObject instead of selectedObject to allow dragging to add scripts
            if (m_currentObject)
            {
                m_editor->OnScriptComponentDroppedToObject(m_currentObject, scriptPath);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Prefer showing file info (if a file is selected)
    if (m_editor && m_editor->selectedFile.IsValid())
    {
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "File");
        ImGui::Separator();

        ImGui::Text("Name: "); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), m_editor->selectedFile.name.c_str());

        ImGui::Text("Path: "); ImGui::SameLine();
        ImGui::TextDisabled(m_editor->selectedFile.path.c_str());

        // If it's a model file, show model info
        if (m_editor->selectedFile.extension == ".obj" || m_editor->selectedFile.extension == ".fbx")
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Model");
            ImGui::Separator();

            ModelInfo modelInfo = LoadModelInfo(m_editor->selectedFile.path);
            if (modelInfo.loaded)
            {
                ImGui::Text("Vertices: "); ImGui::SameLine();
                ImGui::Text(std::to_string(modelInfo.vertexCount).c_str());

                ImGui::Text("Faces: "); ImGui::SameLine();
                ImGui::Text(std::to_string(modelInfo.faceCount).c_str());
            }

            float previewWidth = ImGui::GetContentRegionAvail().x;
            if (previewWidth > 0)
            {
                ImGui::Separator();
                ImGui::Text("Preview");
                if (!m_previewInitialized) InitModelPreview();

                RenderModelPreview();

                float btnWidth = std::min(previewWidth - 20, 200.0f);
                float btnHeight = btnWidth;
                ImVec2 cursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cursor.x + (previewWidth - btnWidth) / 2, cursor.y));

                Ditto::IRenderer* previewR = PreviewRenderer(m_editor);
                void* previewTexId = previewR ? previewR->GetImGuiTextureID(previewR->GetColorTexture(m_previewRT)) : nullptr;
                // Guard against a null texture id (e.g. render targets not yet
                // implemented on the active backend) -- passing null to ImGui::Image
                // can crash the Vulkan backend.
                if (previewTexId)
                    ImGui::Image(previewTexId, ImVec2(btnWidth, btnHeight), ImVec2(0, 1), ImVec2(1, 0));
                else
                    ImGui::TextDisabled("(preview unavailable on this backend)");

                // Mouse drag to rotate
                if (!m_modelInitialized || (ImGui::IsItemHovered() && ImGui::IsMouseDown(0)))
                {
                    m_modelInitialized = true;
                    ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                    if (m_previewCamera)
                    {
                        float rotSpeed = 0.5f;
                        glm::vec3 camOffset = m_previewCamera->position - m_currentPreviewModel.center;
                        float distance = glm::length(camOffset);

                        m_previewCamera->yaw -= mouseDelta.x * rotSpeed;
                        m_previewCamera->pitch += mouseDelta.y * rotSpeed;

                        float yawRad = glm::radians(m_previewCamera->yaw);
                        float pitchRad = glm::radians(m_previewCamera->pitch);

                        m_previewCamera->position = m_currentPreviewModel.center + glm::vec3(
                            distance * cos(pitchRad) * sin(yawRad),
                            distance * sin(pitchRad),
                            distance * cos(pitchRad) * cos(yawRad)
                        );

                        glm::vec3 forward = glm::normalize(m_currentPreviewModel.center - m_previewCamera->position);
                        m_previewCamera->forward = forward;
                        m_previewCamera->right = glm::normalize(glm::cross(forward, m_previewCamera->worldUp));
                        m_previewCamera->up = -glm::normalize(glm::cross(forward, m_previewCamera->right));
                    }
                }
            }
        }

        ImGui::End();
        return;
    }

    // When no file is selected, show GameObject info
    if (!m_editor || !m_editor->selectedObject) {
        ImGui::TextDisabled("Select an object to view its properties");
        ImGui::End();
        return;
    }

    // Use m_currentObject instead of selectedObject (supports lock display)
    if (m_currentObject)
    {
        if (m_editor->engine && m_editor->engine->state == Engine::State::Play) ImGui::BeginDisabled();
        
        // Show GameObject UI
        m_currentObject->OnInspectorGUI();
        
        // Unified Add Component area - supports click to expand menu and drag scripts
        ImGui::Separator();
        ImVec4 prevColor = ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImGui::GetStyle().Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 0.5f);
        if (ImGui::Button("+ Add Component (drag .cpp here)", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }
        ImGui::GetStyle().Colors[ImGuiCol_Button] = prevColor;
        
        // Drag-drop receiver
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CS_SCRIPT"))
            {
                const char* scriptPath = (const char*)payload->Data;
                std::cout << "[Inspector] Received script: " << scriptPath << std::endl;
                m_editor->OnScriptComponentDroppedToObject(m_currentObject, scriptPath);
            }
            ImGui::EndDragDropTarget();
        }
        
        // Component selection popup menu
        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            ImGui::TextUnformatted("Components");
            ImGui::Separator();
            
            // Built-in components
            if (!(m_currentObject->compMask >> 1 & 1) && ImGui::MenuItem("Light"))
                { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<LightComponent>(); }
            if (!(m_currentObject->compMask >> 2 & 1) && ImGui::MenuItem("Renderer"))
                { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<RendererComponent>(); }
            if (!(m_currentObject->compMask >> 3 & 1) && ImGui::MenuItem("Rigidbody"))
                { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<RigidbodyComponent>(); }
            
            ImGui::Separator();
            ImGui::TextUnformatted("Scripts");
            ImGui::Separator();
            
            // Load available scripts from project Scripts directory
            Project* project = ProjectManager::GetInstance().GetCurrentProject();
            if (project)
            {
                std::string scriptsDir = project->path + "/Assets/Scripts";
                if (fs::exists(scriptsDir))
                {
                    for (const auto& entry : fs::directory_iterator(scriptsDir))
                    {
                        if (entry.is_regular_file())
                        {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".cs" || ext == ".cpp")
                            {
                                std::string filename = entry.path().stem().string();
                                std::string fullPath = entry.path().string();
                                if (ImGui::MenuItem(filename.c_str()))
                                {
                                    m_editor->OnScriptComponentDroppedToObject(m_currentObject, fullPath.c_str());
                                }
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("No scripts found");
                }
            }
            else
            {
                ImGui::TextDisabled("No project loaded");
            }
            
            ImGui::EndPopup();
        }
        
        if (m_editor->engine && m_editor->engine->state == Engine::State::Play) ImGui::EndDisabled();
    }

    ImGui::End();
}

// ============================================================================
// Model Preview Implementation
// ============================================================================

// Renderer accessor (owned by Engine; alive whenever the editor is).
static Ditto::IRenderer* PreviewRenderer(Editor* editor)
{
    return (editor && editor->engine) ? editor->engine->renderer.get() : nullptr;
}

void InspectorWindow::InitModelPreview()
{
    if (m_previewInitialized) return;
    Ditto::IRenderer* r = PreviewRenderer(m_editor);
    if (!r) return;

    m_previewRT = r->CreateRenderTarget(m_previewWidth, m_previewHeight);
    m_previewCamera = new Camera(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    // Solid-white wireframe preview shader (HLSL). The model transform is folded
    // into `view` on the CPU, so the shared FrameUniforms block drives it. The
    // cbuffer must mirror the scene shader's layout so SetFrameUniforms matches.
    const std::string previewHlsl = R"(
[[vk::binding(0, 0)]] cbuffer FrameUniforms : register(b0, space0)
{
    float4x4 view;
    float4x4 projection;
    float3   viewPos;        float _pad0;
    float3   lightColor;     float _pad1;
    float3   lightDir;       float lightIntensity;
};
struct VSOutput { float4 position : SV_Position; };
VSOutput VSMain(float3 aPos : POSITION)
{
    VSOutput o;
    o.position = mul(projection, mul(view, float4(aPos, 1.0)));
    return o;
}
float4 PSMain(VSOutput i) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)";
    m_previewPipeline = r->CreatePipeline(previewHlsl);

    m_previewInitialized = true;
}

void InspectorWindow::LoadPreviewModel(const std::string& modelPath)
{
    if (modelPath.empty()) return;

    std::cout << "[Preview] Loading: " << modelPath << std::endl;
    std::cout << "[Preview] Current: " << m_currentPreviewPath << std::endl;

    Ditto::IRenderer* r = PreviewRenderer(m_editor);
    if (!r) return;

    if (m_currentPreviewPath == modelPath && m_currentPreviewModel.mesh) {
        std::cout << "[Preview] Same model, skipping" << std::endl;
        return;
    }

    if (m_currentPreviewModel.mesh) {
        r->DestroyMesh(m_currentPreviewModel.mesh);
        m_currentPreviewModel.mesh = {};
    }

    m_currentPreviewPath = modelPath;

    std::ifstream file(modelPath);
    if (!file.is_open()) {
        std::cerr << "[Preview] Failed to open model: " << modelPath << std::endl;
        return;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> indices;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn") {
            glm::vec3 norm;
            ss >> norm.x >> norm.y >> norm.z;
            normals.push_back(glm::normalize(norm));
        }
        else if (prefix == "f") {
            std::vector<std::string> faceTokens;
            std::string token;
            while (ss >> token) faceTokens.push_back(token);

            for (size_t i = 1; i + 1 < faceTokens.size(); i++) {
                for (size_t j = 0; j < 3; j++) {
                    const std::string& ft = (j == 0) ? faceTokens[0] : (j == 1) ? faceTokens[i] : faceTokens[i + 1];
                    std::istringstream ft_ss(ft);
                    std::string idx;
                    std::getline(ft_ss, idx, '/');
                    int posIdx = std::stoi(idx) - 1;
                    indices.push_back(posIdx);
                }
            }
        }
    }

    if (positions.empty()) {
        std::cerr << "[Preview] No vertices found in model" << std::endl;
        return;
    }

    std::cout << "[Preview] Loaded " << positions.size() << " vertices, " << indices.size() / 3 << " faces" << std::endl;

    glm::vec3 minPos(positions[0]), maxPos(positions[0]);
    for (const auto& pos : positions) {
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }
    m_currentPreviewModel.center = (minPos + maxPos) * 0.5f;
    m_currentPreviewModel.radius = glm::length(maxPos - minPos) * 0.5f;

    std::vector<float> vertexData;
    for (size_t i = 0; i < indices.size(); i++) {
        int idx = indices[i];
        if (idx >= 0 && idx < positions.size()) {
            vertexData.push_back(positions[idx].x);
            vertexData.push_back(positions[idx].y);
            vertexData.push_back(positions[idx].z);
        }
    }

    m_currentPreviewModel.vertexCount = static_cast<int>(vertexData.size() / 3);

    // Position-only mesh (3 floats, attribute 0).
    m_currentPreviewModel.mesh = r->CreateMesh(vertexData.data(), vertexData.size(), 3, { {0, 3, 0} });

    if (m_previewCamera) {
        float distance = m_currentPreviewModel.radius * 2.5f;
        m_previewCamera->position = m_currentPreviewModel.center + glm::vec3(0, distance * 0.3f, distance);
        m_previewCamera->yaw = -90.0f;
        m_previewCamera->pitch = -15.0f;
        m_previewCamera->UpdateCameraVectors();
    }

    m_modelInitialized = false;
}

void InspectorWindow::RenderModelPreview()
{
    if (!m_previewInitialized || !m_previewCamera || !m_previewPipeline) return;
    Ditto::IRenderer* r = PreviewRenderer(m_editor);
    if (!r) return;

    // No model loaded yet: just clear the target to the preview background.
    if (!m_currentPreviewModel.mesh)
    {
        r->BeginRenderTarget(m_previewRT);
        r->Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
        r->EndRenderTarget();
        return;
    }

    r->BeginRenderTarget(m_previewRT);
    r->Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
    r->SetDepthState(true);
    r->SetCullState(true);
    r->SetWireframe(true);

    glm::mat4 view = m_previewCamera->GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f),
        (float)m_previewWidth / (float)m_previewHeight, 0.1f, 100.0f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), -m_currentPreviewModel.center);

    r->BindPipeline(m_previewPipeline);
    Ditto::FrameUniforms fu;
    fu.view = view * model;     // fold the model transform into view
    fu.projection = projection;
    r->SetFrameUniforms(fu);

    r->DrawInstanced(m_currentPreviewModel.mesh, 1);

    r->SetWireframe(false);
    r->SetCullState(false);
    r->EndRenderTarget();
}

void InspectorWindow::CleanupModelPreview()
{
    if (!m_previewInitialized) return;

    if (Ditto::IRenderer* r = PreviewRenderer(m_editor))
    {
        r->DestroyRenderTarget(m_previewRT);
        r->DestroyPipeline(m_previewPipeline);
        if (m_currentPreviewModel.mesh) r->DestroyMesh(m_currentPreviewModel.mesh);
    }
    if (m_previewCamera) delete m_previewCamera;

    m_previewRT = {};
    m_previewPipeline = {};
    m_currentPreviewModel.mesh = {};
    m_previewCamera = nullptr;
    m_previewInitialized = false;
}
