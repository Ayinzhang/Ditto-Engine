#include "ScriptFileWatcher.h"
#include "Logger.h"
#include <iostream>

ScriptFileWatcher& ScriptFileWatcher::GetInstance()
{
    static ScriptFileWatcher instance;
    return instance;
}

void ScriptFileWatcher::Initialize(const std::string& scriptsDirectory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) return;
    
    m_scriptsDirectory = scriptsDirectory;
    m_initialized = true;
    
    DITTO_LOG_INFO_STREAM("[ScriptFileWatcher] Initialized with directory: " << scriptsDirectory );
}

void ScriptFileWatcher::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_watchedScripts.clear();
    m_lastWriteTime.clear();
    m_pendingChanges.clear();
    m_initialized = false;
    
    DITTO_LOG_INFO_STREAM("[ScriptFileWatcher] Shutdown" );
}

void ScriptFileWatcher::SetOnFileChanged(FileChangedCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onFileChanged = callback;
}

void ScriptFileWatcher::Update()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ProcessFileChanges();
}

void ScriptFileWatcher::WatchScript(const std::string& scriptPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_watchedScripts.find(scriptPath) != m_watchedScripts.end()) return;
    
    m_watchedScripts.insert(scriptPath);
    m_lastWriteTime[scriptPath] = GetLastWriteTime(scriptPath);
    
    DITTO_LOG_INFO_STREAM("[ScriptFileWatcher] Now watching: " << scriptPath );
}

void ScriptFileWatcher::UnwatchScript(const std::string& scriptPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_watchedScripts.erase(scriptPath);
    m_lastWriteTime.erase(scriptPath);
    
    DITTO_LOG_INFO_STREAM("[ScriptFileWatcher] Stopped watching: " << scriptPath );
}

bool ScriptFileWatcher::HasFileChanged(const std::string& scriptPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_lastWriteTime.find(scriptPath);
    if (it == m_lastWriteTime.end()) return false;
    
    fs::file_time_type currentTime = GetLastWriteTime(scriptPath);
    return currentTime > it->second;
}

void ScriptFileWatcher::ProcessFileChanges()
{
    if (!m_onFileChanged) return;
    
    for (const auto& scriptPath : m_watchedScripts)
    {
        fs::file_time_type currentTime = GetLastWriteTime(scriptPath);
        
        auto it = m_lastWriteTime.find(scriptPath);
        if (it == m_lastWriteTime.end()) continue;
        
        if (currentTime > it->second)
        {
            DITTO_LOG_INFO_STREAM("[ScriptFileWatcher] File changed: " << scriptPath );
            m_pendingChanges.push_back(scriptPath);
            it->second = currentTime;
        }
    }
    
    for (const auto& scriptPath : m_pendingChanges)
    {
        m_onFileChanged(scriptPath);
    }
    m_pendingChanges.clear();
}

ScriptFileWatcher::~ScriptFileWatcher()
{
    Shutdown();
}

