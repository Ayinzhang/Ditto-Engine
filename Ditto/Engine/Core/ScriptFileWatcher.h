#pragma once
#include <string>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <mutex>

namespace fs = std::filesystem;

class ScriptFileWatcher
{
public:
    using FileChangedCallback = std::function<void(const std::string& scriptPath)>;

    static ScriptFileWatcher& GetInstance();

    void Initialize(const std::string& scriptsDirectory);
    void Shutdown();

    void SetOnFileChanged(FileChangedCallback callback);

    void Update();

    void WatchScript(const std::string& scriptPath);
    void UnwatchScript(const std::string& scriptPath);

    bool HasFileChanged(const std::string& scriptPath);

private:
    ScriptFileWatcher() = default;
    ~ScriptFileWatcher();

    ScriptFileWatcher(const ScriptFileWatcher&) = delete;
    ScriptFileWatcher& operator=(const ScriptFileWatcher&) = delete;

    void ProcessFileChanges();

    FileChangedCallback m_onFileChanged;
    std::unordered_set<std::string> m_watchedScripts;
    std::unordered_map<std::string, fs::file_time_type> m_lastWriteTime;
    std::vector<std::string> m_pendingChanges;
    std::mutex m_mutex;
    bool m_initialized = false;
    std::string m_scriptsDirectory;
};

inline fs::file_time_type GetLastWriteTime(const std::string& path)
{
    try {
        return fs::last_write_time(path);
    } catch (const fs::filesystem_error&) {
        return fs::file_time_type::min();
    }
}