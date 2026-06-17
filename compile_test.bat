@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >/dev/null 2>&1
msbuild Ditto.sln /p:Configuration=Debug /p:Platform=x64 /t:Editor /m /v:minimal
