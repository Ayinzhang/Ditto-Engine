@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Projects\Ditto-Engine"
MSBuild "Ditto\Ditto.vcxproj" -p:Configuration=Debug -p:Platform=x64 -t:Rebuild -nologo -v:m
