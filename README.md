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
| Engine ↔ game interface | Server side (`GAME_API_VERSION 4`): the game module loads, initialises and spawns maps. With `developer 1`, the lines the game prints while spawning a map are identical to the retail engine's on all 30 retail maps. It also runs the retail `gamex86.dll` unmodified, with the same result. With a headless test player (`sv_testplayer 1`), level scripts and cinematics run; ten seconds after the player enters, level variables match the retail game on all 29 comparable maps, and the state of every entity (up to 300 per map) matches on 7 maps and differs on 1 to 9 entities on the others, mostly by random choices that also vary between retail runs. Client prints are still stubs. See `docs/known-differences.md`. Client side (`CGAME_IMPORT_API_VERSION 3`): the client game module loads (the rebuilt one or the retail `cgamex86.dll`), initialises from the game state, receives snapshots and the sounds the game starts, and draws the scene and its 2D layer, with the level's far plane and its fog as the retail renderer draws them. Its collision, TIKI and alias queries are answered; sounds play through Quake III's sound system with FAKK2's volumes, minimum distances and attenuation (from `global/sound0.txt`), including the `.mp3` dialog (decoded with dr_mp3), the level's music follows the game's moods as the retail engine's does, and the level's reverb triggers set an EAX reverb (OpenAL Soft's EFX, shipped with the build, or a Freeverb from the same presets in the software mixer) that the retail engine only produced on an EAX card; the lip-sync data (the `.lip` file beside each dialog sound) is read and handed to the module; the camera-look offsets are a stub. Tested on `zoo` and `training` only |
| TIKI models (`.tik` `.tan` `.skb` `.ska`) | Definitions, animations and skeletons load for the server's and the client game module's queries; bounds and radius match the retail engine's cached values on 742 of 743 models. Tags are placed as the retail engine places them, with bone controllers, the legs/torso split and cross-blending. The renderer draws vertex-animated models (`.tan`) and skeletal ones (`.skb` with `.ska`) in whichever animation and frame the game asks for, interpolating between frames; bones are composed as the retail engine composes them for tags, split between the legs and torso animations, cross-blended from the animation being left, and turned by the entity's bone controllers as the retail renderer does. Models are lit from the map's light grid at FAKK2's spacing, from the centre of their bounds, with the retail engine's rules for the minimum light, light offsets and dynamic lights; the training room's door button matches the retail capture. The player and the actors (the `betterlighting` flag) are lit per vertex from a six-sided light cube built from the map's entity lights, its sun and ambient, as the retail renderer builds it; Julie in `training` matches the retail capture within a unit or two. Not yet: level of detail |
| Map viewer | `viewmap <name>` in the client draws a retail map with a fly camera (`viewpos`, `viewsky`, `viewstop`, normal movement keys): `.ftx` textures, lightmaps, curves, FAKK2's shader script extensions, sky portals, the far plane and its fog as worldspawn sets them, and the models the map places (in the first frame of their idle animation). All 30 retail maps load and draw. Not drawn: fog volumes, anything a level script or the game sets up. The picture has not been compared with the retail game's. See `docs/known-differences.md` |
| `.urc` UI | A fresh implementation (`cl_urc.c`) loads every `ui/*.urc`, draws the HUD (health, water, the one- or two-handed weapon menu, the boss bar) as the retail client chooses it, draws the game's centre and location prints with the Ritual fonts, and shows the main menu at startup and on Escape, and the retail loading screen (the pulsing `textures/menu/loading` picture on black) from a map load's start to its first snapshot. The system cursor works the menus: buttons highlight, click (with their hover and click sounds) and run their commands, check boxes toggle their cvar; Escape closes a menu, and console commands (`pushmenu`, `popmenu`, `listmenus`, `centerprint`, `locationprint`, ...) drive it too. Sliders drag, pulldowns open their lists, labels show their linked cvar through their `linkstring` names, the controls page's key bind list (`ui/bind.scr`) shows and rebinds keys, and the title theme plays at the menu. The menus are drawn as the retail widget library draws them (fonts, borders, slider arrows, check box captions, the bind list's pictures, scroll bars, the saved-game list with its picture, the GFX info text) and capture like retail's pixel for pixel at 640x480; loading and saving from the list wait for the engine's `loadgame`/`savegame`. The level script's screen fades and letterbox are drawn from the player state, as the retail engine drew them, and the prints sit where retail puts them (the intro's quote on black, the credits at the bottom right). FAKK2's attack, use and sneak buttons reach the game, and the inventory bar (`global/inventory.scr`: the number keys, the wheel, and the attack keys to equip the highlighted item) works as the retail client's does, with the items' models turning in their frames, their ammo bars, the wielded weapons in the HUD's windows, and an inventory screen on the `i` key. Fields and lists draw their text but cannot be edited |
| Lens flares | The sun's flare (worldspawn `sunflare`, through the sky portal) and the flares of `+lensflare` entities and lights are drawn as the retail renderer draws them, from `scripts/lensflare2.txt`, hidden behind the world by a trace. The intro's Lochnar and `training`'s shooting targets match the retail captures |

## Sources

- `code/` — ioquake3 engine (GPLv2+, see `COPYING.txt`). `code/thirdparty/` holds the
  libraries ioquake3 bundles, plus `dr_libs/dr_mp3.h` (public domain or MIT-0) for
  FAKK2's MP3 dialog.
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
compatibility with the retail game modules. The build copies `SDL2.dll` and OpenAL
Soft's `OpenAL32.dll` (1.24.3, LGPL 2.1, from `code/thirdparty/libs/`) beside the
executable; the sound system uses OpenAL by default (`s_useOpenAL 0` selects the
software mixer).

Point the engine at an existing install rather than copying data:

    openfakk2ded.exe +set fs_basepath "C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2"
