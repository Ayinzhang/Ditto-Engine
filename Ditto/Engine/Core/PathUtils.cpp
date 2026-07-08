#include "PathUtils.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#elif defined(__linux__)
#include <unistd.h>
#include <vector>
#endif

namespace fs = std::filesystem;

namespace PathUtils
{
    static fs::path ComputeExecutableDir()
    {
#ifdef _WIN32
        
        
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
            if (len == 0) break; 
            if (len < buffer.size())
            {
                buffer.resize(len);
                std::error_code ec;
                fs::path exePath(buffer);
                fs::path dir = exePath.parent_path();
                if (!dir.empty()) return dir;
                break;
            }
            buffer.resize(buffer.size() * 2); 
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buf(size + 1, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) == 0)
        {
            std::error_code ec;
            fs::path exePath = fs::canonical(buf.data(), ec);
            if (!ec) return exePath.parent_path();
        }
#elif defined(__linux__)
        std::error_code ec;
        fs::path exePath = fs::read_symlink("/proc/self/exe", ec);
        if (!ec && !exePath.empty()) return exePath.parent_path();
#endif
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        return ec ? fs::path(".") : cwd;
    }

    const fs::path& GetExecutableDir()
    {
        static const fs::path dir = ComputeExecutableDir();
        return dir;
    }

    fs::path FindAncestorContaining(const fs::path& start, const std::string& marker)
    {
        std::error_code ec;
        fs::path current = start;
        while (!current.empty())
        {
            if (fs::exists(current / marker, ec))
                return current;
            fs::path parent = current.parent_path();
            if (parent == current) break; 
            current = parent;
        }
        return {};
    }

    fs::path ResolveAsset(const std::string& relativeToAssets, const fs::path& preferredRoot)
    {
        std::error_code ec;
        const fs::path& exeDir = GetExecutableDir();

        std::vector<fs::path> candidates;
        if (!preferredRoot.empty())
        {
            candidates.push_back(preferredRoot / "Assets" / relativeToAssets);
            candidates.push_back(preferredRoot / relativeToAssets); 
        }

        
        
        fs::path dittoRoot = FindAncestorContaining(exeDir, "Ditto");
        if (!dittoRoot.empty())
            candidates.push_back(dittoRoot / "Ditto" / "Assets" / relativeToAssets);

        
        
        fs::path assetRoot = FindAncestorContaining(exeDir, "Assets");
        if (!assetRoot.empty())
            candidates.push_back(assetRoot / "Assets" / relativeToAssets);

        const fs::path exeAssets = exeDir / "Assets" / relativeToAssets;
        candidates.push_back(exeAssets);

        fs::path cwd = fs::current_path(ec);
        if (!ec)
            candidates.push_back(cwd / "Assets" / relativeToAssets);

        for (const auto& candidate : candidates)
        {
            if (fs::exists(candidate, ec))
                return candidate;
        }

        
        
        return exeAssets;
    }
}
