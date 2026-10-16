---
name: retail-demo
description: Diagnose how OpenFAKK2's client differs from the retail FAKK2 engine by playing the same demo (.dm3) on both games, frame by frame for pictures or at speed for sound, and compare two demos of the same play recorded on each game as a timeline of what their servers sent. Use this whenever a report is about something seen or heard during play (a weapon effect, an explosion, a muzzle flash, lighting on a model, the HUD while playing, a cinematic after its load, a fade, any sound being too loud, too quiet, missing, panned or played twice), whenever someone hands over a demo or asks to record one, make our own demos, or compare demos, and whenever a picture probe's timing keeps moving the result, even if nobody says "demo". Menus, the loading screen and the intro go to retail-capture; what the game does (AI, physics, doors, scripts) to retail-probe.
---

# Demos against retail

A demo is what a server sent a client: every snapshot, entity, sound and print. OpenFAKK2
writes and reads the retail engine's `.dm3` format (protocol 1, "Demos" in
`docs/known-differences.md`), so either game plays either game's demos. Played on both,
one demo gives both clients exactly the same input, and frame *n* of `cl_avidemo` is the
same moment on both: whatever differs is the client side, our renderer, client game
module, HUD or sound system. No held-key timing, no "run retail twice to see the noise".

## Which skill

| The report is about | Skill | Because |
| --- | --- | --- |
| What happens: AI, physics, doors, scripts, damage, "the rock hits Julie" | `retail-probe` | our server must run the game; a demo replays retail's |
| Something seen or heard during play | **this one** | same snapshots on both, so any difference is ours |
| A menu, the loading screen, the intro, a cinematic while the map loads, the console, the cursor, a video restart, the save picture, a quick still | `retail-capture` | none of those is in a demo |

A demo cannot show a gameplay bug: played on our client it shows retail's game. For that,
record the same probe on both games and compare the two demos (diff mode, below), which
is `retail-probe`'s timeline view.

## Run it

`scripts\demo.ps1` (this skill's folder) does all of it; output goes to
`build\compare\demos\<demo>\`. Ours needs a build (in a fresh worktree copy
`scripts\build-msvc.cmd` from the main checkout, then `scripts\build-msvc.cmd x86 Release`,
or point `-Bin` at the main checkout's `build\x86-Release\Release`). Retail needs the game
disc in a drive and puts its window on screen. Every run takes the harness lock: frames
and diff runs share the machine with other worktrees' games, sound runs take it alone
(`tools/retail-compare/README.md`, "Games side by side, or alone").

```powershell
$demo = '.claude\skills\retail-demo\scripts\demo.ps1'
# a demo someone recorded (a path, or a name in the main checkout's build\demos): both games,
# frame by frame at 2 a second, ranked, pair pictures of the 6 worst
& $demo -Demo swamp2-fog
# record it first: a probe that records demos/<probe>.dm3 on retail (kept as build\demos\<probe>-retail.dm3)
& $demo -Probe probe-dzoo -Map zoo
# after a fix: ours alone, against the retail frames already there, under its own label
& $demo -Demo probe-dzoo-retail -Play ours -Name fixed
# module or engine? ours with Ritual's cgamex86.dll: a difference that goes away is ours' module's
& $demo -Demo probe-dzoo-retail -Play ours -RetailCgame
# sound: both play it at speed while the speakers are recorded (out loud)
& $demo -Demo probe-dzoo-retail -Mode sound
# gameplay: the probe recorded on both games, compared as a timeline
& $demo -Probe probe-dzoo -Map zoo -Mode diff
```

`-Rate` sets frames per second of demo (2 by default; 20 for a flash or a swing, with
`-Keep "8-9.5"` to keep only that window), `-Top` how many pair pictures. A 15 s demo
takes about 20 s for both games at 2 frames a second.

### Who recorded it, who plays it

| | played on retail | played on ours |
| --- | --- | --- |
| **recorded on retail** | the reference | ours' client: the usual comparison |
| **recorded on ours** | ours' **server**, through retail's client | ours end to end |

`-RecordOn ours` (or `both`) records on ours; `-Demo <probe>-ours` then plays our server's
demo on both. When retail's client draws our demo wrongly, what our server sent is wrong,
whatever our renderer does.

## Get a demo

**From a probe** (repeatable, so it can become a suite check). Start from
`tools\retail-compare\probe-dzoo.scr`; read `probe-scripting.md` there for weapons, aim
and cameras. A recording probe must:

```
probe:
waitforplayer
wait 3                                   // a big map (swamp1) is not ready at once: "You must be in a level to record."
stuffcmd "set g_syncronousClients 1"     // spelled so; record refuses without it, on both games
...set up the scene (spawn, give, use)...
stuffcmd "record probe-dxx"              // the probe's own name: demo.ps1 looks for demos/<probe>.dm3
...the play: keys, script lines, cameras...
stuffcmd "stoprecord"
wait 0.2
stuffcmd "quit"
end
```

`g_syncronousClients` also turns the client game module's prediction off while
recording; the demo is still exact. Keep the probe's name short (retail drops the end of a
long command line). A map loaded straight from launch spawns before `global0.scr`, so
global sound aliases are missing on both games (doors are silent): for sound, load
another map first or record on the second map of a run.

**By hand** (anything too fiddly to script): in either game's console,
`g_syncronousClients 1`, then `record <name>`, play, `stoprecord`. Retail writes
`<install>\fakk\demos\<name>.dm3`, ours its home's `fakk\demos`. Copy it into the main
checkout's `build\demos` (never into git: demos are made from Ritual's data) and pass
`-Demo <name>`.

## Read the result

**Frames.** The ranking gives the spread (median and 90th percentile of the share of
pixels differing by more than 24) and the worst frames with their time in the demo; look
at `pairs-<name>\pair-<frame>.png` (retail | ours | difference x4) for those. Ours is
seeded (`cl_demoSeed 1`, the suite's), so two runs of ours agree to 0.05-0.3 points;
retail's particles are random, so a frame full of sparks or an explosion differs by a few
points with nothing wrong. A real difference is structured: a missing or extra sprite, a
colour over a whole surface, a HUD picture, a flare's shape. When it goes away with
`-RetailCgame`, it is in our client game module (`code/fakk/cgame`), otherwise in the
engine. Known first differences are listed under "Demos" in `known-differences.md`
(the HUD's weapon picture, explosion light on Julie, swamp1's cinematic head).

**Sound.** `demo_sound.py` aligns the two recordings by their loudness and prints each
second's level per channel and the difference, marking 3 dB or more and a channel silent
on one side; then the sounds each game started. Read the levels first: a steady offset
over the whole demo is a gain, a difference in a few seconds is one sound. The started
sounds come from retail's `s_show_sounds` and ours' `s_show`, which only the software
mixer prints: pass `-Backend dma` for the list (openal, the default, is what players
hear). Retail prints volumes already scaled by `s_volume`; the comparison divides that
out. An alias with several sounds draws one at random on each game (`site1` / `site2`), so
those are counted as variants, not differences. Keep `-Volume` at 0.25, or 0.06 for loud
weapons: this machine's output compresses above about -20 dBFS. A steady offset over the
whole demo is first a suspect for Windows' volume mixer, which keeps a level per
executable path: the 12 dB "gap" found here on 2026-10-16 was the main checkout's
`openfakk2.exe` left at 25% (`tools/retail-compare/README.md`, "Sound"); check it before
reporting a gain difference. The recording also takes
whatever else plays: the recorder holds the harness alone, so no other session's game
plays or comes in front of retail meanwhile, but your own music or video would be
recorded, so nothing else should play.

**Diff.** `demo_diff.py` pairs snapshots by time since each demo began, and entities by
model, closest first. It prints when the player's position, view and stats first part,
each entity pair that drifts apart or changes animation, entities only one game has, the
sounds the server started (same sound within a second counts as the same event), and the
prints and server commands. Actors' choices, spreads and gibs are drawn from the clock on
both games and part ways early; scripted moves, doors, cameras and projectiles should
not. Held keys drift a little, as in `retail-probe`. Weapon and animation sounds are the
client game module's, from the TIKI files, so they are not in a demo at all: use sound
mode for them.

## Make it permanent

A difference worth keeping fixed becomes a suite check, so it cannot come back unseen. In
`tools\suite\suite.json`, beside `demo-zoo`:

```json
{ "name": "demo-dxx", "areas": ["rendering"], "level": "standard", "kind": "demo", "probe": "probe-dxx", "map": "zoo", "avidemo": 2,
  "note": "what it shows and why" },
```

or, for a demo recorded by hand, `"demoFile": "<name>.dm3"` (in `build\demos`) in place of
`"probe"` and `"map"`. `"keep": ["8-9.5"]` and `"every": 2` thin the frames. A frames
check shares the machine with other worktrees' games, which does not move its pictures;
give it `"exclusive": "timing"` only if it measures time (as `timedemo` does), with the
reason in its note (`tools/suite/README.md`, "suite.json"). Then make its
retail reference with `tools\suite\refresh-retail.ps1 -Only demo-dxx` (the game disc; it
records or copies the demo and plays it on retail), commit the probe and the entry with a
trailer so the merge keeps its first result:

```
Suite-moves: demo-dxx: a new check; its first result becomes ours' last good one
```

A hand-made demo lives only in this machine's `build\demos`: say so in the note; another
machine skips the check until it has the file and a reference. A new take of the demo, or
a changed probe, makes the reference stale until it is refreshed.

## Record what you learned

- A difference found and explained goes in `docs/known-differences.md` ("Demos" lists the
  first differences the demo checks show); one fixed takes its check with it, as above.
- Retail's rules behind a fix, with the `fakk2.exe` addresses, go there too.
- New recording probes are listed in `tools/retail-compare/README.md`.
