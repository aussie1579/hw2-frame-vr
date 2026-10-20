# HW2 Frame VR

**A proof-of-concept VR mod for Homeworld 2 Remastered**, played on a Valve **Steam Frame** streamed from a Windows PC
through SteamVR. The fleet floats around you in stereo 3D, the game camera turns with your head, and the
controllers drive the game: a laser pointer as the mouse, the game's own command menus on the D-pad, and a fleet
tablet on your left wrist.

It is an `opengl32.dll` proxy that sits next to the game's exe. It renders every frame once per eye, hands the frames to
SteamVR, and moves the game's 2D interface onto panels in VR space. The game itself is not modified (see
[What it changes](#what-it-changes)).

> **Status: proof of concept.** It works start to finish in single-player and skirmish on the setup below, at around
> 60 fps in battles. It has been tested on one machine and one headset only. Expect rough edges.

## Requirements

- **Homeworld Remastered Collection** on Steam (Windows). Tested on Steam build 12653478 (HomeworldRM.exe v2.303).
- **SteamVR** and a **Steam Frame**. The controller binding is made for the Frame's controllers. Other SteamVR
  headsets should show the picture, but their controllers need a binding of their own, made in SteamVR's controller
  settings.
- Windows 10 or 11, 64-bit. A GPU that can draw a 2880x2880 window twice per frame (tested on an RTX 4070 SUPER).

## Install

1. Download the release zip from [Releases](https://github.com/aussie1579/hw2-frame-vr/releases) and extract it
   anywhere.
2. Quit the game and run **`Install.cmd`**. It finds the game through Steam (pass `-GameDir "<...\HomeworldRM\Bin\Release>"`
   if it can't) and asks whether to apply the **Large Address Aware** patch. Answer **Y**: the square VR window needs
   more memory than the 32-bit game gets by default.
3. Put the headset on (streaming from the PC) and run **`Launch VR.cmd`**. It starts Steam and SteamVR if they aren't
   running, then Homeworld 2 straight into VR. The main menu appears in front of you.

`Uninstall.cmd` removes everything again. A Steam update or a file check undoes the Large Address Aware patch;
`Launch VR.cmd` tells you when, and running `Install.cmd` again puts it back.

## Controls (Steam Frame)

| Right hand | |
|---|---|
| Laser | the mouse pointer: on the game's interface panels, or out in the world at the ship you point at |
| Bumper | left click: select, drag a selection box, click buttons |
| Trigger | right mouse button: a tap opens the game's right-click menu; hold and move to turn the camera |
| Right stick | up/down zooms; click = focus on what the laser is on; hold = recentre the view |
| A / B / X / Y | move order / cancel (Esc) / focus on the selection / Sensors Manager |
| Menu | the game's menu (F10) |

| Left hand | |
|---|---|
| Left stick | orbit the camera; click = focus on the mothership |
| Trigger | Shift: add to the selection, raise/lower move orders |
| Bumper | Ctrl: force attack, Ctrl + group number assigns a group |
| D-pad, hold | command menus with the game's icons: ▲ orders, ◀ tactics and stances, ▶ formations, ▼ fleet (build, research, launch...). Let go over a slice or click it. |
| Trigger + D-pad | ▲ ship abilities (cloak, mines, EMP...), ◀ special (capture, salvage, kamikaze...), ▼ commands (waypoint, rally, retire...) |
| Palm towards you | the wrist tablet: Sensors Manager, select all, mothership, menu, groups 1-0 |
| View | tactical overlay (Tab) |

Every button can be rebound in `hw2vr.ini` (`[buttons]`, next to the game's exe). The file documents every setting:
world scale, panel placement, stick speeds, click and drag thresholds and more.

## What it changes

- **Adds** `opengl32.dll` (the proxy), `openvr_api.dll` (Valve's OpenVR client), `hw2vr.ini` and a `hw2vr_bindings`
  folder to `HomeworldRM\Bin\Release`. A `hw2vr.log` is written there on each launch.
- **Backs up** an `opengl32.dll` that is already there (for example ReShade) and puts it back on uninstall.
- **Patches one bit** of `HomeworldRM.exe`, the Large Address Aware flag, only if you say yes. The original exe is
  backed up and uninstall restores it.
- **Contains no game files or game code.** The menus use the game's own icons, read at runtime from your installed
  `Homeworld2.big`.
- Made for **single-player and skirmish**; multiplayer is untested.
- If VR can't start, or a game update moves something the mod looks for, that part switches itself off: the game runs
  flat as usual, and `hw2vr.log` says what happened.

## How it works

- **Stereo:** the proxy records each frame's GL calls and replays them a second time with the other eye's camera,
  so both eyes show the same moment.
- **Head tracking:** the game's view matrices are swapped for per-eye ones from the headset pose. A hook on the
  game's camera update turns the game camera itself with your head, so culling and clicking work wherever you look.
- **Interface:** the game's HUD is lifted out of its own framebuffer onto VR panels, a console and the wrist tablet.
  Health bars and other markers stay at their ships' depth.
- **Input:** the controllers are mapped with SteamVR Input. The laser pointer becomes mouse events sent straight to
  the game window.

The reverse-engineering notes, with addresses, signatures and render passes for the tested build, are in
[`docs/FINDINGS.md`](docs/FINDINGS.md). The original feasibility study is in [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md).

## Building from source

Visual Studio 2026 with the C++ desktop workload (its CMake is used). The game is 32-bit, so everything builds Win32.

```powershell
pwsh -File tools\smoke-test.ps1   # build + tests, no game or headset needed
pwsh -File tools\install.ps1      # install the build (-LargeAddressAware for the patch)
pwsh -File tools\launch-vr.ps1    # launch into VR
pwsh -File tools\package.ps1      # build, test and zip a release into dist\
```

## Licence

MIT (see [`LICENSE`](LICENSE)). The third-party code keeps its own licence: [OpenVR](https://github.com/ValveSoftware/openvr)
(BSD-3-Clause, `third_party/openvr`) and [stb_image](https://github.com/nothings/stb) (public domain / MIT,
`third_party/stb`).

## Disclaimer

Unofficial fan project, not affiliated with or endorsed by Gearbox Software, Blackbird Interactive or Valve.
Homeworld is a trademark of Gearbox Software. You need your own copy of the game.
