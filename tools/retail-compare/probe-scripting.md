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
  defaults to 0, as in retail). If the real client runs at 345 u/s, an old
  `seta cl_run "1"` is sitting in `build\sandbox\home\fakk\config.cfg`. Delete the line.
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
