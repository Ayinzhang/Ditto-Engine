#include "ScriptFileWatcher.h"
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
    
    std::cout << "[ScriptFileWatcher] Initialized with directory: " << scriptsDirectory << std::endl;
}

void ScriptFileWatcher::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_watchedScripts.clear();
    m_lastWriteTime.clear();
    m_pendingChanges.clear();
    m_initialized = false;
    
    std::cout << "[ScriptFileWatcher] Shutdown" << std::endl;
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
    
    std::cout << "[ScriptFileWatcher] Now watching: " << scriptPath << std::endl;
}

void ScriptFileWatcher::UnwatchScript(const std::string& scriptPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_watchedScripts.erase(scriptPath);
    m_lastWriteTime.erase(scriptPath);
    
    std::cout << "[ScriptFileWatcher] Stopped watching: " << scriptPath << std::endl;
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
            std::cout << "[ScriptFileWatcher] File changed: " << scriptPath << std::endl;
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