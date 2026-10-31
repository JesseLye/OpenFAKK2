@echo off
rem Launches the OpenFAKK2 build against the retail game data.
rem   run.cmd                 the main menu, windowed at 1024x768
rem   run.cmd training        straight into a map
rem   run.cmd x64             the 64-bit build (build\x64-Release); x86 is the default
rem   run.cmd x64 training    the build comes first, then the map
rem   run.cmd +set r_fullscreen 1 ...   any other arguments are passed through
rem FAKK2_ARCH=x64 in the environment picks the build as well.
rem Config, saves and screenshots go to build\sandbox\home\fakk.

setlocal
set "REPO=%~dp0"
set "GAME=C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2"
if not defined FAKK2_ARCH set "FAKK2_ARCH=x86"
set "ARCH=%FAKK2_ARCH%"
if /i "%~1"=="x64" (set "ARCH=x64" & shift)
if /i "%~1"=="x86" (set "ARCH=x86" & shift)
set "EXE=%REPO%build\%ARCH%-Release\Release\openfakk2.exe"

if not exist "%EXE%" (
    echo %EXE% is not built. Run scripts\build-msvc.cmd %ARCH% Release first.
    pause
    exit /b 1
)
if not exist "%GAME%\fakk\pak0.pk3" (
    echo The retail game was not found at "%GAME%". Edit GAME in this file.
    pause
    exit /b 1
)

set "MAP="
set "ARG1=%~1"
if defined ARG1 if not "%ARG1:~0,1%"=="+" (
    set "MAP=+devmap %ARG1%"
    shift
)

cd /d "%REPO%build\%ARCH%-Release\Release"
start "" "%EXE%" +set fs_basepath "%GAME%" +set fs_homepath "%REPO%build\sandbox\home" +set com_abnormalExitDialog 0 +set r_fullscreen 0 +set r_mode 6 +set s_initsound 1 +set s_useOpenAL 1 %MAP% %1 %2 %3 %4 %5 %6 %7 %8 %9
