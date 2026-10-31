# Installing OpenFAKK2

OpenFAKK2 is an engine. It brings none of the game: you need the original *Heavy Metal:
F.A.K.K. 2*, patched to version 1.02.

## What you need

- **The retail game, version 1.02.** OpenFAKK2 reads its data from your install
  (`fakk\pak0.pk3`, `fakk\pak1.pk3` and the rest), so an installed copy is enough; nothing
  is copied or changed. The default install folder is
  `C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2`. OpenFAKK2 has no disc
  check of its own.
- **Windows 10 or 11.** Windows is the only platform for now, and the build is 32-bit (it
  runs on 64-bit Windows). See [Building from source](07-compiling.md) for why.
- **A graphics card with OpenGL 2.0** or later, for the brightness setting (any card from
  the last fifteen years). Without it the game still runs and brightness falls back to the
  display's gamma ramp, as the original game did.

## Getting OpenFAKK2

There are no prebuilt releases yet. Build it from source: see
[Building from source](07-compiling.md). The build puts everything in
`build\x86-Release\Release`:

| File | What it is |
| --- | --- |
| `openfakk2.exe` | the game |
| `openfakk2ded.exe` | the dedicated server (mainly for testing) |
| `renderer_opengl1.dll` | the renderer |
| `SDL2.dll`, `OpenAL32.dll` | window, input and sound libraries |
| `fakk\gamex86.dll`, `fakk\cgamex86.dll` | the game modules, built from Ritual's 1.02 SDK |

## Pointing it at your game

Leave OpenFAKK2 in its own folder and tell it where the game is with `fs_basepath`:

    openfakk2.exe +set fs_basepath "C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2"

A desktop shortcut with that line as its target is the easiest way to start it every time.
Don't copy OpenFAKK2 into the game's folder: the install's `fakk` folder holds Ritual's
own `gamex86.dll` and `cgamex86.dll`, and keeping the two apart keeps it clear which game
modules you are running ([FAQ](03-faq.md)).

From a checkout of the repository, `run.cmd` does this for you: it starts the build
against the install above (edit `GAME` in it if yours is elsewhere), in a 1024x768 window,
with its own home folder in `build\sandbox\home`. `run.cmd training` goes straight to a
map. It runs the 32-bit build; `run.cmd x64` (or `FAKK2_ARCH=x64` in the environment)
runs the 64-bit one from `build\x64-Release`, with a map after it as before.

## Your files

OpenFAKK2 keeps its own configuration, saves, screenshots and demos in its **home
folder**, never in the game's install:

    %APPDATA%\OpenFAKK2\fakk\

(`fs_homepath` moves it; `run.cmd` uses `build\sandbox\home`.) The install is searched
too, so **your retail saves** (`fakk\save` in the install) appear in OpenFAKK2's load menu
and load as they are. Games saved by OpenFAKK2 use the retail format and go to the home
folder.

Next: [Running the game](02-running.md).
