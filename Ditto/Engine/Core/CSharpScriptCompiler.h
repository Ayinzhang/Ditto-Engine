#pragma once

#include <string>

struct CSharpCompileResult;

namespace Ditto::CSharpScriptCompiler
{
    CSharpCompileResult CompileDetailed(const std::string& csPath, std::string& outDllPath);
}
