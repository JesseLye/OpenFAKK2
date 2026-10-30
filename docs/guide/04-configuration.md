# Configuration

Every setting is a console variable (*cvar*), saved in `fakk\config.cfg` in the home folder
([Installing](01-installation.md)). Change one in the menus, in the console
(`cl_hudScale 0.75`), or on the command line (`+set cl_hudScale 0.75`). A setting marked
**restart** takes effect after the menu's **Apply** or `vid_restart` (`snd_restart` for
sound).

The original's options menus all work, with their original settings. This page covers what
OpenFAKK2 adds or treats differently. Each addition can be turned off to get the original's
behaviour back.

## OpenFAKK2 Options

**Video & Audio → OpenFAKK2 Options**. **Apply** keeps changes and restarts what needs it,
**Back** undoes them, **Default** resets the page.

| Menu | Cvar | Default | What it does |
| --- | --- | --- | --- |
| Anti-Aliasing | `r_ext_multisample` | 0 | 2x, 4x or 8x multisampling; a card without as many samples gets fewer (restart) |
| Anisotropic Filtering | `r_ext_texture_filter_anisotropic`, `r_ext_max_anisotropy` | off, 2 | sharper textures at an angle (restart) |
| Frame Rate Limit | `com_maxfps` | 85 | the original's default; 0 is uncapped |
| Field of View | `fov` | 90 | the player's horizontal field of view, 1 to 160. The original has no such setting (`setu fov` only lasted until you quit) |
| HUD Size | `cl_hudScale` | 1 | 0.25 to 1: the HUD, prints and inventory bar drawn smaller, each towards its corner. Menus stay full size |
| Widescreen View | `cl_widescreenFov` | 1 | wider than 4:3, shows more to the sides; 0 crops the top and bottom, as the original |
| Widescreen HUD | `cl_widescreenHud` | 1 | wider than 4:3, keeps the HUD and menus at their 4:3 shape, the HUD in the window's corners; 0 stretches them, as the original |
| Vertical Sync | `r_swapInterval` | 0 | wait for the display's refresh |
| Use a Controller | `in_gamepad` | 1 | game controllers through SDL (restart) |
| Turn Speed | `pad_yawSpeed` | 200 | degrees a second with the right stick all the way over |
| Look Speed | `pad_pitchSpeed` | 140 | the same, up and down; negative inverts it |
| Move Dead Zone | `pad_deadzoneLeft` | 0.2 | how far the left stick moves before it counts |
| Look Dead Zone | `pad_deadzoneRight` | 0.2 | the same for the right stick |
| OpenAL Sound | `s_useOpenAL` | 1 | OpenAL Soft (shipped with OpenFAKK2); 0 is Quake III's software mixer (restart) |
| 3D Headphones | `s_alHRTF` | 0 | lets OpenAL Soft use HRTF on headphones; off pans as the original's Miles sound did (restart) |
| Borderless | `r_noborder` | 0 | a window with no border (restart) |
| Record Demos | `cl_autoRecordDemo` | 0 | records a demo of every level ([Running](02-running.md#demos)) |

Not on the page: `pad_lookExponent` (2, the right stick's curve, 1 to 4),
`pad_runThreshold` (0.7, how far the left stick goes before Julie runs) and
`cg_exactbounds` (1: Julie's movement is predicted against a model's exact bounds, so she
does not bob while standing on one such as the hovercraft at the start of `fakkhouse`; 0
is the original's prediction, which bobs there).

## Video

The **resolution** list in Video options has *Desktop* (your screen's size, `r_mode -2`, the
default), every size your display offers, and the common wide ones; the original's list
stopped at 1600x1200. Any size works from the console:
`r_mode -1; r_customwidth 2560; r_customheight 1440; vid_restart`.

**Brightness** (the original's slider, `r_gamma`) is applied by OpenFAKK2 to its own
picture, so it works in a window and leaves the rest of the desktop alone. `r_gammaShader 0`
(restart) uses the display's gamma ramp instead, as the original did.

## Settings that differ from Quake III

OpenFAKK2 keeps the original game's defaults, not Quake III's, where they differ: you walk
unless you hold the run key (`cl_run 0`), and the frame rate is capped at 85. One default
is its own: the screen mode starts at your desktop's size (`r_mode -2`). The full list of
settings the original has, and how OpenFAKK2 treats each, is in
[`known-differences.md`](../known-differences.md), "Commands and cvars the retail engine
has".

## For testing

These exist for the project's test harness but can be useful by hand:

| Cvar or command | What it does |
| --- | --- |
| `cl_demoSeed <n>` | fixes the random numbers when a demo starts, so particles and flashes play back the same every time |
| `vpad attach`, `vpad button a 1` ... | an imaginary game controller the console can press |
| `com_abnormalExitDialog 0` | no "start in safe mode?" question after a crash |
