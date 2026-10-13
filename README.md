# OpenFAKK2

A work-in-progress source port of Ritual Entertainment's *Heavy Metal: F.A.K.K. 2*
(v1.02), aiming to be to FAKK2 what ioquake3 is to Quake III Arena.

You need the original game data (`fakk/pak0.pk3`, `fakk/pak1.pk3`). None is included.

## Status

Early. The engine does **not** run the game yet.

| Area | State |
| --- | --- |
| Engine base | ioquake3 `87a99d18`, rebranded. Only the dedicated server (`openfakk2ded`) builds; the client and renderers are disabled until they are ported |
| Game data | Retail pk3s mount; BSP v12 collision data loads for all 30 retail maps |
| Game modules | `gamex86.dll` / `cgamex86.dll` build from Ritual's 1.02 SDK source (x86, MSVC). Under the retail engine the rebuilt `gamex86.dll` matches retail console output at map spawn on four maps; `cgamex86.dll` is untested |
| Engine ↔ game interface | Server side (`GAME_API_VERSION 4`): the game module loads, initialises and spawns maps. With `developer 1`, the lines the game prints while spawning a map are identical to the retail engine's on all 30 retail maps. Aliases, sounds and client prints are still stubs. Client side (`CGAME_IMPORT_API_VERSION 3`): not implemented |
| TIKI models (`.tik` `.tan` `.skb` `.ska`) | Definitions, animations and skeletons load for the server's queries; bounds and radius match the retail engine's cached values on 742 of 743 models. Skeletal tag placement is provisional. No rendering |
| BSP v12 rendering, `.ftx`, `.urc` UI, sound/music | Not implemented |

## Sources

- `code/` — ioquake3 engine (GPLv2+, see `COPYING.txt`).
- `code/fakk/` — Ritual's FAKK2 1.02 SDK game code (`fgame`, `cgame`) and the four
  engine headers it shipped with, patched only as far as needed to compile with a
  standard C++ compiler.
- Reference only, not imported: [OpenMoHAA](https://github.com/openmoh/openmohaa)
  for the Ritual engine subsystems FAKK2 shares with MOHAA.

## Licensing

The engine is GPLv2+. The code under `code/fakk/` is **not**: it is covered by
Ritual's SDK licence (`code/fakk/LICENSE-re.MD`), which does not permit
redistribution of modified copies and is not GPL compatible. Keep the two apart,
and resolve this before publishing the repository or any binaries.

## Building (Windows, MSVC Build Tools)

    scripts\build-msvc.cmd x86 Release

Output goes to `build\x86-Release\Release`. A 32-bit build is required for binary
compatibility with the retail game modules.

Point the engine at an existing install rather than copying data:

    openfakk2ded.exe +set fs_basepath "C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2"
