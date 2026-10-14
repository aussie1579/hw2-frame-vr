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
- GL context (version, core/compat): **TBD at runtime.** Exe contains `WGL_ARB_create_context`, `WGL_ARB_create_context_profile`, `wglCreateContextAttribsARB`, so it can request a profile; which one it asks for is unknown. HwRM.log from 2026-08-09 (same 2.303/3409635 build) reports `GL Info: 4.6 - 4.6.0 NVIDIA 610.62`, RTX 4070 SUPER — a 4.6 string on NVIDIA doesn't distinguish core vs compat. Verify by tracing `wglCreateContextAttribsARB` attribs (apitrace) — this decides whether RenderDoc can be used (RenderDoc needs core profile).
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

## Render passes (RenderDoc)
| Pass | Draw range | Matrices source | Notes |
|---|---|---|---|

## Camera
- View matrix upload site:
- Camera struct (if found via CE/Ghidra):

## Go / no-go
- Decision:
- Reasoning:
