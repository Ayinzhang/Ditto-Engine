#include "AssetHealthWindow.h"
#include "Editor.h"
#include "../Engine/Resources/AssetDatabase.h"
#include "../Engine/Core/Logger.h"
#include "../3rdParty/ImGui/imgui.h"
#include <algorithm>

AssetHealthWindow::AssetHealthWindow(Editor* editor)
    : m_editor(editor)
{
}

void AssetHealthWindow::Draw()
{
    if (!m_isOpen)
        return;

    if (ImGui::Begin("Asset Health", &m_isOpen, ImGuiWindowFlags_MenuBar))
    {
        DrawToolbar();
        DrawSummary();

        
        if (ImGui::BeginTabBar("DiagnosticsTabs"))
        {
            if (ImGui::BeginTabItem("Missing .meta"))
            {
                DrawMissingMetaTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Invalid .meta"))
            {
                DrawInvalidMetaTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Duplicate GUID"))
            {
                DrawDuplicateGuidTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Missing References"))
            {
                DrawMissingReferencesTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void AssetHealthWindow::RefreshDiagnostics()
{
    auto& db = Ditto::AssetDatabase::Get();
    auto diagnostics = db.Diagnose();

    m_missingMeta = diagnostics.missingMeta;
    m_invalidMeta = diagnostics.invalidMeta;
    m_duplicateGuid = diagnostics.duplicateGuid;
    m_missingGuidReference = diagnostics.missingGuidReference;
}

void AssetHealthWindow::DrawToolbar()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::Button("Refresh"))
        {
            RefreshDiagnostics();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-refresh", &m_autoRefresh);

        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        if (ImGui::Button("Fix All Issues"))
        {
            FixAllMissingMeta();
            RefreshDiagnostics();
        }

        ImGui::EndMenuBar();
    }

    if (m_autoRefresh && ImGui::IsWindowFocused())
    {
        static float lastRefresh = 0.0f;
        float currentTime = ImGui::GetTime();
        if (currentTime - lastRefresh > 2.0f)
        {
            RefreshDiagnostics();
            lastRefresh = currentTime;
        }
    }
}

void AssetHealthWindow::DrawSummary()
{
    int totalIssues = m_missingMeta.size() + m_invalidMeta.size() +
                      m_duplicateGuid.size() + m_missingGuidReference.size();

    if (totalIssues == 0)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No issues found!");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Total issues: %d", totalIssues);
        ImGui::Text("  Missing .meta: %zu", m_missingMeta.size());
        ImGui::Text("  Invalid .meta: %zu", m_invalidMeta.size());
        ImGui::Text("  Duplicate GUID: %zu", m_duplicateGuid.size());
        ImGui::Text("  Missing references: %zu", m_missingGuidReference.size());
    }

    ImGui::Separator();
}

void AssetHealthWindow::DrawMissingMetaTab()
{
    ImGui::Text("Assets without .meta files:");
    ImGui::Text("These assets need .meta files with GUIDs for proper referencing.");
    ImGui::Separator();

    if (m_missingMeta.empty())
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No missing .meta files!");
        return;
    }

    if (ImGui::Button("Fix All"))
    {
        FixAllMissingMeta();
    }

    ImGui::Separator();

    for (size_t i = 0; i < m_missingMeta.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s", m_missingMeta[i].c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 80);
        if (ImGui::SmallButton("Fix"))
        {
            FixMissingMeta(m_missingMeta[i]);
            RefreshDiagnostics();
        }
        ImGui::PopID();
    }
}

void AssetHealthWindow::DrawInvalidMetaTab()
{
    ImGui::Text("Assets with invalid or corrupted .meta files:");
    ImGui::Separator();

    if (m_invalidMeta.empty())
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No invalid .meta files!");
        return;
    }

    for (const auto& path : m_invalidMeta)
    {
        ImGui::BulletText("%s", path.c_str());
    }
}

void AssetHealthWindow::DrawDuplicateGuidTab()
{
    ImGui::Text("Assets with duplicate GUIDs:");
    ImGui::Text("Each asset must have a unique GUID.");
    ImGui::Separator();

    if (m_duplicateGuid.empty())
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No duplicate GUIDs!");
        return;
    }

    for (size_t i = 0; i < m_duplicateGuid.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s", m_duplicateGuid[i].c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        if (ImGui::SmallButton("Regenerate GUID"))
        {
            RegenerateGuid(m_duplicateGuid[i]);
            RefreshDiagnostics();
        }
        ImGui::PopID();
    }
}

void AssetHealthWindow::DrawMissingReferencesTab()
{
    ImGui::Text("Assets referencing missing GUIDs:");
    ImGui::Separator();

    if (m_missingGuidReference.empty())
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No missing references!");
        return;
    }

    for (const auto& ref : m_missingGuidReference)
    {
        ImGui::BulletText("%s", ref.c_str());
    }
}

void AssetHealthWindow::FixMissingMeta(const std::string& assetPath)
{
    auto& db = Ditto::AssetDatabase::Get();
    if (db.CreateAssetMetaFile(assetPath))
    {
        DITTO_LOG_INFO_STREAM("[AssetHealth] Created .meta for: " << assetPath);
    }
    else
    {
        DITTO_LOG_ERROR_STREAM("[AssetHealth] Failed to create .meta for: " << assetPath);
    }
}

void AssetHealthWindow::FixAllMissingMeta()
{
    int fixed = 0;
    for (const auto& path : m_missingMeta)
    {
        if (Ditto::AssetDatabase::Get().CreateAssetMetaFile(path))
        {
            ++fixed;
        }
    }

    DITTO_LOG_INFO_STREAM("[AssetHealth] Fixed " << fixed << " missing .meta files");
    RefreshDiagnostics();
}

void AssetHealthWindow::FixDuplicateGuid(const std::string& assetPath)
{
    RegenerateGuid(assetPath);
}

void AssetHealthWindow::RegenerateGuid(const std::string& assetPath)
{
    auto& db = Ditto::AssetDatabase::Get();
    if (db.RegenerateGuid(assetPath))
    {
        DITTO_LOG_INFO_STREAM("[AssetHealth] Regenerated GUID for: " << assetPath);
    }
    else
    {
        DITTO_LOG_ERROR_STREAM("[AssetHealth] Failed to regenerate GUID for: " << assetPath);
    }
}
