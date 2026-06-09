#include "ShaderCompiler.h"
#include "../../Core/Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace Ditto
{
    // Locate the Vulkan SDK Bin dir (dxc.exe + spirv-cross.exe live there).
    // Prefer the VULKAN_SDK env var; fall back to the known install path until
    // the env var propagates to new processes (a reboot picks it up).
    static std::string SdkBinDir()
    {
        char* env = nullptr; size_t len = 0;
        if (_dupenv_s(&env, &len, "VULKAN_SDK") == 0 && env)
        {
            std::string s = env; free(env);
            return s + "\\Bin";
        }
        return "C:\\VulkanSDK\\1.4.350.0\\Bin";
    }

    static std::string ReadText(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
    }

    static std::vector<uint32_t> ReadU32(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) return {};
        std::streamsize sz = f.tellg();
        if (sz <= 0) return {};
        f.seekg(0);
        std::vector<uint32_t> data((size_t)sz / 4);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    }

    bool ShaderCompiler::ToolsAvailable()
    {
        const std::string bin = SdkBinDir();
        return fs::exists(bin + "\\dxc.exe") && fs::exists(bin + "\\spirv-cross.exe");
    }

    CompiledShader ShaderCompiler::Compile(const std::string& hlslSource, ShaderStage stage,
                                           const std::string& entryPoint, bool generateGLSL)
    {
        CompiledShader out;

        const std::string bin = SdkBinDir();
        if (!ToolsAvailable())
        {
            out.error = "Vulkan SDK shader tools not found in " + bin;
            Logger::Get().Error("[ShaderCompiler] " + out.error);
            return out;
        }

        // Unique temp file set (counter avoids needing Date/random, which are
        // unavailable here, and keeps names stable per process).
        static int s_counter = 0;
        const std::string id = std::to_string(s_counter++);
        const fs::path tmp     = fs::temp_directory_path();
        const fs::path hlslP   = tmp / ("ditto_sh_" + id + ".hlsl");
        const fs::path spvP    = tmp / ("ditto_sh_" + id + ".spv");
        const fs::path glslP   = tmp / ("ditto_sh_" + id + ".glsl");
        const fs::path errP    = tmp / ("ditto_sh_" + id + ".err");

        { std::ofstream f(hlslP, std::ios::binary); f << hlslSource; }

        auto cleanup = [&]()
        {
            std::error_code ec;
            fs::remove(hlslP, ec); fs::remove(spvP, ec);
            fs::remove(glslP, ec); fs::remove(errP, ec);
        };

        const std::string profile = (stage == ShaderStage::Vertex) ? "vs_6_0" : "ps_6_0";

        // NOTE: system() runs `cmd /c <command>`, and cmd strips the outer pair of
        // quotes when the command both starts and ends with one. Our commands do
        // (quoted exe path ... quoted redirect), so wrap the WHOLE command in an
        // extra pair of quotes -- cmd strips those and leaves the inner quoting intact.
        auto run = [](const std::string& cmd) { return std::system(("\"" + cmd + "\"").c_str()); };

        // HLSL -> SPIR-V. -Zpc packs matrices column-major; explicit [[vk::binding]]
        // in the HLSL pins set/binding so no shift flags are needed.
        std::string dxc = "\"" + bin + "\\dxc.exe\" -spirv -T " + profile + " -E " + entryPoint +
            " -Zpc \"" + hlslP.string() + "\" -Fo \"" + spvP.string() + "\" 2>\"" + errP.string() + "\"";
        if (run(dxc) != 0)
        {
            out.error = "DXC failed: " + ReadText(errP);
            Logger::Get().Error("[ShaderCompiler] " + out.error);
            cleanup();
            return out;
        }
        out.spirv = ReadU32(spvP);
        if (out.spirv.empty())
        {
            out.error = "DXC produced no SPIR-V";
            Logger::Get().Error("[ShaderCompiler] " + out.error);
            cleanup();
            return out;
        }

        if (generateGLSL)
        {
            // SPIR-V -> desktop GLSL 460 for the OpenGL backend.
            std::string sc = "\"" + bin + "\\spirv-cross.exe\" \"" + spvP.string() +
                "\" --version 460 --no-es --output \"" + glslP.string() + "\" 2>\"" + errP.string() + "\"";
            if (run(sc) != 0)
            {
                out.error = "spirv-cross failed: " + ReadText(errP);
                Logger::Get().Error("[ShaderCompiler] " + out.error);
                cleanup();
                return out;
            }
            out.glsl = ReadText(glslP);
            if (out.glsl.empty())
            {
                out.error = "spirv-cross produced no GLSL";
                Logger::Get().Error("[ShaderCompiler] " + out.error);
                cleanup();
                return out;
            }
        }

        out.ok = true;
        cleanup();
        return out;
    }
}
