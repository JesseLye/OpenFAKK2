# Running the game

Start `openfakk2.exe` with `fs_basepath` pointing at your install
([Installing](01-installation.md)). The retail intro plays (Escape skips it) and the main
menu comes up; **New Game** runs the opening cinematic and starts in `fakkhouse`, as the
original does.

## Command line

Anything after the executable is a console command, as in Quake III. Useful ones:

| Command line | Does |
| --- | --- |
| `+set fs_basepath "<install>"` | where the game's data is (needed unless OpenFAKK2 sits in the install) |
| `+set fs_homepath "<folder>"` | where your config, saves and demos go (default `%APPDATA%\OpenFAKK2`) |
| `+devmap training` | straight into a level, cheats allowed |
| `+set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1600 +set r_customheight 900` | a window of any size |
| `+set developer 1` | the game's developer messages in the console |
| `+set logfile 2` | the console into `fakk\qconsole.log` in the home folder |

The console opens with `~` (or the key below Escape).

## Settings

Retail's options menus all work. OpenFAKK2's own settings (widescreen, field of view, HUD
size, anti-aliasing, the controller, sound backend) are on **Video & Audio → OpenFAKK2
Options**; the [Configuration](04-configuration.md) page lists them with their console
names.

## Saving and loading

Saving and loading work as in the original, through the menus or the console
(`savegame <name>`, `loadgame <name>`), and the game autosaves at the start of each level.
Saves are written in the retail format, and games saved by the original load in OpenFAKK2.

## A game controller

Plug one in before starting: it is picked up on its own (`in_gamepad 1`). The left stick
moves, the right stick looks, the triggers are Julie's two hands, the shoulders step through
the inventory, the D-pad picks one of four inventory slots, A jumps, B crouches, X uses,
Y opens the inventory, and Start is Escape. In the menus the left stick moves the cursor and
A clicks. The controls page rebinds the pad's buttons like keys. Sensitivity and dead zones
are in [Configuration](04-configuration.md).

## Demos

A demo records what you see and hear, for playing back later. OpenFAKK2's demos are the
original game's `.dm3` files: either game plays the other's.

    g_syncronousClients 1      (spelled so; record refuses without it, as in the original)
    record mydemo              (writes fakk\demos\mydemo.dm3 in the home folder)
    stoprecord
    demo mydemo                (plays it back)
    timedemo 1; demo mydemo    (plays it as fast as possible and prints the frame rate)

`cl_autoRecordDemo 1` records every level by itself.

## The map viewer

`viewmap <map>` (for example `viewmap swamp1`) flies a camera through any retail level
without starting the game: the movement keys move, `viewpos` prints where you are,
`viewstop` ends it. Nothing a level script does is shown.

## The dedicated server

`openfakk2ded.exe +set fs_basepath "<install>" +map zoo` runs a level with no window.
FAKK2 is a single-player game, so this is mainly useful for testing (the project's test
suite runs most of its checks on it).

## If something goes wrong

See the [FAQ](03-faq.md). After a crash, the console (and `qconsole.log`, with
`+set logfile 2`) holds the crash's location and stack, and `crash.dmp` is written in the
home folder's `fakk`; include both in a report.
