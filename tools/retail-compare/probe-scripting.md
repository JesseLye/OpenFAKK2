# Scripting a probe

How to make the player do things in a probe: start it, press keys, give and fire weapons,
aim, move, set up the scene. It applies to both kinds of probe. What a probe records and
how it ends depend on its skill:

- `.claude/skills/retail-probe`: readings (`whatis`, `levelvars`) compared field by
  field; `references/probe-cookbook.md` there covers reading state.
- `.claude/skills/retail-capture`: screenshots compared as pictures.

Probes live here, in `tools\retail-compare\<name>.scr`. Times are game seconds.

## Start and end

```
probe:
waitforplayer          // returns when the player spawns: the same moment on both games
wait 2                 // entry (450 ms) plus the retail client's stall (300 ms), with margin
...
stuffcmd "pendingevents"   // a reading probe: retail-probe's runners stop on its output
stuffcmd "quit"            // a picture probe: capture.ps1 waits for the game to exit
end
```

- Wait at least 2 s after `waitforplayer` before pressing keys. On a map with an intro
  (`training`, `swamp1`) wait about 25 s for it to end.
- Every runner starts the probe with `+map <m> +script thread <probe>.scr::probe`, before
  the player spawns, so one file runs unchanged on both games. Never put a `+wait` on the
  command line: it holds the command buffer, the player spawns before the thread starts,
  and `waitforplayer` never returns.
- A probe that ends with both lines works with every runner.
- Keep the probe's name short. The retail client silently drops the end of a long command
  line, so with `-Extra` a long name means the probe never starts on retail (14 characters
  was too long with `+set r_mode 4`).
- Keep a probe to one question.

`.claude\skills\retail-probe\scripts\make_probe.py` expands a short template:
`@cmd <command>` is `stuffcmd "<command>"`, `@hold <key> <seconds>` is a press, a wait
and a release, `@sweep [from] [to]` reads entities. Its `@end` adds `levelvars` and
`pendingevents` but not `quit`, so add `stuffcmd "quit"` yourself to a picture probe.

## Example maps

Ritual's tools release has 32 small maps, each showing one feature. They make good test
levels: small, quick to load, and with nothing else going on. Install them into both
sandboxes (the retail game's and ours' home) with

```
tools\retail-compare\install-example-maps.ps1
```

It copies the `.bsp`, `.scr` and `.pth` files from `$RitualTools\fakk\maps\example`
(`common.ps1`; `FAKK2_TOOLS` overrides it) into `build\sandbox\retail\fakk\maps\example`
and `build\sandbox\test-home\fakk\maps\example`, and skips files already there. Ritual's licence
does not allow copying them, so they stay under `build\` and are never committed. Run it
again in each new worktree: its sandboxes are its own.

Name them with their folder: `-Map example/rain`. Both games load maps from a subfolder,
and each map's own level script (`maps\example\<map>.scr`) runs as usual. The runners
put the probe in the map's folder, because the game looks for a script named without a
path (`+script thread probe.scr::probe`) next to the map, not in `maps\`.

`probe-load` takes one screenshot three seconds after entry and quits, which shows
whether a map loads and what the player sees there:
`.claude\skills\retail-capture\scripts\capture.ps1 -Probe probe-load -Map example/zoo`.
All 32 loaded on both games with it, and the pictures matched (2026-10-15). On retail it
also ran on `example/gibs_and_health`, a 168-character command line from `capture.ps1`, so
the length limit under "Start and end" is not a plain count of characters.

A few maps use textures that are not in the paks (`autocamera`, `box3`, `multiexploder`,
`teleporter`, `zoo`'s sky); both games draw the default image there. The level scripts
start at entry, so a probe's waits run alongside them.

- `CameraScript`: 5 s after entry the script cuts to a camera following a spline path,
  then to one orbiting Gruff, then back to the player (once). Gruff and an Eden male stand
  in the room.
- `autocamera`: automatic cameras (`func_camera` with `auto_radius`, `auto_state`) that
  take over near monkey bars and a horizontal pipe.
- `autosprite`: wall faces with `deformVertexes autosprite` and `autosprite2`
  (`testsprite1`/`2` in `mark.shader`, the portal sprite), turning to face the view.
- `beam`: two dark blue `func_beam`s; the script sets a light style on one.
- `beam1`: one `func_beam` that hurts what crosses it (`damage 10`).
- `box3`: an empty lit room; the plainest level here.
- `climb`: a `func_rope` to climb.
- `cover`: an Eden male (`edenmale_EAexample.tik`) on a path-node grid with cover nodes.
- `door`: a use trigger sends Balthazar walking through a `func_door` to a path node.
- `doors`: two sliding `func_door`s and two `func_rotatingdoor`s.
- `earthquake`: a trigger that starts a four-second `func_earthquake` (the view shakes).
- `explodeobject`: five `func_explodeobject`s (a statue, rocks with debris) and a
  `func_multi_exploder`; hit or shoot them.
- `exploder`: a trigger that sets off a `func_exploder`.
- `explodingwall`: a trigger that blows out a wall of eleven `func_explodingwall` pieces.
- `fallingrock`: a trigger drops a `func_fallingrock` that bounces along nine waypoints.
- `fulcrum`: two `func_fulcrum` tables that tip under weight (45° limit). Its script ends
  on its first line, so its cameras never cut.
- `funcspawn`: a `trigger_once` that spawns a creeper (`func_spawn`).
- `gibs_and_health`: two female Lympthorns and three health plants. The Lympthorns spit at
  the player at random moments, so two runs differ: not for picture comparisons.
- `letterbox`: every 13 s the script shows the letterbox bars and a red beam for 8 s
  (first at 5 s).
- `multiexploder`: a trigger that sets off a four-second `func_multi_exploder`.
- `patrol`: a dark creeper walking back and forth between two path nodes.
- `pickup`: a use trigger makes Balthazar pick up a Shgliek, carry it and throw it.
- `pushobject`: two `func_pushobject` boxes; pushing one onto its `trigger_pushobject`
  opens a door ("Got me!").
- `rain`: `func_rain` and a `func_emitter` of falling leaves.
- `runthrough`: a `func_runthrough` that throws plant seeds when walked through.
- `scriptgravity`: four brush `script_object`s the script throws (`physics_on`,
  `physics_velocity`) and resets, every 7 s or so.
- `sinkobject`: four `func_sinkobject` lily pads that sink under the player.
- `teleporter`: two teleport pads and a `func_teleportdest`.
- `useanim`: two `func_useanim` spots where Julie plays her push animation.
- `useobject`: a crank Julie turns (`func_useanim`, `crank_turn_forward`).
- `vehicle`: an ATV with its tyres (`vehicles_atv`).
- `zoo`: a corridor of 33 doors, behind each one creature, weapon or item: the game's
  enemies and weapons. The same file as `zoo` in `pak0.pk3`, so `-Map zoo` needs no install.

## Keys

The headless test player (`sv_testplayer 1`, retail-probe's `compare-player.ps1`) only
knows `+forward +back +moveleft +moveright +moveup +movedown +left +right +attackleft
+attackright +use +speed`, each with its `-` form. It cannot pitch. The player walks
unless `+speed` is held; `+left`/`+right` turn at about 140 degrees per second.

The real client (retail-probe's `probe-client.ps1`, and `capture.ps1`) takes every
client console command as well: `+lookup`, `+lookdown`, `centerview`, `screenshot`. Not
`viewpos`: in this build that is the map viewer's command and prints nothing in a game.

Held keys are timed by rendered frames, and the two games render at different rates, so
the same hold lands a little differently on each. Run each side twice to see its own
spread before calling a gap that size a bug.

## Weapons

```
@cmd give weapon_sling.tik      // any weapon_*.tik from the paks; "give all" runs global/giveall.scr
wait 0.5
@cmd use Sling dual             // the item's "name" from its tik, then right | left | dual
wait 2                          // the raise animation has to finish before it can fire
@hold attackright 1.5           // sling: charge, then fire on release. Guns fire on press.
```

- `use <weapon>` with no hand does nothing, silently. Give the hand.
- A dual-handed weapon fires its primary mode with `+attackleft` and its alternate with
  `+attackright` (`global/julie_torso.st`, `CHECK_ATTACK_LEFT` / `CHECK_ATTACK_RIGHT`): the
  shotgun's plasma blast is `+attackleft`, its grenade `+attackright`. The sling charges on
  either (the right button is its alternate mode). A right-hand weapon (the swords, the
  uzi, the hand gun) attacks with `+attackright`, a left-hand one with `+attackleft`.
- The sling's `maxchargetime` is 1.5 s. A charge weapon's projectile leaves about 0.5 s
  after the release (the release animation).
- `give`, `whatis` and the other cheat commands work under the harness on both engines.
  Cheat cvars do not take effect on ours under `+map`.

Ammo. A weapon whose tik says `startammo 0` (the gas sling, `weapon_sling_gas.tik`) fires
nothing, silently, until it has ammo, and `give ammo_gasplant.tik` does not help: it
spawns an `AmmoEntity` at the origin that is never credited. The `ammo` event is a script
event, not a console command, so give it as a script line between the give and the use:

```
@cmd give weapon_sling_gas.tik
wait 0.5
$player ammo "Gas Pod" 5        // the ammo name from the tik, as giveall.scr does it
wait 0.5
@cmd use GasSling dual
```

Weapons with `autoaim` in the tik (the crossbow) fire at a yaw a few degrees off the
player's facing even with nothing to aim at. Retail does the same.

## Aim

```
@hold lookdown 0.6     // steep pitch down. 0.4 s is shallow. The pitch wraps past 90°,
wait 0.3               // so 1.2 s of +lookup came back shallower than 0.4 s.
```

0.6 s of `+lookdown` gave a shot 83 to 84 degrees down, the spread being the key timing.
`+lookdown` for 0.15 s tips the view enough to fire at the floor a little ahead.
Pitch needs the real client: the headless test player cannot pitch.

## Movement

```
@hold forward 3        // walk ~275 units on flat ground
wait 1                 // settle
```

- Walking: 91 u/s; jump (`@hold moveup 0.2`): 400 u/s up with 160 u/s forward, gravity
  800, about a second in the air. `+forward` alone walks on both engines (`cl_run`
  defaults to 0, as in retail). Harness runs start from an empty `config.cfg` on the
  test home, so the player's own `cl_run 1` never reaches them; if the real client runs
  at 345 u/s, something on the command line set it.
- To fire while moving, press the movement key before the attack and release it after:
  `stuffcmd "+forward"`, `wait 1`, `@hold attackright 1.5`, `wait 0.3`,
  `stuffcmd "-forward"`. Moving adds the player's velocity to a projectile
  (`addownervelocity`), which is why some collision bugs only show standing still.
- Moving while a weapon's fire animation plays carries Julie much further on ours than on
  retail (a known difference). Turn or wait instead of sidestepping after a shot.

## Setting up the scene

A probe is a level script, so script lines work alongside `stuffcmd`:

- `$player health 50`, `$player ammo "Gas Pod" 5` and similar set the player's state.
  `$player origin (...)` does not move her (her think copies the player state back each
  frame); use a camera for a viewpoint.
- Script objects respond to their own targetname: `$door_1 open`, `$player immune
  falling`. `+use` works on the test player too (`@hold use 0.2`), and walking into a
  trigger with `@hold forward` fires it.
- A camera gives the same view on both games, where held keys do not:
  `spawn Camera targetname cam1 origin "x y z" angles "pitch yaw 0"`, then
  `cuecamera $cam1` and a one-second wait.
- To show one effect alone, spawn its model and start its animation, which a model
  spawned by a script does not do by itself:

```
spawn fx_explosion.tik origin "x y z" targetname "fx"
wait 0.1
$fx anim idle                   // runs the entry commands that make the effects
```
