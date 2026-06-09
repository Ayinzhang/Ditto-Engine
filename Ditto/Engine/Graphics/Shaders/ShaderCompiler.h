#pragma once
#include "CompiledShader.h"
#include <string>

namespace Ditto
{
    // Compiles HLSL -> SPIR-V (DXC) and SPIR-V -> GLSL (SPIRV-Cross) by invoking
    // the Vulkan SDK's dxc.exe / spirv-cross.exe (subprocess). One HLSL source
    // drives both render backends: Vulkan consumes SPIR-V, OpenGL consumes the
    // cross-compiled GLSL. Built-in shaders are compiled once at startup.
    //
    // Subprocess (vs linking) avoids a CRT mismatch: the SDK only ships release
    // SPIRV-Cross libs, which won't link into a Debug (/MDd) build.
    class ShaderCompiler
    {
    public:
        // Compile `hlslSource` at entry point `entryPoint` for `stage`. When
        // `generateGLSL` is true, also cross-compile the SPIR-V to GLSL 460.
        static CompiledShader Compile(const std::string& hlslSource, ShaderStage stage,
                                      const std::string& entryPoint, bool generateGLSL);

        // True if the SDK shader tools (dxc.exe / spirv-cross.exe) were found.
        static bool ToolsAvailable();
    };
}
