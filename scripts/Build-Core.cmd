@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SC6_VS=%%i"
if not defined SC6_VS exit /b 1
call "%SC6_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "SC6_PROJECT=%~dp0.."
set "SC6_BUILD=%~1"
if not defined SC6_BUILD set "SC6_BUILD=%SC6_PROJECT%\build-core"
if not exist "%SC6_BUILD%" mkdir "%SC6_BUILD%"
if errorlevel 1 exit /b 1
for %%T in (ContractTest SessionPolicyTest) do (
    cl /nologo /std:c++20 /EHsc /W4 /WX /I "%SC6_PROJECT%\include" "%SC6_PROJECT%\tests\%%T.cpp" /Fo:"%SC6_BUILD%\%%T.obj" /Fe:"%SC6_BUILD%\%%T.exe"
    if errorlevel 1 exit /b 1
    "%SC6_BUILD%\%%T.exe"
    if errorlevel 1 exit /b 1
)
