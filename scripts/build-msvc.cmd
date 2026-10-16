@echo off
rem Configure and build with the MSVC Build Tools and their bundled CMake/Ninja.
rem Usage: scripts\build-msvc.cmd [x86|x64] [Release|Debug] [extra cmake args...]
setlocal

rem Resolve the repository root before shift, which also moves %0.
set ROOT=%~dp0..

set ARCH=%1
if "%ARCH%"=="" set ARCH=x86
set CONFIG=%2
if "%CONFIG%"=="" set CONFIG=Release
shift
shift
set EXTRA=
:collect
if "%~1"=="" goto collected
set EXTRA=%EXTRA% %1
shift
goto collect
:collected

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VSROOT=%%i
if "%VSROOT%"=="" (
    echo Could not locate a Visual Studio installation.
    exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul 2>&1
if errorlevel 1 exit /b 1

set CMROOT=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake
set PATH=%CMROOT%\CMake\bin;%CMROOT%\Ninja;%PATH%

set BUILD=%ROOT%\build\%ARCH%-%CONFIG%

cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% %EXTRA%
if errorlevel 1 exit /b 1
cmake --build "%BUILD%"
exit /b %errorlevel%
