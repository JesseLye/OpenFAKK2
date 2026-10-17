# FAQ and troubleshooting

### Is it finished?

No. OpenFAKK2 is playable but has not reached a first release: the opening levels have been
checked closely against the original, but nobody has played all 30 levels through on it
yet, and Windows is the only platform. See the [status by area](06-status.md).

### Do I need the CD?

You need the game's data, installed and patched to 1.02. OpenFAKK2 itself has no disc
check.

### "...is not built" / the game doesn't start

Build it first ([Building from source](07-compiling.md)). If the game starts but stops at a
black screen or an error about `pak0.pk3`, `fs_basepath` doesn't point at the folder that
contains `fakk\pak0.pk3`.

### It asks whether to start in safe mode

The last run didn't exit cleanly (a crash, or it was killed). Answering yes starts with
default settings. `+set com_abnormalExitDialog 0` turns the question off.

### Which game modules am I running?

The ones built beside `openfakk2.exe` (`fakk\gamex86.dll`, `fakk\cgamex86.dll`), from
Ritual's 1.02 SDK. A `gamex86.dll` or `cgamex86.dll` in your home folder's `fakk` is
loaded first, so Ritual's own DLLs can be run on purpose by copying them there. The line
`LoadLibrary (...gamex86.dll)` in the console says which one loaded. Don't copy OpenFAKK2's
files into the game's install, where the two sets would mix.

### The picture is stretched / cropped on a widescreen monitor

It shouldn't be: OpenFAKK2 shows more to the sides on a wide screen and keeps the HUD and
menus at their 4:3 shape. `cl_widescreenFov 0` and `cl_widescreenHud 0` bring back the
original's crop and stretch ([Configuration](04-configuration.md)).

### The HUD is huge at 1440p or 4K

Lower **HUD Size** in OpenFAKK2 Options (`cl_hudScale`, 0.25 to 1). The menus stay full
size.

### The brightness slider does nothing / changes my whole desktop

OpenFAKK2 applies brightness to its own picture with a shader, so it works in a window and
leaves the desktop alone. It needs OpenGL 2.0; on older cards it falls back to the
display's gamma ramp, as the original did, which some drivers ignore.

### The sound is quieter than the original

Check Windows' volume mixer first. It remembers a level for every program *path*, so a copy
of OpenFAKK2 in a folder you once turned down starts quiet; the project lost an evening to
a build left at 25% (12 dB). OpenFAKK2's levels and panning were measured against the
original's and match within a decibel or so.

### Doors are silent on the first level I load

If you start a level straight from the command line (`+devmap`), it loads before the game's
global sound aliases, so some sounds are missing, on the original as well. Load a level
from the menu, or a second level, and they are there.

### Julie moves strangely at very high frame rates

Fixed on 2026-10-16: at high frame rates (or now and then at the default 85) she strafed or
walked late, and at 250 fps she barely moved. If you still see it, please report it with
your `com_maxfps`.

### The console prints `TIKI_LoadAnim: Could not load ...`

Harmless: a few animations named in the game's model files don't exist in its archives.
The original drops them too.

### Can I play multiplayer?

FAKK2 is single-player only. The dedicated server is there for testing.

### Where do I report a bug?

Open an issue on OpenFAKK2's GitHub page (a crash or hole someone could exploit goes
privately instead: see [`SECURITY.md`](../../SECURITY.md)). Describe what you did and
what you saw, the level, and your settings if they matter, and attach the console log
(`+set logfile 2` writes `fakk\qconsole.log` in the home folder) and, after a crash,
`crash.dmp` from the same folder. If the original game does the same thing, say so:
OpenFAKK2 aims to behave like the original, oddities included.
