#include "CSharpScriptCompiler.h"

#include "CSharpScript.h"

#include <windows.h>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::string FindDittoEngineDll()
    {
        const std::vector<std::string> possiblePaths = {
            "3rdParty/Mono/bin/Release/netstandard2.0/DittoEngine.dll",
            "Ditto/3rdParty/Mono/bin/Release/netstandard2.0/DittoEngine.dll",
            "../Ditto/3rdParty/Mono/bin/Release/netstandard2.0/DittoEngine.dll",
            "../../Ditto/3rdParty/Mono/bin/Release/netstandard2.0/DittoEngine.dll",
            "3rdParty/Mono/DittoEngine.dll",
            "../../3rdParty/Mono/DittoEngine.dll",
            "../3rdParty/Mono/DittoEngine.dll",
            "Ditto/3rdParty/Mono/DittoEngine.dll",
            "../Ditto/3rdParty/Mono/DittoEngine.dll",
            "../../Ditto/3rdParty/Mono/DittoEngine.dll",
        };

        for (const auto& path : possiblePaths)
        {
            if (fs::exists(path)) return fs::absolute(path).string();
        }

        return "3rdParty/Mono/DittoEngine.dll";
    }

    std::string FindMSBuildPath()
    {
        char* vsPathEnv = nullptr;
        size_t vsPathLen = 0;
        if (_dupenv_s(&vsPathEnv, &vsPathLen, "VSINSTALLDIR") == 0 && vsPathEnv)
        {
            std::string roslynPath = std::string(vsPathEnv) + "MSBuild\\Current\\Bin\\Roslyn";
            free(vsPathEnv);
            if (fs::exists(roslynPath))
                return roslynPath;
        }

        const char* regKey = "SOFTWARE\\Microsoft\\VisualStudio\\Setup\\Instances";
        HKEY hKey = nullptr;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            char name[MAX_PATH];
            DWORD index = 0;
            DWORD nameSize = MAX_PATH;

            while (RegEnumKeyExA(hKey, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
            {
                std::string instanceKey = std::string(regKey) + "\\" + name;
                HKEY hInstKey = nullptr;

                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, KEY_READ, &hInstKey) == ERROR_SUCCESS)
                {
                    char installPath[MAX_PATH];
                    DWORD size = MAX_PATH;
                    DWORD type = REG_SZ;

                    if (RegQueryValueExA(hInstKey, "InstallLocation", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS)
                    {
                        std::string roslynPath = std::string(installPath) + "MSBuild\\Current\\Bin\\Roslyn";
                        if (fs::exists(roslynPath))
                        {
                            RegCloseKey(hInstKey);
                            RegCloseKey(hKey);
                            return roslynPath;
                        }
                    }
                    RegCloseKey(hInstKey);
                }
                nameSize = MAX_PATH;
            }
            RegCloseKey(hKey);
        }

        char* pathEnv = nullptr;
        size_t pathLen = 0;
        if (_dupenv_s(&pathEnv, &pathLen, "PATH") == 0 && pathEnv)
        {
            std::string pathStr(pathEnv);
            free(pathEnv);

            size_t pos = 0;
            while ((pos = pathStr.find(';')) != std::string::npos)
            {
                std::string dir = pathStr.substr(0, pos);
                std::string cscPath = dir + "\\csc.exe";
                if (fs::exists(cscPath))
                {
                    size_t lastSlash = dir.find_last_of("\\/");
                    if (lastSlash != std::string::npos)
                    {
                        std::string roslynDir = dir.substr(0, lastSlash);
                        if (roslynDir.find("Roslyn") != std::string::npos)
                            return roslynDir;
                    }
                }
                pathStr.erase(0, pos + 1);
            }
        }

        const char* drives[] = { "D:", "C:", "E:", "F:" };
        for (const auto& drive : drives)
        {
            std::string vsBase = std::string(drive) + "\\Visual Studio 2022";
            if (fs::exists(vsBase))
            {
                std::string roslynPath = vsBase + "\\MSBuild\\Current\\Bin\\Roslyn";
                if (fs::exists(roslynPath))
                    return roslynPath;

                for (const auto& entry : fs::directory_iterator(vsBase))
                {
                    if (!entry.is_directory())
                        continue;
                    roslynPath = entry.path().string() + "\\MSBuild\\Current\\Bin\\Roslyn";
                    if (fs::exists(roslynPath))
                        return roslynPath;
                }
            }

            std::string progFiles = std::string(drive) + "\\Program Files\\Microsoft Visual Studio\\2022";
            if (fs::exists(progFiles))
            {
                for (const auto& entry : fs::directory_iterator(progFiles))
                {
                    if (!entry.is_directory())
                        continue;
                    std::string roslynPath = entry.path().string() + "\\MSBuild\\Current\\Bin\\Roslyn";
                    if (fs::exists(roslynPath))
                        return roslynPath;
                }
            }
        }

        return "";
    }

    void ParseCSharpDiagnostics(CSharpCompileResult& result)
    {
        static const std::regex pattern(R"((?:^|\r?\n)(?:(.+?)\((\d+),(\d+)\):\s*)?(warning|error)\s+(CS\d+):\s+([^\r\n]+))");
        for (std::sregex_iterator it(result.output.begin(), result.output.end(), pattern), end; it != end; ++it)
        {
            const std::smatch& match = *it;

            CSharpCompileResult::Diagnostic diagnostic;
            diagnostic.file = match[1].matched ? match[1].str() : result.scriptPath;
            diagnostic.line = match[2].matched ? std::stoi(match[2].str()) : 0;
            diagnostic.column = match[3].matched ? std::stoi(match[3].str()) : 0;
            diagnostic.severity = match[4].str();
            diagnostic.code = match[5].str();
            diagnostic.message = match[6].str();
            result.diagnostics.push_back(diagnostic);
        }
    }

    std::string FindNetStandardDll()
    {
        std::vector<fs::path> candidates = {
            fs::path("C:/Program Files/dotnet/sdk"),
            fs::path("C:/Program Files/dotnet/packs/NETStandard.Library.Ref"),
            fs::path("C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref"),
        };

        for (const fs::path& root : candidates)
        {
            std::error_code ec;
            if (!fs::exists(root, ec)) continue;
            for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (entry.is_regular_file(ec) && entry.path().filename() == "netstandard.dll")
                    return fs::absolute(entry.path()).string();
            }
        }
        return "";
    }
}

namespace Ditto::CSharpScriptCompiler
{
    CSharpCompileResult CompileDetailed(const std::string& csPath, std::string& outDllPath)
    {
        CSharpCompileResult compileResult;
        compileResult.scriptPath = csPath;
        if (!fs::exists(csPath))
        {
            compileResult.output = "Script file not found: " + csPath;
            compileResult.errorCount = 1;
            return compileResult;
        }

        fs::path scriptPath(csPath);
        fs::path absScriptPath = fs::absolute(scriptPath);
        if (outDllPath.empty())
        {
            fs::path projectRoot = absScriptPath.parent_path();
            for (fs::path current = absScriptPath.parent_path(); !current.empty() && current.has_parent_path(); current = current.parent_path())
            {
                if (current.filename() == "Assets")
                {
                    projectRoot = current.parent_path();
                    break;
                }
            }
            fs::path tempDir = projectRoot / "Temp";
            std::error_code ec;
            fs::create_directories(tempDir, ec);
            fs::path dllAbsPath = tempDir / (absScriptPath.stem().string() + ".dll");
            outDllPath = dllAbsPath.string();
        }
        compileResult.outputDllPath = outDllPath;

        std::string dittoEngineDll = FindDittoEngineDll();
        if (dittoEngineDll.empty())
        {
            compileResult.output = "DittoEngine.dll not found";
            compileResult.errorCount = 1;
            return compileResult;
        }

        std::string monoMscorlib;
        {
            const std::vector<std::string> mscorlibPaths = {
                "3rdParty/Mono/mscorlib.dll",
                "../../3rdParty/Mono/mscorlib.dll",
                "../3rdParty/Mono/mscorlib.dll",
                "Ditto/3rdParty/Mono/mscorlib.dll",
                "../Ditto/3rdParty/Mono/mscorlib.dll",
                "../../Ditto/3rdParty/Mono/mscorlib.dll",
            };
            for (const auto& path : mscorlibPaths)
            {
                if (fs::exists(path))
                {
                    monoMscorlib = fs::absolute(path).string();
                    break;
                }
            }
        }

        if (monoMscorlib.empty())
        {
            compileResult.output = "Mono mscorlib.dll not found";
            compileResult.errorCount = 1;
            return compileResult;
        }

        std::string netstandardDll = FindNetStandardDll();
        if (netstandardDll.empty())
        {
            compileResult.output = "netstandard.dll not found";
            compileResult.errorCount = 1;
            return compileResult;
        }

        std::string roslynPath = FindMSBuildPath();
        if (roslynPath.empty())
        {
            compileResult.output = "Roslyn compiler path not found";
            compileResult.errorCount = 1;
            return compileResult;
        }

        std::string cscPath = roslynPath + "\\csc.exe";
        if (!fs::exists(cscPath))
        {
            compileResult.output = "csc.exe not found: " + cscPath;
            compileResult.errorCount = 1;
            return compileResult;
        }

        if (fs::exists(outDllPath))
        {
            try { fs::remove(outDllPath); }
            catch (const fs::filesystem_error&)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                try { fs::remove(outDllPath); } catch (...) {}
            }
        }

        fs::path logPath = fs::temp_directory_path() / (absScriptPath.stem().string() + "_csc.log");
        std::string cmd = "cmd /c \"\"" + cscPath + "\""
            + " /target:library"
            + " /nostdlib+"
            + " /reference:\"" + monoMscorlib + "\""
            + " /reference:\"" + netstandardDll + "\""
            + " /reference:\"" + dittoEngineDll + "\""
            + " /out:\"" + outDllPath + "\""
            + " \"" + absScriptPath.string() + "\""
            + " >\"" + logPath.string() + "\" 2>&1\"";

        int result = system(cmd.c_str());
        {
            std::ifstream log(logPath, std::ios::binary);
            if (log)
                compileResult.output.assign(std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>());
            std::error_code ec;
            fs::remove(logPath, ec);
        }

        ParseCSharpDiagnostics(compileResult);
        for (const auto& diagnostic : compileResult.diagnostics)
        {
            if (diagnostic.severity == "warning")
                ++compileResult.warningCount;
            else if (diagnostic.severity == "error")
                ++compileResult.errorCount;
        }
        if (compileResult.diagnostics.empty())
        {
            size_t pos = 0;
            while ((pos = compileResult.output.find("warning CS", pos)) != std::string::npos)
            {
                ++compileResult.warningCount;
                pos += 10;
            }
            pos = 0;
            while ((pos = compileResult.output.find("error CS", pos)) != std::string::npos)
            {
                ++compileResult.errorCount;
                pos += 8;
            }
        }

        compileResult.ok = result == 0 && fs::exists(outDllPath);
        if (!compileResult.ok && compileResult.errorCount == 0)
            compileResult.errorCount = 1;

        return compileResult;
    }
}
