---
name: merge
description: Merge a branch (usually a worktree's) into main in OpenFAKK2 and keep the test suite's last good results right - check the branch was verified, merge, build, run the suite on main, and make new last good results only of the checks the branch declared it moves or that got better. Use this whenever asked to merge a branch or worktree, land or integrate a change, "bring it into main", or tidy up after a finished worktree, even if nobody mentions the suite or baselines.
---

# Merging a branch into main

The test suite (`tools/suite/`) judges every branch against **ours' last good results**,
kept in the main checkout's `build\suite\reference\ours` and shared by all worktrees. A
merge is the one moment they may change, and only for what the branch meant to change. A
regression accepted here is hidden from every later run, so this skill never accepts a
change nobody explained.

Run it from a session in the **main checkout**: a worktree session cannot touch main.

## 1. Before merging

- **The branch was verified.** If it has a worktree, its last suite run must have passed
  for its current changes:
  ```
  pwsh -NoProfile -File <worktree>\tools\suite\verify-gate.ps1 -Stamp
  ```
  succeeds only then. If it refuses, run the `verify` skill in that worktree (or here
  after the merge, step 4, before accepting anything) and read why.
- **What the branch says it moves:**
  ```
  git log main..<branch> --format="%h %s%n%(trailers:key=Suite-moves,valueonly)"
  ```
  Each `Suite-moves: <checks>: <why>` line names checks whose results change on purpose.
- **Probes it changed:** `git diff --name-only main...<branch> -- tools/retail-compare/*.scr`.
  Their retail references must be made again (step 3).
- Main is clean (`git status`), and nobody is mid-run: the harness lock
  (`build\harness.lock.owner`) shows another session's run; wait for it.

## 2. Merge

```
git merge --no-ff <branch>
```

as main's history does ("Merge branch 'worktree-...'"). Resolve conflicts by reading
both sides; a conflict in `tools/suite/suite.json` or a probe usually means both branches
changed checks, so keep both.

## 3. References for changed probes

For each changed probe, make its checks' retail references again (needs the game disc;
puts the retail game's windows on screen):

```
tools\suite\refresh-retail.ps1 -Only <checks that run the probe>
```

`run-suite.ps1 -List -Only <group>` shows the names. Without the disc, say so: those
checks are skipped as stale until someone refreshes them.

## 4. Run the suite on main

```
tools\suite\run-suite.ps1 -Level quick -Changed -Since HEAD^1 -AcceptMoves
```

(`-Level standard` for a branch that touched several areas or the renderer and the UI
together.) It builds main, runs the checks for what the merge changed, and keeps as ours'
last good results only:

- the checks a `Suite-moves` trailer since `HEAD^1` declares, which the report lists under
  "Moved as declared";
- the checks that got better ("Better than ours' last good result");
- the checks whose retail reference was just made again.

It prints `kept as ours' last good results: ...`. Everything else keeps its old result.

## 5. Read the report

`build\suite\runs\<time>\report.md`:

- **Newly failing, not declared**: something on main is now worse and nobody said why.
  Compare with the branch's own last report (`<worktree>\build\suite\runs\`): if the
  branch passed and main fails, it is the merge (two branches that each pass alone);
  if the branch never ran it, it is the branch. Either way: do not accept it, do not run
  `-UpdateBaseline`, and tell the user with the evidence. Fix it in a worktree, or
  revert the merge if the user wants.
- **Moved as declared**: look at the evidence (the pair pictures, the differing fields)
  and check it moved the way the trailer says. A declared move that shows something else
  is a regression with a label on it: report it.
- A declared move the run did not select is listed at the end of the report; run it
  with `-Only` and `-AcceptMoves` too.

## 6. Afterwards

- Tell the user: what was merged, which checks were kept as new last good results and
  why (the trailers), anything failing, and any reference left stale.
- `baseline.json` in each kept check records the commit and why; nothing else needs
  committing (references and results are never committed).
- Do not push, and do not remove the worktree or branch, unless the user asks.

Never run `run-suite.ps1 -UpdateBaseline` (which keeps every result) as part of a merge.
It is for setting the suite up, or for a user who has read a whole report and says so.
