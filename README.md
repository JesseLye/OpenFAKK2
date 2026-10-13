# OpenFAKK2

A work-in-progress source port of Ritual Entertainment's *Heavy Metal: F.A.K.K. 2*
(v1.02), aiming to be to FAKK2 what ioquake3 is to Quake III Arena.

You need the original game data (`fakk/pak0.pk3`, `fakk/pak1.pk3`). None is included.

## Status

Early. The engine does **not** run the game yet.

| Area | State |
| --- | --- |
| Engine base | ioquake3 `87a99d18`, rebranded. The dedicated server (`openfakk2ded`) and the client (`openfakk2`, GL1 renderer only) build. The client has a console, a map viewer, and joins a local game: `map <name>` loads the client game module and draws the level from the game's camera |
| Game data | Retail pk3s mount; BSP v12 collision data loads for all 30 retail maps |
| Game modules | `gamex86.dll` / `cgamex86.dll` build from Ritual's 1.02 SDK source (x86, MSVC). Under the retail engine the rebuilt `gamex86.dll` matches retail console output at map spawn on four maps. Under OpenFAKK2 both the rebuilt and the retail `cgamex86.dll` load and draw |
| Engine ↔ game interface | Server side (`GAME_API_VERSION 4`): the game module loads, initialises and spawns maps. With `developer 1`, the lines the game prints while spawning a map are identical to the retail engine's on all 30 retail maps. It also runs the retail `gamex86.dll` unmodified, with the same result. With a headless test player (`sv_testplayer 1`), level scripts and cinematics run; ten seconds after the player enters, level variables match the retail game on all 29 comparable maps, and the state of every entity (up to 300 per map) matches on 7 maps and differs on 1 to 9 entities on the others, mostly by random choices that also vary between retail runs. Client prints are still stubs. See `docs/known-differences.md`. Client side (`CGAME_IMPORT_API_VERSION 3`): the client game module loads (the rebuilt one or the retail `cgamex86.dll`), initialises from the game state, receives snapshots and the sounds the game starts, and draws the scene and its 2D layer. Its collision, TIKI and alias queries are answered; sounds play through Quake III's sound system without FAKK2's volumes, distances, reverb or music; the camera-look offsets and the lip-sync data are stubs. Tested on `zoo` and `training` only |
| TIKI models (`.tik` `.tan` `.skb` `.ska`) | Definitions, animations and skeletons load for the server's and the client game module's queries; bounds and radius match the retail engine's cached values on 742 of 743 models. Skeletal tag placement is provisional. The renderer draws vertex-animated models (`.tan`) in whichever animation and frame the game asks for, interpolating between frames; skeletal models (`.skb`) register but draw nothing, so the player and the creatures are invisible |
| Map viewer | `viewmap <name>` in the client draws a retail map with a fly camera (`viewpos`, `viewsky`, `viewstop`, normal movement keys): `.ftx` textures, lightmaps, curves, FAKK2's shader script extensions, sky portals, and the vertex-animated models the map places (as static models). All 30 retail maps load and draw. Not drawn: skeletal models, fog and the far plane, anything a level script or the game sets up. The picture has not been compared with the retail game's. See `docs/known-differences.md` |
| `.urc` UI, music | Not implemented |

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
