# Known differences from the retail engine

This file tracks where OpenFAKK2 is known or suspected to behave differently from
`fakk2.exe` 1.02, and which rules were inferred rather than read from the binary. It
covers the server side in detail; the renderer has a section of its own near the end.

How the comparison is done is described in `tools/retail-compare/README.md`. As of this
writing, the lines the game module prints during map spawn (`developer 1`) are identical
on all 30 retail maps, with either the retail or the rebuilt `gamex86.dll`.

Ritual's own design documents (the 22 Word files in the docs folder of their Tools 1.02
release: shaders, cvars, TIKI, sound, the GUI, particles, client commands) and its
`entities.def` are available as text: `tools\ritual-docs.ps1` converts them from the
clone that `$RitualTools` names into `build\ritual-docs`. They describe what Ritual
intended, often at an earlier stage than the shipped game, and rank below the binary:
use them as leads for what to look for, never as evidence that retail does it. Nothing
from them may be committed; Ritual's licence does not allow copying them.

## Unexplained

### Idle animation variants picked differently

On `zoo`, three actors that have two idle animations (`vymish_multi`, `vymish_ground`,
`vymish_blue`; one of the three flips depending on the run being compared) start on a
different variant than under retail. Seen in `pendingevents` right after spawn: their
`animdone` events are due at different times. Everything else in that dump matches (77 of
80 events, all level and game variables).

What is established:

- Retail is deterministic: two retail runs of the same map produce identical dumps.
- The pick algorithm matches `fakk2.exe` (`TIKI_Anim_Random` at `0x44dea0`): prefix match
  where the next character is not `_`, at most 16 candidates, a weighted pick using the C
  runtime's `rand()`, which the engine never seeds.
- `rand()` has eleven callers in `fakk2.exe`. On a dedicated server with no clients only
  two can run: the animation pick and the alias pick (`0x41c170`). The third server-side
  caller (`0x443140`) is the client connection challenge. Map start draws nothing.
- Solving for the generator position that reproduces retail's six observable picks gives
  two candidates: retail has made 23 fewer draws than OpenFAKK2 before those actors, or
  19 more.
- OpenFAKK2's first 23 draws on `zoo` are the `anim idle` command in the server init
  section of the 21 weapon models placed in the map (two of them have it twice). Those
  weapons are entities 1 to 21, the first in the map, and the game runs each entity's
  init commands in entity order once all entities have spawned. The same command in item
  and creature models produces the draws that follow, which retail does appear to make.
  So the "23 fewer" reading amounts to: under retail, `anim idle` on a weapon does not
  draw.
- The engine side of that call was read again and matches: the game's `Anim_Random`
  goes through a per-model-index table (`0x444780`) that `modelindex` (`0x444fd0`) and
  `setmodel` fill for any name ending in `tik`, then the pick function, which draws
  whenever at least one animation matches. The weapon models spell the animation `idle`
  in lower case and list it like the item models do.

What is not known: why the weapon models would not draw under retail when the item models
do. `sv_precache` is 1 on both. The picks are repeatable under retail (two retail runs
agree on every reading on `zoo`). Not yet tried: checking the same prediction on a second
map (count the weapon draws that come first and see whether retail's picks are that many
behind), and comparing what `Anim_Random` returns for a weapon under retail, which would
need a probe that makes the result visible.

Impact: cosmetic. Which idle variant an actor starts on.

### Collision patch warnings retail does not print

With `developer 1`, OpenFAKK2 prints `CM_SetBorderInward: mixed plane sides` and
`CM_AddFacetBevels... invalid bevel` while building curve collision for solid patches
(45 on `cemetery`, 76 on `training`, 48 on `gruff`; none on `zoo`, `otto` or `swamp1`).
Retail prints none on the same maps, although `fakk2.exe` contains both message strings.

What is established:

- Retail's patch loader (`0x425fe0`) builds collision only for patches whose shader
  contents include solid or a clip bit (`contents & 0x1` or `& 0xF0000`), and passes the
  shader's `subdivisions` (floor 16) as the subdivision distance. OpenFAKK2 does the same.
- Retail's `CM_GeneratePatchCollide` (`0x4278c0`) has the same structure as Quake III's.
- Replacing `cm_patch.c` with OpenMoHAA's version changed neither the warnings nor any
  spawn result.

What is not known: whether retail generates different facets for these patches, or
generates the same ones and does not report them at this developer level. No trace result
has been compared directly; the evidence that collision agrees is indirect (entities
reported "stuck in world" or "off of ground" match on all 30 maps).

Impact: unknown. Possibly none.

## Cvar differences

From `cvarlist` after loading `zoo` on both engines. Only server-relevant ones are listed
here; the client's are under "Commands and cvars the retail engine has" below.

| Cvar | Retail | OpenFAKK2 | Note |
| --- | --- | --- | --- |
| `cheats` | 1 after `map` | 1 after `map` | Now the same. Retail's engine names Quake III's `sv_cheats` `cheats` (`Cvar_Init` `0x42fb36`: "1", `ROM`, `SYSTEMINFO`), the name the game reads. Its `SV_Map_f` (`0x4420d0`) sets it before the spawn, only without `developer`: 1 for `devmap`, 0 for `map`; but the client's disconnect during the load (`0x420990`) sets it back to 1, so a local game always has cheats, with `developer` 0 too (a retail run of `+set developer 0 +map zoo` prints 1). OpenFAKK2 had kept Quake III's name and set 0 after the spawn |
| `protocol` | 1 | 71 | ioquake3's value; the wire format is OpenFAKK2's own for now |
| `nextmap` | `map zoo` | `map_restart 0` | Retail sets it to reload the same map |
| `sv_pure` | 0 | 0 | Was 1; Quake III's pure checksums cannot account for native game modules |
| `sv_timeout` | 120 | 200 | |
| `com_maxfps` | 85 | 85 | Both register 85 (`fakk2.exe` `0x42e31b`, `CVAR_ARCHIVE`). The 125 seen earlier was archived into the sandbox `config.cfg` from the player's own install config, which OpenFAKK2 read through `fs_basepath`; the harness now shadows that (see `tools/retail-compare/README.md`) |
| `sv_allowdownload` | 1 | 0 | |
| `g_skill` | 1 | not registered | Registered by the retail engine, not the game |
| `paused` | 0 | 0 | Was ioquake3's `cl_paused`, renamed; `sv_paused` stays, read-only |
| `cl_run` | 0 | 0 | Was 1 (Quake III's). Registered at `0x422626` with "0", `CVAR_ARCHIVE`; the run button (`0x41e540`) is set when `in_speed.active == (cl_run == 0)`. Julie walks with `+forward` alone until the player turns "always run" on; the shipped `default.cfg` binds only `SHIFT +speed` and sets no `cl_run` |

## Commands and cvars the retail engine has

Compared on 2026-10-15 by running `cmdlist` and `cvarlist` on `zoo` in the retail client
and in OpenFAKK2's, and checked against Ritual's design documents (`tools\ritual-docs.ps1`).
Retail writes only changed cvars to its `config.cfg`, and the sandbox's sets none of
these, so the retail values are its registered defaults. Retail lists its commands before
the client game module loads, so the module's own commands are not part of the
comparison. What the game and client game modules register comes with Ritual's code in
`code/fakk` and is not listed.

Defaults: OpenFAKK2 registers retail's `r_picmip` 0 (Quake III's 1 loaded every
texture at half size), `r_overBrightBits` 0, `r_ignoreFastPath` 0, `r_textureMode`
`GL_LINEAR_MIPMAP_NEAREST`, `cl_timeout` 125 and `r_ambientScale` 0.5.
`R_SetupEntityLighting` (`0x48ed10`) multiplies a model's ambient light from the grid by
`r_ambientScale` (at `0x48f0b5`) and its directed light by `r_directedScale`, after
averaging the samples and before the minimum light, as OpenFAKK2 does; with Quake III's
0.6 a box and a statue spawned on `zoo` (`probe-ambient.scr`) came out 4 to 7 levels
brighter than retail, with 0.5 they match within 1. Archived values a player already has
in `config.cfg` stay. On purpose, `r_mode` stays -2 (the desktop's resolution) where
retail's default is 3 (640x480). OpenFAKK2's renderer has no fast path: Quake III's would
skip the light cube, the far plane's fog passes and OpenGL's fog on models, which only the
generic path does, and with `r_ignoreFastPath 0` Julie was drawn black
(`probe-wgroup.scr`); the cvar keeps retail's default and changes nothing in the picture.

Missing from OpenFAKK2, to be done:

- Input: `+togglemouse`, with `ui_showmouse` and `ui_hidemouse` below: a cursor in the
  game (`0x41d960`; while it is up the mouse moves it instead of the view, `0x41e100`, and
  its buttons go to the interface, not their bindings, `0x41fad1`).
- Renderer: `r_showlod` (each model's level of detail as figures over it, `0x4897f6`;
  it waits on level of detail itself, below). Shadows of
  `cg_shadows 2` (stencil) and `3` (projected) for models the game flags
  `RF_SHADOW_PRECISE`; the default 1 is a mark the client game module draws itself.
- `S_UpdateEntity`'s velocity, which retail keeps per entity (`0x43ec90`) and hands to its
  3D sound interface with the position (`0x45cf26`); neither of OpenFAKK2's sound
  backends moves a sound by it. (`S_SetGlobalAmbientVolumeLevel` stores a level that
  scales the looping sounds, `0x43ed10` and `0x45c528`, but the client game module never
  calls it.)
- The interface: `ui_minicon` (the mini console, advanced options), the console as a
  window that can be moved (`ui_consoleposition`, `ui_saveconsolepos`,
  `ui_loadconsolepos`), `ui_drawcoords`, `ui_showmouse`, `ui_hidemouse`, `ui_hud`,
  `ui_resetcvars`, `ui_checkrestart`, `ui_testlist`, `inv_restart`, `vidmode`, `dialog`,
  and the tools `notepad`, `editscript`, `editshader`, `maplist`, `soundpicker` and
  `viewspawnlist` (registered with them, `0x40e805`). The
  development menus (`emitter`, `emitter2`, `zound`, `leveldesign`, `phook`,
  `animate2`, `cinematicmenu`) also need fields that can be edited and lists filled by
  `additem`.

Being done elsewhere: saving and loading (`savegame`, `loadgame`, `autosavegame`,
`loadbrokengame`, `saveshot`), carrying the player's state from level to level
(`gamemap`), and the intro (`_intro`, `dointro`).

Left out on purpose:

- `cl_classlist`, `cl_classtree`, `cl_classevents`, `cl_dumpclassevents`,
  `cl_dumpallclasses`, `cl_dumpevents`, `cl_eventhelp`, `cl_eventlist`,
  `cl_pendingevents`, `cl_eventlimit`, `cl_eventstats`, `cl_showevents` and
  `cl_timeevents` list and time the retail engine's own C++ classes and events, which
  OpenFAKK2's engine does not have. (The client game module's `cg_` versions of these
  work.)
- What only served Miles, IPX, MIDI, DirectInput, SMP, Windows or the CD:
  `s_milesdriver`, `s_speaker_type`, `s_khz`, `net_noipx`, `net_noudp`, `in_midi*`,
  `midiinfo`, `in_debugjoystick`, `in_joyBallScale`, `r_glDriver`, `r_maskMinidriver`,
  `r_smp`, `r_showSmp`, `r_ext_compress_textures`, `r_ext_gamma_control`,
  `r_ext_texture_env_combine`, `r_fastdlights`, `r_stereo`, `win_*`, `sys_cpuid`,
  `arch_*_version`, `fs_cdpath`, `fs_copyfiles`, `fs_restrict`, `cl_cdkey`, `cl_motd`,
  `cl_updateInfoString`, `gamedll`, `cgamedll`, `viewlog`.
- The demo loop's command strings (`cliff`, `fog`, `homes`, `pens`, `swamps`, `train`,
  `loadmaps*`), which name demos the game does not ship.

Ritual's documents also name things no retail binary has (`cg_norain`, `sv_gore`,
`sv_gibs`, `s_usemiles`, `ui_backgroundMouse`, `+info`, `+scores`, `invuse`,
`testmodel`, `nextframe`, `prevframe`, `nextskin`, `prevskin`), and list `twosided` as a
TIKI surface flag; in `fakk2.exe` that word belongs to the shader parser (`0x477510`).

## The console

- **The configs run at startup** (`Com_Init` `0x42e14b`): `default.cfg`, `menu.cfg` (the
  menus' defaults: `g_skill`, `cl_altbindings`, `cl_ctrlbindings`, the third-person
  camera), the player's config, `localized.cfg` (the dialog texts) and `autoexec.cfg`;
  only the player's config is skipped in safe mode. The `config` cvar names the player's
  config, `config.cfg` when not set; it prints "Config: <name>" and is where the
  configuration is written (`0x42e710`, `0x42e650`). Retail reads it with `exec`, which
  adds `.cfg`, and writes it under the name as given, so `+set config foo` reads `foo.cfg`
  and writes `foo`; ours does the same. Retail also kept the name in the registry for the
  next start; ours does not. Quake III ran only `default.cfg`, its own config and
  `autoexec.cfg`.
- **Console aliases** are Quake II's (`alias` `0x42c8a0`): `alias` lists them,
  `alias <name> [words]` makes one of the words joined by spaces as a line of its own
  (names under 32 characters), and a command line whose first word is no command but an
  alias runs it (`Cmd_ExecuteString` `0x42cf0b`: commands, then aliases, then cvars,
  then the two game modules, then the server), at most 16 times between two runs of the
  command buffer ("ALIAS_LOOP_COUNT"). The config file keeps them under its own heading
  (`0x42cab0`); retail's config file has three headed parts, bindings, cvars and
  aliases, and ours now has too. `aliasdump` (the client's, `0x41c550`) prints the
  global sound alias list, as developer output (`0x41c460`).
- **Weapon groups.** A key has an ALT and a CTRL binding besides its own (`altbind`,
  `ctrlbind`, `unaltbind`, `unctrlbind`, `altbindlist`, `ctrlbindlist`, `0x41f900`), used
  when it is pressed with ALT or CTRL held while `cl_altbindings` or `cl_ctrlbindings`
  (advanced options, archived, 0) is on (`CL_KeyEvent` `0x41f9c0`); on release the button
  commands of both it and the plain binding are let go. `unbindall` leaves them, and the
  config file writes each key's `altbind` and `ctrlbind` after its `bind` (`0x41f790`).
  `default.cfg` gives CTRL+n `saveweaps <n>` (`0x40e4a0`), which makes the console alias
  `weapgroup<n>` take up the weapons held now (`0x40e380`: `use "<item>" dual`,
  `dualwield "<left>" "<right>"`, `use "<item>" left|right`, or `holster`) and centre-prints
  "Saved Weapon Group <n>", and ALT+n `weapgroup<n>`. `probe-wgroup.scr` saves the green
  sword as group 3, holsters and runs `weapgroup3`: both games list the same alias and
  bindings and the sword comes back. Pressing ALT or CTRL itself has not been tried in a
  run (the harness cannot hold them); that path follows `0x41f9c0`.
- **Sound settings** (`S_Init` `0x43e670`): `s_volume` defaults to 1.0 (Quake III's 0.8
  before). `s_loadas8bit` (archived and latched, the advanced options' "Force 8-bit
  Sound") keeps every sound at 8 bits in retail's own mixer (`0x440196`) and opens Miles'
  output at 8 bits (`0x45b5e6`); OpenFAKK2 cuts each sound to 8 bits as it loads, for
  either backend, which is the first of those; streamed sounds (dialog, music) are left at
  16. `s_separation` (archived, 0.5) sets how far apart retail's own mixer puts the ears
  (`0x43f2cc`) and the pan it gives a Miles sample (`0x45e8d0`), but with Miles' 3D
  provider, which places the sounds itself, it changes nothing: `probe-sound-pan`
  recorded on retail with 0.5 and with 0 gave the same levels at all fourteen bearings.
  OpenFAKK2 registers it and leaves the panning alone.
- **The `fps` counter** (archived, 0). `Com_Frame` (`0x42ea73`) takes the frame rate as 16
  over the last 16 frames' times in seconds; the interface's root window draws, over
  everything (`0x419160`), "FPS %4.1f" (white above 23.94, yellow above 18, red below),
  "wt%5d wv%5d cl%d" and "t%5d v%5d Mtex%5.2f" in `facfont-20`, one font height in from
  the left, the last line ending at the bottom. The numbers come from the renderer
  (`0x47a710`, through a client import, `0x4220c0`): the frame's triangles (indexes over
  3) and vertexes, those of its 3D view (copied when the root has drawn the scene,
  `0x47a6f0`; here when the frame first turns to 2D), the map lights offered to light cubes
  (`0x48e20f`), and the texels the frame bound, in millions. With `fps 1` and a game
  running, a frame rate under 18 plays `sound/misc/lowframerate.wav` at once and every 4
  seconds until it is back above 19.8 (`0x4241f3`). On `zoo` before a script camera
  (`probe-fps.scr`) both games print `wt 431 wv 730 cl0` and `Mtex 0.37`; the totals are
  549 and 954 here against 547 and 950 (the 2D layer), and the lines fall on the same
  pixels.
- **`cl_avidemo <n>`** (Ritual's "Dumping Demo AVI"; `CL_Frame` `0x421f45`, Quake III
  1.32's): while a game runs, `screenshot silent` every frame, and every frame counts as
  1/n of a second however long it took. ioquake3's `video` command is there as well.
- **The mouse** (`CL_MouseMove` `0x41e3e0`): `freelook` is retail's name for Quake III's
  `cl_freelook`; with strafe held the mouse's y moves Julie forward twice over (once in
  the strafe branch, once where freelook is tested, as retail does it) and up by `m_up`
  (archived, 0). `m_invert_pitch` is no setting of its own: the client sets it from
  `m_pitch`'s sign at startup (`0x422780`), and the controls menu's check box shows it and
  sets `m_pitch` to -0.022 or 0.022. The holster button is `+holster` (button 4), as in
  retail; OpenFAKK2 had called it `+holsterweapon`.
- **`add`, `subtract` and `scale`** (`0x42f650`, `0x42f700`, `0x42f7b0`) change a cvar
  by an amount, as `set` would set it (read-only and cheat cvars refuse), writing a
  whole result as an integer.
- **`com_dropsim`** (a cheat cvar, `0x42e3ce`) drops that share (0 to 1) of incoming
  network packets, as Quake III 1.32's did (`Com_EventLoop` `0x42de63`); ioquake3's
  `net_dropsim`, a percentage, is still there. Neither drops the loopback's packets.
- **How the console prints**: a cvar as `"name" is:"value" default:"default"`
  (`0x42f3ae`), and `echo` each word followed by a space (`0x42c780`). One script run
  on both games (sums, alias listing, an alias, a loop of two aliases, `echo`) printed
  the same lines.
- **`cd` and `dir`** (`0x431b50`, `0x431cd0`, registered in `FS_Startup` with `path` and
  `touchFile`) work on the operating system's directories, not the game's file system.
  `fs_filedir` is where they start, the game directory's `maps` folder at first; `cd
  <dir>` moves it, relative to it, and `cd` alone prints it. `dir` is a copy of the DOS
  command: `[drive:][path][filename]`, `/A` with `DRHAS` and `-` (hidden and system files
  are left out unless asked for), `/O` with `NSEDG` and `-` (up to five keys), any other
  switch prints its help, and each line is the date (12-hour, `a`/`p`), the size
  (`<dir>`, `k` or above a megabyte `m`) and the name. A name without wildcards that is a
  directory lists that directory. OpenFAKK2's `dir` printed the same as retail's on the
  same folder for the listing, `/OS`, `/O-N /AD`, `*.pk3`, an unknown switch and a name
  that matches nothing. Ours lists the game's file system with `fdir` (Quake III's), which
  retail does not have. Retail leaves the process in the listed directory when nothing
  matches; ours goes back.
- **`mapdir`**: with `developer` set, `map <name>` loads `maps/<mapdir>/<name>`
  (`SV_Map_f` `0x4420f8`). `cd` sets `mapdir` to the part of the new directory below the
  maps folder, but compares it with the folder as `FS_Startup` spells it, with a doubled
  separator (`...\fakk\\maps\`, as retail's `fs_filedir` reads), which no directory name
  has; so `cd` always clears `mapdir`, in retail too, and it has to be set by hand.

## Rules inferred, not read from the binary

These produce results that agree with retail wherever they have been exercised, but the
corresponding retail code has not been examined.

- **Traces skip entities whose solid type is `SOLID_NOT` or `SOLID_TRIGGER`.** Inferred
  from an actor seated in a non-solid chair (`homes1`) and items inside brush entities
  (`towncenter_evil`); since read in `SV_ClipMoveToEntities` (`0x448fe0`), which also
  showed which entities a trace skips for its pass entity (see "Firing a projectile"
  below).
- **A temporary box model takes its entity's contents.** First inferred from solid pods
  that solid-only traces must hit (`otto`, `oracle`); since read in `SV_ClipHandleForEntity`
  (`0x448660`), which also showed that a brush entity the game has made `SOLID_BBOX` clips
  as its box rather than its brushes (see below).
- **Alias range lookup and weighted pick** come from OpenMoHAA's version of the same code.
- **Dialog selection** follows `fakk2.exe` (`0x41bdc0`) except for two details that were
  not recovered: the test for whether use is tracked per actor (taken to be "the alias is
  global, or no actor was given"), and the multiplier that turns the random fraction into
  an index (taken to be the number of available lines).
- **Tag placement** follows `fakk2.exe` (`TIKI_Tag_Orientation` `0x44fe40`,
  `TIKI_Tag_OrientationEx` `0x44fc30` with its skeletal part `0x44ee40`, and the
  client's lerped variant `0x451c90` with `0x4505e0` skeletal and `0x451a30` vertex;
  the game imports `0x444d00`/`0x444da0` pass everything through): bones from the tag
  to the root are composed root first as the renderer composes them (see there), with
  the entity's bone controllers, the legs/torso split and cross-blending; the lerped
  variant blends towards the old frame by `backlerp` first. Quaternions blend by slerp
  (`0x43ca60`). Two choices of OpenFAKK2's own: bones no animation of the state moves
  take the main animation's frame (retail leaves them zero), and an invalid request
  gives a zero origin with identity axes (retail returns all zeros from the plain
  function and identity from the lerped one). Bone quaternions scale by 1/32767, as
  retail decodes them (`0x4d57dc`; see "Skeletal models" under the renderer).
- **`Anim_AbsoluteDelta`** returns the same value as `Anim_Delta`.
- **The delta-driven animation flag is never set.** Retail's cached definitions flag only
  four animations, all in the unused `oldjulie` model, and the keyword that sets the flag
  has not been identified. (An earlier version set it for every animation with movement,
  which stopped actors from walking: the game applies movement only to animations that
  are *not* flagged.)
- **`pointbrushnum`** always returns 0.
- **Tag and frame-bound scaling** (`load_scale * scale`, plus the setup origin).

## Model indexes

A brush entity's model index is its inline model number, as in Quake III: `setmodel`
(`0x445050`) hands a `*<n>` name to `SV_SetBrushModel` (`0x444510`), which stores `n`,
and nothing else; any other name gets a config string index from `SV_FindIndex`
(`0x4459a0`) and, for a model definition, its definition in a table kept by index
(`0x444fd0`; the config string is taken before the definition is loaded, so a definition
that fails to load still uses up an index). `IsModel` (`0x444850`) is a lookup in that
table (`0x444780`). The client game module relies on the same thing: it indexes the
inline models with the entity's model index directly.

What keeps a brush entity from being taken for the definition with the same index is the
config string layout: `SV_ClearWorld` (`0x448810`) registers `*1` to `*<n-1>` as the
first model config strings before the game runs, so index `n` is `*n` and no definition
can have it. An earlier version kept the config string index in the entity instead,
which also worked for the server alone but not for the client game module; a version
with the inline number and without the reserved config strings ran definitions' init
commands on brush entities (`swamp1` had creatures stuck in a trigger that `setsize` had
made `SOLID_BBOX`).

Where a brush entity has been made `SOLID_BBOX`, `SV_ClipHandleForEntity` (`0x448660`)
clips it as a temporary box of its mins and maxs and contents, not as its brushes, and the
box is not rotated (`SV_ClipToEntity`, `0x448f30`).

## The client game module

The module is a native library; `GetCGameAPI` returns its entry points and `CG_Init`
takes a table of 124 engine functions. The table's order is the SDK's `cg_public.h`, and
matches the retail engine's (`0x41ce90` fills it; it leaves `apiversion` 0 and the
`NumSkins` and `Tag_OrientationEx` slots NULL, and the renderer functions in it are the
renderer's exports). The retail and the rebuilt `cgamex86.dll` both run.

Read from `fakk2.exe` and followed:

- **`GetSnapshot`** (`0x41c6d0`): a frame from the other side of a server restart
  (`SNAPFLAG_SERVERCOUNT` differs from the current frame's) is refused, at most 256
  entities are returned, and they are ordered so that an attached entity follows its
  parent: entities with no parent or an already placed parent first, then up to eight
  passes for the rest, then whatever is left with a developer message. The `snapshot_t`
  layout is the SDK's (`numServerCommands` is never filled in).
- **`UpdateLoadingScreen`** (`0x423490`) redraws the screen only while the client has not
  yet been primed.
- **Reading an entity** (`MSG_ReadDeltaEntity`, `0x439e70`) ends by copying `netorigin`
  to `origin` and `netangles` to `angles`, turning `angles` into the entity's `quat`,
  and turning `bone_angles[i]` into `bone_quat[i]` for every `bone_tag[i]` that is not
  -1 (`0x43cbe0` is the SDK's `EulerToQuat`). The quaternions never travel; without
  this the client game module hands the renderer zero bone controllers, which wiped
  out everything above Julie's waist.
- **`CM_InlineModel`** (`0x426750`) is the plain collision function; the model-index
  rule above is what makes the game's argument an inline model number.
- **`Anim_CrossblendTime`** (`0x44e3b0`) returns the animation definition's cached
  cross-blend time in milliseconds (the `crossblend` keyword, 200 ms by default); the
  module times its cross-blends with it and hands the renderer and the tag functions
  the animation being left and how far along the blend is.

Not read, done another way:

- **Sounds the game starts.** The server queues every sound the game starts for every
  connected client and sends them with the next snapshot (the wire format is OpenFAKK2's
  own); the retail import (`0x448520`) has not been read, so whether it filters by
  visibility is unknown. The client game module receives them as the SDK's
  `server_sound_t` and plays them through Quake III's sound system with FAKK2's volumes
  and minimum distances. The sound information files `global/sound0.txt` to `sound9.txt`
  are read as the retail engine reads them at sound start-up (`0x43f890`: `sound`,
  `mindist`, `volume`, `loopstart`, `loopend`, `maxnumber`, `maxfactor`, `localsound`,
  `defaultmindist` and `defaultvolume`, `maxdist` and `defaultmaxdist` read and ignored,
  `$define` macros, defaults 160 and 1.0, 1000 sounds at most), and a negative volume or
  distance from the module takes the sound's own (retail `0x45bf00`). The attenuation is
  what that function asks the Miles sound system for: a minimum distance and a maximum of
  64 times it, both in metres at 1/52.49 per unit, the volume times `s_volume` on a 0 to
  127 scale, a `localsound` not placed in the world; the curve between them, full within
  the minimum distance, the inverse of the distance beyond and no quieter than 1/64, is
  Miles' documented model, and recordings of the retail game agree (see "Panning and
  loudness" below). `S_StartLocalSound` (`0x43ed80`)
  resolves an alias, registers the name and plays it on the listener's entity on
  `CHAN_MENU`; the channel numbers are FAKK2's. Not followed: `maxnumber` and
  `maxfactor` (Quake III's limit of 4 or 8 like sounds per entity stays), loop blocks,
  the `s_show` print is retail's format only; `S_StopSound` stops every match where
  retail's Miles stop (`0x45d710`) ends the first sample or `.mp3` stream with that
  entity and channel, and `CHAN_AUTO` here stops all of the entity's channels; a new
  sample on an entity's busy channel (not `CHAN_AUTO`) replaces the old one in retail
  (Miles' picker `0x45bc70`, the software mixer's `S_PickChannel` `0x43f010`) and plays
  beside it here, while `.mp3` streams never replace each other in retail either except
  on `CHAN_MENU` (`0x45bd10`); the `.mp3` dialog files (1015 of them) are decoded
  whole with dr_mp3 when registered, where the retail engine streams every `.mp3` name
  through Miles (`0x45bf00` sends them down a separate path); ambient-volume
  calls are ignored; looping sounds are keyed by their
  order in the frame. `s_soundinfo [name]` prints the table's size or a name's entry.
- **Stopping an actor's dialog.** Retail keeps the entity and channel of every sample and
  `.mp3` stream it starts, a `localsound` included (`0x45bf00`, `0x45bd10`), so its stop
  (`0x45d710`) finds them. The dialog `.mp3`s are `localsound`s in the sound information,
  and the OpenAL backend used to file local sounds under entity -1, so an actor's
  `stopdialog` missed the line it was saying: on `training`, a trigger that stops the
  trainer and starts her next line played it over the old one. Local sounds keep their
  entity now (`S_AL_StartLocalSoundEx`); the software mixer always did. The game module
  refuses a new line while the actor's last one plays, so with the stop working an
  actor says one line at a time. Checked on `training` with logging on both backends
  (the stop ends the old line before the next starts); not by ear.
- **Panning and loudness**, measured rather than read: the retail game and OpenFAKK2 were
  recorded from the speakers (WASAPI loopback, `tools/retail-compare/sound-record.ps1`)
  playing a looping 1 kHz tone a probe places around the player on `zoo`
  (`probe-sound-pan`, `probe-sound-distance`, `probe-sound-turn`) and swamp1's music on its
  own (`probe-music`), at `s_volume 0.25` (this machine's output path compresses anything
  louder than about -20 dBFS). Without an EAX card the retail engine opens Miles' "Fast 2D
  Positional Audio" provider, which pans like a balance control: the ear nearer the sound
  keeps all of it and the far one loses 1.5 dB at 15 degrees off ahead, 3.5 at 30, 5.8 at
  45, 8.9 at 60, 13.3 at 75 and 17.1 at 90, by the bearing level with the listener (a tone
  10 units above the ear pans fully), front and back mirrored, with both ears 3 dB down
  for a sound behind; no delay between the ears. A sound straight ahead or at the listener
  is at full volume in both. The distance curve is the inverse one above, the same in
  minimum distances whether the minimum is 32 or 64. Both backends now pan that way
  (`S_SoundPan`): the software mixer directly, at a full scale of 255 where Quake III's
  was 127 (half the music's), and the OpenAL one by putting each world source at the
  bearing where OpenAL Soft's plain stereo panning (constant power: 4.5 dB down in both
  ears straight ahead or at the listener) splits it as Miles did, with its gain making up
  the nearer ear; with HRTF, surround or another OpenAL, OpenAL pans as it does. Before,
  a sound ahead was 4.4 dB quieter than retail on the OpenAL backend and 11.9 dB on the
  software mixer, with the music at retail's level on both (within 0.3 dB), so the world
  sounded quiet under the music. After, every bearing is within 0.5 dB of retail's on
  both. The listener's axis was also mirrored: FAKK2's client game module negates the yaw
  for Miles (`cg_view.c`), whose positions the retail engine gives as ( -x, z, -y )
  (`0x45bf00`), and the engine took it as it came, so a player facing anything but yaw 0
  heard left for right and front for back; `CL_CG_S_Respatialize` mirrors it back (the
  roll comes out negated). Not followed: Miles' volumes are whole numbers from 0 to 127,
  and the truncation takes a further 2 dB off a sound at eight times its minimum distance
  at `s_volume 0.25` (6 dB at sixteen), less at full volume; the pan is a whole number
  too.
- **Reverb.** The game's `trigger_reverb` entities (placed by the level's `maps/<map>.snd`
  sound-manager file; 28 levels have them, `swamp1` and `training` do not) put an EAX
  environment number (`eax_generic` 0 to `eax_psychotic` 25) and a level in the player
  state, and the client game module passes them to `S_SetReverb` when they change. The
  retail engine (`0x45d3d0`) hands the number to Miles as the EAX room type and the
  level as the EAX effect volume, only when `s_reverb` is 1 (its default was 0) and the
  sound provider is EAX, so most players never heard it. OpenFAKK2 renders it itself in
  the software mixer (`snd_reverb.c`): the 26 environments are OpenAL Soft's EFX presets
  for them (`efx-presets.h`), played through a Freeverb-style late reverb (eight comb
  filters and four allpasses per channel, Jezar's tunings scaled to the output rate),
  with the combs' feedback set for the preset's decay time, their damping from its
  high-frequency decay ratio, a pre-delay of its reflection and late-reverb delays, and a
  wet level of its gain times its late-reverb gain times the trigger's level (times
  Freeverb's output scale, capped so `underwater` and `psychotic` do not clip). Only the
  world's sounds feed it; local sounds, music, cinematics and dialog streams stay dry.
  `s_reverb` defaults to 1 here. `s_setreverb [<type|name> [level]]` sets or lists the
  environments by hand (until the next trigger). Checked by trace on `fog` and by an
  offline simulation of the filter's arithmetic (decay times within 20% of the presets',
  no clipping at full level); not by ear. The OpenAL backend, which is the default, does
  it the way the retail engine did: an EFX auxiliary effect slot holds an EAX reverb
  effect (`AL_EFFECT_EAXREVERB`, or the standard reverb where a driver lacks it) set from
  the same preset table, the slot's gain is the trigger's level (retail's "EAX effect
  volume"), and only world sources send to it. Checked by trace on `fog` through four
  environments and an `s_reverb` toggle, without AL errors; not by ear.
- **Music** follows the retail engine's mood system, read from `fakk2.exe`. The
  soundtrack the game names in its configstring is a `.mus` file
  (`MUSIC_LoadSoundtrackFile`, `0x440be0`): `path <dir>` prefixes the files after it, a
  line `<mood> <file>` or `<file>` (then the mood is the file name without its extension)
  adds a song, 16 at most, and `!<mood> loop|restart|interrupt|volume <v>|fadetime <t>`
  sets a song's flags, `interrupt` implying `restart` and a fade time of 0; volume
  defaults to 1, fade time to 1; loading with no mood yet requested requests
  normal/normal. `MUSIC_UpdateMood` (`0x441570`) records the current and fallback moods
  the player state carries, and coming back to the pair that preceded an action mood
  skips the current mood's song for the fallback's when that song is `interrupt`.
  `MUSIC_UpdateVolume` (`0x441690`) fades the game's music volume to the new value over
  the fade time. Each frame (`0x4412a0`): a one-shot song is over once it is within the
  fallback song's fade time of its end (`0x45e7e0`), and the fallback becomes the
  requested mood; a requested mood whose song is not the one playing starts it
  (`0x45e380`) on one of two handles, the song playing fading out over the new song's
  fade time unless the new song is `interrupt`, which stops it at once (`0x45e2e0`); a
  looping song starts at full volume and a one-shot fades in over its own fade time; a
  song without `restart` resumes where it was stopped. A handle's volume (`0x45e5e0`) is
  the song's volume times `s_musicvolume` (retail default 0.60, now ours) times the
  game's music volume times its fade, on top of `s_volume`. OpenFAKK2 feeds the two
  handles through two raw streams a quarter second ahead of real time, on either sound
  backend. Not followed: the retail engine truncates fade times to whole seconds, and
  its fade-down of the game's volume starts from 1 rather than the current value. The
  retail console commands `loadsoundtrack <name>` and `playsong <mood> [fallback]`
  exist, plus `music_info`. Checked by trace on `swamp1` and from the console on both
  sound backends, and its level by recording (`probe-music`: within 0.3 dB of retail's on
  both).
- **Sound backends.** The build ships OpenAL Soft 1.24.3 (`OpenAL32.dll`, LGPL 2.1,
  `code/thirdparty/libs/`) beside the executable and `s_useOpenAL` defaults to 1, so the
  OpenAL backend is what plays; `s_useOpenAL 0` selects Quake III's software mixer, which
  has everything the OpenAL one has (its reverb is the Freeverb above). The retail
  engine's Miles Sound System had no counterpart to this choice. Quake III's default
  sound, `sound/feedback/hit.wav`, is not in FAKK2; a sound that cannot be loaded is
  silence on both backends.
- **Camera-look offsets** (`get_camera_offset`) are always zero.
- **Mouse look.** Quake III scales the mouse by a value its client game module sends
  every frame (`trap_SetUserCmdValue`), which starts at zero when the client state is
  cleared; FAKK2's module has no such call, so the mouse never turned the view. The
  scale starts at 1 now (`CL_ClearState`).
- **Sprite temp models.** The client game module adds a `.spr` temp model (emitter
  smoke, the intro's meteor trails) as `RT_SPRITE` with the model handle and no custom
  shader; Quake III's sprite path would draw the custom shader at the entity's radius,
  which are none and zero, so nothing appeared. The renderer now draws such an entity
  with the model's shader at the size the entity's scale gives (`R_AddEntitySurfaces`),
  laid out as the retail renderer lays sprites out (see "Weapon effects").
  The intro's trails match the retail capture at 60 seconds.
- **Level changes.** The game module ends a level with `gamemap <next>` (`G_ExitLevel`),
  which Quake III names but this tree had dropped; it is the `map` command again. The
  `stopsound` command it sends first stops all sounds. A map name may carry the spawn
  point after a `$`: fakkhouse's trainer cinematic ends with `map
  training$spawn_fakkhouse`, the only way a new game reaches `training`. The retail
  `map` command (`0x442130`) checks for the bsp named before the `$`, and
  `SV_SpawnServer` (`0x445e90`) loads that bsp but sets `mapname` to the whole name and
  hands that to the game, whose `Level::SpawnEntities` splits it again; the client game
  does the same for the world it draws. OpenFAKK2 does likewise (`SV_MapBaseName`); it
  looked for `maps/training$spawn_fakkhouse.bsp`, printed "Can't find map" and left the
  player in fakkhouse (2026-10-15, `probe-exit.scr` through `probe-client.ps1`). Not
  followed: on arrival the retail game autosaves (`Saving game...training0000`).
- **The console** has no `console` shader in FAKK2 (the retail console was a window);
  it is drawn on a dark translucent sheet rather than the default shader's black.
- **`in_nograb`** is not archived here (it is in Quake III), so a harness run that sets
  it does not leave the next normal run without a mouse.
- **The health and water gauges** (`ui/healthhud.urc`): a `statbar_tileshader` is laid
  over its widget at the image's own size from the top left and the drained part is cut
  off, where this port had stretched the image over the widget. The gauges are 64x128
  images whose arcs fill the top 72 rows, in 64x72 widgets, so stretched they came out
  half-height and drained from the middle. A plain `tileshader` is tiled the same way:
  the menus' frame strips (32x16 and 16x32 images in widgets up to 570 long, with `map`
  rather than `clampmap`) now repeat along their widgets instead of being stretched,
  and the options and controls pages draw as before. A `statbar_shader` (the boss bar)
  is still stretched. The renderer's new
  `ShaderImageSize` tells the UI an image's size. Captured at 100, 75, 50, 25 and 5
  health and water (`probe-hud.scr`), the gauges match the retail captures.
- **`com_abnormalExitDialog 0`** (a startup cvar, default 1) skips ioquake3's "didn't
  exit properly, start in safe mode?" dialog that a stale pid file brings up after the
  game was stopped rather than quit; the harness and debugging launches pass it, since
  the dialog blocked those runs before any output.
- **Filled rectangles.** FAKK2's `white` shader (`scripts/sprites.shader`) is additive,
  so Quake III's `SCR_FillRect` with it could never darken anything: the console sheet,
  a menu's `bgfill`, the fade to black. Fills go through the renderer's own
  alpha-blended white (`re.DrawBox`) now.
- **The screen fade and letterbox.** The level script's `fadein`/`fadeout` and
  `letterbox` reach the client in the player state (`ps.blend`, `STAT_ADDFADE`,
  `STAT_LETTERBOX`, see the game's `player.cpp`), which the SDK's client game module
  never reads, so the retail engine draws them. OpenFAKK2 draws them under the prints
  and menus (`URC_DrawFade`): the blend over the whole screen, additively through
  `textures/jack/whitefade` when `STAT_ADDFADE` is set, and black bars of
  `STAT_LETTERBOX / 0x7fff` of the screen's height at the top and bottom. Not read from
  retail: that the bars are that tall (the game's default fraction is 1/8, which gives
  a 16:9 picture), and where in the frame retail draws the blend.
- **User interface** (`cl_urc.c`). The retail engine draws its menus, HUD and prints
  itself from `ui/*.urc`; `UI_Init` (`0x40e9d0`) loads every file, registers the
  commands, and keeps the four HUD menus by the name in the file (note `ui/weaphud.urc`
  holds `dualweaphud` and vice versa); its update (`0x40af50`) hides them all unless
  `ui_hud` is on, a map is active, no menu is up and the cinematic flag is clear, then
  shows `healthhud`, `weaphud` when `activeItems[4]` (the dual weapon slot) is below 0
  and `dualweaphud` otherwise, and `bosshud` while `stats[13]` is at least 1. Centre and
  location prints are their own messages (`svc_centerprint`: string; `svc_locprint`:
  short x, short y, string), stored per client on the server (`0x4450f0`, 256 bytes,
  "Centerprint text exceeds buffer size" beyond) and drawn with `facfont-20` for
  4 seconds (`0x418cc0`); a text starting with `@` names a shader. OpenFAKK2 does the
  same, as a fresh implementation of the file grammar the 31 shipped files use. Not
  read from retail: the cinematic flag used here is `stats[STAT_CINEMATIC]` (retail
  tests a field of its print object), the print's fade (in over the first half second,
  out over the last), the `@shader` size (256x128), the stat-bar flash (half a second
  after a change), and `ui_hud`, a command in retail, is a cvar here. The prints'
  places come from retail captures of the intro: a location print's x is its left edge
  and its y is measured up from the bottom of the screen to the bottom of its first
  line (`locprint 230 220` puts the quote's first line at y 241, left-aligned at
  x 230), later lines following below; a centre print is centred on the screen line by
  line. The retail
  client shows the system cursor over a menu (`ShowCursor`); so does this one: the
  mouse is released while a menu is up and its window position reaches the menu, whose
  topmost button or check box under it hovers (`hovershader`, `hoversound` on arrival)
  and takes the left button (`clicksound`, `stuffcommand`; a check box toggles its
  `linkcvar`). `aliascache` names are picked by weight among the aliases sharing the
  prefix; a new sound on the menu channel stops the one before it. The retail client's
  return to the menu (`0x4212f0`) puts on `music/intro.mus` when no map is active; so
  does this one's, when it shows the main menu. `togglemenu` without a name toggles
  `main`, which is what `default.cfg` binds to Escape.

  The menus are drawn as retail's widget library in `fakk2.exe` (uilib) draws them,
  and capture pixel for pixel like retail at 640x480 (the main, load/save, video/audio,
  advanced, controls, credits, quit and GFX info menus; what differs is the animated
  clouds and the two games' own cvar values). `resource X` makes the class `UIX`, with
  `Label` aliased to `FakkLabel` (`0x40cc40`, which draws as `UILabel` when it has no
  stat, `0x4185e0`); the loader creates every widget in `verdana-12` (`0x4aad60`;
  `verdana-14` was used before), on UIWidget's defaults of white on UGrey (0.75) with an
  indented border (`0x49d320`). `UIWidget::Display` (`0x49ef30`) draws each widget in a
  viewport of its own frame, so what overruns it is cut off (the bind list's
  "MWHEELDOW"): the background colour; the border (`RAISED`/`INDENT_BORDER` a one-pixel
  3D edge, `0x49a260`, `3D_BORDER` two pixels of the background colour, `0x49a650`),
  whose colours come from the background colour and its alpha (`0x49e800`: two thirds,
  a third, and a lighter one), so the files' clear backgrounds show none; the material
  in the foreground colour, `shader` stretched and `tileshader` tiled; the
  `hovershader` over it, not instead of it; then the class's Draw. Text goes through
  `UIFont::PrintJustified` (`0x4b03c0`) and the renderer's `DrawString` (`0x481690`):
  glyphs are spaced and sized as the "Ritual fonts" entry under the renderer describes
  (the intro's quote now lines up with retail's glyph for glyph), and the position is
  truncated to whole pixels. Labels and
  buttons centre their text (a held button's a pixel down and right, `0x4ae660`).
  `UISlider` (`0x4b5970`, `0x4b64e0`) has 15-pixel arrows at both ends
  (`textures/menu/arrow_left`/`right`, `_sel` while held, a click steps by `stepsize`)
  and its thumb (the image's own width, `_sel` while dragged) moving between them;
  it writes its cvar as `%d` or `%.3f` (`0x4b61c0`). `UICheckBox` (`0x4bb6e0`) draws
  its 16x16 box 16 pixels in, centred, and its title left-justified 48 pixels in.
  `UIPulldownMenu` (`0x4be190`) draws its `menushader` at the image's size, the
  `selmenushader` while its list is open; the list (`UIPopupMenu`, `0x4bf1f0`,
  `0x4bf4a0`) opens below it in `verdana-14`, as wide as its widest entry plus 24, an
  entry per line plus 4 above and below, black on white with a raised edge (the
  entry under the cursor is drawn white under black, `0x4bda90`, so it looks the
  same). Labels with a `linkcvar` show the value through their `linkstring` names;
  `ui_newvidmode`, which the resolution box shows, is set to `r_mode` when a menu comes
  up and to -1 when the last goes (`0x40ab60`).

  `UIFakkBindList` (`0x4c0390`, `0x4c1360`) is a header row of labels (the
  `ui/bind.scr` header pictures in the title colours) over a label per `binditem`
  (its picture) and two `UIBindButton`s, with the fill picture tiled in columns between
  them and a scroll bar; it shows (height - row) / row rows. The selected row (row 0 at
  first) is in the highlight colours, its selected cell (the primary key at first) in
  the select colours, or the active ones with a two-pixel border in
  `activebordercolor` while it waits for a key, when `textures/menu/presskey` shows in
  the middle of the screen (`0x4c10e0`); the other rows are in the inactive colours. A
  key cell (`0x4bc410`) shows `textures/bind/<key name>`, or `textures/bind/emptykey`
  with the name centred over it when there is no such picture. `UIVertScroll`
  (`0x4b3ab0`, 16 pixels down the right of its owner, `0x4b4620`) is a black-lined
  UGrey frame with an arrow box at each end (a raised edge while there is something to
  scroll, the marlett font's "5" and "6") and a UDarkGrey thumb as tall as the shown
  share of the track (at least 6). The load and save menu's `FakkLoadGameClass` is a
  `UIListCtrl` (`0x4ace40`): columns "Location", "Elapased Time" (sic) and "Date &
  Time", four, two and four tenths of its width (`0x414710`), headers a font height
  plus four tall in 0.3 grey with white lines, a row per `save/*.ssv` (`0x4147d0`; its
  header is the save time, the map time in milliseconds and the 64-character location)
  newest first, the selected one white with black text, and its picture
  (`save/<name>.tga`, or `textures/menu/no_saved_games.tga`) on the `LoadSaveShot`
  label (`0x415390`). `widgetcommand LoadSaveList` takes `loadgame`, `savegame` and
  `removegame` (which deletes the four files from the home directory); the engine has
  no `loadgame` or `savegame` command yet, so the first two do nothing. The GFX info
  menu's `MultiLineEdit` (`0x4b7530`) shows `r_gfxinfo`, which the renderer now fills
  as retail's does (`0x460790`): the `gfxinfo` text with each extension on its own
  `GL_EXTENSIONS:` line, cut at 4096 bytes. The font scripts say `rgbGen identity`, but
  retail draws its text in the menus' colours, so the renderer gives `gfx/fonts/*`
  stages the caller's colour. Not checked: how retail places the menus at other
  resolutions (they are scaled to the window here). Not done: keyboard in menus beyond
  Escape, the arrows in the load list
  and a waiting bind cell; Field and List editing; pulled-down lists' sounds; a pressed
  button's border; `ordernumber`. On purpose: the main menu's "Back to Game" button
  (`backtogame`) is hidden and cannot be clicked until a level is active. Retail's
  pushmenu (`0x40c044`) sends it `disable` while the client is not active (`disable`,
  `0x49e670`, hides it), yet retail's captures show it at startup and after a
  `popmenu`/`pushmenu main`. The menu shaders use `rgbGen global` /
  `alphaGen global`, the colour the widget sets (its `fgcolor`): the renderer maps them
  to the vertex colour. A stage written with `nextbundle` is now one multitexture
  stage (`CollapseNextBundles`); it was two passes, the second darkening the frame.
- **Pause.** The retail engine registers `paused` read-only (`Com_Init`, `0x42e0b0`) with
  a `pause` command (`0x42e060`) that toggles it, refusing to pause in deathmatch or
  without a local server. Its UI update (`0x40af50`) pauses the game when a menu is up
  and the game was not paused already, and lifts the pause it put on when the last menu
  closes with the game active; `CL_InitRef` (`0x4221e0`), the gamestate (`0x423b60`),
  `SV_SpawnServer` (`0x445e90`) and the return to the menu (`0x4212f0`) clear it, and
  the server keeps it only while one client is connected (`0x4475f0`). While it is set
  the sound update (`0x45d460`) pauses every playing sample and stream once (`0x45bb80`)
  and resumes them when it clears (`0x45bc00`); sounds started in between play, and the
  two music handles (`0x9b4118`, `0x9b4134`) sit before the stream table it walks
  (`0x9b4150`), so the level's music plays on under the menu. OpenFAKK2 registers
  ioquake3's `cl_paused` under the name `paused` (the client game module reads it: the
  pause graphic, effects held), adds the `pause` command and the menus' pausing, and
  holds the playing sources while `paused` is set (OpenAL: `alSourcePause`; software
  mixer: the channel is skipped and its start moved on by the time it was held, the
  loops left out until then), leaving the music's streams alone. Not followed: the
  same update halves the sound volume (`0x43ed10`) while the client's cinematic flag
  (`0xb129ec`, the one that makes Escape `skipcinematic`) is set.
- **The loading screen.** The retail `CL_MapLoading` (`0x4207f0`) calls `UI_BeginLoad`
  (`0x40d520`), which sets a loading flag, and pops every menu (`0x4a29f0`) before its
  first screen update; `UI_EndLoad` (`0x40d4f0`) clears the flag when the first active
  snapshot makes the client active (`0x41d520`), when a server restart's snapshot
  arrives (`0x423450`), and in `Com_Error`'s disconnect (`0x4209b0`). While the flag is
  set and no menu is up (or the one on top is not fullscreen: retail tests a field of
  it), the UI update (`0x40af50`) fills the screen black (`0x40acd0`) and draws
  `textures/menu/loading` (registered at `UI_Init`, 256x256; `scripts/menu.shader`
  pulses its second stage) in white at the image's own pixel size, centred on the
  screen; `pushmenu` does nothing meanwhile (`0x40bf90`). The client game module draws
  nothing then (its `CG_DrawActiveFrame` returns while `infoScreenText` is set), so
  without this OpenFAKK2 showed whatever the frame buffer held. OpenFAKK2 does the same
  from `CL_MapLoading`, `CL_FirstSnapshot` and `CL_Disconnect` (`URC_BeginLoading`,
  `URC_EndLoading`, `URC_DrawLoading`). Not read from retail: whether the
  `textures/menu/ritual` and `textures/menu/gathering` pictures `UI_Init` also
  registers are drawn at any point of a load (they are the intro's logos, `0x40ad90`).
- **Tag functions.** (They honour the blending arguments the module passes; see
  "Tag placement" above. Retail's table points `Tag_Orientation` straight at
  `0x44fe40`, `Tag_NamedOrientation` at `0x450560`, which looks the name up and asks
  for a cross-blend of 0 towards animation 0 — the SDK module never calls it, and
  OpenFAKK2 uses the frame asked for — and `Tag_LerpedOrientation` at `0x451c90`;
  `Tag_OrientationEx` is NULL in retail's table and follows the server's import here.)
- **Lip-sync data** comes from the server's `.lip` reader, which is in the same
  executable.
- **The pure-server check** is off (`sv_pure 0`, as retail has it).
- **Message size.** The game state for `training` is 37K; Quake III's 16K limit and its
  16-bit fragment offsets were raised to 128K and 32 bits.
- **The client's own key and mouse events** are not passed to the module; FAKK2's has
  no entry points for them.

## With a player present

OpenFAKK2 can put a headless test player in the game (`sv_testplayer 1`). The reference is
the retail game run normally with its real client and nobody touching the controls. Both
run a probe script that waits for the player and prints state at fixed game times.
`tools/retail-compare/compare-player.ps1 -Map <map> -Probe <probe>` runs both and
compares level variables, pending events by kind, and every `whatis` reading.

### Retail differs from itself

The engine passes the game a random seed taken from the clock (`fakk2.exe` `0x445370`
calls the game's `Init` with `Com_Milliseconds()`, and the game calls `srand` with it), so
anything the game decides with its own random numbers varies between runs. OpenFAKK2 does
the same. Running retail twice (`-RetailTwice`) with the all-entity probe:

| Map | Readings identical between two retail runs |
| --- | --- |
| `intro` | 148 of 180 (script models placed at random) |
| `blood` | 296 of 300 |
| `cemetery` | 293 of 296 |
| `homes2good` | 295 of 299, pending events 145 and 140 |
| `towncenter_good` | 295 of 300, pending events 99 and 136 |
| `end` | 94 of 95 |
| `swamp3`, `zoo` | all |

A difference from retail on those maps means something only when it is larger than this,
or of a different kind. Positions of wandering actors, moving script objects, and counts
of `setmovedelta` events are not comparable run to run.

The engine's own generator (used for animation variants and aliases) is never seeded and
is a separate one, so those picks are repeatable under retail.

### Every entity

`probe-entities.scr` runs `whatis` on entities 0 to 299 ten seconds after the player
enters, so every field that command prints is compared for every entity in the level.
Over the 29 maps where the probe runs (`under` never fires it on either engine):

- Every level variable matches on every map.
- All readings match on 7 maps (`cliff2`, `gruff`, `gruff_cinema`, `oracleway`, `over`,
  `swamp2`, `water`). On the rest, 1 to 9 readings differ out of up to 300, except
  `intro` (32, the same number retail differs from itself by).
- What differs: the animation an actor is playing (the unexplained variant picks above,
  plus the game's own random choices), and positions and angles of things that move at
  random. Two that are neither: four rope pieces on `swamp3` are about 0.1 units from
  retail's positions, and one script object on `creeperpens` is in a different area.
  Neither has been investigated.
- Pending events match by kind on 21 maps. On `blood`, `cliff2` and `over` the player's
  idle animation is at a different point in its cycle (see the stall below). On `end`
  and `otto` one more actor `animdone` is pending, which a different idle variant would
  cause. On `homes1`, `homes2good` and `towncenter_good` the counts of actor movement
  events differ, as they do between retail runs.

This probe found four engine differences, now fixed by following `fakk2.exe`:

- **Snapshot entities** were chosen by Quake III's rules (`SV_BuildClientSnapshot` at
  `0x447dd0`, `SV_AddEntitiesVisibleFromPoint` at `0x448090`, `SV_AddEntToSnapshot` at
  `0x448000`):
  - `SVF_SENT` is set on every entity in use each time a snapshot is built, before any
    visibility test. It does not mean the entity was in a snapshot.
  - The client's own entity is added first. The viewpoint is the camera origin when
    `PMF_CAMERA_VIEW` is set.
  - An entity attached or bound to a parent is skipped when the parent is hidden
    (`SVF_NOCLIENT` or `RF_DONTDRAW`), added when the parent was added, and otherwise
    tested with the parent's areas, clusters, centroid and radius.
  - `SVF_SENDONCE` entities are always added, like `SVF_BROADCAST` ones. Entities with
    no model, no looping sound and no `SVF_SENDPVS` are never added. A looping sound
    with a minimum distance of 10000 is added without a visibility test.
  - Entities beyond the level's far plane are culled (`(farplane + 32)²` against the
    squared distance to the centroid less the squared radius).
  - The entity carrying `RF_SKYORIGIN` is added when the level has a sky portal, and
    everything visible from its origin is added with `RF_SKYENTITY`. Entities seen
    through a portal get `RF_PORTALENTITY`, or `RF_DUALENTITY` when also seen directly.
  - A snapshot holds up to 1024 entities.
- **Brush model contents** (`SV_SetBrushModel` at `0x444510`): contents become
  "everything" only when the game has not set them. OpenFAKK2 overwrote them.
- **Linking** (`SV_LinkEntity` at `0x4488f0`): a rotated brush entity's box is the bounds
  of its corners under the rotation matrix the game keeps in the entity state, placed at
  the state's origin; Quake III used a sphere. `s.solid` is packed by solid type with
  the game's own bit layout. A freed entity is unlinked and nothing else.
- **The test player started too early** (next section).

Only `SVF_SENT` is observable through `whatis`; which entities a snapshot contains has
not been compared with retail, and nothing reads the snapshots yet.

### Player entry time and stall

The retail client enters the world some frames after the map starts, once it has loaded.
The test player imitates that with `sv_testplayerdelay` (450 ms of game time, chosen so
that the dumps line up with retail's on the retail maps). Scripts that use
`waitforplayer` are released at that moment, so entering at map start instead leaves any
later-started script waiting forever.

After entering, the retail client is still busy for a few frames: 200 ms after entry no
snapshot has been built, and by 500 ms one has (seen through `SVF_SENT` with
`probe-entry.scr`). No movement command arrives in that time either. A level that opens
with a cinematic freezes the player in that gap, so the player stays at the spawn point;
a player that moves at once drops to the floor first. The test player therefore sends
nothing and gets no snapshots for `sv_enterstall` (300 ms) after entering.

OpenFAKK2's own client enters only once it has finished loading, so without the gap it
drew the player's view at once and could steer Julie until the cinematic took over:
`training` waits 0.2 s after `waitforplayer` before its fake player and first camera,
and that view, with the HUD, showed in between (2026-10-15, `probe-cine.scr` through
`retail-capture`; retail's first picture is the camera). The server now imposes the same
gap on every client that enters (`SV_ClientEnterStalled`): for `sv_enterstall` ms it
sends the client no snapshot, so the loading screen stays up, and drops its movement
commands, marking them as seen so that their copies in later packets are not run
afterwards. Not read from retail: what keeps the retail client busy in that time.

The length of the gap under retail is real time spent loading and is not the same on
every map. It decides where the player's idle animation is in its cycle, which is why
that one pending event still differs on three maps.

### With scripted input

A level script can press the player's keys at chosen game times (`stuffcmd "+forward"`).
`probe-walk.scr` reads the player, walks forward for three seconds, and reads it again.
On `zoo`:

- Before walking, all 18 fields `whatis` prints for the player match retail.
- After walking, 17 match. The position differs by 2.0 units after 275 travelled
  (retail x = -2077.05, OpenFAKK2 x = -2079.06), under half a server frame of movement.

That gap is the harness, not the game. `probe-walk-jump.scr` (walk two seconds, jump,
land, stop, player read every 0.1 s) shows every speed agreeing with retail: 91 u/s
walking, a 400 u/s jump carrying 160 u/s forward, gravity 800, the same animations at
the same times. The shortfall appears only while she slows down on the ground: 1 to 2
units at each of the jump wind-up, the landing and the stop, 4 to 6 in all (retail ends
at x = -1954.84 to -1956.16 over three runs, ours at -1960.45). Ground friction is
integrated per movement command (`PM_Friction` in `bg_pmove.c`, `drop = control *
pm_friction * frametime`), and the headless test player builds one command per server
frame (`SV_TestPlayer_Frame`, `sv_fps` 20, 50 ms) where the retail client builds one per
rendered frame. The stopping distances back this up: retail covers 9.08 units from 91 u/s
to a stop, which the per-command model gives for 30 to 40 fps; ours at 20 Hz covers 7.54,
the model's 7.63. At `sv_fps 40` ours stops at -1954.26 and through the real client at
-1957.14, both inside retail's spread. The real client first *ran* this probe at 345 u/s
because our `cl_run` defaulted to 1; retail's default is 0 (see "Cvar differences"), and
ours now matches. A sandbox `config.cfg` written by an older build still carries
`seta cl_run "1"`, since the cvar is archived; delete that line or the client keeps running.

What the retail client puts in a movement command has not been read from the binary.
Established by comparison: the player walks unless `+speed` is held. Assumed: moves of
127, and a turn rate of 140 degrees per second for `+left` / `+right`.

### Firing a projectile

`probe-sling.scr` gives the player the sling (`give weapon_sling.tik`, `use Sling dual`),
charges it for 1.5 seconds with `+attackright`, releases, and dumps every entity 0.3
seconds later. On `zoo` the rock (`fx_slingshot.tik`, entity 134) lands 864 units ahead on
both engines, within 0.02 units of retail's position.

This found one engine difference, fixed by following `fakk2.exe`. **Which entities a
trace skips** (`SV_ClipMoveToEntities` at `0x448fe0`): besides the pass entity, its own
missiles, and other missiles with the same owner, as in Quake III, retail also skips the
*owner* of the pass entity, as Quake II did. The game moves a projectile with the
projectile itself as the pass entity, and a fresh rock starts inside the player's box
(it is launched from the sling's `tag_barrel` in Julie's hand, about 19 units ahead of
her origin, and its 16-unit box overlaps her 30-unit one). Quake III's rule left the rock
colliding with Julie whenever its first frame of movement did not
carry it clear of her box: standing still and aiming steeply down, the rock stayed
bouncing at her chest instead of flying past (`probe-sling-down.scr`, run through the
real client because the test player cannot pitch; moving added her velocity to the rock
and hid the problem). The same function confirmed the inferred `SOLID_NOT` /
`SOLID_TRIGGER` rule above, and showed that when a trace starts inside an entity retail
also records that entity's number in the accumulated trace, which Quake III did not.
`SV_PointContents` has not been compared.

### Weapon effects

Compared through the real client on both engines with the same probes and screenshots
(`probe-shotgun`, `probe-rocket`, `probe-sword`, `probe-fx-guns`, `probe-fx-range`,
`probe-fx-melee`, `probe-fx-expl`; `probe-sprites` and `probe-fx-order` spawn the test
models in `tools/retail-compare/fx`). Every weapon's muzzle burst, impact puffs, holes,
explosion, soot, beams and swipes now appear as in retail. What it took:

- **Client effect blocks in TIKI frame commands** (`TIKI_ParseFrameCommands`,
  `0x44b760`). A client effect spreads its parameters over the lines between a `(` line
  and a `)` line; each of those lines, the brackets included, is a command of its own at
  the frame of the line above, with no frame selector. The client game module relies on
  it: `(` and `)` are commands (`StartBlock`, `EndBlock` in `cg_commands.cpp`), and `)`
  is what makes the effect. This port had joined the whole block onto its first line,
  cut it to ten arguments, and so never closed it: no `tagspawn`, `originspawn`,
  `tagtraceimpactmark` or other block effect on an animation frame ever ran (the
  shotgun's holes and puffs, every explosion's fire and smoke). The selectors are
  `start` or `first` (frame 0), `last` or `end` (the last frame), `every`, `exit`,
  `entry` or `enter`, and any other word outside a block is read as a number, so it is
  frame 0. Two shipped definitions rely on the extra words: `claw.tik`'s `start melee`
  and `bspurt.tik`'s `end remove`.
- **Decals are fitted to the collision model** (`CM_MarkFragments`, `0x426a90`; the
  client game module's `CM_MarkFragments` import is slot 0x1d of `CL_InitCGameImport`
  `0x41ce90`), not to the drawn surfaces as in Quake III's `R_MarkFragments`. For each
  brush the decal's box touches (`CM_BoxBrushes`, `0x42a790`, 32 at most), each side not
  flagged `SURF_NOIMPACT` or `SURF_NOMARKS` whose plane faces the projection with a
  cosine of -0.9 or less takes the decal's corners moved along the projection to one
  unit short of the plane, as long as that is within the projection's length either way,
  clipped by the brush's other sides (`0x426e00`, Quake III's `R_ChopPolyBehindPlane`
  with 16-point buffers and a 0.5 epsilon). The one-unit standoff is why retail's marks
  never fight the wall they lie on: the mark shaders have no `polygonOffset` (and the
  retail renderer applies polygon offset only from that keyword, `0x47c640`), so marks
  fitted exactly to the surface z-fought it, which was the rocket's soot. Curved patches
  take no marks. The client game module's `CM_ImpactMark` (projection 32 units, at most
  10 points a fragment) and its per-frame `CG_Decal` for a decal entity (`0x300206e0`, so
  a decal that lives a server frame is laid several times over) are retail's own.
- **Sprites** (`.spr` models). A sprite is half its shader's image wide and half its
  image high either side of its origin (`0x491e40` records the image's size), times the
  shader's `spriteScale` and the entity's scale; `spriteGen` lays the quad out
  (`0x4773c6` parses it and puts `spriteScale` back to 1; `0x491790` builds the quad):
  `parallel` faces the viewer, `parallel_oriented` faces the viewer turned by the
  entity's roll (`atan2` of its left axis's z and y), `oriented` lies in the plane of the
  entity's left and up axes (an explosion's shockwave ring), and `parallel_upright`
  stands vertical, turned to face the viewer about the world's up axis, and is not drawn
  when the view is within a degree of straight up or down. The texture runs from the
  top left as the quad is laid out, which mirrors a `parallel_oriented` sprite against a
  `parallel` one, and the triangles are wound as retail winds them. This port had
  ignored `spriteGen`, so every sprite was a viewer-facing square as wide as its image.
- **Sprites are drawn after everything else.** Retail keeps them in a list of their own
  (`0x4643c0`), sorted apart and submitted after every other surface of the view
  (`0x463e40` takes both lists). In one sorted list an alpha-blended sprite whose shader
  sorts as opaque, the explosion's `explsmoke`, was painted over by any wall or floor
  drawn after it, and the rocket's smoke cloud vanished against the floor. Within the
  sprite list the key is `sortedIndex << 22 | (2^28 - the squared distance from the
  view)`, smallest first; the distance term is 28 bits wide and is ORed over the shader
  index's low six bits, so for sprites nearer than 2048 units those bits read as ones:
  sprites whose shaders fall in the same block of 64 sorted shaders are drawn back to
  front, blocks in shader order. OpenFAKK2 computes the same key from its own sorted
  shader list, which is not numbered as retail's, so which shaders share a block can
  differ: two overlapping sprites of different shaders can come out in the other order
  (`probe-fx-order`, a few levels of brightness at the overlap).
- **Swipes** (the trails of the swords, the axe, the chain sword and the soul sucker,
  and an explosion's burning debris) are drawn, where this port had ignored them.
  `RE_SwipeBegin` (`0x480eb0`) starts one of 32 swipes a frame (the first begun in a new
  frame clears the old ones), `RE_SwipePoint` (`0x480f50`) adds an edge of two points
  and a time (1024 at most), and `RE_SwipeEnd` does nothing. Each swipe is a world
  surface without fog, added after the world (`0x480fd0`), and drawn as a triangle strip
  through its edges (`0x481060`): an edge is as opaque as its age allows,
  `1 - (thistime - time) / life`, at most 1, with its colour scaled alike; a faded edge,
  or one newer than `thistime`, is left out, and when the newest has faded the swipe is
  emptied. `s` runs from 0 to 1 over the edges, `t` is 1 at an edge's first point and 0
  at its second. A swipe no longer handed over is drawn again with `thistime` aged by
  the time since it was last drawn, so a finished swing fades out.

Still different:

- **Dynamic lights on the world** are Quake III's (see the renderer section): a muzzle
  flash, the crossbow's charge and an explosion light the floor around them far more
  brightly in retail.
- **Moving while a weapon's fire animation plays** carried Julie much further on ours: a
  0.6-second `+moveleft` right after a shotgun blast moved her to the corridor wall on
  ours and a few units in retail. Seen in passing; not looked into.

## The renderer and the map viewer

Each item below says what it was taken from: the retail renderer in `fakk2.exe` where an
address is given, and a capture of the retail game's picture where one was compared;
otherwise the SDK headers, the data, or OpenMoHAA's renderer.

The map viewer (`viewmap <name>` in the client) draws what the map file says, with no
server and no game module. That differs from the game in ways that are by design:

- **Level scripts do not run.** A script can turn the sky portal off, set the sky's
  day/night blend, and set fog and a far plane. `cliff1` does all of these at its start
  (`$world skyportal 0`, `$world skyalpha 0`, a far plane fading in), so in the game its
  sky starts as fog. The viewer always uses a sky alpha of 1, leaves the portal on, and
  keeps the far plane worldspawn gives.
- **`cliff1`'s sky is empty in the viewer.** Its sky camera, taken from the map entity,
  sits 17 units above the top of the sky dome, so the dome is seen from outside and its
  faces are culled; the planets and mountains are below the horizon of most views. The
  same model draws correctly on `homes1` and `training`, where the camera is inside the
  dome. Whether the game moves the camera or the model on `cliff1` is not known.
- **Entities are where the map puts them**, in the first frame of their idle animation.
  Nothing is hidden, moved, animated or spawned. Of a model's own initialisation only
  `surface <name> +nodraw` is applied.
- **Which models belong to the sky** is decided by testing a few points around each
  model against what is visible from the sky camera. The game's server decides this for
  real (see the snapshot rules above).

What is implemented, and on what basis:

- **`.ftx` textures**: a twelve byte header (width, height, has-alpha) and RGBA pixels,
  top row first. This is `ftx_t` in `qfiles.h` of Ritual's tools release, and the
  order `ftxconvert` reads the pixels in. All 2948 files in the retail archives fit it.
  `qfiles.h` calls the files "pre mipmapped", but none holds anything after the
  full-size image. A file whose has-alpha is 0 has its alpha set to 255; in the retail
  files those bytes are already 255.
- **Shader scripts**: `if mtex|no_mtex|0|1` ... `endif`, `nextbundle`, vectors without
  parentheses, `rgbGen constant|colorwave|fromEntity|fromClient|global`,
  `alphaGen constant|fromEntity|fromClient|global|skyAlpha|oneMinusSkyAlpha`,
  `depthMask`, `noDepthTest`, `portalsky`, `skyParms full|half`, `tcMod macro|offset`,
  animated textures of up to 64 frames. From the scripts' own usage; `surfaceColor` and
  similar are ignored. From `fakk2.exe`: `tcMod macro <s> <t>` (`0x478476`) takes the
  coordinates from the vertex's world position on the axis plane its normal faces most
  (Quake II's base axes, the table at `0x4fb844`), times 1/size and divided by the first
  stage's image size (`0x495240`, `0x495330`); `alphaGen dot|oneMinusDot [min] [max]`
  (`0x47689b`, `0x47691a`) is min + d (max - min), d the normal's dot product with the
  direction to the viewer, at least 0, or (1 - d) squared, at most 1, for `oneMinusDot`
  (`0x494120`, `0x494200`); `rgbGen colorwave <r> <g> <b> <wave>` (`0x4763d7`) scales the
  colour by the wave (`0x4943e8`). `probe-hay` on `creeperpens` (the hay's two macro
  stages, the slime pool's two colorwave stages) went from a mean of 15.7/13.1/11.0 and
  27.5/8.1/16.0 against retail to 12.8/11.1/10.6 and 4.9/8.0/5.0; what is left is the
  models' lighting and the slime wave's phase. `nextBundle` (`0x476bbc`) moves on to the
  stage's next texture and goes on parsing the same stage, and `rgbGen` and `alphaGen`
  write the stage's own fields (`0x476353`, `0x476654`), so one written after
  `nextBundle` replaces the first texture's. `probe-dot` on `swamp2` frames the suck juice
  pool, whose `oneMinusDot` stage was opaque blue and whose multitextured stage takes
  `rgbGen constant 1 .7 0` after its `nextBundle`: blue went from a mean of 5.3 against
  retail to 1.1, and over eight shots a quarter second apart the pool's red swings over
  26 to 88 on ours and 32 to 77 on retail with the alpha waves, out of phase.
  `tcMod parallax <s> <t>` (`0x4783ed`) moves the coordinates by the view's x and y times
  the rates (`0x4951f0`); `animMapOnce` (`0x476d71`) is `animMap` stopping on its last
  image, and `frameFromEntity` (`0x476c03`) shows the image the entity's frame names,
  which the game's `shader frame` sets (`R_BindAnimatedImage` `0x47cf5b`). No retail
  script uses these three.
- **What a view leaves out** (`R_ViewLeavesOut`): an entity's or a poly's render flags
  decide which views draw it as retail decides (`0x463fa2` for entities, `0x47b3f8` for
  the client game module's polys, which now keep their flags): `RF_SKYENTITY` only in
  the sky portal's view, and in a mirror or portal's view only what is flagged
  `RF_PORTALENTITY` or `RF_DUALENTITY`, which the main view leaves out when it is
  flagged for portals only; a poly flagged `RF_THIRD_PERSON` is left out of the main
  view as an entity is. Neither game module sets the portal flags, so retail's mirrors
  and portals show the world without entities; no shipped map has one.
- **Sky portals**: when a view has seen a `portalsky` surface and the scene names a sky
  origin, the scene is drawn from there first, turned the way the viewer is looking.
  Entities flagged `RF_SKYENTITY` are drawn only in that view, and others only outside
  it. From the SDK's scene definition and flag descriptions, and OpenMoHAA; the retail
  renderer (`0x47bee0`) turns the camera the same way, the viewer's axes times the sky
  axes. Its developer cvars: `r_skyportal 1` puts the camera at `r_skyportal_origin`
  ("x y z", warning "Invalid sky portal origin" otherwise) and keeps the viewer's axes
  as they are (`0x47bf76`).
- **`r_drawsprites`** (cheat, 1): 0 leaves the scene's sprites out (`0x4643c0`, the
  sprites the client game module adds as `.spr` models). `probe-sprites` with
  `+set r_drawsprites 0` drew none of its sprites on either game.
- **`r_customaspect`** (archived, latched, 1) is `r_mode -1`'s window aspect as it is
  (`R_GetModeInfo`, `0x45fe58`), where Quake III divides the width over the height by
  `r_customPixelAspect`, which retail does not have.
- **The client's definition commands** print as retail's do (`tiki.c`): `tiki <name>`
  (`0x421430`; `models/` and `.tik` added when missing) lists the animations sorted by
  alias with their file, frames, time, file length and delta, the surfaces with their
  triangles (a vertex-animated surface's count printed twice, as its vertex count too,
  as retail does) and skins, the tags or the bones with their parents, and the totals
  (`0x451f30`); `tikilist` (`0x451e00`) every animation file once with its length, then
  every definition, with the totals. `probe-tiki` on `zoo` gave the same lines as retail
  for `obj_statue`, Julie, the usage and a missing model, and the same 738 animations,
  10992312 bytes and 156 definitions; a definition's own size counts this engine's
  structures, not retail's. `cachealltikis` (`0x452490`) loads every `models/*.tik`
  with retail's progress lines; retail also rewrites its binary cache of each
  definition (`0x44d4d0`), which this engine does not keep.
- **Models from `.tik` definitions**: the `.tan` file of the animation the entity asks
  for is loaded on first use, and the entity's frame and old frame are decoded and
  interpolated by `backlerp`, as Quake III does with MD3 frames; vertexes are scaled by
  the definition's `scale` and moved by its `origin`. Surfaces take the shader of the
  skin the entity's surface flags select, and `MDL_SURFACE_NODRAW` hides one. A
  surface's skins may be named on one `shader` line or on several (`obj_button.tik`
  names red and green, inactive and lit, on four lines); the first named is skin 0.
  Until that was read, each line replaced the last and every button was green. A shader
  name with an extension is a texture under the path in force on that line, which
  starts as the definition file's own directory and changes at its `path` line; a name
  without one is a shader script's. From the retail parser (`TIKI_ParseSetup` `0x44c170`
  looks for a `.` and prefixes the path at that moment; `TIKI_LoadTIKIfile` `0x44ca70`
  seeds the path from the file name through `0x44a440`). The one definition where the
  order matters is `ammo_gasplant.tik`, whose surface line precedes its `path` line:
  retail reads its texture as `models/monster/gasyerass2/gasyerass2.tga`, which exists,
  and the shipped `.cik` cache agrees. Until this was read the path was applied after the
  whole file was parsed, the texture was looked for under `models/ammo/gasplant/`, and the
  swamp's gas pods drew as nothing. The retail engine also loads the shipped `.cik` cache
  in preference to the `.tik` (`TIKI_RegisterTiki` `0x44d5e0` tries `TIKI_LoadCachedTIKI`
  `0x44d3a0` first); OpenFAKK2 parses the `.tik`, and on the retail data the two agree on
  every surface's shader. The file layout is `qfiles.h`'s `tikiHeader_t`, `tikiFrame_t`,
  `tikiTag_t`/`tikiTagData_t`, `tikiSurface_t` and `tikiXyzNormal_t` (Ritual's tools
  release), as `max2tan` writes it: frames after the header, then each tag's name and
  per-frame data, then the surfaces with their triangles, collapse map, texture
  coordinates and packed vertexes. All 569 version 2 files in the retail archives have
  exactly this layout, and in all of them a frame's `offset` is its lower bound and its `scale` is
  its size over 65535. Of what is read, the file's own frame bounds and radius and the
  collapse map and `minLod` (level of detail) are not used; the bounds are taken from
  the vertexes. The vertex normal is two angles in a short, as in Quake III: `max2tan`'s
  `MyNormalToLatLong` puts the angle around z in the high byte and the angle from +z in
  the low one, both at 255/360 of a step per degree, while the decoder takes 256 steps a
  turn as Quake III's does (under a degree off). How retail decodes the normal has not
  been read; `fakk2.exe` holds no floating point constant for either step, so it
  probably uses a table. Retail's `TIKI_LoadAnimTan` (`0x449a10`) refuses a version
  other than 2 or no frames and only warns past 1000 vertexes or 2000 triangles a
  surface; ours refuses past 1200 and 4096 (`qfiles.h`'s `TIKI_MAX_VERTS`,
  `TIKI_MAX_TRIANGLES`). The largest retail surface has 601 and 787. One `.tan` in the
  archives is empty and no `.tik` names it. The origin is added before scaling, as
  `TIKI_Tag_Orientation` (`0x44fe40`) places tags. Cross-blending, torso animations and
  level of detail are not applied.
- **Skeletal models** (`.skb` with `.ska` animations): the skeleton's bones and
  weighted vertexes are read at registration and each animation's frames on first use.
  Bones are composed root first the way `TIKI_Tag_Orientation` (`0x44fe40`) does it: a
  bone's rotation is its quaternion times its parent's (Hamilton product, x y z w), then
  the entity's bone controllers; its origin is the parent's plus the bone's offset
  turned by the parent's axes (`0x43c800`, Quake's axis convention; the matrix is
  `0x43c980`); the root gets the definition's origin and everything is scaled at the
  end. Frame quaternions (x y z w) are stored as shorts times 32767 and offsets times
  63 (`qfiles.h` `TIKI_BONE_QUAT_MULTIPLIER`, `TIKI_BONE_OFFSET_MULTIPLIER`), and retail
  decodes them with 1/32767 (`0x4d57dc`, read by the tag code and `R_AddSkelSurfaces`)
  and 1/63. Every frame quaternion in the 1140 retail version 3 `.ska` files has a
  length within 2 of 32767. OpenFAKK2 divided by 32768 until the format audit; the
  difference is 3 parts in 100000. Vertexes
  are the weighted sum of each bone's placed offset. The file layout is `qfiles.h`'s
  `skelHeader_t`, `skelBoneName_t`, `skelSurface_t`, `skelVertex_t`, `skelWeight_t`,
  `skelAnimHeader_t`, `skelFrame_t` and `skelBone_t`, as `max2skl` writes them: the
  version 3 files in the archives (29 `.skb`, 1140 `.ska` and `marktest.ska` below) all
  fit it apart from `marktest.ska`. Every bone's parent
  is -1 or an earlier bone, and a `.skb` bone's flags are `TIKI_BONEFLAG_LEG` (1, for a
  `LEGBONE`) or 0. No vertex has more than 6 weights (`max2skl`'s default limit is 8).
  Retail's `TIKI_LoadSkel` (`0x449fd0`) refuses a version other than 3 or more than
  100 bones and only warns past 1000 vertexes or 2000 triangles a surface. Not read: a
  `.ska`'s `type`. `max2skl` never sets it, and 29 retail files hold the uninitialised
  `0xcccccccc`. The collapse map (level of detail) is not read either. The archives also
  hold version 1 and 2 skeletons and animations (6 `.skb`, 132 `.ska`), an empty `.ska`, and
  `models/monster/grawlix/marktest.ska`, whose layout does not fit; no `.tik` names
  any of them. From the retail renderer's
  `R_AddSkelSurfaces` (`0x4872d0`): a bone whose `.skb` flags are exactly 1 (Julie's
  pelvis, spine and legs) takes the entity's legs animation and every other bone the
  torso animation, when `usetorso` is set; either animation alone moves every bone;
  the bone controllers apply only when `useAngles` is set (the controller quaternion
  multiplies the composed rotation, as for tags). Frames are interpolated by slerp
  from the old frame towards the current one (`0x43ca60`, the shorter way round, linear
  within 1e-6 of parallel), and not at all without `r_lerpmodels`; the result is then
  blended from the cross-blend frame the same way by the cross-blend lerp (the share of
  the animation itself), when that lerp is not negative (the same branches of
  `0x4872d0`; only the cross-blend animation and frame are read, not the old frame and
  backlerp the module also fills in). Still assumed: turning normals with the vertex's
  bones. Level of detail is not applied in the renderer. `r_showskel 1` (cheat) draws a
  skeletal model's bones in place of its surfaces, once a frame: each bone's axes four
  units long in red, green and blue and a cyan line from its parent (`0x495db3`,
  `0x496042`); `probe-skel` gave Julie's skeleton pixel for pixel as retail's.
- **Ritual fonts** (`fonts/<name>.RitualFont`, the `.urc` interface's and the prints'
  text): `RitFont`, `height`, `aspect`, 256 `indirections` and up to 256 `locations`
  `{ x y w h }` in pixels of a 256-wide image `256/aspect` high, registered as
  `gfx/fonts/<name>`. This is what Ritual's `fontgen` writes and what the retail loader
  (`0x481190`, a function Ghidra leaves undefined) reads; it stores a glyph as x/256,
  y*aspect/256, w/256, h*aspect/256 and needs `aspect` before `locations`, as every
  shipped font has it. The retail text draw (`0x481690`, `re` export table at `0x461f28`) and
  string width (`0x481a40`) advance by the glyph's `w` alone, add nothing for a
  character without a glyph, count a tab as three space glyphs, and draw every glyph
  as tall as the font's `height`, whatever its rectangle's height (`facfont-20`'s glyphs
  are 22 pixels, so the prints' text is squeezed into 20; arial's and comic's 19 and 15
  are stretched to 20 and 16). `fontgen` leaves one pixel after each glyph in the image
  for filtering, and the two factors the draw multiplies by (`0x4fa79c`, `0x4fa7a0`) are
  1 and never written. Until the format audit OpenFAKK2 advanced by `w + 1`, gave a
  missing space a third of the height and drew each glyph at its own height: a centre
  print in `facfont-20` came out 36 pixels wider than retail's. With the fix,
  `tools/retail-compare/probe-font.scr` puts the centre and location prints where
  retail does and ending on the same pixel; of the text's rows, 6.5% and 8.3% of the pixels still differ
  (outlines, and the scene behind), against 28% before.
- **Sprites**: a `.spr` model is the shader of that name, sized, laid out by its
  `spriteGen` and sorted as the retail renderer does it (read from `fakk2.exe`; see
  "Weapon effects" under "With a player present"). Its reach for the fog test is the
  image's full width times `spriteScale` and the entity's scale (`0x4643c0`).
  Until `spriteGen` was read the crosshair (`oriented`, turned by the game to the
  surface its trace hits) stood upright and half sank into the floor; it now lies on
  the floor as in the retail capture (`tools/retail-compare/probe-crosshair.scr`). Not
  done: the retail sprite path also skips a quad whose corners are all outside one
  frustum plane, which only saves drawing.
- **The entity's scale** (`refEntity_t.scale`, the entity state's `scale`, which maps and
  scripts set: `intro`'s dropship is placed at 0.02, Obadiah in `swamp1` at 2.0) multiplies
  a `.tik` model's vertexes, bounds and radius on top of the definition's own scale, as the
  SDK's field description says and as the tag functions already did. Until it was applied
  an attached model sat where the scaled tag is while its parent drew unscaled.
  `cl_showRefEnts <text>` prints every entity the client game module adds whose model name
  contains the text, and each scene's camera, which is how this was found.
- **Render flags** (`RF_*`) use FAKK2's values throughout, so that the client game
  module's flags mean the same thing to the renderer. The `refEntity_t`, `refdef_t`
  and `glconfig_t` layouts are the SDK's, since the module fills them in.
- **A scene without a world** (`RDF_NOWORLDMODEL`: the inventory's model windows)
  lights its TIKI models with the flat ambient Quake III gives such scenes; the TIKI
  path had skipped the lighting setup there and drew them black.
- **Vertex-lit world surfaces** (the triangle soups the map compiler makes of the
  models placed with `make_static`, lit per vertex at compile time; `swamp1`'s trees
  and branches) take the compiler's vertex colours in their first stage whatever
  `rgbGen` the shader script gave it, unless the shader says `surfaceparm nolightmap`
  (`R_FindShader`, `0x4759b0`, after parsing a script for a `LIGHTMAP_BY_VERTEX`
  lookup sets stage 0 to `exactVertex`; a surface without a script gets the same
  default). `swamp1`'s `swampbranch` says `rgbGen lightingDiffuse`, which on the world
  entity is black, and `swbranch2` says nothing, which is full bright; the branches
  came out as black silhouettes among bright leaves until this followed retail. Seen
  from three cameras in `swamp1` (`probe-swbranch.scr`) the branches and leaves now
  match the retail capture to within a unit or two.
- **The light grid** is 192 x 192 x 320 units a sample (`fakk2.exe` `0x4839c0`), not
  Quake III's 64 x 64 x 128 (and the retail engine reads no `gridsize` key). With the
  Quake III size no FAKK2 map's grid lump was the expected length, so "light grid
  mismatch" dropped it and every model was lit with the flat ambient a world-less scene
  gets, plus a minimum light add this port had made unconditional. The training room's
  door button, whose origin is inside the door frame, came out twice as bright as retail.
- **Entity lighting** follows `R_SetupEntityLighting` in the exe (`0x48ed10`) where it
  differs from Quake III: only an entity the game flags `minlight` gets the 32-unit
  minimum add, a `fullbright` entity skips the grid, a dynamic light reaches only its
  radius (`radius * 7500 / d^2`) and does not bend the light direction, directed light is
  held to 255 (scaling the ambient with it), and a `lightoffset` entity (a sentient
  flashing when hit) adds its colour to the ambient. A TIKI model is lit from the centre
  of its frame's bounds, not its origin (`0x48cff0`): the bounds the animation file
  stores for the frame (a `.tan` frame's at byte 0, a `.ska` frame's at byte 0), not
  those of the vertexes, and a light cube's radius is the file's own frame radius (byte
  60 of a `.tan` frame, 24 of a `.ska` frame) times the scales and a half, which the
  vertex bounds had made about half as large. The centre is moved by the definition's
  `lightoffset <x y z>` (setup section, `TIKI_ParseSetup` `0x44c3eb`; added at `0x48d0b2`
  and scaled with the model). No retail model has one; `probe-lightoff.scr` with
  `fx\test_lightoff.tik` (the statue lit from 168 units higher) gives the same dark
  statue on both games, within half a level. `r_debugLight 2` prints every entity's light
  origin, ambient, directed light and direction.
- **Entity lights and the light cube** (`tr_entlight.c`). A model the game flags
  `betterlighting` (`RF_EXTRALIGHT`: the player and the actors) or `invisible` is not lit
  from the grid at all. The BSP's `ENTLIGHTS` lump holds the map's static lights (56
  bytes each: origin, colour, intensity, leaf, a trace flag, a spot flag, spot direction
  and radius; `0x4830c0`) and `ENTLIGHTSVIS` the lights that reach each leaf (`0x483220`;
  `LIGHTDEFS` is not read by the renderer). For such a model (`0x48e170`) a six-sided
  light cube in the model's axes starts from the worldspawn's `ambientlight` plus the
  game's `lightoffset` or `minlight` (`0x48d3d0`), adds `suncolor` where a trace along
  `sundirection` reaches the sky (`0x48cd50`), and the lights of up to four leaves the
  model's bounds reach (centre and eight probes half a radius out, `0x48d1d0`), each once
  and none from an area the view's area mask hides: a point light adds
  `intensity * 7500 / d^2` to the faces that face it (`0x48dd80`), a spot light only when
  the cube's centre is inside its cone, soft over the outer 32 units (`0x48d990`), a light
  of negative intensity only to invisible models, and a light with the trace flag only
  when nothing solid is in the way. Each vertex takes the three faces its normal points at,
  weighted by the normal's components (`0x48e6e0`); with dynamic lights in the scene
  those are added per vertex instead and the colour is held to 255 by scaling
  (`0x48e2e0`). `r_light_emphasize` and `r_light_emphasizePercent` scale the cube as
  retail's cvars do (both 0); `r_entityLights 0` turns the lights off. Julie seen from
  the same camera matches the retail capture within a unit or two on every patch
  measured; `r_debugLight 3` prints each model's cube and the lights that went into it.
  Retail's three aids for the cubes (archived, 0 by default) are drawn as it draws them,
  at the nearest depth so over everything: `r_light_lines <n>` joins each light that
  reaches a cube at least `n` strongly to the cube's centre and marks it with six arms as
  long as the cube's radius, in its colour times its strength over 255, brought back to
  unit length when brighter than white (`0x48d810`, from the point and spot lights);
  `r_light_sun_line 1` draws the trace towards the sun, yellow at the model when it
  reached the sky and black when not, white at its far end (`0x48cdc6`); `r_light_nolight
  1` lights every cube full white (`0x48e1c3`). The retail renderer draws the lines as it
  lights each model; the cubes are built in the front end here, so the lines are kept in
  world space and drawn after the view's surfaces are sorted. `r_light_int_scale` ("0.05")
  is registered and never read, as in retail. `probe-llines` frames Julie on `zoo`: her
  lines and stars and her full white cube matched retail's shots.
- **Dynamic lights** are Quake III's; of FAKK2's light types `additive` is honoured
  and the lens flare bits give the light a lens flare (next). On the world they are much
  dimmer than retail's: a muzzle flash that lights the floor yellow around Julie in
  retail barely shows here (`probe-fx-guns`). Swipes are drawn (see "Weapon effects").
- **Debug lines**: the game module's (`G_DebugLine`: `cam show`, the AI's node display,
  `csys_draw`) and the client game module's are drawn as `fakk2.exe` draws them. The
  renderer is handed the game's array and count (`ri.DebugLines`, as retail's refimport
  at `0xcdbd50`); `R_DebugLine` (`0x461fb0`) adds a solid, one pixel line to it, up to
  `g_numdebuglines` (which the renderer registers too, "4096", latched), and the game
  clears it each server frame. `R_RenderView` (`0x4646d0`) draws them after sorting the
  view's surfaces (`0x462060`), except in a view without the world or the sky portal's:
  unblended in their colour, depth tested without writing depth, or with
  `r_debuglines_depthmask 1` written at the nearest depth (`glDepthRange 0 0`), so in
  front of everything; with `r_stipplelines 1` (the default) each line keeps its own
  stipple pattern and width (`G_LineStipple`). Both cvars are archived and "0" and "1"
  by default. `probe-dlines` on `zoo` draws the `csys_draw` axes from inside a box and
  matched retail's shots, both depth tested and with `r_debuglines_depthmask`.
- **The player's buttons and the inventory bar.** `default.cfg` binds `+attackleft`,
  `+attackright`, `+use` and `+cameralook`, which Quake III's client does not know, so
  the keys fired nothing (an unknown command goes to the server as chat). The client
  registers FAKK2's buttons by their bit in `usercmd_t.buttons` (`+attackright` 0,
  `+attackleft` 1, `+sneak` 2, `+run` 3, `+holster` 4, `+use` 5) and `+cameralook`
  as a client flag. As retail's `CL_MouseMove` does (`0x41e3fc`), the mouse always adds
  its pitch (`m_pitch`) and yaw (`-m_yaw`) to a camera offset, and while `+cameralook` is
  held it turns nothing else; the client game module reads the offset, the flag and
  `centerview`'s reset (`0x41de76`) through `get_camera_offset` (`0x41ff70`) and turns
  its third person camera about the player by the offset while the key is held or the
  game allows it (`CF_CAMERA_ANGLES_ALLOWOFFSET`), which the stub had left still. Not
  compared with a capture: the probes cannot move the mouse. The number keys and the wheel are `warpinv <slot>`,
  `nextinv` and `previnv`, which the retail client handles itself (`0x408dc0`):
  `global/inventory.scr` defines the slots and their items, the bar shows the slot's
  items the player carries (named by the player state's `inventory_name_index`
  through the item config strings), and while it is up the attack keys equip the
  highlighted item by sending the game `use "<item>" lefthand|righthand|dualhand`
  (`0x409e50`, `0x40a090`), or a bare `use "<item>"` for one that is used; the bar goes
  away five seconds after its last change. OpenFAKK2 does the same in `cl_inv.c`,
  drawing the slot buttons and item frames from the script's shaders, each frame's
  model turning ("move spin") or rocking ("move bob") in its `modelwindow`, and the
  item's ammo as the script's bar (`barshader`, filled by the count over the limit from
  the player state's ammo arrays) with the count beside it. The HUD's `invmodelrender
  <n>` widgets (`weaphud.urc`, `dualweaphud.urc`) show the wielded weapons the same way,
  the item being the one `activeItems[n]` names (2 the left hand, 3 the right, 4 the
  two-handed one). The bar's layout and its model windows are the retail client's
  (`0x409950`, its helper `0x407dd0`, and captures of the bar on `training`): the slot
  buttons run down the right edge from `inv_vert_offset`, and beside them, from the same
  top, a column holds a frame for every item of the current slot, with the model only in
  the ones the player carries; a window looks down +x with a 30 degree field of view
  each way, the model sits 1.8657 times its largest extent ahead (so it fills the window
  and crops at the edges), centred by its bounds, then moved by the script's `offset` and
  by `rotateoffset` turned with it, and turned by its `angles`; only the highlighted
  item moves, a `spin` adding the deltas times the time in 333 ms units, a `bob` the
  deltas times a sine of that. The HUD's windows use the item's `hud` values the same
  way, which puts the sword upright with its handle close up as retail does.
  `inventory` (the `i` key) is retail's full-screen inventory, which switches the
  video mode (`0x40ab60`, `ui_newvidmode`) and draws a screen of its own; here it opens
  a screen of this client's own layout, a row per slot with the items the player
  carries, which the arrows, the wheel and the number keys move through, the attack
  keys equip from, and escape closes.
- **The inventory bar under a menu (deliberate).** Retail leaves the weapon bar drawn
  over a menu: Escape and `pushmenu` both reach the same push (`0x40bf90`), which closes
  the full-screen inventory instead of pushing (`0x408470`) but leaves the bar alone,
  and the bar's own five-second close event (`0x408ac0`) is all that removes it, so
  the bar covers the main menu's buttons for the rest of those seconds
  (`probe-menu`: retail's bar is still over the menu 4 s after it opened and gone by
  12 s). OpenFAKK2 closes the bar when a menu is pushed and does not draw it while a
  menu is up, so the main menu comes up clear.
- **Shader parameters from the entity.** FAKK2's shader scripts may write
  `fromEntity` for a wave parameter (`alphaGen wave sin 0 fromEntity 0 0.6` on
  `textures/training/floor_voobing`, the training hall's flashing floor pads) and for
  `tcMod scroll|offset|rotate` values. The game's `shader wavebase|waveamp|wavephase|
  wavefreq` entity commands pack them into the entity state's `surfaces[0..3]` (base
  and phase signed 3.4 fixed point, amplitude and frequency 4.4; `q_shared.h`
  `*_TO_PKT`) and `shader translation|offset|rotation` into `tag_num`/`skinNum`, which
  the client game module hands the renderer as `shader_data` (4.11, 1.14 and 9.6 fixed
  point). The renderer read such a word as zero, so the pads never flashed; it now takes
  the value from the current entity at draw time (`R_EntityWave`, the tcMod cases in
  `ComputeTexCoords`). Which packed field feeds which parameter was taken from the
  game's `Entity::Shader`, not read from the retail renderer; the training pads match
  the retail capture at 25 seconds.
- **Lens flares** (`tr_lensflare.c`) follow the retail renderer: `0x48b690` reads
  `scripts/lensflare2.txt` into three sets (`sun`, `entity`, `dlight`; defaults
  `dot_min` .8, `fullscale` .7, up to 20 flares), `0x48bf90` registers
  `textures/sprites/corona` with them, and `0x48bb20` draws them after the world in a
  -1..1 screen space: a light's point P comes from the view's matrices (`0x48b030`),
  flare i sits at P (1 - 2 where) with a quad of +-size in that space (so wider than
  tall on a wide screen) and alpha = alphascale * t, t = (dot - dot_min) / (1 - dot_min);
  it shows only when P is inside the view, dot > dot_min and a world trace
  (`CM_BoxTrace`, `CONTENTS_SOLID`) from the eye is clear: to the light itself, or to a
  far point 16384 units along its direction that must hit a `SURF_SKY` surface when the
  light is in the portal sky (`0x48af10` turns a sky-camera point by the sky's axis
  into the view's world). The set's full-screen blend (`0x48b5a0`) is white at
  fullscale * t^4, fading over `fullfade` ms once the light has gone. The sun is
  worldspawn's `sunflare` (`0x48bf50`), in the sky camera's world when
  `sunflare_inportalsky` is set. The other two sets are used the other way round from
  their names: `0x48bf90` reads the file's `entity` section into the object at
  `0x96d790` and its `dlight` section into `0x96d5e8`, and `0x48bb20` draws `0x96d5e8`
  for `RF_LENSFLARE` and for lights, `0x96d790` for `RF_VIEWLENSFLARE`. So a
  refentity with `RF_LENSFLARE` gets the dlight set in its `shaderRGBA` (only the
  small ghost flares, nothing at the light itself), plus a corona quad of
  `r_lightcoronasize` (default .1, archived) half-size at the light whenever it is in
  front of the eye and visible (`0x48b3c0`); a dynamic light with the `lensflare` or
  `viewlensflare` bit gets the same in its colour; an entity with `RF_VIEWLENSFLARE`
  gets the entity set, with its .2 flare at the light. Giving `RF_LENSFLARE` the
  entity set had put that big flare on `training`'s shooting targets, three times
  retail's light on a target faced squarely; with the sets the right way round the
  light each flare adds matches retail's within 2% (`probe-lensflare.scr`, a camera on
  the four targets with and without `+lensflare`), and the intro's Lochnar (a green
  light with `+lensflare`) shows retail's small corona and ghost rings at 13 seconds.
  A refentity's `RF_SKYENTITY` bit must match the view's `isPortalSky` (`0xcdc688`),
  so the main view leaves sky entities out, and only the sun's set draws its
  full-screen blend (`0x48b5a0` on `0x96d448`). Not read from retail: the sun's colour
  (white here), what happens to a flare in the sky portal's own pass (retail draws in
  the back end after every draw-surface list, and tests the view against the front
  end's `tr.viewParms`; here every light is drawn once, in the main view). `r_sunflare "x y z"`
  (with `r_sunflare_inportalsky`) places the sun by hand as retail's developer cvar
  does: anything but its default "0" is read as the sun's position (`0x48bc92`), so
  "0 0 0" is the world's origin. `r_place_sunflare 1` writes where the view's centre
  meets the world (a trace 16384 units out, 0.01 back) into `r_sunflare` every frame and
  leaves out the sun's full-screen blend (`0x48bb57`, `0x48bf13`); `probe-skel` got the
  same point on both games to the fifth decimal. The sun flare was compared with the sun
  put at `training`'s
  player start, ours and retail show the same disc and ring at the same strength. The
  2D "fromClient" colour: a stretched picture's `rgbGen/alphaGen fromClient` stages read
  the entity colour, and read it when their batch is flushed, so `RB_SetColor` copies
  the 2D colour there and first flushes any queued pictures whose shader reads it
  (without that, every flare took the next flare's colour and the full-screen blend
  took the reset colour and whited the screen out). `r_lensflare 0` turns the flares
  off, `2` prints each set's dot product and bearing four times a second.
- **The far plane and its fog** come from the scene (`refdef_t.farplane_*`, which the
  client game module fills from `CS_FOGINFO`; the game sets it from worldspawn's
  `farplane`, `farplane_color` and `farplane_cull`). Read from the retail renderer: its
  `R_SetupFrustum` (`0x462c30`) gives a view through a portal no far plane; otherwise
  `r_farplane` (with `r_farplane_color`, default ".5 .5 .5", and `r_farplane_nocull`)
  overrides the scene's, and a non-zero distance adds a fifth frustum plane facing back
  at that distance along the view axis, which `R_CullLocalBox` (`0x462250`),
  `R_CullPointAndRadius` and the world walk (`0x486440`, plane bit 16) test while the
  cull flag is set, and sets `GL_FOG_END` to the distance and `GL_FOG_COLOR` to the
  colour times `identityLight` unless `r_farplane_nofog`; `GL_SetDefaultState`
  (`0x4605d0`) makes the fog linear and nothing moves its start from 0. The projection's
  own far distance is not changed. `RB_BeginDrawingView` (`0x471110`) clears the colour
  buffer to the colour times `identityLight` for a view with a far plane that is not a
  portal view, which is what shows wherever nothing is drawn (`swamp1` has no sky
  surfaces at all) and which covers the sky portal's view, drawn before — so under a far
  plane a map's sky is fog. What a surface draws under the far plane follows
  `RB_BeginSurface` (`0x47c510`) and `FinishShader` (`0x478a60`). A shader of one
  `lightingDiffuse` stage with no `alphaGen` (models) is drawn with `GL_FOG` on, through
  a state bit in `GL_State` (`0x470ad0`), when `r_useglfog` (retail default 1) is on.
  Every other shader draws a second stage list that `FinishShader` builds: its stages,
  their colours faded by each vertex's distance from the viewer over the far plane
  distance (`0x494910`, at most 1) where the stage's blend allows it
  (`adjustColorsForFog`, as for fog volumes; `ComputeColors` `0x47d400`), then a pass of
  a white image in the far plane's colour (`rgbGen` 16, `0x4948a0`) with that distance
  for alpha, blended `src_alpha one_minus_src_alpha` with the last stage's depth bits. A
  shader whose first stage blends and fades gets no pass and fades out instead, and no
  fog volume applies as well (`tess.fogNum` is 9999). The pass uses the colour times
  `identityLight`, as the clear and `GL_FOG_COLOR` do (retail uses the raw colour there;
  the same at the default overbright). Neither 2D drawing nor the sky under a far plane
  that does not cull is fogged; a sky under one that culls keeps OpenGL's fog here
  (retail hands its sky iterator the far plane list; not read). Before this, OpenFAKK2
  fogged every shader with OpenGL's fog, and on the Intel driver the world came out
  unfogged, with the far plane's cull edge in plain view on `swamp1`. Checked on
  `swamp1` from the player's start against the retail picture: the sky, the far tree
  and the far bank sample the same colours (within one step of 255), the water fades
  into the far plane without an edge; models stay fogged with `r_useglfog 0` too. The
  map viewer takes the far plane from worldspawn's keys directly, with the game's
  defaults (none, black, culling on); a level script's later changes to it are not seen
  there.

Not implemented: level of detail (the `.tan` collapse maps and `minLod` are read, the
`.skb` collapse maps are not, and neither is applied). Not compared with retail: fog
volumes (`fogParms` brushes on `cemetery`, `water` and the swamps), which go through
Quake III's fog code.

## Not yet comparable
- Input beyond walking in a straight line: turning, jumping, weapons, using objects,
  triggers the player must walk into. The test player can now be given key presses, but
  only a forward walk has been compared.
- What is sent to clients (snapshots, sounds) and what the client game module makes of
  it. The test player shows `print` text on the console as a client would and discards
  everything else; the real client draws the module's picture, which has not been
  compared with the retail game's.
- Save and load.
