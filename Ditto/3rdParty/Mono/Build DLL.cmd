@echo off
for /f "tokens=*" %%i in ('where /r "C:\Program Files\Microsoft Visual Studio" csc.exe 2^>nul') do (
    set "CSC=%%i"
    goto :found
)
for /f "tokens=*" %%i in ('where /r "C:\Program Files (x86)\Microsoft Visual Studio" csc.exe 2^>nul') do (
    set "CSC=%%i"
    goto :found
)
for /f "tokens=*" %%i in ('where /r "%VSINSTALLDIR%..\.." csc.exe 2^>nul') do (
    set "CSC=%%i"
    goto :found
)
for /f "tokens=*" %%i in ('where csc.exe 2^>nul') do (
    set "CSC=%%i"
    goto :found
)
echo Error: csc.exe not found
pause
exit /b 1

:found
echo Using CSC: %CSC%
"%CSC%" /target:library /nostdlib+ /reference:mscorlib.dll /out:DittoEngine.dll DittoEngine.cs
pause