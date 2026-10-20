# CLAUDE.md — HW2 Frame VR

VR mod for **Homeworld 2 Remastered** (HomeworldRM.exe, Homeworld Remastered Collection on Steam), played on a **Valve Steam Frame streamed from the Windows PC** via SteamVR. Owner: Brendo. Full background and reasoning: `docs/FEASIBILITY.md` — read it before making architectural decisions.

## Goal (in tiers — don't skip ahead)

1. **Tier 0/1 — Baseline:** theater mode + a tuned SteamVR binding for Frame controllers. Fallback product.
2. **Tier 2 — Stereo MVP (the main target):** an `opengl32.dll` proxy that renders stereo, drives the game camera from the HMD pose, moves the HUD onto a floating quad, and submits frames to SteamVR.
3. **Tier 3 — Usable VR:** synchronized stereo, world-scale slider, right-controller pointer → mouse, left-hand palette for build/formations/tactics menus.
4. **Tier 4 — Stretch:** engine-level hooks (tabletop scale, grab-and-rotate world, 3D move orders from controller position).

## Current status

**Phase 0 (spikes) — done, GO (2026-10-14).** Brendo chose to skip Phase 1 and go straight to Tier 2.
**Tier 2 (stereo MVP) — v0.5.0; headset tests 1-5 done: stereo, head tracking, HUD split (2D panel + stereo 3D overlay) and synchronized stereo by GL replay all confirmed in-headset; ~60 fps in battle, every frame replayed (see FINDINGS).** Synchronized stereo (`src/stereo/frame_replay` + `src/proxy/gl_record_hooks`, AFR fallback via `[vr] stereo = afr`), frame loop (`src/vr/vr_frame`), per-eye view + projection (`src/stereo`), HUD lifted from its FBO onto an OpenVR overlay fixed in game-camera space (`src/hud`), OpenVR loaded at runtime from `third_party/openvr` (v2.15.6, win32). Ctrl+Shift+F8 toggles VR. No multiplayer guard, by decision (see Conventions).
**Tier 3 (VR command interface) — v0.13.0, awaiting headset test 15.** v0.13.0: left trigger + D-pad opens ability menus (`[buttons] <button>_alt`, `menu_modifier`): up = abilities (cloak, defence field, ping, mines, EMP...), left = special (capture, kamikaze...), down = commands (waypoint, rally, retire, scuttle); trigger + right free. Test 14 (v0.12.2): health bars "much more stable"; the "static theatre image" was the SteamVR dashboard (opened over the intro videos) - launch-vr.ps1 now passes -noMovies. v0.12.3: Hyperspace (J, the game's icon) in the Orders menu; the game's command panel has more context abilities not yet offered (ping, cloak, defence field, mines, waypoints, rally, retire, scuttle, capture...). v0.12.2: health-bar jitter = the game snaps markers to whole pixels, so the game camera now holds still within a 3° dead zone (`[vr] game_camera_deadzone`); camera motion blur neutralised in VR (program 51 gets last camera = current); 0.12.1 marker depth memory. Audio: a game started while SteamVR runs uses "Steam Streaming Speakers" (open question to Brendo). v0.12.0: square 2880x2880 VR window launched straight into VR (`tools/launch-vr.ps1`, `-hw2vr`), needs the opt-in LAA patch (`install.ps1 -LargeAddressAware`, applied 2026-10-19); mouse events delivered to the window (`[controls] mouse = auto`); menu screens (no camera updates) shown on the panel; `game_fov = auto`. v0.11.1: Test 12 (v0.11.0): camera turn works; lens flares now hidden in VR (`[vr] lens_flares`), markers re-aligned with the turning camera (grid remembers its turn). Open: jagged UI = eye images are 3440x1440 over a ~square FOV (31 px/deg across, 13 up/down) — a square game window (2880x2880 works, posted mouse messages reach below the monitor) is the proposed fix; green flash on subsystem hover unexplained. v0.11.0: the **game camera turns with the head** (yaw + pitch, engine hook on the camera update `FUN_0078bf32`, `[vr] game_camera = head`): the game draws, culls (it culls to its own frustum) and picks where the player looks, so orders work behind you; eyes placed relative to the turned camera (E·H), picture unchanged; optional `[vr] game_fov` for the headset's taller view (off). Bumper clicks no longer become drag boxes (`click_ms` 300, `drag_degrees` 3). Test 11 (17 Oct log, v0.10.1): right-stick focus and the Close tab work (4 of ~20 closes needed a second press), F10 menu wrongly shows the build-window panel. v0.10.0 test: build window couldn't be closed (B only opens; Ctrl+B/Ctrl+L/R toggle — FINDINGS), Brendo wants the middle click (focus) on the right stick click (hold = recentre now), D-pad diagonals double-pressed. Test 9 (v0.9.0): tablet appears (palm axis +X works) but had far too many buttons; Brendo wants the game's own textures, a D-pad press-and-hold submenu per direction, right trigger = right mouse (hold + move = swivel the camera), bumper = left click. Now (`[vr] hud = tablet`): **D-pad command menus** — radial menus with the game's icons read at runtime from `Homeworld2.big` (`src/assets/game_assets`, vendored stb_image): orders / tactics+stances / formations / fleet (`src/input/command_menus`, `src/input/radial_menu`, `[buttons] dpad_* = menu:<name>`), orders target what the laser pointed at; a slim **wrist tablet** (4 game-icon buttons + groups 1-0, `src/input/tablet`, `src/input/ui_canvas`); **controls** (`src/input/controls`): bumper = left button, trigger = right button (tap = right click, held past 2° = relative-mouse camera turn); the game's **right-click menu** is cropped onto a window where the laser pointed (`src/hud/screen_windows` popup watch); resources above the tablet, build/research/launch beside it while open, whole screen UI in front only for menus/dialogs. World markers at ship depth via the `PHASE_UIIn` engine hook (`src/proxy/engine_hooks`, `src/hud/marker_geometry`, `src/stereo/depth_probe`). Steam Frame-only binding (`bindings/`, actions `right_trigger`/`right_bumper`). Open: swivel speed/direction untested, crop regions assume HUD scale 1, selection/subtitle panels not shown, 3D markers flat at depth. The left-hand palette from the original Tier 3 plan is now the wrist tablet + D-pad menus.
The proxy skeleton exists: `src/proxy` (366 forwarded exports + `wglGetProcAddress`/`wglSwapBuffers` hooks), `src/stereo/view_override` (spike: Ctrl+Shift+F9 cycles a fixed camera yaw/offset). Build + test with `tools/smoke-test.ps1` (no game needed), deploy with `tools/install.ps1` / `tools/uninstall.ps1`, end-user release zip with `tools/package.ps1` (`dist/`; double-click `Install.cmd` / `Launch VR.cmd` / `Uninstall.cmd` from `tools/package/`; nothing extracted from the game ever goes in it or in the repo).
HWRM is **32-bit and not Large Address Aware** (2 GB) — build everything Win32 and keep the proxy lean (LAA is opt-in only, see Conventions). See `docs/FINDINGS.md`.

### Phase 0 checklist (go/no-go)
- [x] Record HomeworldRM.exe bitness (32/64), file version and Steam build ID → `docs/FINDINGS.md` *(x86, v2.303 / CL 3409635, Steam build 12653478)*
- [x] Record GL context type: core vs compatibility profile, GL version actually created *(3.3 compatibility via wglCreateContextAttribsARB, vsync off; game itself uses no legacy GL)*
- [x] **Dormant VR check:** *(negative on build 12653478 — no OpenVR/OpenXR/LibOVR code; details in FINDINGS)* search the binary for `openvr_api`, `IVRSystem`, `IVRCompositor`, `VR_Init`, "vive", "oculus", "steamvr", VR-ish command-line flags, and VR-related Lua bindings. Gearbox reportedly tested a VR spectator mode in 2016. If anything turns up, chase it before anything else.
- [x] ~~RenderDoc~~ apitrace capture of a skirmish frame: identify main scene pass, where view/projection matrices are uploaded (`glUniformMatrix4fv` / UBO), shadow/glow/post passes, and HUD/sensors-manager passes *(pass map + `inMatV`/`inMatC`/`inMatP` uniforms; HUD in its own FBO — see FINDINGS)*
- [x] Prove one view-matrix override shifts the scene cleanly without breaking HUD *(2 in-game runs; light glows needed the view-space `inMatP` rule; CPU-positioned 2D markers stay with the game camera — see FINDINGS)*
- [x] Write the go/no-go call in `docs/FINDINGS.md` *(GO, final)*

**Go if** the camera matrix is identifiable + overridable and HUD draws are distinguishable. **No-go/rethink if** matrices are baked per-object CPU-side or HUD/scene draws interleave unpredictably.

## Architecture (planned)

```
HomeworldRM.exe
  └─ opengl32.dll (our proxy) ── forwards to system opengl32.dll
       ├─ hooks: wglSwapBuffers, wglCreateContext*, glUniformMatrix4fv / UBO uploads, glViewport
       ├─ camera:  replace view matrix with  gameView * eyeOffset * hmdPose  (world-scale multiplier)
       ├─ stereo:  v1 alternating-frame (AFR) → v2 synchronized (replay scene per eye)
       ├─ HUD:     detect ortho/2D passes → render to FBO → textured quad in VR space
       ├─ submit:  OpenVR IVRCompositor::Submit (GL texture)  [OpenXR XR_KHR_opengl_enable later if needed]
       └─ input:   OpenVR actions → laser raycast (panel UI mask / world projection) → SetCursorPos + SendInput
```

Suggested layout:
```
src/proxy/      # DllMain, export forwarding, GL hooks
src/vr/         # OpenVR init, pose, submit, actions
src/stereo/     # matrix capture/override, AFR/sync logic
src/hud/        # 2D-pass detection, FBO, quad
src/input/      # laser maths, controls, key names, input injection, sprites
bindings/       # SteamVR action manifest + Steam Frame default binding (generate_bindings.py)
docs/           # FEASIBILITY.md, FINDINGS.md (RE notes), decisions
tools/          # RenderDoc/CE/Ghidra helper scripts
```

## Tech choices

- C++20, CMake, MSVC (target bitness must match HomeworldRM.exe — confirm in Phase 0).
- MinHook for function hooks; OpenVR SDK first (simplest GL path), OpenXR only if needed.
- GLM for matrices. Keep dependencies vendored or via vcpkg.
- Config in an `.ini` next to the DLL: world scale, IPD multiplier, stereo mode, HUD distance/size, disable-post-fx toggles, debug overlay.

## Conventions & rules

- **Never modify game files in place.** Proxy DLL drops into `HomeworldRM\bin\Release\`; ship an install/uninstall script. Back up anything replaced.
  - *One exception (decided 2026-10-14):* an **opt-in** Large-Address-Aware patch of `HomeworldRM.exe` in the install script. Back up the original exe first; uninstall restores it. A Steam update or file verify silently reverts it, so the proxy checks the LAA bit at startup and warns (log + in VR) when it's off. Never required: the proxy must still run within 2 GB.
- **Mod target: vanilla first.** Do Phase 0–2 captures and RE with no `-workshopmod` (mods can replace shaders). Check FX: Galaxy compatibility once the camera hook is proven.
- **Single-player and skirmish are the target.** No multiplayer guard (decided 2026-10-19: there is no competitive HW2 scene, so it isn't worth building). Don't suggest one.
- Every hook must fail safe: if VR init fails, pass through to flat rendering untouched.
- Log to `hw2vr.log` next to the DLL; verbose logging behind a config flag.
- RE findings (addresses, signatures, struct layouts, pass indices) go in `docs/FINDINGS.md` with the game build they were found on. Prefer AOB signatures over hard-coded addresses.
- Lua/data-side tweaks (UI scale, HUD layout, camera limits) live in a separate Workshop-style mod folder `luamod/`, not in the DLL.
- Comfort: fade or snap game-driven camera jumps (focus, cinematics) rather than smoothly flying the player's head.

## Tools on Brendo's machine (local)

Start the RE environment with `tools/start-re.ps1` (Ghidra 11.3.2 on `re/ghidra/HWRM.gpr` with HomeworldRM.exe open writable + Cheat Engine with the MCP bridge). MCP servers are in `.mcp.json` and run from the project `.venv`.

- Ghidra MCP (decompile, xrefs, strings, rename) — render-loop hunting. Ghidra's addresses use base 0x400000; the live exe is ASLR-relocated.
- Cheat Engine MCP (memory scan, breakpoints, struct dissect) — live camera struct discovery. Attach with `evaluate_lua` → `openProcess("HomeworldRM.exe")`. Don't enable DBVM unless a tool needs it.
- RenderDoc 1.46 (core-profile GL only) and apitrace 14.0 **win32** (`%LOCALAPPDATA%\Programs\apitrace-14.0-win32`) for GL captures.
- SteamVR + Steam Frame streaming for testing.

## Related projects (patterns to reuse)

- **Factorio VR palette** — left-hand-attached menu palette + right-hand pointer-as-mouse on Steam Frame. Same input design for Tier 3.
- **SPAZ 2 Frame controls** — fixing stick/pointer feel on Steam Frame.
- **EDF6 RealSync** — prior DLL hooking / RE workflow with CE + Ghidra.

## Key risks (watch for these)

Camera extraction across multiple passes · stereo disparity at Homeworld's huge scales (needs world-scale) · near/far plane z-fighting · post-fx misaligned between eyes · single-thread CPU limits in big battles · indirect feel of two-step mouse move orders.

## References

- Karos Graveyard (HWRM modding wiki): https://github.com/hwrm/KarosGraveyard/wiki
- Steam Frame dev docs: https://partner.steamgames.com/doc/steamhardware/steamframe
- Frame controllers / binding fallback: https://partner.steamgames.com/doc/steamhardware/steamframe/controllers
- OpenVR SDK: https://github.com/ValveSoftware/openvr
- Interaction reference (native Homeworld VR): Homeworld: Vast Reaches
