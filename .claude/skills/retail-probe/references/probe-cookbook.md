# Probe cookbook

Snippets for `tools\retail-compare\*.scr` probes. Times are game seconds. Templates for
`scripts\make_probe.py` use `@cmd`, `@hold`, `@sweep` and `@end`; the expansions are
shown where they are not obvious.

## Skeleton

```
probe:
waitforplayer
wait 2                 // entry (450 ms) plus the retail client's stall (300 ms), with margin
...
wait 0.5
stuffcmd "levelvars"
stuffcmd "pendingevents"   // the runners wait for this line's output
end
```

## Keys the headless test player understands

`+forward +back +moveleft +moveright +moveup +movedown +left +right +attackleft
+attackright +use +speed`, each with its `-` form. The player walks unless `+speed` is
held; `+left`/`+right` turn at about 140 degrees per second. Anything else (pitch, mouse,
camera, menu, HUD) needs the real client through `probe-client.ps1`, where every client
console command is available: `+lookup`, `+lookdown`, `centerview`, `screenshot`. Not
`viewpos`: in this build that is the map viewer's command and prints nothing in a game.

## Weapons

```
@cmd give weapon_sling.tik      // any weapon_*.tik from the paks; "give all" runs global/giveall.scr
wait 0.5
@cmd use Sling dual             // the item's "name" from its tik, then right | left | dual
wait 2                          // the raise animation has to finish before it can fire
@hold attackright 1.5           // sling: charge, then fire on release. Guns fire on press.
```

A dual-handed weapon fires its primary mode with `+attackleft` and its alternate with
`+attackright` (`global/julie_torso.st`, `CHECK_ATTACK_LEFT` / `CHECK_ATTACK_RIGHT`): the
shotgun's plasma blast is `+attackleft`, its grenade `+attackright`. The sling charges on
either (the right button is its alternate mode). A right-hand weapon (the swords, the
uzi, the hand gun) attacks with `+attackright`, a left-hand one with `+attackleft`.
The sling's `maxchargetime` is 1.5 s. Projectiles are `Classname
Projectile` in `whatis`; their `Edict Owner` is the player's entity number (0) until the
game clears it. A rock that lands sits with `Movetype 10` at floor height plus its half
size (8.125 on a floor at 0.125); one that hits a wall or an actor is removed, so a sweep
that finds no projectile may mean "it hit something", not "it was never fired".

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

Check `playerammo_<name>` in `levelvars` afterwards: it counts down by one per shot, so
it also tells you whether the shot was fired at all.

Timing the reads. A charge weapon's projectile leaves about 0.5 s after `-attackright` is
stuffed (the release animation), so reads in the first 0.3 s after the release see no
projectile on either engine. A projectile without `bouncetouch` in its tik (the gas pod)
explodes on the first thing it touches and is removed, so a sweep seconds later finds
nothing; read its slot every 0.05 s from the release for a second to catch it in flight:

```
@hold attackright 1.5
wait 0.05
@cmd whatis 134         // first free slot after the given weapon (133 on zoo)
@cmd whatis 135
wait 0.05
... repeat for 1 s ...
```

Weapons with `autoaim` in the tik (the crossbow) leave at a yaw a few degrees off the
player's facing even with nothing to aim at. Retail does the same, so a yaw difference
between the projectile's `Angles` and the player's is not a finding by itself.

## Aim

```
@hold lookdown 0.6     // steep pitch down. 0.4 s is shallow. The pitch wraps past 90°,
wait 0.3               // so 1.2 s of +lookup came back shallower than 0.4 s.
@cmd whatis 0          // the player's Angles stay (0 0 0): pitch is view state, not entity state
```

Nothing prints the view pitch. Read it off the projectile instead: its `Angles` in
`whatis` are the direction it left at (0.6 s of `+lookdown` gave 83 to 84 degrees down,
the 1 degree spread being the key timing). Pass `-Fields` with `Angles` to
`probe-client.ps1` to see them.

## Movement

```
@hold forward 3        // walk ~275 units on flat ground
wait 1                 // settle before reading
@cmd whatis 0
```

To fire while moving, press the movement key before the attack and release it after:
`stuffcmd "+forward"`, `wait 1`, `@hold attackright 1.5`, `wait 0.3`, `stuffcmd "-forward"`.
Moving adds the player's velocity to a projectile (`addownervelocity`), which is why some
collision bugs only show standing still.

Jump with `@hold moveup 0.2`. Walking: 91 u/s; jump: 400 u/s up with 160 u/s forward,
gravity 800, about a second in the air (`probe-walk-jump.scr`). `+forward` alone walks on
both engines (`cl_run` defaults to 0, as in retail); if the real client reads `run_norm`
at 345 u/s, an old `seta cl_run "1"` is sitting in `build\sandbox\home\fakk\config.cfg`,
archived from an earlier build. Delete the line. Whenever the player slows
down on the ground (jump wind-up, landing, letting go of the key) the headless comparison
reads her 1 to 2 units short of retail per slow-down, and 4 to 6 after a walk-jump-stop.
That is the test player's command cadence, not the physics: see "Read the result" in
SKILL.md. `-Extra "+set sv_fps 40"` on `compare-player.ps1` brings ours inside retail's
own 1.3-unit run-to-run spread.

## Reading state

```
@cmd whatis 0          // the player
@cmd whatis 134        // a known entity number
@sweep                 // 0..299, ten per 0.1 s: about 3 s of game time
@sweep 100 199         // a narrower range reads faster
@cmd levelvars
@cmd pendingevents
```

Entity numbers for things spawned during play are the first free slots after the map's
own entities, so on a map with 130 entities the first projectile is around 133 or 134
(the given weapon takes one slot too). Read the thing that moves first, then the sweep.

## Doors, triggers, scripted things

`+use` works on the test player (`@hold use 0.2`). Walk into a trigger with `@hold forward`.
Script objects respond to their own targetname: a probe is a level script, so
`$door_1 open` or `$player immune falling` are available if the question is about the
reaction rather than the trigger.
