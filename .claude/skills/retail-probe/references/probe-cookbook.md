# Probe cookbook: reading state

How a reading probe gets its numbers. Making the player do things (keys, weapons, ammo,
aim, movement, doors, `make_probe.py`) is in `tools\retail-compare\probe-scripting.md`,
shared with the retail-capture skill; read it first. Times are game seconds. Templates
for `scripts\make_probe.py` use `@cmd`, `@hold`, `@sweep` and `@end`.

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

## Projectiles

Projectiles are `Classname Projectile` in `whatis`; their `Edict Owner` is the player's
entity number (0) until the game clears it. A rock that lands sits with `Movetype 10` at
floor height plus its half size (8.125 on a floor at 0.125); one that hits a wall or an
actor is removed, so a sweep that finds no projectile may mean "it hit something", not
"it was never fired".

Check `playerammo_<name>` in `levelvars` after firing: it counts down by one per shot, so
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

An `autoaim` weapon's projectile leaves at a yaw a few degrees off the player's facing,
on retail too, so a yaw difference between the projectile's `Angles` and the player's is
not a finding by itself.

## Reading the aim

```
@hold lookdown 0.6
wait 0.3
@cmd whatis 0          // the player's Angles stay (0 0 0): pitch is view state, not entity state
```

Nothing prints the view pitch. Read it off the projectile instead: its `Angles` in
`whatis` are the direction it left at (0.6 s of `+lookdown` gave 83 to 84 degrees down).
Pass `-Fields` with `Angles` to `probe-client.ps1` to see them.

## Reading movement

Whenever the player slows down on the ground (jump wind-up, landing, letting go of the
key) the headless comparison reads her 1 to 2 units short of retail per slow-down, and 4
to 6 after a walk-jump-stop (`probe-walk-jump.scr`). That is the test player's command
cadence, not the physics: see "Read the result" in SKILL.md. `-Extra "+set sv_fps 40"`
on `compare-player.ps1` brings ours inside retail's own 1.3-unit run-to-run spread.
