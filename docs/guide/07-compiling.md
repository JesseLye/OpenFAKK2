# Building from source

## Windows (the supported platform)

You need **Visual Studio 2022 or its Build Tools** (or later) with the *Desktop development with C++*
workload (the MSVC compiler and a Windows SDK). It brings CMake and Ninja with it; nothing
else needs installing. The libraries OpenFAKK2 uses are in the repository
(`code/thirdparty`).

From the repository's folder:

    scripts\build-msvc.cmd x86 Release

The script finds Visual Studio, sets up its compiler for the architecture, and runs CMake
and Ninja. The result is in `build\x86-Release\Release`
([what's in it](01-installation.md#getting-openfakk2)). `Debug` in place of `Release` makes
a debug build in `build\x86-Debug`. Anything after the first two arguments goes to CMake.

**32-bit or 64-bit.** `x86` is the build the game shipped as and the one most checked
against retail; it is also the only one that can run Ritual's original `gamex86.dll` and
`cgamex86.dll`, which is how a bug is pinned on the engine or on the game code. `x64`
builds the same code as 64-bit (`build\x64-Release`, modules `gamex86_64.dll` and
`cgamex86_64.dll`); it plays the same, and reads and writes the same save files and path
files as the 32-bit build and the original game. What Ritual's code assumed about
32-bit pointers, and how it was settled, is under "The x64 build" in
[known-differences.md](../known-differences.md).

To build without the script, from an *x86 Native Tools Command Prompt for VS 2022*:

    cmake -S . -B build\x86-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build\x86-Release

The build also writes linker maps (`*.map`) beside the binaries, which
`tools\crash-symbols.py` uses to name the functions in a crash report.

## Linux and macOS

Not supported yet. The engine is ioquake3's and keeps its Linux and macOS code, but
Ritual's game code has not been built there, and is expected to need work: besides the
32-bit assumptions above, it uses `long` (64-bit on Linux and macOS), the C library's
`rand()` (a different sequence on each platform, so levels would play out differently
from the original), and a little x86 assembly.

## Checking a change

The project checks its own changes against the original game with a test suite
(`tools\suite\run-suite.ps1`): see [How OpenFAKK2 is made and checked](08-development.md).
