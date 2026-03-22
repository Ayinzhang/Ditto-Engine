@echo off
cd /d "E:\Engine Source\Ditto\Ditto\Projects\MyProject"
call "D:\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat"
cl /LD /EHsc /std:c++latest /I"E:\Engine Source\Ditto\Ditto\3rdParty\GLM" /I"E:\Engine Source\Ditto\Ditto\3rdParty\GLFW\include" /I"E:\Engine Source\Ditto\Ditto\3rdParty\ImGui" /I"E:\Engine Source\Ditto\Ditto\Engine\Core" /I"E:\Engine Source\Ditto\Ditto\Engine\Graphics" /I"E:\Engine Source\Ditto\Ditto\Engine\Physics" /I"E:\Engine Source\Ditto\Ditto\3rdParty\GLFW" /I"E:\Engine Source\Ditto\Ditto" /D"SCRIPT_DLL" /O2 /MD "E:\Engine Source\Ditto\Ditto\Projects\MyProject\Assets\Scripts\NewScript.cpp" /Fe:"E:\Engine Source\Ditto\Ditto\Projects\MyProject\Scripts.dll"
pause
