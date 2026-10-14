# What `whatis` prints

`whatis <n>` (a cheat event on the player, `Player::WhatIs` in `player_util.cpp`) prints
one block per entity. The comparison script matches blocks by their order of appearance
and compares every field, numbers within `-Tolerance`.

| Field | Meaning | Reading it |
| --- | --- | --- |
| `Entity #` | entity number | 0 is the player; 1023 means "none" in other fields |
| `Class ID`, `Classname` | C++ class id and classname | `Player`, `Actor`, `Weapon`, `Projectile`, `ScriptSlave`, ... |
| `Targetname` | script name | empty for spawned things |
| `Modelname` | tiki path | `models/fx_slingshot.tik` is the sling's rock |
| `Animname` | current animation | retail picks idle variants at random (`idle1`/`idle2`); not a difference |
| `Origin` | position | compare with the player's `Origin`; the player stands at z = 0.125 on a floor at 0 |
| `Angles` | pitch yaw roll | projectiles keep (0 0 0) unless the game sets them |
| `Bounds` | mins / maxs | player (-15 -15 0) to (15 15 96); sling rock ±8 |
| `Velocity` | units per second | a rock in flight: forward ~800 plus up to 250 up; sitting still but non-zero means it is being bounced or stopped this frame |
| `SVFLAGS` | hex server flags | every entity in use reads `8000` once a snapshot has been built; a weapon in inventory reads `8001` (not sent to clients) |
| `Movetype` | game movetype (`g_phys.h`) | 0 none, 1 stationary, 2 noclip, 3 push, 4 stop, 5 walk, 6 step, 7 fly, 8 toss, 9 flymissile, 10 bounce, 11 slide, 12 rope, 13 gib, 14 vehicle |
| `Solidtype` | 0 not, 1 trigger, 2 bbox, 3 bsp | traces skip 0 and 1 |
| `Contents` | hex contents | `2000000` body (player), `100000` shot-clip (projectile) |
| `Areanum` | BSP area | -1 for an unlinked entity (in inventory) |
| `Parent` | attached-to entity | 1023 when not attached |
| `Health`, `Max Health` | | the player's drops if something damaged her |
| `Edict Owner` | `ownerNum` | a projectile's owner is who fired it (0); 1023 none. Traces skip an entity's owner and its owner's other missiles |

## Patterns

- **A projectile that flew**: hundreds of units from the player along her yaw, z near the
  floor plus half its size once landed, `Movetype 10`, `Solidtype 2`.
- **A projectile that collided with its owner**: within her 30 x 30 x 96 box or just
  outside it, implausible velocity (large negative z, or oscillating), still present
  frames later.
- **A projectile that hit something else**: absent from the sweep (removed on impact).
- **Weapon in inventory**: `SVFLAGS 8001`, `Areanum -1`, `Origin (0 0 0)`, `Parent 1023`.
- **Weapon in hand**: `Origin` at the hand tag, roughly 19 forward, 12 right and 62 up
  from the player's origin when standing idle.
- **Noise, not a bug**: `Animname` idle variants on actors, positions of wandering
  actors, counts of `setmovedelta` events, and the player's idle cycle phase. Two retail
  runs of the same map differ in these too (`compare-player.ps1 -RetailTwice`).
