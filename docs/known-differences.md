# Known differences from the retail engine

This file tracks where OpenFAKK2 is known or suspected to behave differently from
`fakk2.exe` 1.02, and which rules were inferred rather than read from the binary. It
covers the server side only; the client and renderer are not ported yet.

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
| `sv_pure` | 0 | 1 | |
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
- **A temporary box model takes its entity's contents.** Inferred from solid pods that
  solid-only traces must hit (`otto`, `oracle`).
- **Alias range lookup and weighted pick** come from OpenMoHAA's version of the same code.
- **Dialog selection** follows `fakk2.exe` (`0x41bdc0`) except for two details that were
  not recovered: the test for whether use is tracked per actor (taken to be "the alias is
  global, or no actor was given"), and the multiplier that turns the random fraction into
  an index (taken to be the number of available lines).
- **Skeletal tag placement.** Bone composition is a guess; bone controllers and blending
  are not applied. Nothing has exercised it.
- **`Anim_AbsoluteDelta`** returns the same value as `Anim_Delta`.
- **The delta-driven animation flag is never set.** Retail's cached definitions flag only
  four animations, all in the unused `oldjulie` model, and the keyword that sets the flag
  has not been identified. (An earlier version set it for every animation with movement,
  which stopped actors from walking: the game applies movement only to animations that
  are *not* flagged.)
- **`pointbrushnum`** always returns 0.
- **Tag and frame-bound scaling** (`load_scale * scale`, plus the setup origin).

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

## Not yet comparable
- Input beyond walking in a straight line: turning, jumping, weapons, using objects,
  triggers the player must walk into. The test player can now be given key presses, but
  only a forward walk has been compared.
- What is sent to clients (snapshots, sounds). The test player shows `print` text on the
  console as a client would and discards everything else.
- Save and load.
