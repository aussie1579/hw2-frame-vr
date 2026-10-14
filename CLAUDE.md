# CLAUDE.md — HW2 Frame VR

VR mod for **Homeworld 2 Remastered** (HomeworldRM.exe, Homeworld Remastered Collection on Steam), played on a **Valve Steam Frame streamed from the Windows PC** via SteamVR. Owner: Brendo. Full background and reasoning: `docs/FEASIBILITY.md` — read it before making architectural decisions.

## Goal (in tiers — don't skip ahead)

1. **Tier 0/1 — Baseline:** theater mode + a tuned SteamVR binding for Frame controllers. Fallback product.
2. **Tier 2 — Stereo MVP (the main target):** an `opengl32.dll` proxy that renders stereo, drives the game camera from the HMD pose, moves the HUD onto a floating quad, and submits frames to SteamVR.
3. **Tier 3 — Usable VR:** synchronized stereo, world-scale slider, right-controller pointer → mouse, left-hand palette for build/formations/tactics menus.
4. **Tier 4 — Stretch:** engine-level hooks (tabletop scale, grab-and-rotate world, 3D move orders from controller position).

## Current status

**Phase 0 (spikes) — done, GO (2026-10-14).** Brendo chose to skip Phase 1 and go straight to Tier 2.
**Tier 2 (stereo MVP) — v0.4.0; headset tests 1-2 done (stereo + head tracking work; HUD split into 2D overlay + stereo 3D overlay in 0.4.0, see FINDINGS "Tier 2 headset tests").** AFR stereo (`src/vr/vr_frame`), per-eye view + projection (`src/stereo`), HUD lifted from its FBO onto an OpenVR overlay fixed in game-camera space (`src/hud`), OpenVR loaded at runtime from `third_party/openvr` (v2.15.6, win32). Ctrl+Shift+F8 toggles VR. **No multiplayer guard yet** — VR is opt-in per session until there is one.
The proxy skeleton exists: `src/proxy` (366 forwarded exports + `wglGetProcAddress`/`wglSwapBuffers` hooks), `src/stereo/view_override` (spike: Ctrl+Shift+F9 cycles a fixed camera yaw/offset). Build + test with `tools/smoke-test.ps1` (no game needed), deploy with `tools/install.ps1` / `tools/uninstall.ps1`.
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
       └─ input:   OpenVR actions → pointer raycast → SendInput mouse; palette overlay on left hand
```

Suggested layout:
```
src/proxy/      # DllMain, export forwarding, GL hooks
src/vr/         # OpenVR init, pose, submit, actions
src/stereo/     # matrix capture/override, AFR/sync logic
src/hud/        # 2D-pass detection, FBO, quad
src/input/      # raycast, mouse injection, palette
bindings/       # SteamVR action manifest + default bindings (Frame, OpenXR generic, Touch)
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
- **Single-player and skirmish only.** Refuse to load (or disable VR) in multiplayer — don't risk online play with an injected DLL.
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
