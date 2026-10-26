@echo off
rem Build with Visual Studio (MSVC). Run from any prompt; vcvars is located automatically.
rem Output is always build\QuickLinks.exe (one copy only). If that file is in use (the widget is
rem running), the build stops with exit code 2 instead of writing somewhere else.
setlocal
cd /d "%~dp0"
if exist build\QuickLinks.exe (
    2>nul ( >>build\QuickLinks.exe call ) || (
        echo [LOCKED] build\QuickLinks.exe is in use. Close QuickLinks first, then build again.
        exit /b 2
    )
)
where cl >nul 2>nul && goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto nomsvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto nomsvc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || goto nomsvc

:build
if not exist build\obj mkdir build\obj
rc /nologo /fo build\obj\app.res app.rc || exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\obj\ /Febuild\QuickLinks.exe main.cpp links.cpp shell.cpp build\obj\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO || exit /b 1
echo.
echo Built: %~dp0build\QuickLinks.exe
exit /b 0

:nomsvc
echo [ERROR] MSVC not found. Install "Visual Studio Build Tools" with the C++ workload.
exit /b 1
