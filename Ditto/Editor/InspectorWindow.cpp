#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "InspectorWindow.h"
#include "../Engine/Core/Logger.h"
#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Graphics/Shaders/ShaderAsset.h"
#include "../Engine/Animation/AnimatorComponent.h"
#include "../Engine/Graphics/ParticleSystemComponent.h"

// Defined below; the model preview routes all GPU work through the RHI.
static Ditto::IRenderer* PreviewRenderer(Editor* editor);
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <functional>
#include <vector>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/ext/matrix_transform.hpp"

namespace fs = std::filesystem;

namespace
{
    std::string LowerExtension(const std::string& path)
    {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    std::string FileNameFromPath(const std::string& path)
    {
        size_t pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    std::string ToAssetRelativePath(const std::string& path)
    {
        Project* project = ProjectManager::GetInstance().GetCurrentProject();
        if (!project) return path;

        fs::path filePath = fs::absolute(path).lexically_normal();
        fs::path assetsPath = fs::absolute(fs::path(project->path) / "Assets").lexically_normal();
        std::wstring fileW = filePath.wstring();
        std::wstring assetsW = assetsPath.wstring();
        std::replace(fileW.begin(), fileW.end(), L'\\', L'/');
        std::replace(assetsW.begin(), assetsW.end(), L'\\', L'/');

        if (fileW.rfind(assetsW + L"/", 0) == 0)
            return filePath.lexically_relative(assetsPath).generic_string();
        return path;
    }

    std::string LowerText(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool MatchesSearch(const std::string& text, const std::string& search)
    {
        if (search.empty()) return true;
        return LowerText(text).find(LowerText(search)) != std::string::npos;
    }

    bool DrawFileObjectField(const char* label, const std::string& value, const char* id, std::string* droppedPath)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        std::string display = value.empty() ? "None" : FileNameFromPath(value);
        bool opened = ImGui::Button((display + "##" + id).c_str(), ImVec2(width, 0.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_FILE"))
                *droppedPath = static_cast<const char*>(payload->Data);
            ImGui::EndDragDropTarget();
        }
        if (opened)
            ImGui::OpenPopup(id);
        return opened;
    }

    void DrawMaterialFileInspector(const std::string& materialPath)
    {
        Ditto::MaterialAsset material = Ditto::LoadMaterialAsset(materialPath);
        if (!material.ok)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Material load failed");
            ImGui::TextWrapped("%s", material.error.c_str());
            return;
        }

        bool changed = false;
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Material");
        ImGui::Separator();

        std::string droppedShader;
        DrawFileObjectField("Shader", material.shaderName, "MaterialShaderAsset", &droppedShader);
        if (!droppedShader.empty())
        {
            std::string ext = LowerExtension(droppedShader);
            if (ext == ".shader" || ext == ".hlsl" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
            {
                material.shaderName = ToAssetRelativePath(droppedShader);
                changed = true;
            }
        }
        if (ImGui::BeginPopup("MaterialShaderAsset"))
        {
            char shaderBuf[256];
            strcpy_s(shaderBuf, sizeof(shaderBuf), material.shaderName.c_str());
            ImGui::TextUnformatted("Shader Name");
            if (ImGui::InputText("##MaterialShaderName", shaderBuf, sizeof(shaderBuf)))
            {
                material.shaderName = shaderBuf;
                changed = true;
            }
            ImGui::EndPopup();
        }

        Ditto::ShaderAsset shader = Ditto::LoadShaderAsset(material.shaderName);
        if (!shader.ok)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Shader unavailable");
            ImGui::TextWrapped("%s", shader.error.c_str());
        }
        else
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Properties");
            ImGui::Indent(12.0f);
            for (const Ditto::ShaderProperty& property : shader.properties)
            {
                const std::string label = property.displayName.empty() ? property.name : property.displayName;
                if (property.type == Ditto::ShaderPropertyType::Color)
                {
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::SameLine();
                    if (ImGui::ColorEdit4(("##MaterialFileColor" + property.name).c_str(), &material.color.x, ImGuiColorEditFlags_AlphaBar))
                        changed = true;
                }
                else if (property.type == Ditto::ShaderPropertyType::Texture2D)
                {
                    std::string droppedTexture;
                    DrawFileObjectField(label.c_str(), material.mainTexturePath, ("MaterialFileTexture" + property.name).c_str(), &droppedTexture);
                    if (!droppedTexture.empty())
                    {
                        std::string ext = LowerExtension(droppedTexture);
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
                        {
                            material.mainTexturePath = ToAssetRelativePath(droppedTexture);
                            changed = true;
                        }
                    }

                    char texBuf[256];
                    strcpy_s(texBuf, sizeof(texBuf), material.mainTexturePath.c_str());
                    ImGui::TextUnformatted("Path");
                    ImGui::SameLine();
                    if (ImGui::InputText(("##MaterialFileTexturePath" + property.name).c_str(), texBuf, sizeof(texBuf)))
                    {
                        material.mainTexturePath = texBuf;
                        changed = true;
                    }
                    if (!material.mainTexturePath.empty() && ImGui::SmallButton(("Clear##MaterialFileTexture" + property.name).c_str()))
                    {
                        material.mainTexturePath.clear();
                        changed = true;
                    }
                }
            }
            ImGui::Unindent(12.0f);
        }

        if (changed)
            Ditto::SaveMaterialAsset(material, materialPath);
    }

    const char* ShaderPropertyTypeName(Ditto::ShaderPropertyType type)
    {
        switch (type)
        {
        case Ditto::ShaderPropertyType::Color: return "Color";
        case Ditto::ShaderPropertyType::Float: return "Float";
        case Ditto::ShaderPropertyType::Range: return "Range";
        case Ditto::ShaderPropertyType::Texture2D: return "Texture2D";
        default: return "Unknown";
        }
    }

    void DrawShaderFileInspector(const std::string& shaderPath)
    {
        Ditto::ShaderAsset shader = Ditto::LoadShaderAsset(shaderPath);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Shader");
        ImGui::Separator();

        if (!shader.ok)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shader load failed");
            ImGui::TextWrapped("%s", shader.error.c_str());
            return;
        }

        ImGui::Text("Render Queue"); ImGui::SameLine();
        ImGui::Text("%d", shader.pipelineState.renderQueue);
        ImGui::Text("Render Type "); ImGui::SameLine();
        ImGui::TextUnformatted(shader.pipelineState.renderType.empty() ? "Opaque" : shader.pipelineState.renderType.c_str());
        ImGui::Text("ZWrite     "); ImGui::SameLine();
        ImGui::TextUnformatted(shader.pipelineState.depthWrite ? "On" : "Off");
        ImGui::Text("Blend      "); ImGui::SameLine();
        ImGui::TextUnformatted(shader.pipelineState.blend ? "On" : "Off");

        ImGui::Separator();
        ImGui::TextUnformatted("Properties");
        if (shader.properties.empty())
        {
            ImGui::TextDisabled("No Properties block entries found");
        }
        else
        {
            ImGui::Indent(12.0f);
            for (const Ditto::ShaderProperty& property : shader.properties)
            {
                ImGui::TextUnformatted(property.displayName.empty() ? property.name.c_str() : property.displayName.c_str());
                ImGui::SameLine(160.0f);
                ImGui::TextDisabled("%s", ShaderPropertyTypeName(property.type));
            }
            ImGui::Unindent(12.0f);
        }

        ImGui::Separator();
        ImGui::Text("Generated HLSL"); ImGui::SameLine();
        ImGui::Text("%zu bytes", shader.engineHLSL.size());
        ImGui::TextDisabled("Double-click the shader asset in Project to edit the source externally.");
    }
}

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
            DITTO_LOG_INFO_STREAM("[Inspector] Received script: " << scriptPath );
            
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

        if (m_editor->selectedFile.extension == ".mat")
        {
            DrawMaterialFileInspector(m_editor->selectedFile.path);
            ImGui::End();
            return;
        }

        if (m_editor->selectedFile.extension == ".shader" || m_editor->selectedFile.extension == ".hlsl" ||
            m_editor->selectedFile.extension == ".glsl" || m_editor->selectedFile.extension == ".vert" ||
            m_editor->selectedFile.extension == ".frag")
        {
            DrawShaderFileInspector(m_editor->selectedFile.path);
            ImGui::End();
            return;
        }

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
                        m_previewCamera->pitch = std::clamp(m_previewCamera->pitch + mouseDelta.y * rotSpeed, -89.0f, 89.0f);

                        float yawRad = glm::radians(m_previewCamera->yaw);
                        float pitchRad = glm::radians(m_previewCamera->pitch);

                        m_previewCamera->position = m_currentPreviewModel.center + glm::vec3(
                            distance * cos(pitchRad) * sin(yawRad),
                            distance * sin(pitchRad),
                            distance * cos(pitchRad) * cos(yawRad)
                        );

                        m_previewCamera->LookAt(m_currentPreviewModel.center);
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
        static char addComponentSearch[128] = "";
        ImVec4 prevColor = ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImGui::GetStyle().Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 0.5f);
        if (ImGui::Button("Add Component", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
        {
            addComponentSearch[0] = '\0';
            ImGui::OpenPopup("AddComponentPopup");
        }
        ImGui::GetStyle().Colors[ImGuiCol_Button] = prevColor;
        
        // Drag-drop receiver
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CS_SCRIPT"))
            {
                const char* scriptPath = (const char*)payload->Data;
                DITTO_LOG_INFO_STREAM("[Inspector] Received script: " << scriptPath );
                m_editor->OnScriptComponentDroppedToObject(m_currentObject, scriptPath);
            }
            ImGui::EndDragDropTarget();
        }
        
        // Component selection popup menu
        const float inspectorWidth = ImGui::GetWindowWidth();
        const float popupWidth = std::clamp(inspectorWidth - 32.0f, 300.0f, 420.0f);
        const float popupHeight = std::clamp(ImGui::GetIO().DisplaySize.y * 0.42f, 360.0f, 520.0f);
        ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            ImGui::TextUnformatted("Components");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 18.0f);
            if (ImGui::SmallButton("x##CloseAddComponent"))
                ImGui::CloseCurrentPopup();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##AddComponentSearch", addComponentSearch, sizeof(addComponentSearch));
            ImGui::Separator();

            std::string search = addComponentSearch;
            struct ComponentMenuItem
            {
                const char* name;
                int singleMask;
                std::function<void()> add;
            };

            std::vector<ComponentMenuItem> items = {
                { "Light", ComponentIndex::Light, [&]() { m_currentObject->AddComponent<LightComponent>(); } },
                { "Camera", ComponentIndex::Camera, [&]() { m_currentObject->AddComponent<CameraComponent>(); } },
                { "Mesh Renderer", ComponentIndex::Renderer, [&]() { m_currentObject->AddComponent<RendererComponent>(); } },
                { "Sprite Renderer", ComponentIndex::SpriteRenderer, [&]() { m_currentObject->AddComponent<SpriteRendererComponent>(); } },
                { "Rigidbody", ComponentIndex::Rigidbody, [&]() { m_currentObject->AddComponent<RigidbodyComponent>(); } },
                { "Box Collider", 0, [&]() { m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::Box); } },
                { "Sphere Collider", 0, [&]() { m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::Sphere); } },
                { "Mesh Collider", 0, [&]() { m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::MeshConvex); } },
                { "Rigidbody 2D", ComponentIndex::Rigidbody2D, [&]() { m_currentObject->AddComponent<Rigidbody2DComponent>(); } },
                { "Box Collider 2D", 0, [&]() { m_currentObject->AddComponent<Collider2DComponent>(Collider2DComponent::Box); } },
                { "Circle Collider 2D", 0, [&]() { m_currentObject->AddComponent<Collider2DComponent>(Collider2DComponent::Circle); } },
                { "Canvas", ComponentIndex::Canvas, [&]() { m_currentObject->AddComponent<CanvasComponent>(); } },
                { "Rect Transform", ComponentIndex::RectTransform, [&]() { m_currentObject->AddComponent<RectTransformComponent>(); } },
                { "Image", ComponentIndex::UIImage, [&]() { m_currentObject->AddComponent<UIImageComponent>(); } },
                { "Text", ComponentIndex::UIText, [&]() { m_currentObject->AddComponent<UITextComponent>(); } },
                { "Button", ComponentIndex::UIButton, [&]() { m_currentObject->AddComponent<UIButtonComponent>(); } },
                { "Audio Source", ComponentIndex::AudioSource, [&]() { m_currentObject->AddComponent<AudioSourceComponent>(); } },
                { "Animator", ComponentIndex::Animator, [&]() { m_currentObject->AddComponent<AnimatorComponent>(); } },
                { "Particle System", ComponentIndex::ParticleSystem, [&]() { m_currentObject->AddComponent<ParticleSystemComponent>(); } },
            };

            auto drawComponentItem = [&](const ComponentMenuItem& item) -> bool
            {
                if (!MatchesSearch(item.name, search)) return false;
                if (item.singleMask != 0 && (m_currentObject->compMask & item.singleMask) != 0)
                    return false;
                if (ImGui::MenuItem(item.name))
                {
                    if (m_editor) m_editor->PushUndoSnapshot();
                    item.add();
                    ImGui::CloseCurrentPopup();
                }
                return true;
            };

            if (!search.empty())
            {
                bool any = false;
                for (const ComponentMenuItem& item : items)
                    any = drawComponentItem(item) || any;

                Project* project = ProjectManager::GetInstance().GetCurrentProject();
                if (project)
                {
                    std::string scriptsDir = project->path + "/Assets/Scripts";
                    if (fs::exists(scriptsDir))
                    {
                        for (const auto& entry : fs::directory_iterator(scriptsDir))
                        {
                            if (!entry.is_regular_file()) continue;
                            std::string ext = entry.path().extension().string();
                            if (ext != ".cs" && ext != ".cpp") continue;
                            std::string filename = entry.path().stem().string();
                            if (!MatchesSearch(filename, search)) continue;
                            any = true;
                            std::string fullPath = entry.path().string();
                            if (ImGui::MenuItem((filename + "##SearchScript").c_str()))
                            {
                                m_editor->OnScriptComponentDroppedToObject(m_currentObject, fullPath.c_str());
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                }
                if (!any)
                    ImGui::TextDisabled("No matching components");

                ImGui::EndPopup();
                if (!ImGui::IsPopupOpen("AddComponentPopup"))
                    addComponentSearch[0] = '\0';
                if (m_editor->engine && m_editor->engine->state == Engine::State::Play) ImGui::EndDisabled();
                ImGui::End();
                return;
            }
            
            if (ImGui::BeginMenu("Rendering"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::Light) && ImGui::MenuItem("Light"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<LightComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::Camera) && ImGui::MenuItem("Camera"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<CameraComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::Renderer) && ImGui::MenuItem("Mesh Renderer"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<RendererComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::SpriteRenderer) && ImGui::MenuItem("Sprite Renderer"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<SpriteRendererComponent>(); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Physics"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::Rigidbody) && ImGui::MenuItem("Rigidbody"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<RigidbodyComponent>(); }
                if (ImGui::MenuItem("Box Collider"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::Box); }
                if (ImGui::MenuItem("Sphere Collider"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::Sphere); }
                if (ImGui::MenuItem("Mesh Collider"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<ColliderComponent>(ColliderComponent::MeshConvex); }
                if (!(m_currentObject->compMask & ComponentIndex::Rigidbody2D) && ImGui::MenuItem("Rigidbody 2D"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<Rigidbody2DComponent>(); }
                if (ImGui::MenuItem("Box Collider 2D"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<Collider2DComponent>(Collider2DComponent::Box); }
                if (ImGui::MenuItem("Circle Collider 2D"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<Collider2DComponent>(Collider2DComponent::Circle); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("UI"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::Canvas) && ImGui::MenuItem("Canvas"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<CanvasComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::RectTransform) && ImGui::MenuItem("Rect Transform"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<RectTransformComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::UIImage) && ImGui::MenuItem("Image"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<UIImageComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::UIText) && ImGui::MenuItem("Text"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<UITextComponent>(); }
                if (!(m_currentObject->compMask & ComponentIndex::UIButton) && ImGui::MenuItem("Button"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<UIButtonComponent>(); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Audio"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::AudioSource) && ImGui::MenuItem("Audio Source"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<AudioSourceComponent>(); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Animation"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::Animator) && ImGui::MenuItem("Animator"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<AnimatorComponent>(); }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Effects"))
            {
                if (!(m_currentObject->compMask & ComponentIndex::ParticleSystem) && ImGui::MenuItem("Particle System"))
                    { if (m_editor) m_editor->PushUndoSnapshot(); m_currentObject->AddComponent<ParticleSystemComponent>(); }
                ImGui::EndMenu();
            }

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
    m_previewCamera = std::make_unique<Camera>(glm::vec3(0, 2, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    m_previewCamera->SetPerspective(45.0f, 0.1f, 100.0f);

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

    DITTO_LOG_INFO_STREAM("[Preview] Loading: " << modelPath );
    DITTO_LOG_INFO_STREAM("[Preview] Current: " << m_currentPreviewPath );

    Ditto::IRenderer* r = PreviewRenderer(m_editor);
    if (!r) return;

    if (m_currentPreviewPath == modelPath && m_currentPreviewModel.mesh) {
        DITTO_LOG_INFO_STREAM("[Preview] Same model, skipping" );
        return;
    }

    if (m_currentPreviewModel.mesh) {
        r->DestroyMesh(m_currentPreviewModel.mesh);
        m_currentPreviewModel.mesh = {};
    }

    m_currentPreviewPath = modelPath;

    std::ifstream file(modelPath);
    if (!file.is_open()) {
        DITTO_LOG_ERROR_STREAM("[Preview] Failed to open model: " << modelPath );
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
        DITTO_LOG_ERROR_STREAM("[Preview] No vertices found in model" );
        return;
    }

    DITTO_LOG_INFO_STREAM("[Preview] Loaded " << positions.size() << " vertices, " << indices.size() / 3 << " faces" );

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
        m_previewCamera->LookAt(m_currentPreviewModel.center);
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
    glm::mat4 projection = m_previewCamera->GetProjectionMatrix((float)m_previewWidth / (float)m_previewHeight);
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
    m_previewRT = {};
    m_previewPipeline = {};
    m_currentPreviewModel.mesh = {};
    m_previewCamera.reset();
    m_previewInitialized = false;
}

