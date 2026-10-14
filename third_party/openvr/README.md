# OpenVR SDK (vendored subset)

- Version: **v2.15.6** (ValveSoftware/openvr, tag published 2026-03-27, commit `41bc382`)
- Source archive: `gh api repos/ValveSoftware/openvr/zipball/v2.15.6`, SHA-256 `5f1af1f1d797efd659b1a12d5f359bc6a6210a41f2482ee3efc95ff3b6aac3ef`
- Files taken: `headers/openvr.h` → `include/`, `bin/win32/openvr_api.dll` → `bin/win32/` (x86, matches the 32-bit game), `LICENSE` (BSD-3-Clause).

Only the header is used at build time. The proxy loads `openvr_api.dll` from its own folder at runtime
(`src/vr/openvr_session.cpp`) and replicates `vr::VR_Init`, so no import library is linked and a missing DLL just
means "no VR".
