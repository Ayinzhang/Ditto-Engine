#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace Ditto
{
    enum class ShaderStage { Vertex, Pixel };

    // Result of compiling one HLSL entry point. `spirv` feeds the Vulkan backend;
    // `glsl` (SPIRV-Cross output) feeds the OpenGL backend.
    struct CompiledShader
    {
        std::vector<uint32_t> spirv;
        std::string glsl;
        bool ok = false;
        std::string error;
    };
}
