# How OpenFAKK2 is made and checked

## The rule: the original executable decides

Ritual released FAKK2's game code (the 1.02 SDK, in `code/fakk`) but not its engine.
OpenFAKK2's engine is ioquake3's, extended with everything FAKK2's engine did that Quake
III's didn't: the TIKI model system and its renderer, the `.urc` menus and HUD, FAKK2's
shader extensions, lens flares, its sound and music rules, its saved games and its network
protocol.

Each of those is rebuilt from what the retail `fakk2.exe` (1.02) does, read from the
executable with Ghidra (through the REA tool) and checked by running both games. A
behaviour is right when it matches the original, not when it looks plausible. Ritual's
design documents (in their *Heavy Metal FAKK 2 Tools* release) are leads, not answers:
where they and the executable disagree, the executable wins.

Everything learned this way is written down in
[`known-differences.md`](../known-differences.md), with the addresses in `fakk2.exe` it
was read from: what matches, what still differs, and what differs on purpose and why.

## Running both games: the harness

`tools/retail-compare` runs the same *probe* (a level script that waits for the player,
presses keys, fires weapons, moves cameras and prints entity state or takes screenshots)
on the retail game and on OpenFAKK2, and compares what comes back: entity readings,
screenshots paired with a difference image, recorded sound, or demos. The retail game runs
from a copy in `build\sandbox\retail` (it needs the game disc in a drive); OpenFAKK2 runs on
its own empty home folder, so no run sees another's settings. Its
[README](../../tools/retail-compare/README.md) covers the runners and the probes.

## The test suite

`tools/suite` turns the probes into checks (177 of them: spawn output on every map, level
state, every entity, saves, menus, screenshots, demos played frame by frame, sound, frame
time). Each check is judged two ways: against the retail game's result (cached, so a run
never starts the retail game) and against OpenFAKK2's *last good* result, so that what
already differed is told from what got worse.

    tools\suite\run-suite.ps1 -Level quick -Changed     (the checks for what a branch changed)
    tools\suite\run-suite.ps1 -Level standard           (most of them)

A check's last good result only changes when a branch merges, and only for a check its
commit declares it changes on purpose (a `Suite-moves:` trailer) or one that got better.
A regression can't become the new normal without someone saying why. The
[suite's README](../../tools/suite/README.md) has the details.

## How the work is done

OpenFAKK2 is built largely by AI coding agents (Claude Code), several at once, each in its
own git worktree under `.claude/worktrees`, with the human directing and merging. What keeps
that trustworthy is the same for agents and people:

- **A gate before finishing.** A hook (`.claude/settings.json`) asks a session that changed
  code to run the suite before it finishes, and records when it has passed for exactly those
  changes (`tools/suite/verify-gate.ps1`); merging checks for that record.
- **Skills** in `.claude/skills` are the project's playbooks: `retail-probe` (gameplay
  against retail), `retail-demo` (pictures and sound through demos), `retail-capture` (menus
  and stills), `verify` (running and reading the suite) and `merge` (merging a branch and
  keeping the last good results right).
- **Sharing the machine.** Each worktree has its own homes and sandboxes, so up to four
  worktrees' games run at once; checks that time things or record sound take the machine
  alone ([harness README](../../tools/retail-compare/README.md), "Games side by side, or
  alone").

## Rules for contributions

- **Never commit Ritual's data or documents.** The game's files, Ritual's design
  documents and tools release, demos and screenshots of the game stay out of the
  repository (demos live in `build\demos`, references in `build\suite`; `build\` is
  ignored).
- **Keep the engine and the game code apart.** The engine is GPLv2+; `code/fakk` is under
  Ritual's SDK licence, which doesn't allow redistributing modified copies and isn't GPL
  compatible. Change `code/fakk` only where the game code can't compile otherwise, and say
  why.
- **Show it against retail.** A change to how the game behaves or looks comes with the probe
  or check that shows it matching the original, and a line in `known-differences.md` with
  the addresses it was read from.
