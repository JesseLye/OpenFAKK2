# Known differences from the retail engine

This file tracks where OpenFAKK2 is known or suspected to behave differently from
`fakk2.exe` 1.02, and which rules were inferred rather than read from the binary. It
covers the server side in detail; the renderer has a section of its own near the end.

How the comparison is done is described in `tools/retail-compare/README.md`. As of this
writing, the lines the game module prints during map spawn (`developer 1`) are identical
on all 30 retail maps, with either the retail or the rebuilt `gamex86.dll`.

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

From `cvarlist` after loading `zoo` on both engines. Only server-relevant ones are listed;
retail also has about 70 client, sound and input cvars that OpenFAKK2 lacks until the
client is ported.

| Cvar | Retail | OpenFAKK2 | Note |
| --- | --- | --- | --- |
| `cheats` | 1 after `map` | 0 | The game reads `cheats`. Quake III enables cheats only for `devmap` |
| `protocol` | 1 | 71 | ioquake3's value; the wire format is OpenFAKK2's own for now |
| `nextmap` | `map zoo` | `map_restart 0` | Retail sets it to reload the same map |
| `sv_pure` | 0 | 0 | Was 1; Quake III's pure checksums cannot account for native game modules |
| `sv_timeout` | 120 | 200 | |
| `com_maxfps` | 85 | 125 | |
| `sv_allowdownload` | 1 | 0 | |
| `g_skill` | 1 | not registered | Registered by the retail engine, not the game |
| `paused` | 0 | not registered | OpenFAKK2 has `sv_paused` / `cl_paused` |

## Rules inferred, not read from the binary

These produce results that agree with retail wherever they have been exercised, but the
corresponding retail code has not been examined.

- **Traces skip entities whose solid type is `SOLID_NOT` or `SOLID_TRIGGER`.** Inferred
  from an actor seated in a non-solid chair (`homes1`) and items inside brush entities
  (`towncenter_evil`).
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
  function and identity from the lerped one). Bone quaternions scale by 1/32768 as in
  retail (the TIKI code used 1/32767).
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
  Miles' documented model and has not been read. `S_StartLocalSound` (`0x43ed80`)
  resolves an alias, registers the name and plays it on the listener's entity on
  `CHAN_MENU`; the channel numbers are FAKK2's. Not followed: `maxnumber` and
  `maxfactor` (Quake III's limit of 4 or 8 like sounds per entity stays), loop blocks,
  the `s_show` print is retail's format only; `S_StopSound` stops the entity's channel
  (the retail rule has not been read); the `.mp3` dialog files (1015 of them) are decoded
  whole with dr_mp3 when registered, where the retail engine streams every `.mp3` name
  through Miles (`0x45bf00` sends them down a separate path); ambient-volume
  calls are ignored; looping sounds are keyed by their
  order in the frame. `s_soundinfo [name]` prints the table's size or a name's entry.
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
  sound backends; not by ear.
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
  with the model's shader at the size the entity's scale gives (`R_AddEntitySurfaces`).
  The intro's trails match the retail capture at 60 seconds.
- **Level changes.** The game module ends a level with `gamemap <next>` (`G_ExitLevel`),
  which Quake III names but this tree had dropped; it is the `map` command again. The
  `stopsound` command it sends first stops all sounds.
- **The console** has no `console` shader in FAKK2 (the retail console was a window);
  it is drawn on a dark translucent sheet rather than the default shader's black.
- **`in_nograb`** is not archived here (it is in Quake III), so a harness run that sets
  it does not leave the next normal run without a mouse.
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
  prefix; a new sound on the menu channel stops the one before it. Sliders
  (`setrange`, `stepsize`, `slidertype`) drag their thumb (the 8x16 `slider_thumb`
  images) and set their cvar; check boxes draw their 16x16 image at the left of their
  rect; a label with a `linkcvar` shows the cvar's value through its `linkstring`
  names; a pulldown (`menushader`, `addpopup`) opens a list under its arrow and runs
  the entry's command. The retail client's return to the menu (`0x4212f0`) puts on
  `music/intro.mus` when no map is active; so does this one's, when it shows the main
  menu. `togglemenu` without a name toggles `main`, which is what `default.cfg` binds
  to Escape. The FakkBindList widget reads its `filename` script (`ui/bind.scr`:
  headers, widths, colours, sounds, `binditem` rows) and shows each command's first two
  bound keys; a clicked key cell takes the next key pressed in place of the old one,
  Escape leaves it, the wheel scrolls. Its rows are set in `verdana-12` (a guess from
  the retail capture; the script names no font). The font scripts say `rgbGen
  identity`, but retail draws its text in the menus' colours, so the renderer gives
  `gfx/fonts/*` stages the caller's colour. Not done: keyboard in menus beyond
  Escape, Field and List editing, `ui_newvidmode` (the resolution box stays empty),
  borders, `invmodelrender`, `tileshader` tiling (drawn stretched), `ordernumber`,
  the `paused` cvar retail sets at the menu. The menu shaders use `rgbGen global` /
  `alphaGen global`, the colour the widget sets (its `fgcolor`): the renderer maps them
  to the vertex colour. A stage written with `nextbundle` is now one multitexture
  stage (`CollapseNextBundles`); it was two passes, the second darkening the frame.
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
nothing and gets no snapshots for `sv_testplayerstall` (300 ms) after entering.

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
  Probably the key press landing in a different part of the frame; not investigated.

What the retail client puts in a movement command has not been read from the binary.
Established by comparison: the player walks unless `+speed` is held. Assumed: moves of
127, and a turn rate of 140 degrees per second for `+left` / `+right`.

## The renderer and the map viewer

None of this has been compared with the retail game's picture, and none of the retail
renderer has been read. It follows the SDK headers, the data, and OpenMoHAA's renderer
where noted.

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

- **`.ftx` textures**: a twelve byte header (width, height, has-alpha) and RGBA pixels.
  From the files: all 2948 in the retail archives fit this.
- **Shader scripts**: `if mtex|no_mtex|0|1` ... `endif`, `nextbundle`, vectors without
  parentheses, `rgbGen constant|colorwave|fromEntity|fromClient|global`,
  `alphaGen constant|fromEntity|fromClient|global|skyAlpha|oneMinusSkyAlpha`,
  `depthMask`, `noDepthTest`, `portalsky`, `skyParms full|half`, `tcMod macro|offset`,
  animated textures of up to 64 frames. From the scripts' own usage. Approximations:
  `tcMod macro` is a plain scale, `alphaGen dot|oneMinusDot` is opaque, `colorwave`
  ignores its colour, `spriteGen`, `surfaceColor` and similar are ignored.
- **Sky portals**: when a view has seen a `portalsky` surface and the scene names a sky
  origin, the scene is drawn from there first, turned the way the viewer is looking.
  Entities flagged `RF_SKYENTITY` are drawn only in that view, and others only outside
  it. From the SDK's scene definition and flag descriptions, and OpenMoHAA.
- **Models from `.tik` definitions**: the `.tan` file of the animation the entity asks
  for is loaded on first use, and the entity's frame and old frame are decoded and
  interpolated by `backlerp`, as Quake III does with MD3 frames; vertexes are scaled by
  the definition's `scale` and moved by its `origin`. Surfaces take the shader of the
  skin the entity's surface flags select, and `MDL_SURFACE_NODRAW` hides one. Assumed:
  that vertex normals use Quake III's two-angle encoding. The origin is added before
  scaling, as `TIKI_Tag_Orientation` (`0x44fe40`) places tags. Cross-blending, torso
  animations and level of detail are not applied.
- **Skeletal models** (`.skb` with `.ska` animations): the skeleton's bones and
  weighted vertexes are read at registration and each animation's frames on first use.
  Bones are composed root first the way `TIKI_Tag_Orientation` (`0x44fe40`) does it: a
  bone's rotation is its quaternion times its parent's (Hamilton product, x y z w), then
  the entity's bone controllers; its origin is the parent's plus the bone's offset
  turned by the parent's axes (`0x43c800`, Quake's axis convention; the matrix is
  `0x43c980`); the root gets the definition's origin and everything is scaled at the
  end. Frame quaternions are stored as shorts over 32768 and offsets over 63. Vertexes
  are the weighted sum of each bone's placed offset. From the retail renderer's
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
  bones. Level of detail is not applied in the renderer.
- **Sprites**: a `.spr` model is the shader of that name, drawn as a sprite whose half
  size is the image's width times the shader's `spriteScale` times the entity's scale.
  That is OpenMoHAA's rule; the retail renderer's has not been read.
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
- **Dynamic lights** are Quake III's; of FAKK2's light types `additive` is honoured
  and the lens flare bits give the light a lens flare (next). Debug lines and sword
  swipes are accepted and not drawn.
- **The player's buttons and the inventory bar.** `default.cfg` binds `+attackleft`,
  `+attackright`, `+use` and `+cameralook`, which Quake III's client does not know, so
  the keys fired nothing (an unknown command goes to the server as chat). The client
  registers FAKK2's buttons by their bit in `usercmd_t.buttons` (`+attackright` 0,
  `+attackleft` 1, `+sneak` 2, `+run` 3, `+holsterweapon` 4, `+use` 5) and `+cameralook`
  as a client flag that does nothing yet (retail turns the camera about the player
  with the mouse while it is held). The number keys and the wheel are `warpinv <slot>`,
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
  `sunflare_inportalsky` is set; a refentity with `RF_LENSFLARE` gets the entity set in
  its `shaderRGBA`, plus a corona quad of `r_lightcoronasize` (default .1) half-size
  at the light whenever it is in front of the eye and visible (`0x48b3c0`); a dynamic
  light with the `lensflare` or `viewlensflare` bit gets the same in its colour; an
  entity with `RF_VIEWLENSFLARE` gets the dlight set. The intro's Lochnar (a green
  light with `+lensflare`) matches the retail captures at 13 to 15 seconds. Not read
  from retail: the sun's colour (white here), what happens to a flare in the sky
  portal's own pass (retail skips `RF_SKYENTITY` entities in the main pass and has a
  second pass; here every light is drawn once, in the main view). `r_sunflare "x y z"`
  (with `r_sunflare_inportalsky`) places the sun by hand as retail's developer cvar
  does, which is how the sun flare was compared: with the sun put at `training`'s
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
  plane a map's sky is fog. Retail enables `GL_FOG` per shader through a state
  bit in `GL_State` (`0x470ad0`) that `RB_BeginSurface` (`0x47c510`) sets only for
  shaders flagged in `FinishShader` (`0x478a60`: a single stage of one `rgbGen` and no
  `alphaGen`) when `r_useglfog` is on, and fogs everything else by vertex colour from
  distance over the far plane distance (`0x494910`). Not followed yet: OpenFAKK2 fogs
  every shader with OpenGL's fog except the sky and 2D drawing, and `r_useglfog 0`
  turns fog off rather than switching to vertex colours. The map viewer takes the far
  plane from worldspawn's keys directly, with the game's defaults (none, black, culling
  on); a level script's later changes to it are not seen there.

Not implemented: fog volumes (`fogParms` brushes), the light grid (models are lit with a default
light; the map's grid is reported as a mismatch and ignored), entity lights, lens flares,
beams, decals, level of detail, cross-blending between animations.

## Not yet comparable
- Input beyond walking in a straight line: turning, jumping, weapons, using objects,
  triggers the player must walk into. The test player can now be given key presses, but
  only a forward walk has been compared.
- What is sent to clients (snapshots, sounds) and what the client game module makes of
  it. The test player shows `print` text on the console as a client would and discards
  everything else; the real client draws the module's picture, which has not been
  compared with the retail game's.
- Save and load.
