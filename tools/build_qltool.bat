@echo off
rem Builds tools\qltool.cpp into build\qltool.exe (MSVC).
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
if not exist build\obj mkdir build\obj
cl /nologo /EHsc /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /Fobuild\obj\ /Febuild\qltool.exe tools\qltool.cpp || exit /b 1
echo Built: %CD%\build\qltool.exe
exit /b 0

:nomsvc
echo [ERROR] MSVC not found.
exit /b 1
