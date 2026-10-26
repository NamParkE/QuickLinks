@echo off
rem Builds spike\mica_spike.cpp into build\mica_spike.exe (MSVC). A test build: delete it
rem (build\mica_spike.exe, build\mica_spike.obj) once the check is done.
setlocal
cd /d "%~dp0.."
where cl >nul 2>nul && goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto nomsvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto nomsvc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul || goto nomsvc

:build
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Febuild\mica_spike.exe spike\mica_spike.cpp /link /SUBSYSTEM:WINDOWS shell32.lib || exit /b 1
echo Built: %CD%\build\mica_spike.exe
exit /b 0

:nomsvc
echo [ERROR] MSVC not found.
exit /b 1
