@echo off
rem Launches the OpenFAKK2 build against the retail game data.
rem   run.cmd                 the main menu, windowed at 1024x768
rem   run.cmd training        straight into a map
rem   run.cmd +set r_fullscreen 1 ...   any other arguments are passed through
rem Config, saves and screenshots go to build\sandbox\home\fakk.

setlocal
set "REPO=%~dp0"
set "GAME=C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2"
set "EXE=%REPO%build\x86-Release\Release\openfakk2.exe"

if not exist "%EXE%" (
    echo %EXE% is not built. Run scripts\build-msvc.cmd x86 Release first.
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

cd /d "%REPO%build\x86-Release\Release"
start "" "%EXE%" +set fs_basepath "%GAME%" +set fs_homepath "%REPO%build\sandbox\home" +set com_abnormalExitDialog 0 +set r_fullscreen 0 +set r_mode 6 +set s_initsound 1 +set s_useOpenAL 1 %MAP% %1 %2 %3 %4 %5 %6 %7 %8 %9
