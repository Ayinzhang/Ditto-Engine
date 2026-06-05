#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "InspectorWindow.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/ProjectManager.h"
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

                ImGui::Image((void*)(intptr_t)m_previewTexture, ImVec2(btnWidth, btnHeight),
                    ImVec2(0, 1), ImVec2(1, 0));

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

void InspectorWindow::InitModelPreview()
{
    if (m_previewInitialized) return;

    std::cout << "[Preview] Initializing..." << std::endl;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glGenFramebuffers(1, &m_previewFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_previewFBO);

    glGenTextures(1, &m_previewTexture);
    glBindTexture(GL_TEXTURE_2D, m_previewTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_previewWidth, m_previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_previewTexture, 0);

    glGenRenderbuffers(1, &m_previewRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_previewRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_previewWidth, m_previewHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_previewRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_previewCamera = new Camera(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    const char* vertSrc = R"(
        #version 460 core
        layout(location = 0) in vec3 aPos;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* fragSrc = R"(
        #version 460 core
        out vec4 col;
        
        void main() {
            col = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    )";

    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, nullptr);
    glCompileShader(vert);

    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, nullptr);
    glCompileShader(frag);

    m_previewProgram = glCreateProgram();
    glAttachShader(m_previewProgram, vert);
    glAttachShader(m_previewProgram, frag);
    glLinkProgram(m_previewProgram);

    glDeleteShader(vert);
    glDeleteShader(frag);

    std::cout << "[Preview] Program: " << m_previewProgram << std::endl;

    m_previewInitialized = true;
}

void InspectorWindow::LoadPreviewModel(const std::string& modelPath)
{
    if (modelPath.empty()) return;

    std::cout << "[Preview] Loading: " << modelPath << std::endl;
    std::cout << "[Preview] Current: " << m_currentPreviewPath << std::endl;

    if (m_currentPreviewPath == modelPath && m_currentPreviewModel.VAO != 0) {
        std::cout << "[Preview] Same model, skipping" << std::endl;
        return;
    }

    if (m_currentPreviewModel.VAO) {
        glDeleteVertexArrays(1, &m_currentPreviewModel.VAO);
        glDeleteBuffers(1, &m_currentPreviewModel.VBO);
        if (m_currentPreviewModel.EBO) glDeleteBuffers(1, &m_currentPreviewModel.EBO);
        m_currentPreviewModel.VAO = 0;
    }

    if (m_previewTexture) {
        glDeleteTextures(1, &m_previewTexture);
        m_previewTexture = 0;
    }
    glGenTextures(1, &m_previewTexture);
    glBindTexture(GL_TEXTURE_2D, m_previewTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_previewWidth, m_previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, m_previewFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_previewTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

    m_currentPreviewModel.vertexCount = vertexData.size() / 3;
    m_currentPreviewModel.indexCount = 0;

    glGenVertexArrays(1, &m_currentPreviewModel.VAO);
    glGenBuffers(1, &m_currentPreviewModel.VBO);

    glBindVertexArray(m_currentPreviewModel.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_currentPreviewModel.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);

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
    if (!m_previewInitialized) {
        std::cout << "[Preview] Not initialized" << std::endl;
        return;
    }
    if (!m_previewCamera) {
        std::cout << "[Preview] No camera" << std::endl;
        return;
    }
    if (!m_previewProgram) {
        std::cout << "[Preview] No preview program" << std::endl;
        return;
    }

    if (!m_currentPreviewModel.VAO)
    {
        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_previewFBO);
        glViewport(0, 0, m_previewWidth, m_previewHeight);
        glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        return;
    }

    GLint previousFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    GLint cullFaceMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);

    glBindFramebuffer(GL_FRAMEBUFFER, m_previewFBO);
    glViewport(0, 0, m_previewWidth, m_previewHeight);

    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    glm::mat4 view = m_previewCamera->GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)m_previewWidth / (float)m_previewHeight, 0.1f, 100.0f);
    glm::vec3 viewPos = m_previewCamera->position;

    glUseProgram(m_previewProgram);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), -m_currentPreviewModel.center);

    GLint modelLoc = glGetUniformLocation(m_previewProgram, "model");
    GLint viewLoc = glGetUniformLocation(m_previewProgram, "view");
    GLint projLoc = glGetUniformLocation(m_previewProgram, "projection");
    GLint lightLoc = glGetUniformLocation(m_previewProgram, "lightDir");

    if (modelLoc >= 0) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    if (viewLoc >= 0) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
    if (projLoc >= 0) glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);
    if (lightLoc >= 0) glUniform3f(lightLoc, 0.5f, 0.8f, 0.5f);

    glBindVertexArray(m_currentPreviewModel.VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_currentPreviewModel.vertexCount);

    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (cullFaceEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace(cullFaceMode);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void InspectorWindow::CleanupModelPreview()
{
    if (!m_previewInitialized) return;

    if (m_previewFBO) glDeleteFramebuffers(1, &m_previewFBO);
    if (m_previewRBO) glDeleteRenderbuffers(1, &m_previewRBO);
    if (m_previewTexture) glDeleteTextures(1, &m_previewTexture);
    if (m_previewCamera) delete m_previewCamera;

    if (m_currentPreviewModel.VAO) {
        glDeleteVertexArrays(1, &m_currentPreviewModel.VAO);
        glDeleteBuffers(1, &m_currentPreviewModel.VBO);
        if (m_currentPreviewModel.EBO) glDeleteBuffers(1, &m_currentPreviewModel.EBO);
    }

    m_previewFBO = 0;
    m_previewRBO = 0;
    m_previewTexture = 0;
    m_previewCamera = nullptr;
    m_currentPreviewModel.VAO = 0;
    m_previewInitialized = false;
}
