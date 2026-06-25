#pragma once

#include <string>
#include <vector>

struct Editor;

// Shows asset database diagnostics and repair tools.
class AssetHealthWindow
{
public:
    explicit AssetHealthWindow(Editor* editor);

    void Draw();
    void RefreshDiagnostics();
    void Open() { m_isOpen = true; }
    void Close() { m_isOpen = false; }

private:
    Editor* m_editor;
    bool m_isOpen = false;

    std::vector<std::string> m_missingMeta;
    std::vector<std::string> m_invalidMeta;
    std::vector<std::string> m_duplicateGuid;
    std::vector<std::string> m_missingGuidReference;

    bool m_autoRefresh = false;
    int m_selectedTab = 0;

    void DrawToolbar();
    void DrawMissingMetaTab();
    void DrawInvalidMetaTab();
    void DrawDuplicateGuidTab();
    void DrawMissingReferencesTab();
    void DrawSummary();

    void FixMissingMeta(const std::string& assetPath);
    void FixAllMissingMeta();
    void FixDuplicateGuid(const std::string& assetPath);
    void RegenerateGuid(const std::string& assetPath);
};
