# Differences from the original game

OpenFAKK2 aims to behave like the original `fakk2.exe`: where the two differ, the original
is right unless the difference is on purpose. This page is the short version for players.
The full record, subsystem by subsystem, with what was read from the original executable
and at which address, is [`known-differences.md`](../known-differences.md).

## Added on purpose

Each of these can be turned off to get the original's behaviour back
([Configuration](04-configuration.md)).

- **Widescreen.** On a screen wider than 4:3 you see more to the sides instead of losing the
  top and bottom, and the HUD and menus keep their 4:3 shape instead of stretching. Lens
  flares keep their shape too.
- **HUD size.** The HUD can be drawn smaller, for high resolutions.
- **Field of view** as a saved setting.
- **Brightness** that changes only the game's picture, not the desktop's.
- **Game controllers**, with default bindings, menu navigation and rebinding.
- **An OpenFAKK2 Options page**, every screen resolution, 8x anti-aliasing, anisotropic
  filtering, a borderless window.
- **OpenAL sound**, panned as the original's Miles sound was, with HRTF on headphones as an
  option; and the levels' reverb, which the original produced only on an EAX sound card
  with `s_reverb 1` (off by default), rendered by OpenFAKK2 itself and on by default.
- **Demos recorded automatically** if you want them.
- **The map viewer** (`viewmap`) and **crash reports** with the crash's location.

## Done the original's way, even where odd

These are the original's behaviour, kept because the aim is to match it:

- `record` refuses unless `g_syncronousClients` (misspelt, as in the original) is set.
- A level started straight from the command line loads before the global sound aliases,
  so some of its sounds are missing, on both games.
- A few animations named in the game's model files don't exist, and are dropped with a
  console message.
- The server runs at 20 frames a second (`sv_fps 20`): the game's balance assumes it (fire
  and some damage happen once per server frame).

## Changed from the original on purpose, to fix it

A few original behaviours were bugs worth not copying. For example, the inventory bar no
longer stays drawn over the main menu's buttons, and after a menu (a save, say) Julie no
longer lurches forward and then ignores you for as long as the menu was up. Each one is
marked *(deliberate)* in
[`known-differences.md`](../known-differences.md), with the reason.

## Not like the original yet

Found by comparing the two games frame by frame and not yet fixed:

- The weapon picture in the HUD sometimes shows a weapon where the original shows none.
- An explosion lights Julie bright yellow; on the original she keeps her own colours.
- In swamp1's opening cinematic Julie's head turns differently.
- Close-up textures look a little blurrier.
- Some idle animations start on a different variant than the original's.

## Left out

- Commands that only served the original's own C++ class system, Miles, IPX networking,
  MIDI, DirectInput, the CD, or other hardware of the time.
- The original's demo loop, which names demos the game doesn't ship.
- Multiplayer: FAKK2 has none.
