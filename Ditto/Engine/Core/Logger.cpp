#include "Logger.h"
#include <iostream>

namespace Ditto
{
    Logger& Logger::Get()
    {
        static Logger instance;
        return instance;
    }

    void Logger::Log(LogLevel level, const std::string& message)
    {
        // Mirror to the standard streams first so headless / game-mode runs and
        // existing redirection keep working exactly as before.
        if (level == LogLevel::Error)
            std::cerr << message << std::endl;
        else
            std::cout << message << std::endl;

        std::lock_guard<std::mutex> lock(m_mutex);

        // Collapse a run of identical consecutive messages into one entry.
        if (!m_entries.empty())
        {
            LogEntry& last = m_entries.back();
            if (last.level == level && last.message == message)
            {
                ++last.count;
                return;
            }
        }

        m_entries.push_back(LogEntry{ level, message, 1 });

        switch (level)
        {
            case LogLevel::Info:    ++m_infoCount;    break;
            case LogLevel::Warning: ++m_warningCount; break;
            case LogLevel::Error:   ++m_errorCount;   break;
        }

        // Ring-buffer trim: drop the oldest entries past the cap.
        if (m_entries.size() > m_maxEntries)
        {
            size_t overflow = m_entries.size() - m_maxEntries;
            for (size_t i = 0; i < overflow; ++i)
            {
                switch (m_entries[i].level)
                {
                    case LogLevel::Info:    if (m_infoCount > 0)    --m_infoCount;    break;
                    case LogLevel::Warning: if (m_warningCount > 0) --m_warningCount; break;
                    case LogLevel::Error:   if (m_errorCount > 0)   --m_errorCount;   break;
                }
            }
            m_entries.erase(m_entries.begin(), m_entries.begin() + overflow);
        }
    }

    std::vector<LogEntry> Logger::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries;
    }

    void Logger::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
        m_infoCount = m_warningCount = m_errorCount = 0;
    }

    void Logger::GetCounts(int& info, int& warning, int& error) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        info = m_infoCount;
        warning = m_warningCount;
        error = m_errorCount;
    }
}
