#include "ShaderCompiler.h"
#include "../../Core/Logger.h"
#include "../../Core/PathUtils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdint>

namespace fs = std::filesystem;

namespace Ditto
{
    
    
    
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

    
    
    
    
    
    
    

    
    
    
    static constexpr uint32_t kCacheVersion = 2;

    static uint64_t Fnv1a64(const void* data, size_t len, uint64_t h = 14695981039346656037ull)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    static fs::path CacheDir()
    {
        return PathUtils::GetExecutableDir() / "ShaderCache";
    }

    static fs::path CachePath(uint64_t key, const char* ext)
    {
        char name[32];
        snprintf(name, sizeof(name), "%016llx", (unsigned long long)key);
        return CacheDir() / (std::string(name) + ext);
    }

    static uint64_t CacheKey(const std::string& hlslSource, ShaderStage stage, const std::string& entryPoint)
    {
        uint64_t h = Fnv1a64(&kCacheVersion, sizeof(kCacheVersion));
        const int s = (stage == ShaderStage::Vertex) ? 0 : 1;
        h = Fnv1a64(&s, sizeof(s), h);
        h = Fnv1a64(entryPoint.data(), entryPoint.size(), h);
        h = Fnv1a64(hlslSource.data(), hlslSource.size(), h);
        return h;
    }

    
    
    static bool TryLoadCached(uint64_t key, bool needGLSL, CompiledShader& out)
    {
        std::error_code ec;
        const fs::path spvP = CachePath(key, ".spv");
        if (!fs::exists(spvP, ec)) return false;

        std::vector<uint32_t> spirv = ReadU32(spvP);
        if (spirv.empty()) return false;

        std::string glsl;
        if (needGLSL)
        {
            const fs::path glslP = CachePath(key, ".glsl");
            if (!fs::exists(glslP, ec)) return false;
            glsl = ReadText(glslP);
            if (glsl.empty()) return false;
        }

        out.spirv = std::move(spirv);
        out.glsl = std::move(glsl);
        out.ok = true;
        return true;
    }

    static void StoreCached(uint64_t key, const CompiledShader& sh)
    {
        std::error_code ec;
        fs::create_directories(CacheDir(), ec);
        if (ec) return;   

        {
            std::ofstream f(CachePath(key, ".spv"), std::ios::binary | std::ios::trunc);
            if (f) f.write(reinterpret_cast<const char*>(sh.spirv.data()), sh.spirv.size() * sizeof(uint32_t));
        }
        if (!sh.glsl.empty())
        {
            std::ofstream f(CachePath(key, ".glsl"), std::ios::binary | std::ios::trunc);
            if (f) f << sh.glsl;
        }
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

        
        
        const uint64_t key = CacheKey(hlslSource, stage, entryPoint);
        if (TryLoadCached(key, generateGLSL, out))
            return out;

        const std::string bin = SdkBinDir();
        if (!ToolsAvailable())
        {
            out.error = "Shader not in cache and Vulkan SDK shader tools not found in " + bin;
            Logger::Get().Error("[ShaderCompiler] " + out.error);
            return out;
        }

        
        
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

        
        
        
        
        auto run = [](const std::string& cmd) { return std::system(("\"" + cmd + "\"").c_str()); };

        
        
        
        
        
        std::string dxc = "\"" + bin + "\\dxc.exe\" -spirv -T " + profile + " -E " + entryPoint +
            " -fspv-entrypoint-name=main -Zpc \"" + hlslP.string() + "\" -Fo \"" + spvP.string() + "\" 2>\"" + errP.string() + "\"";
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
            
            
            
            
            
            std::string sc = "\"" + bin + "\\spirv-cross.exe\" \"" + spvP.string() +
                "\" --version 460 --no-es --combined-samplers-inherit-bindings --output \"" + glslP.string() + "\" 2>\"" + errP.string() + "\"";
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
        StoreCached(key, out);
        cleanup();
        return out;
    }
}
