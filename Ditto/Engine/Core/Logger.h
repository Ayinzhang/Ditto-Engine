#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstddef>

// ---------------------------------------------------------------------------
// Ditto Logger
//
// A small, thread-safe, leveled logging facility. Every message is mirrored to
// std::cout / std::cerr (so existing console-window-less runs are unchanged)
// AND appended to an in-memory ring buffer that the editor's Console tab reads.
//
// Physics runs on a worker-thread pool, so logging must be safe to call from
// any thread -- all access to the buffer is guarded by a mutex.
//
// Consecutive identical messages are collapsed into a single entry with a
// `count` (Unity-style), so a per-frame log doesn't flood the buffer.
// ---------------------------------------------------------------------------
namespace Ditto
{
    enum class LogLevel
    {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    struct LogEntry
    {
        LogLevel level;
        std::string message;
        int count;   // number of consecutive identical messages collapsed here
    };

    class Logger
    {
    public:
        static Logger& Get();

        void Log(LogLevel level, const std::string& message);

        void Info(const std::string& message)    { Log(LogLevel::Info, message); }
        void Warning(const std::string& message) { Log(LogLevel::Warning, message); }
        void Error(const std::string& message)   { Log(LogLevel::Error, message); }

        // Copy of the current entries for the UI to render (thread-safe snapshot).
        std::vector<LogEntry> Snapshot() const;

        void Clear();

        // Counts per level for the Console's filter badges.
        void GetCounts(int& info, int& warning, int& error) const;

        void SetMaxEntries(size_t n) { m_maxEntries = n; }

    private:
        Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        mutable std::mutex m_mutex;
        std::vector<LogEntry> m_entries;
        size_t m_maxEntries = 1000;
        int m_infoCount = 0, m_warningCount = 0, m_errorCount = 0;
    };
}

// Convenience macros mirroring Unity's Debug.Log family.
#define DITTO_LOG_INFO(msg)  ::Ditto::Logger::Get().Info(msg)
#define DITTO_LOG_WARN(msg)  ::Ditto::Logger::Get().Warning(msg)
#define DITTO_LOG_ERROR(msg) ::Ditto::Logger::Get().Error(msg)
