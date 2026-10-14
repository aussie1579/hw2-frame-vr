# Findings (RE notes)

Record every finding with the game build it was found on.

## Build info
*Found 2026-10-14 on Steam build 12653478 (fresh reinstall).*

- Steam app 244160 "Homeworld Remastered Collection", **Steam build ID 12653478**, install `G:\Steam\steamapps\common\Homeworld\HomeworldRM\Bin\Release\`
- HomeworldRM.exe: 6,381,056 bytes, SHA-256 `A9C63778C4F3DB33D06AFFA7C23742C1AF077C0F376C4690F800C9D1DFBE77F7`
  - No VERSIONINFO resource. In-game version string (HwRM.log): **Version 2.303, Build Number / Changelist 3409635** (the string `-3409635` is embedded in the exe)
  - PE timestamp 2023-11-09, linker 14.16 (VS2017), image base 0x00400000, Dynamic base + NX
- **Bitness: 32-bit (x86, machine 0x14C).** All DLLs in `Bin\Release` are x86 too. → proxy, MinHook, OpenVR all built Win32 (`x86-windows`). SteamVR still ships 32-bit `bin\vrclient.dll`, `bin\win32\openvr_api.dll` and a 32-bit OpenXR runtime (`steamxr_win32.json`), so in-process OpenVR is viable.
- **NOT Large Address Aware** (COFF characteristics 0x102) → 2 GB user address space. The proxy must be memory-frugal (eye textures, HUD FBO, OpenVR client all live in this space). Decision 2026-10-14: an **opt-in** LAA patch in the install script (exe backed up, restored on uninstall; proxy warns if a Steam update reverts it). See CLAUDE.md → Conventions.
- Imports `OPENGL32.dll` directly (static import) → an `opengl32.dll` proxy in `Bin\Release` will be picked up (opengl32 is not a KnownDLL).
- Other imports of note: `steam_api.dll`, `bifrost.dll` (Gearbox online services — relevant to the multiplayer guard), `VideoCodecs.dll`, `DSOUND`, `WINHTTP`, `WS2_32`.
- GL context: **OpenGL 3.3 *compatibility* profile** (apitrace, 2026-10-14). Sequence: `wglChoosePixelFormat` (RGBA32, depth 32, double-buffered) → `wglCreateContext` (temp legacy context, call 2) → `wglCreateContextAttribsARB({MAJOR 3, MINOR 3, PROFILE_MASK = COMPATIBILITY_PROFILE_BIT}, share = NULL)` (call 2305) → `wglMakeCurrent`. Then `wglSwapIntervalEXT(0)` (**vsync off**). Driver reports 4.6.0 NVIDIA 610.62.
  - The game's own per-frame rendering uses **no legacy GL** (0 fixed-function calls in a gameplay frame); only the Steam overlay does. So RenderDoc may work despite the compat profile if the Steam overlay is disabled — untested, not needed so far.
- **No SteamStub/DRM wrapper** (sections: .text .rdata .data .rsrc .reloc — no `.bind`), so static analysis works on the shipped exe. Ghidra project: `re/ghidra/HWRM.gpr` (local, gitignored), auto-analysis 210 s on Ghidra 11.3.2.
- **ASLR relocates the exe**: Ghidra uses the preferred base 0x00400000; live process 2026-10-14 had it at **0x00A80000**. Record addresses as `HomeworldRM.exe+RVA` (or AOB), never absolute.
- Other modules in the live process worth knowing about: `gameoverlayrenderer.dll` (Steam overlay, also hooks GL swap — our proxy must coexist), `AcLayers.DLL` (a Windows app-compat shim layer is applied to HomeworldRM.exe), `GLU32.dll`, `nvoglv32.dll` (NVIDIA 32-bit GL ICD).
- Known command-line flags seen in logs/exe: `-workshopmod <big>`, `-forceResolution`, `-w`, `-h`, `-noMovies`, `-superZoom`, `-bgNoSim`, `-useRandomSeed`, `-ss*` (screenshot), `-produceHODMemoryInfo`, `-dpsim`/`-dp8sim`.

## Dormant VR code check
*2026-10-14, Steam build 12653478.*

- Files scanned: `HomeworldRM.exe` and every DLL in `Bin\Release` (bifrost, DCDefault, DCWindow, HW2Box, seDXAudio, seFDAudio, steam_api, VideoCodecs). ASCII + UTF-16 strings; case-insensitive.
- Strings/imports searched: `openvr`, `openvr_api`, `IVRSystem`, `IVRCompositor`, `IVRInput`, `VR_Init`, `VR_IsHmdPresent`, `vrclient`, `steamvr`, `oculus`, `LibOVR`, `ovr_*`, `vive`, `openxr`, `xrCreate*`, `headset`, `hmd`, `stereo`, `vrmode`, `enablevr`, `\bvr\b`, `spectat*`, `eyeoffset`, `ipd`. Import tables checked with `dumpbin /dependents`.
- Results: **Negative.** No OpenVR/OpenXR/LibOVR imports or strings anywhere.
  - `ovr_dots`, `ovr_orders`, `ovr_orders_fill`, `ovr_rgb`, `ovr_rgblines`, `ovr_sensorarcs`, `ovr_tex`, `ovr_texrgb` in the exe are **overlay** material/shader names (sensors manager, move-order lines) — not Oculus. *Useful later:* these name the HUD/overlay passes we need to separate.
  - `WGL_3DL_stereo_control` / `wglSetStereoEmitterState3DL` are entries in the GL extension-loader name table, not a stereo path.
  - `bifrost.dll`: `bifrost_account_kHardware_Oculus` / `kService_Oculus` — Gearbox online-account platform enum only.
  - `steam_api.dll`: `ISteamUtils_IsVRHeadsetStreamingEnabled` — generic Steamworks API, not called-for evidence.
- Conclusion: the 2016 VR-spectator test code did not survive into the 2023 binary (at string level). Optional follow-up: Ghidra pass over the command-line parser for undocumented flags. Not blocking.

## Render passes
*apitrace 14.0 (win32) trace, 2026-10-14, Steam build 12653478, vanilla skirmish at 3440×1440. Trace (3.8 GB, 217,627 frames, 169 M calls) kept locally in `re/traces/` (gitignored). Menus are ~327/609 calls/frame; gameplay is the last ~10.6k frames (2.3k–5.9k calls/frame). Analysed frame index 215000 (calls 160483853–160488552, 4,675 calls, 395 `glDrawElements`).*

Steam overlay note: `gameoverlayrenderer.dll` hooks below opengl32, so its drawing is recorded just **after** each `wglSwapBuffers` (146 calls bracketed by `glPushAttrib(GL_ALL_ATTRIB_BITS)`…`glPopAttrib`, `glOrtho(0,3440,1440,0)`, `glBegin`). Ignore that block when analysing.

| Pass | FBO / viewport | Draws | Programs | Matrices | What it is |
|---|---|---|---|---|---|
| P01 | fbo 1, 3440×1440, 2 clears | 12 | 10, 12, 17 | `inMatC` (view rotation only), `inMatP` = separate narrow perspective (≈55° vfov, near ≈2, far ≈2000) | Background / skybox |
| P02 | fbo 36, **4096×4096** | 50 | 36 | `inMatM` per draw + `inMatFL`/`inMatKL` (key/fill light); **no camera** | Light/shadow-type pass — leave untouched |
| P03 | fbo 1, 3440×1440 | 52 | 41, 38, 34, 26, 24, 40, 67 | `inMatV` or `inMatC` = camera view, `inMatP` = main projection, `inMatM`/`inMatMV` per object | **Main 3D scene** |
| P04–P08 | fbo 2 ↔ fbo 1 | 2 + 3 blits | 62, 60 | fullscreen | Resolve / scene post |
| P09–P22 | fbos 5,6,10–13,18–20,30–32; 3440×1440 → 860×360 → 215×90 → 53×22 and back up | 1 each | 85–94 | fullscreen quad matrices | Bloom/glow down/up-sample chain |
| P23–P24 | fbo 34 | 1 + blit | 46 | `inMatV`-style pair | Composite |
| P25 | **fbo 35**, 3440×1440, cleared | 262 | 70, 54, 4, 68, 8, 5, 6, 3 | mostly ortho `inMatP = [2 0 0 0 | 0 2 0 0 | 0 0 -1 0 | -1 -1 0 1]` (maps [0,1]² → NDC) + identity `inMatMV`; **prog 70 uses the 3D camera** (`inMatC` view, `inMatP` perspective, `inMatMV` = world with scale 342) | **HUD/UI layer in its own FBO**; prog 70 = in-world overlay (likely the `ovr_*` sensor/order/bracket draws) |
| P26–P30 | fbos 12,13,10,3,4 | 1 each | 86, 91, 88, 89, 92 | fullscreen | Final post |
| P31 | **fbo 0** (back buffer), 3440×1440 | 3 | 53, 8 | ortho/identity | Final composite (scene + HUD) to screen |

GL usage pattern: 93 linked programs; VAOs everywhere; per-draw `glBufferData(GL_STREAM_DRAW)` for UI; **no UBOs** (no `glBindBufferBase/Range` in gameplay); 200 `glUniformMatrix4fv` per frame, `transpose = GL_FALSE` (column-major). `glBindProgramPipeline` appears twice per frame.

## Camera
- **Upload site:** `glUniformMatrix4fv`, every frame, re-uploaded to each program that needs it (not shared via UBO). Camera and projection are **separate uniforms**, never pre-multiplied on the CPU. **Exception:** some effects (the ship light glows) are transformed into view space **on the CPU** and drawn with only `inMatP` — see group C below.
- **Uniform naming** (from `glGetUniformLocation` at link time): `inMatV`, `inMatC`, `inMatP`, `inMatM`, `inMatMV`, `inMatFL`/`inMatKL` (fill / key light — never touch), plus `vec4 inCamera` (camera world position) in programs 58, 60, 78.
- **Where the camera enters — all 93 programs classified** (vertex shaders read from the trace; note 82 of the 274 `glShaderSource` calls use apitrace's `string = &"..."` form, which an earlier pass missed):

  | Group | Programs | Shader form | Camera override |
  |---|---|---|---|
  | A | 56 | `P * V * M` or `P * C * MV` (`inMatMV` = model→world here) | `inMatV`/`inMatC` → `E·V` |
  | B | 31 | `P * MV` only (`PVM = inMatP*inMatMV`, 28 programs) | `inMatMV` → `E·MV` **only if that program's `inMatP` is perspective**; with an ortho `inMatP` it is 2D HUD/post → untouched |
  | C | 4 (9, 54, 55, 62) | `P * pos` only — vertices already in view space | `inMatP` → `P·E` if perspective (progs 62, 9); ortho → 2D HUD (54, 55) → untouched |
  | D | 2 (36, 37) | light/shadow space (`inMatM`, `inMatFL`, `inMatKL`) | untouched |

  - **Prog 62 = light glows/flares**, one draw right after the main scene (pass P05): `screenCen = inMatP*inCen; screenCorner = inMatP*inPos` with CPU-computed view-space positions, sized in screen space via `inWindow`. Missed by the first spike → lights rendered where the *original* camera saw them.
  - 28 group-A programs reconstruct the eye position with `inverse(inMatV)` (reflections), so they follow the override automatically. `inCamera` (58, 60, 78: billboard "pull towards camera") does **not** — Tier 2 must also override it per eye (only matters for translation, not yaw).
- Locations differ per program (e.g. `inMatV` is loc 118 in prog 41, loc 117 in prog 38, loc 1 in prog 40), so the proxy maps location→name per program by hooking `glGetUniformLocation`. Program IDs are runtime-assigned — key on names, never on IDs/locations. No program is ever relinked.
  - Locations differ per program (e.g. `inMatV` is loc 118 in prog 41, loc 117 in prog 38, loc 1 in prog 40), so the proxy must map location→name per program by hooking `glGetUniformLocation` (or querying `glGetActiveUniform` after link). Program IDs are runtime-assigned — key on names, never on IDs/locations.
- **Main projection** (frame 215000): `[0.5978 0 0 0 | 0 1.428 0 0 | 0 0 -1 -1 | 0 0 -6 0]` → **infinite far plane, near = 3**, aspect 2.389 (= 3440/1440), vfov ≈ 70°, hfov ≈ 118°. World coordinates are O(10⁴) units (camera at ≈ (-2479, -6475, 6649) in view-translation terms).
- **Skybox** uses its own projection (`[0.8041 0 0 0 | 0 1.921 0 0 | 0 0 -1.001 -1 | 0 0 -4.002 0]`) and a rotation-only `inMatC` (prog 17 adds a tiny parallax translation). Per eye it needs head *rotation* only; its own FOV must be swapped for the eye FOV too.
- **Override plan this implies (Tier 2):** per eye, apply groups A–C above with `E = eye × head pose` (rotation-only `E` when the uploaded view has no translation, i.e. sky); replace every perspective `inMatP` (m[11] == −1) with the per-eye asymmetric projection (keep infinite far, near 3 × world-scale) — for group C that becomes `P_eye·E`; set `inCamera` to the eye position. Leave ortho `inMatP`, `inMatM`, `inMatFL`/`inMatKL` alone.
- **Mouse picking uses the game's own (un-overridden) camera** — confirmed in the spike (selection offset by exactly the override). Tier 3 pointer→mouse must therefore project the controller ray with the game's *original* camera, which the proxy sees on every upload.
- Camera struct (CE/Ghidra): not needed yet.

## View-override spike (Phase 0 item 5)
- **Run 1** (2026-10-14, proxy 0.1.0, yaw mode only — the log shows a single mode change): scene "mostly okay" and moved as one; HUD unaffected; **ship light glows offset beside their ships** (group C, prog 62, not yet handled); ship selection offset by the same amount (expected, see picking above). One frame overrode `inMatV` ×5, `inMatC` ×3, `inMatMV`-as-view ×2.
- Fix: group C handling (`P·E`) + on-screen mode square (top-left: red yaw, green offset, blue both). Covered by `tools/smoke-test.ps1`.
- **Run 2** (2026-10-14, proxy 0.1.0 + fix): all four modes cycled three times (hotkey confirmed in log; light glows now overridden, `view-space inMatP x1` per frame). Main view "relatively fine" in every mode — scene, glows, background move as one. **Sensors Manager looked offset** (reported in yaw+offset).
  - Cause: SM 3D content is overridden (disc/rings/blobs/bearing numbers are progs 96–100, group A/B via `inMatC`/`inMatMV`, linked at skirmish load ~call 131.59 M). What stays put is **2D overlay the game positions on the CPU with its own camera**, drawn into the HUD FBO with the ortho `inMatP` — e.g. prog 54 draws the SM focus marker (diamond + box, shadowed outline via `inOfs` ±1 px) at normalized screen (0.49–0.52, 0.49–0.52), i.e. where the *game* camera looks. Same class as mouse picking. Expected in a flat-screen test (only the 3D layer was rotated); red should show it too, green hardly (150 units is tiny at SM zoom).
  - **VR implication:** don't head-lock the HUD layer. Draw the HUD FBO as a quad fixed in *game-camera space*, rendered into each eye with the same head transform as the scene; then CPU-positioned markers stay on their ships under head rotation and pointer→quad→mouse picking stays consistent. Residual mismatch only from eye/head translation (marker at quad depth vs object depth).

## Phase 0 result
**GO — final** (2026-10-14, build 12653478). The camera is identifiable and overridable in-game: all 3D (scene, sky, glows, SM 3D) follows a proxy-side camera transform via `inMatV`/`inMatC`/`inMatMV`/view-space `inMatP` rules, with no visual breakage in the main view. HUD/2D is distinguishable (own FBO, ortho `inMatP`). Known work for Tier 2: game-camera-locked HUD quad (above), `inCamera` per eye, skybox projection, per-eye post/bloom, picking through the HUD quad.

## Go / no-go
- Decision: **GO** (2026-10-14, build 12653478) — confirmed by the in-game view-override spike, see "Phase 0 result" below.
- Reasoning: both go criteria from the plan are met on paper. (1) The camera matrix is identifiable and overridable: separate `inMatV`/`inMatC` + `inMatP` uniforms uploaded via `glUniformMatrix4fv` every frame, named consistently, never baked into vertices or pre-multiplied. (2) HUD draws are distinguishable: the HUD renders into its own FBO (fbo 35) with ortho matrices and is composited to the back buffer at the very end. Risks found: the HUD layer also contains in-world 3D overlay draws (prog 70) that need the per-eye camera; the skybox uses a separate projection; the post chain (bloom) will need per-eye handling or disabling; vsync is forced off.

## Tier 2 headset tests
- **Test 1** (2026-10-14, proxy 0.2.0, HMD reported by SteamVR as "Oculus Quest2", 144 Hz, 2644x2644 recommended, IPD 66.4 mm; eye tangents L -1.665/R 1.212/D -1.731/U 1.169 (left), mirrored right): VR started via the hotkey, ran 2 min with no Submit errors; HUD FBO 35 identified and captured. **Stereo + head-tracked camera in the 3D scene "worked well"; HUD panel fixed in space.** Problems: ship selection circles and the mouse cursor very offset.
  - Circles: drawn in the HUD layer (prog 70, `GL_LINES` x512, in-world rings) so they live on the overlay. VR had recentred on the pose at hotkey time (headset not on the head; F10 never pressed), and the panel is 3 m away while the world-scaled ships are much farther, so viewing from anywhere but the recentre point slides markers off their ships.
  - Cursor: the game draws it (prog 8 quads, textures 152/166) straight into the back buffer *after* the composite, so it landed in the eye images, head-locked.
  - Fixes in 0.3.0: auto-recentre when the headset is put on (`GetTrackedDeviceActivityLevel` → `UserInteraction`); HUD panel follows head position (orientation stays with the game camera); `glDrawArrays`/`glDrawElements` hooked so UI programs (those that drew in the HUD FBO this frame) drawing into fbo 0 after the capture are redirected onto the overlay with coverage alpha. Covered by the smoke test's HUD-pipeline run.
- **Test 2** (proxy 0.3.0): markers and circles now sit over their ships, but HUD-layer elements read as **too close** — "icons floating close to the camera, not at the ship". Cause: the HUD FBO mixes 2D UI (ortho `inMatP`) with world-anchored 3D overlay (perspective `inMatP`: selection circles prog 70 `GL_LINES`, `tryV` icons with pixel offsets progs 68/70/96, SM disc/blobs/numbers 97–100), and 0.3.0 put all of it on the flat panel at 3 m.
  - Fix in 0.4.0: split the HUD layer per draw. Ortho draws in the HUD FBO are redirected into the overlay texture (cleared when the game first binds the HUD FBO each frame; game colour blend kept, alpha → coverage). Perspective draws stay in the game's HUD FBO and now get the per-eye camera, so the game's composite puts them in each eye image at their ships' depth. The copy/convert/clear capture step is gone. Smoke test checks all three cases (2D → overlay, 3D → eyes, cursor → overlay).
  - Still on the panel by design: 2D markers the game positions on the CPU with an ortho projection (e.g. the SM focus marker, prog 54) — those can only be fixed by reprojection.
