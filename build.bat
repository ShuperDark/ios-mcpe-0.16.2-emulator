@echo off
rem Builds the guest runtime libraries (zig) and the host emulator (MSVC + CMake/Ninja).
rem Usage: build.bat [debug|release]
setlocal
set CONFIG=x64-release
if /I "%1"=="debug" set CONFIG=x64-debug
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
py -m pip show ziglang >nul 2>&1 || py -m pip install ziglang || exit /b 1
py guest\build.py || exit /b 1
if not exist "out\build\%CONFIG%\build.ninja" cmake --preset %CONFIG% || exit /b 1
cmake --build "out\build\%CONFIG%" || exit /b 1
echo.
echo Build OK: out\build\%CONFIG%\iOS MCPE(v0.16.2) Emulator\iOSMCPE-Emulator.exe
