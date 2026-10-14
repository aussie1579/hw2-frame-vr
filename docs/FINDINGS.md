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
- **Upload site:** `glUniformMatrix4fv`, every frame, re-uploaded to each program that needs it (not shared via UBO). Camera and projection are **separate uniforms**, never pre-multiplied on the CPU, and model matrices never contain the camera.
- **Uniform naming convention** (from `glGetUniformLocation` at link time, consistent across all 93 programs):
  - `inMatV` / `inMatC` → **camera view** (world → view). Two shader conventions exist: `gl_Position = inMatP * inMatV * inMatM * pos` and `gl_Position = inMatP * inMatC * inMatMV * pos` (here `inMatMV` is really model→world despite the name).
  - `inMatP` → projection (perspective for 3D, ortho for HUD/post).
  - `inMatM` / `inMatMV` → model → world.
  - `inMatFL` / `inMatKL` → fill / key light matrices (must not be touched).
  - `inCamera` exists in 3 programs (not yet examined).
  - Locations differ per program (e.g. `inMatV` is loc 118 in prog 41, loc 117 in prog 38, loc 1 in prog 40), so the proxy must map location→name per program by hooking `glGetUniformLocation` (or querying `glGetActiveUniform` after link). Program IDs are runtime-assigned — key on names, never on IDs/locations.
- **Main projection** (frame 215000): `[0.5978 0 0 0 | 0 1.428 0 0 | 0 0 -1 -1 | 0 0 -6 0]` → **infinite far plane, near = 3**, aspect 2.389 (= 3440/1440), vfov ≈ 70°, hfov ≈ 118°. World coordinates are O(10⁴) units (camera at ≈ (-2479, -6475, 6649) in view-translation terms).
- **Skybox** uses its own projection (`[0.8041 0 0 0 | 0 1.921 0 0 | 0 0 -1.001 -1 | 0 0 -4.002 0]`) and a rotation-only `inMatC` (prog 17 adds a tiny parallax translation). Per eye it needs head *rotation* only; its own FOV must be swapped for the eye FOV too.
- **Override plan this implies:** in the `glUniformMatrix4fv` hook, for uniforms named `inMatV`/`inMatC` substitute `eye × view` (rotation-only eye transform when the uploaded matrix has zero translation, i.e. sky); for `inMatP` that is perspective (m[11] == −1) substitute the per-eye asymmetric projection (keep infinite far, near 3 × world-scale). Leave ortho `inMatP`, `inMatM`/`inMatMV`, `inMatFL`/`inMatKL` alone.
- Camera struct (CE/Ghidra): not needed yet.

## Go / no-go
- Decision: **Provisional GO** (2026-10-14, build 12653478) — final once the view-override spike works.
- Reasoning: both go criteria from the plan are met on paper. (1) The camera matrix is identifiable and overridable: separate `inMatV`/`inMatC` + `inMatP` uniforms uploaded via `glUniformMatrix4fv` every frame, named consistently, never baked into vertices or pre-multiplied. (2) HUD draws are distinguishable: the HUD renders into its own FBO (fbo 35) with ortho matrices and is composited to the back buffer at the very end. Risks found: the HUD layer also contains in-world 3D overlay draws (prog 70) that need the per-eye camera; the skybox uses a separate projection; the post chain (bloom) will need per-eye handling or disabling; vsync is forced off.
