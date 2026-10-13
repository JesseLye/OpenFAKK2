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
- OpenFAKK2's first 23 draws on `zoo` are single-candidate `idle` picks for 21 weapon
  models (two of them twice), made while the game precaches after spawning entities.

What is not known: why retail would not make those 23 draws. `sv_precache` is 1 on both.
Untested ideas: retail's lookup failing for weapon models because of how their animation
lists are built, the precache scripts running at a different point relative to the first
frames, or the game taking a different path because of a cvar (`cheats` differs, see
below).

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
the retail game run normally with its real client and nobody touching the controls
(`tools/retail-compare/run-retail-client.ps1`). Both run a probe script that waits for
the player, waits ten seconds of game time and dumps `levelvars` and `pendingevents`.

`tools/retail-compare/compare-player.ps1 -Map <map>` runs both and compares. Results over
the 30 retail maps:

- `under` could not be compared: the probe never fires on either engine there.
- On the other 29, every level variable matches (12 to 75 variables per map).
- Pending events match by kind and count on 21 maps. Both engines take the dump at level
  time 10.80, so the comparison is phase-aligned.
- Eight maps differ by small counts:

| Map | Difference (retail / OpenFAKK2) |
| --- | --- |
| `creeperpens` | `Actor setcontrollerangles` 87 / 86 |
| `end` | `Actor animdone` 7 / 8 |
| `homes1` | `Actor animate_newanim` 1 / 0, `Actor animdone` 26 / 27 |
| `homes2good` | `Actor setmovedelta` 46 / 24 |
| `intro` | `ScriptModel movedone` 7 / 8 |
| `landersroost` | `Actor setmovedelta` 17 / 3 |
| `otto` | `Actor animdone` 1 / 2 |
| `towncenter_good` | `Actor animate_newanim` 1 / 0, `Actor animdone` 28 / 29, `Actor setmovedelta` 0 / 3, `SlidingDoor close` 1 / 0 |

None of these has been investigated. The walking-actor differences (`setmovedelta`) and
the animation-transition ones are the kind the unexplained variant picks above would
produce, since a different idle or walk variant has a different length; that is a guess.

### Player entry time

The retail client enters the world some frames after the map starts, once it has loaded.
The test player imitates that with `sv_testplayerdelay` (450 ms of game time, chosen so
that the dumps line up with retail's on the retail maps). Scripts that use
`waitforplayer` are released at that moment, so entering at map start instead leaves any
later-started script waiting forever.
## Not yet comparable

- Anything that needs input: movement, weapons, using objects, triggers the player must
  walk into. The test player stands still.
- What is sent to clients (snapshots, sounds, on-screen text). The test player discards
  it; `sv_testplayer 2` only prints the reliable commands.
- Save and load.
