@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\Projects\Ditto-Engine
msbuild Ditto.sln -t:Build -p:Configuration=Debug -p:Platform=x64 -m -nologo -clp:ErrorsOnly -v:q
echo BUILD_EXIT=%errorlevel%
