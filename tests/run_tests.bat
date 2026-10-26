@echo off
rem Builds and runs the model tests (links.cpp), then deletes the test build (build\tests).
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
if not exist build\tests mkdir build\tests
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\tests\ /Febuild\tests\links_test.exe tests\links_test.cpp links.cpp user32.lib || (rmdir /s /q build\tests & exit /b 1)
build\tests\links_test.exe
set "RESULT=%errorlevel%"
rmdir /s /q build\tests
exit /b %RESULT%

:nomsvc
echo [ERROR] MSVC not found.
exit /b 1
