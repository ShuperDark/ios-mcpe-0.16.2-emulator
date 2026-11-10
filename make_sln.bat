@echo off
rem Generates a Visual Studio solution for the emulator and opens it.
rem   out\build\vs-solution\iOS MCPE(v0.16.2) Emulator.slnx  (Visual Studio 2026)
rem   out\build\vs-solution\iOS MCPE(v0.16.2) Emulator.sln   (Visual Studio 2022)
rem The newest installed Visual Studio is used. Usage: make_sln.bat [noopen]
setlocal
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
py -m pip show ziglang >nul 2>&1 || py -m pip install ziglang || exit /b 1
cmake --preset vs-solution || exit /b 1
set SLN=out\build\vs-solution\iOS MCPE(v0.16.2) Emulator.slnx
if not exist "%SLN%" set SLN=out\build\vs-solution\iOS MCPE(v0.16.2) Emulator.sln
echo.
echo Solution: %SLN%
if /I not "%1"=="noopen" start "" "%SLN%"
