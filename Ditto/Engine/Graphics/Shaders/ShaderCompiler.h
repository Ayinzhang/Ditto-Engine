#pragma once
#include "CompiledShader.h"
#include <string>

namespace Ditto
{
    
    
    
    
    
    
    
    class ShaderCompiler
    {
    public:
        
        
        static CompiledShader Compile(const std::string& hlslSource, ShaderStage stage,
                                      const std::string& entryPoint, bool generateGLSL);

        
        static bool ToolsAvailable();
    };
}
