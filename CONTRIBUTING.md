# Contributing to OpenFAKK2

OpenFAKK2 is not public yet and has no issue tracker; this is how work on it is done today.

**Read [How OpenFAKK2 is made and checked](docs/guide/08-development.md) first.** In short:

- **The original game decides.** A change to how the game behaves or looks matches what the
  retail `fakk2.exe` (1.02) does, and shows it: a probe or a suite check run against the
  original, and a line in [`known-differences.md`](docs/known-differences.md) with the
  addresses in the executable it was read from. A difference kept on purpose is marked
  *(deliberate)* there, with the reason, and can be switched back.
- **Run the suite** before you're done: `tools\suite\run-suite.ps1 -Level quick -Changed`,
  and read its report ([the suite's README](tools/suite/README.md)). A check you change on
  purpose is declared in the commit with a `Suite-moves:` trailer and the reason.
- **Never commit Ritual's data or documents**: the game's files, Ritual's design documents or
  tools, demos, or screenshots of the game beyond the README's few in `docs/assets/images`.
- **Keep the engine and `code/fakk` apart.** The engine is GPLv2+; `code/fakk` is Ritual's
  SDK code under its own licence. Change it only where the game code can't compile
  otherwise.

**On AI-written code:** OpenFAKK2 is built largely with AI coding agents (Claude Code), and
contributions are held to the same checks however they were written. The project's agent
playbooks are in `.claude/skills`.

**Reporting a bug:** see the [FAQ](docs/guide/03-faq.md#where-do-i-report-a-bug).

OpenFAKK2 starts from ioquake3, whose own contribution guidelines apply to ioquake3, not
here.
