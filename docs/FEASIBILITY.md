# Homeworld 2 × Steam Frame VR — Feasibility Study

*2026-10-14*

## Verdict

**Feasible as a hobby project, but in tiers.** A head-tracked, stereoscopic Homeworld 2 Remastered you can sit inside, with the HUD on a floating panel and a laser-pointer mouse, is a realistic multi-weekend-to-few-month project. A *native-feeling* VR RTS (grab and rotate the battlefield, draw move orders in 3D with your hands) is a much bigger reverse-engineering job on a closed-source engine and should be treated as a stretch goal, not the plan.

| Tier | What you get | Difficulty | Confidence |
|---|---|---|---|
| 0 — Theater mode | Flat game on a big virtual screen, Frame controllers via Steam Input | Trivial | High |
| 1 — Injector stereo | 3D depth, head-look, no controller rework | Low–Medium | Medium |
| 2 — Custom OpenGL proxy | Real stereo + 6DoF head camera, HUD on a quad, pointer-as-mouse | Medium–High | Medium |
| 3 — Engine-level mod | Tabletop scale, hand-driven camera, 3D move orders | High | Low–Medium |

**Recommendation:** run the Phase 0 spikes below (one or two sessions) before committing. Two of them could dramatically change the cost — especially the dormant-VR-code check.

## Target platform: what the Steam Frame actually needs

- **Run it streamed from the PC, not standalone.** The Frame can run standalone on its ARM chip, but it is designed primarily for low-latency streaming of the Steam library from a PC, and it stays fully compatible with SteamVR and OpenXR. A Windows x86 OpenGL game plus an injected DLL is a PC-side job; the headset just receives the stream.
- **Display budget:** 2160×2160 per eye, 72–144 Hz, ~110° FOV, pancake lenses. Rendering HWRM twice per frame at that resolution is trivial for a modern GPU; the bottleneck will be the engine's CPU side (see Risks).
- **Foveated streaming** is handled by Valve's stream, not by the game, so the mod gets it for free.
- **Controllers:** SteamVR uses a Frame binding if present, otherwise falls back to an OpenXR generic or Oculus Touch binding and remaps it. For non-VR (theater) play, the controllers act as a full gamepad through Steam Input. A mod targeting OpenVR/OpenXR actions doesn't need Frame-specific code.

## Which Homeworld 2 to target

**Target Homeworld 2 Remastered (HomeworldRM.exe), not HW2 Classic.**

- HWRM renders through **OpenGL** (requires GL 3.3; ReShade has historically been loaded into it as `opengl32.dll` from `HomeworldRM\bin\Release`). HW2 Classic is also OpenGL but older fixed-function-era code with more driver quirks.
- HWRM has the live modding ecosystem: Lua-driven data (`.big` archives, ships, UI, SCAR mission scripts), Steam Workshop, and the Karos Graveyard wiki, which consolidates the old HW2 docs.
- Lua modding **cannot** do stereo rendering or head tracking — it has no access to the renderer. It *is* useful for supporting tweaks: UI scale, HUD layout, camera limits.
- **To verify in Phase 0:** whether the current HomeworldRM.exe is 32- or 64-bit (affects hooking toolchain and which OpenVR DLL you ship).

### Interesting lead: Gearbox once tested VR

In August 2016 the HWRM store page briefly listed HTC Vive support, and SteamDB's update history at the time reportedly showed builds testing a **VR map/skirmish spectator mode**. It never shipped (the game only launches in theater mode), but the VR flag reportedly stayed on the store page for years afterwards. If any of that code survived in the shipping binary (OpenVR imports, `IVRSystem`/`IVRCompositor` strings, a hidden command-line flag or Lua hook), it could hand you a stereo render path for free. **This is the single cheapest high-value check in the whole study.**

## Approaches

### A. Theater mode + Steam Input (baseline)
Zero code. Launch through SteamVR, play on a curved virtual screen, bind Frame controllers to mouse/keys. Worth doing first anyway: it gives you the input-binding layer every later tier reuses, and tells you how much the *flat* experience already satisfies.

### B. Off-the-shelf injectors
- **vorpX** targets DX9–11 primarily, with only partial OpenGL support, so HWRM is a gamble. Even when it works you get geometry/Z-buffer 3D and head-look, no real controller integration.
- **ReShade** loads into HWRM and could provide a depth buffer for a Z-buffer-3D effect, but that's "fake" stereo with artifacts on the HUD and particles.
- **Not available:** UEVR (Unreal-only). Homeworld 3 is UE4, so UEVR is the zero-effort route if the goal is simply "Homeworld in VR."

### C. Custom OpenGL proxy DLL (recommended main path)
Ship an `opengl32.dll` proxy (or a Detours/MinHook hook on `wglSwapBuffers` / `glDraw*`) that:

1. **Finds the camera.** Use RenderDoc or apitrace on HWRM to identify where the view/projection matrices are uploaded (uniform buffer or `glUniformMatrix4fv`). Alternatively locate the camera struct in memory with Cheat Engine/Ghidra and feed it directly.
2. **Renders stereo.** Start with **alternating-frame rendering** (offset the camera per eye on alternating frames — easiest, halves effective frame rate per eye) and graduate to **synchronized sequential** (render the scene twice per frame by replaying the draw list or calling the engine's scene-render function twice).
3. **Applies head tracking.** Compose HMD pose onto the game camera. Homeworld's orbit camera maps well to a "commander floating in space" feel.
4. **Separates the HUD.** Detect orthographic-projection / 2D passes (UI, sensors overlay, cursor) and redirect them into an off-screen texture shown on a world-locked or hand-locked quad.
5. **Submits frames** to SteamVR via OpenVR (`IVRCompositor::Submit` with GL textures) or OpenXR (`XR_KHR_opengl_enable`). Both accept GL textures natively.
6. **Pointer-as-mouse.** Raycast the right controller onto the HUD quad and the game viewport, convert to window coordinates, and inject mouse events — same pattern as the Factorio palette and SPAZ 2 pointer work.

### D. Engine-level mod (stretch)
Reverse HomeworldRM.exe to call the scene renderer per eye with proper projection matrices, change world scale (tabletop mode, as Vast Reaches does), and drive unit commands directly (3D move widget from controller position instead of the two-step mouse move). Every interaction feature means finding and hooking another internal function.

### Comparison

| | Theater | Injector | GL proxy | Engine mod |
|---|---|---|---|---|
| True stereo | No | Partial | Yes | Yes |
| 6DoF head camera | No | Yes | Yes | Yes |
| HUD usable | Yes | Often broken | Yes (quad) | Yes |
| Motion controls | Gamepad only | No | Pointer → mouse | Native-feel |
| RE required | None | None | Moderate | Heavy |
| Survives game patches | Yes | Mostly | Mostly (API-level) | Fragile |
| Rough effort | Hours | Hours–days | Weeks | Months |

## Key risks

- **Camera extraction.** The whole proxy approach hinges on reliably finding and overriding the view matrix without breaking shadow, glow, and post-process passes (each has its own matrices). This is the #1 technical risk; Phase 0 tests it directly.
- **Scale.** Homeworld battles span enormous distances. At 1:1 scale, stereo disparity on ships even a short way off is effectively zero and everything looks flat. You'll want an adjustable world-scale / IPD multiplier, which also enables a tabletop view. Huge near/far plane ratios may cause z-fighting once you change projection.
- **Post-processing.** Bloom, motion blur and screen-space effects tuned for one view may misalign between eyes. Plan to disable most of them in the VR profile.
- **CPU-bound engine.** The HW2 lineage historically leans on a single core; large battles may still drop frames. Synchronized stereo doubles GPU work, not simulation work.
- **Input fidelity.** HW2's move orders use a two-stage mouse gesture for vertical placement. Pointer-as-mouse will work but feel indirect until Tier 3.
- **Multiplayer.** Keep the mod to campaign and skirmish. Injected DLLs and online play are a bad mix.
- **Comfort.** Game-driven camera rotations (cinematics, focus jumps) can cause motion sickness; consider snapping or fading camera jumps.

## Phased plan

### Phase 0 — Spikes (1–2 sessions, go/no-go)
1. Check HomeworldRM.exe bitness and GL context type (core vs compatibility profile).
2. Scan the binary for leftover VR code: OpenVR imports/strings, VR-related command-line flags, Lua functions. If found, chase it first.
3. Capture a frame in RenderDoc: identify the main scene pass, the matrix upload, and the HUD/sensors passes.
4. Try overriding the view matrix in one pass and confirm the scene shifts cleanly.

**Go if:** the camera matrix is identifiable and overridable, and HUD draws are distinguishable. **No-go / rethink if:** matrices are baked per-object on the CPU or HUD and scene are interleaved unpredictably.

### Phase 1 — Baseline (hours)
Theater mode plus a tuned SteamVR/Steam Input binding for Frame controllers. Doubles as the fallback product.

### Phase 2 — Stereo MVP (weeks)
GL proxy with alternating-frame stereo, HMD-driven camera, HUD on a quad, OpenVR submit.

### Phase 3 — Usable VR (weeks)
Synchronized stereo, world-scale slider, pointer-as-mouse, left-hand palette for build/formation/tactics menus (reuse the Factorio palette design).

### Phase 4 — Stretch (open-ended)
Engine-level hooks for grab-and-rotate world, tabletop mode, controller-driven 3D move orders.

## Alternatives to weigh

- **Homeworld: Vast Reaches** — a purpose-built Homeworld VR RTS (FarBridge, on SteamVR since late 2025, OpenXR), set between HW1 and HW2. Also a good reference for interaction design (wrist command UI, tabletop scaling).
- **Homeworld 3 + UEVR** — UE4 game, so UEVR should give stereo and 6DoF head tracking with no custom code.
- Neither gives you *HW2's campaign and mods* in VR, which is the real justification for this project.

## Tooling

- RenderDoc / apitrace — OpenGL frame capture and matrix hunting
- Ghidra + Cheat Engine — binary analysis, camera struct, render loop
- Detours or MinHook — function hooking
- OpenVR SDK (simplest with GL) or OpenXR SDK — compositor submit and controller actions
- Karos Graveyard wiki and the Homeworld Discord #hwrm-modding — Lua/UI side

## Sources

- Steamworks: Steam Frame — https://partner.steamgames.com/doc/steamhardware/steamframe
- Steamworks: Steam Frame Controllers — https://partner.steamgames.com/doc/steamhardware/steamframe/controllers
- ReShade forum, HWRM log showing OPENGL32.dll — https://reshade.me/forum/troubleshooting/265-homeworld-remastered-mostly-blacked-out
- Steam discussion, HWRM VR listing (2016) — https://steamcommunity.com/app/244160/discussions/0/350540780274680119
- Homeworld: Vast Reaches — https://store.steampowered.com/app/4014160/
- Karos Graveyard wiki — https://github.com/hwrm/KarosGraveyard/wiki
- UEVR — https://github.com/praydog/UEVR
