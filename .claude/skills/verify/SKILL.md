---
name: verify
description: Check that a change to OpenFAKK2 broke nothing, by running the test suite (tools/suite) on the checks that cover what this branch changed, plus a floor of cheap ones, and reading its report. Use this before finishing any session that changed code under code/ or tools/ (the Stop hook asks for it), before merging a worktree, when asked to "verify", "run the tests", "check for regressions" or "make sure nothing broke", and after a fix to show it holds across the game, even if nobody names the suite.
---

# Verifying a change

The suite (`tools/suite/`, described in `tools/suite/README.md`) runs OpenFAKK2 through
the harness's probes and compares what it does with retail's cached results and with
ours' last good results. It tells a check that is **newly failing** (worse than ours
was) from one that **differs from retail as before** (a known difference). It never
starts the retail game.

## 1. What changed

```
git diff --name-only $(git merge-base HEAD main)    # committed on this branch
git status --short                                  # and not yet committed
```

## 2. Which checks

`run-suite.ps1 -Changed` maps the changed files to areas with suite.json's `paths` table
and adds the checks of any probe that changed. The table, in short:

| Changed | Areas |
|---|---|
| `code/renderergl1/`, `code/renderercommon/`, `code/client/cl_view*` | rendering |
| `code/client/cl_urc.c`, other `code/client/` | interface (and rendering) |
| `code/client/snd_*` | sound (only runs with `-Sound`; it plays out loud) |
| `code/server/sv_save.c` | saves |
| `code/server/`, `code/qcommon/`, `code/fakk/fgame/` | server (and data) |
| `code/fakk/cgame/` | rendering, interface |
| `code/tiki/` | data, rendering |
| `tools/retail-compare/`, `tools/suite/`, `.claude/skills/retail-*` | the checks that use the changed runner or probe |

Every level also runs the floor: the build (its warning count), the data checks, spawn
output on all 30 maps, and the invariants (no crash, the player's `config.cfg`
untouched, console warnings per map, frame time, restarts and map changes).

## 3. Run it

```
tools\suite\run-suite.ps1 -Level quick -Changed            # default: minutes
tools\suite\run-suite.ps1 -Level standard -Changed         # the areas and their neighbours
tools\suite\run-suite.ps1 -Only fstrafe, shots/zoo -NoBuild  # a few checks again
tools\suite\run-suite.ps1 -Level quick -Changed -List      # what it would run
```

Use `quick` unless asked for more; `standard` before merging a large change. The
suite builds first. Run it in the background and wait for it; never pipe it into
`Select-Object -First`.

A change that touches the x64 build (the port, anything under `code/` once the x64
build exists) is verified on both lanes: the run above, then
`tools\suite\run-suite.ps1 -Level quick -Changed -Arch x64`, which builds and runs the
x64 build and also runs each check on x86 beside it, so a difference between the two
builds is newly failing ("against x86" in the evidence). The stamp records the last
run only; stamp after the second.

## 4. Read the report

`build\suite\runs\<time>\report.md`: the summary, then **Newly failing** with the
evidence (the differing fields, pair images `retail | ours | diff`, log excerpts, crash
stacks), then **Known differences that still differ**, then passes. Open the pair images
with the Read tool.

- A newly failing check is yours until shown otherwise. Fix it, or explain it in your
  answer with the evidence. Never call a failure noise without running that check again
  (`-Only <name>`); if it passes on a rerun, say so and give both results.
- Other worktrees' games may have run beside yours: checks share the machine unless
  `suite.json` marks them `exclusive` (`build\harness.holders` lists who holds it; see
  `tools/retail-compare/README.md`, "Games side by side, or alone"). When a failure looks
  like timing (a position a few units off after held keys, a frame time), rerun it
  alone: `$env:FAKK2_HARNESS_MODE = 'exclusive'` before `-Only <name>`. If it fails only
  when shared, that is no excuse to wave it through: give the check `"exclusive":
  "timing"` with the evidence in its note, and say so. A failure that repeats alone is
  real; `fstrafe` looked flaky and was a frame-rate bug.
- A crash fails its check and the report carries the named stack.
- "Skipped: no retail reference" or "made from an older probe" means a reference needs
  `tools\suite\refresh-retail.ps1 -Only <check>` (the game disc, windows on screen). If
  you changed a probe, refresh its reference.
- A deliberate improvement shows as a check that differs less than before ("Better than
  ours' last good result"); say so.
- **A check your change moves on purpose** (a fix that is right by the binary but makes a
  picture or a count differ more from the cached result, a probe you changed) is
  declared, not waved through: add a trailer to the commit that makes the change, in
  the message's last paragraph beside `Co-Authored-By` (git reads only that paragraph as
  trailers; the same line anywhere else declares nothing),
  ```
  Suite-moves: crosshair, shots/*: the crosshair is now drawn at retail's size (0x4b03c0)
  ```
  (check names or groups as the report names them, wildcards allowed, then why), and run
  the suite again. The report lists it under "Moved as declared", it no longer counts as
  newly failing, and the `merge` skill makes its new result ours' last good one once the
  branch is on main. Never declare a move you have not looked at in the evidence.
- Never run `-UpdateBaseline` from a branch: last good results are shared by every
  worktree and are only changed on main, by the `merge` skill.

For how a probe or a capture works and how to dig into one result, use the
`retail-probe` (readings) and `retail-capture` (pictures) skills.

## 5. On a pass

```
tools\suite\verify-gate.ps1 -Stamp
```

records that these changes were verified, so the Stop hook stays quiet until the code
changes again. It refuses if the last run did not pass or the code changed since.
