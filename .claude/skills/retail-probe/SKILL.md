---
name: retail-probe
description: Verify OpenFAKK2's in-game behaviour against the retail FAKK2 engine with probe scripts that drive the player (give a weapon, fire it, walk, look up or down) and dump entity state with `whatis`. Use this whenever a change touches the server, game module, physics, traces, weapons or projectiles, whenever someone reports what happens in play ("the rock hits Julie", "she falls through", "the door doesn't open"), and whenever you need to show a fix working in the real game rather than in a unit test, even if nobody says "probe", "harness" or "retail".
---

# Probing gameplay against retail

OpenFAKK2 is reverse engineered from `fakk2.exe`, so the question is never "does it work"
but "does it do what retail does". The harness in `tools/retail-compare/` answers that
with a *probe*: a level script that waits for the player, presses keys at fixed game
times, and prints entity state. The same probe runs on the retail game and on ours, and
the dumps are compared field by field. This skill is the playbook for writing a probe,
choosing how to run it, and reading what comes back.

## Pick the runner

| Need | Runner | Reference available |
| --- | --- | --- |
| Server-side state after scripted keys, compared with retail | `tools\retail-compare\compare-player.ps1 -Map <m> -Probe <p>` (headless test player) | Yes: retail runs the same probe |
| Anything that needs aim pitch, the mouse, the camera or the HUD | `scripts\probe-client.ps1 -Probe <p> -Map <m>` (the real client, this repo's build) | No retail run; compare builds instead |
| "Did my change fix it?" | `probe-client.ps1` twice: `-Bin <main checkout>\build\x86-Release\Release -Name before`, then the default build `-Name fixed` | Before/after |

The headless test player (`sv_testplayer 1`) only knows the movement and attack keys:
`+forward +back +moveleft +moveright +moveup +movedown +left +right +attackleft
+attackright +use +speed`. It cannot pitch. A bug that depends on where the player is
aiming will not show there, so when a report mentions standing still, looking down, or
aiming at something, go to the real client and vary pitch with `+lookdown` / `+lookup`.
The sling bug that motivated this skill was invisible at pitch 0 on every runner and
obvious with 0.6 s of `+lookdown`.

Both runners resolve paths from their own location, so a worktree tests its own build in
its own `build\` tree. Point `-Bin` at another checkout's build to compare two versions.

## Write the probe

Probes live in `tools\retail-compare\<name>.scr`. Start from `probe-sling.scr`,
`probe-gaspod-down.scr` (ammo, pitch, fast reads of a projectile that explodes) or
`probe-walk-jump.scr` (movement), or expand a short template with `scripts\make_probe.py` (see
`references/probe-cookbook.md` for the directives and ready-made snippets). The shape:

```
probe:
waitforplayer
wait 2                      // let the entry stall pass (see below)
stuffcmd "give weapon_sling.tik"
wait 0.5
stuffcmd "use Sling dual"   // use <item name> <right|left|dual>: the hand is required
wait 2
stuffcmd "whatis 0"         // the player, before
stuffcmd "+attackright"
wait 1.5
stuffcmd "-attackright"     // charge weapons fire on release
wait 0.3
stuffcmd "whatis 0"         // the player, after
... whatis 1 to 299, ten per 0.1 s wait ...
stuffcmd "levelvars"
stuffcmd "pendingevents"    // the runners stop when they see this; a probe must end with it
end
```

Things that cost real runs to learn:

- `use <weapon>` with no hand does nothing, silently. Give the hand (`dual` for the sling).
- A weapon with `startammo 0` in its tik (the gas sling) fires nothing until it has ammo,
  and `give ammo_*.tik` does not load it. Put `$player ammo "Gas Pod" 5` in the probe as a
  script line, and check `playerammo_*` in `levelvars` to see that a shot was spent.
- A charge weapon's projectile leaves about 0.5 s after `-attackright`; reads in the
  first 0.3 s see nothing on either engine. A projectile without `bouncetouch` (the gas
  pod) is removed where it lands, so read its slot every 0.05 s after the release rather
  than relying on a sweep seconds later. Snippets in the cookbook.
- `give`, `whatis` and the other cheat events work under the harness on both engines.
- You rarely know a spawned entity's number in advance. Sweep `whatis 0` to `299` and
  select by classname afterwards; the sweep takes about three seconds of game time, so
  anything moving fast has moved by the time it is read. Read what matters first.
- The player enters about 450 ms after map start and is frozen for 300 ms more, so wait
  at least two seconds after `waitforplayer` before pressing keys.
- `stuffcmd` text goes through the client's command buffer. Never pass `+wait` on the
  engine command line of a probe run: it holds that buffer and nothing the probe stuffs
  runs.
- Keep a probe to one question. A probe that walks, fires and reads 300 entities is fine;
  a probe that tests three unrelated things makes the comparison unreadable.

## Run it

```powershell
# retail vs ours, headless. Retail's result is cached per map and probe; -Refresh reruns it.
tools\retail-compare\compare-player.ps1 -Map zoo -Probe probe-sling -Secs 120 [-Refresh]

# the real client with this repo's build; prints the projectile and the player afterwards
.claude\skills\retail-probe\scripts\probe-client.ps1 -Probe probe-sling-down -Map zoo -Classname Projectile

# the same probe on the unpatched build in the main checkout
.claude\skills\retail-probe\scripts\probe-client.ps1 -Probe probe-sling-down -Map zoo -Name before `
    -Bin <main checkout>\build\x86-Release\Release

# pull readings out of any log the runners wrote
.claude\skills\retail-probe\scripts\readings.ps1 -Log build\compare\player-retail-zoo-probe-sling.log -Classname Projectile -Entity 0
```

Logs land in `build\compare`: `player-retail-<map>-<probe>.log`, `player-ours-<map>-<probe>.log`
from the headless comparison, `client-<name>-<map>-<probe>.log` from the client runner.
The client's log echoes every server print twice (once as the raw `serverCommand`, once
as console output); `readings.ps1` drops the echo.

If a run produces no dump, look at the end of its log before changing anything: a log
that stops at `Hunk_Clear` was blocked by the stale-pid-file dialog (the runners pass
`+set com_abnormalExitDialog 0`, so this means something else launched the game), and a
probe that never printed `pending events as of` either did not finish in `-Secs` or does
not end with `pendingevents`. In a fresh worktree there is no build yet, and
`scripts\build-msvc.cmd` is gitignored: copy it from the main checkout, then
`scripts\build-msvc.cmd x86 Release`.

## Read the result

`compare-player.ps1` prints one summary line, then the differing variables, event kinds
and readings. "whatis readings: 137 retail / 137 ours; identical 130 of 137" with the
differing fields named is the headline. Before reading differences as bugs, know what
retail does to itself: actor idle variants (`Animname idle1` vs `idle2`) and the
positions of wandering actors differ between two retail runs, and numbers within
`-Tolerance` (0.01) are treated as equal. `-RetailTwice` shows the noise floor for a map.

Know the harness's own artefact too. The headless test player sends one movement command
per server frame (`sv_fps` 20, so every 50 ms); the retail client sends one per rendered
frame, and ground friction is integrated per command (`PM_Friction`, explicit Euler). So
after any slow-down on the ground, ours reads 1 to 2 units short of retail, 4 to 6 after
a walk, jump and stop, while every speed, the jump velocity and gravity agree. Before
calling a position difference of that size a bug, rerun ours with
`-Extra "+set sv_fps 40"` (or through the real client): if it lands inside retail's
spread, it was the cadence. Per-frame event counts in the summary double at 40, for the
same reason.

For a projectile or other spawned entity, the telling fields are `Origin` and
`Velocity` relative to the player's `Origin`, then `Movetype`, `Solidtype` and
`Edict Owner`. `references/whatis-fields.md` decodes the numbers. A rock that flew reads
hundreds of units from the player; one that collided with her reads within her box
(30 wide, 96 tall) with a velocity that makes no sense for free flight.

Treat "no difference" with suspicion until the probe has been shown to exercise the
behaviour: a before/after pair where *before* reproduces the report is the evidence, not
a single run that looks fine. If before and after agree and both look fine, the probe is
not hitting the condition yet. Vary what the report mentions (standing vs moving, pitch,
charge time, which map) before concluding the report is wrong.

## Record what you learned

- Findings about retail's rules, with the `fakk2.exe` addresses they were read from, go
  in `docs/known-differences.md`; the player probes have their own subsections under
  "With a player present". REA sessions are ephemeral, so this file is the only place the
  evidence survives.
- New probes are listed in `tools/retail-compare/README.md` under "Choose the probe".
- Commit the probe with the fix it verified. A probe that reproduced a bug is the
  regression test for it.
