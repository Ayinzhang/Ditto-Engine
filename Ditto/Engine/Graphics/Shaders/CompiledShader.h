#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace Ditto
{
    enum class ShaderStage { Vertex, Pixel };

    
    
    struct CompiledShader
    {
        std::vector<uint32_t> spirv;
        std::string glsl;
        bool ok = false;
        std::string error;
    };
}
