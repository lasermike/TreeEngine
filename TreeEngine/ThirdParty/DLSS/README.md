# NVIDIA DLSS SDK (DLSS Super Resolution)

A subset of the [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS), **v310.9.16** (released October 16, 2026), for
the DLSS upscaler backend (`Game/UpscalerDlss.cpp`; see `Game/UPSCALING.md`). The engine uses the NGX API directly;
Streamline is not used.

| Path | From the SDK | What it is |
|---|---|---|
| `include/*.h` | `include/` | The NGX API and its helpers (`nvsdk_ngx.h`, `nvsdk_ngx_defs.h`, `nvsdk_ngx_params.h`, `nvsdk_ngx_helpers.h`, `nvsdk_ngx_helpers_d3d.h`, ...) |
| `lib/x64/nvsdk_ngx_d.lib`, `nvsdk_ngx_d_dbg.lib` | `lib/Windows_x86_64/x64/` | NGX's static libraries for the DLL C runtime (`/MD`; `_dbg` for `/MDd`), linked into RenderPlatform12 |
| `bin/nvngx_dlss.dll` | `lib/Windows_x86_64/rel/` | The DLSS runtime (310.9.16), the **release** build without on-screen debug overlays |
| `LICENSE.txt` | `LICENSE.txt` | The NVIDIA RTX SDKs license with its DLSS supplement |
| `THIRD_PARTY_NOTICES.txt` | `doc/DLSS_Programming_Guide_Release.pdf`, sections 9.5 and 9.6 | Copyright and license texts for third-party code in the DLL |

The DLL is signed by NVIDIA Corporation (checked with `Get-AuthenticodeSignature`); NGX refuses an unsigned or
altered one. It and the libraries are stored in Git LFS. The `TreeClassic` build copies the DLL next to the
executable, where NGX looks for it. Without it, or on a GPU without DLSS, DLSS is reported as unavailable and the
other anti-aliasing modes are unaffected.

## License obligations

This is not open source. The NVIDIA RTX SDKs license (`LICENSE.txt`) allows shipping `nvngx_dlss.dll` in object code
inside an application with material additional functionality, and requires, among other terms:
- notifying NVIDIA before the commercial release of an application that uses DLSS;
- NVIDIA attribution in the application: splash screen, about box and credits, following NVIDIA's guidelines;
- including the texts in `THIRD_PARTY_NOTICES.txt` in the application's public documentation (programming guide
  section 4.2.3);
- not modifying, reverse engineering or redistributing the SDK on its own, and using it only on systems with NVIDIA
  GPUs.

Read `LICENSE.txt` before any release.

## Updating

1. Download the release archive (`https://github.com/NVIDIA/DLSS/archive/refs/tags/<tag>.zip`) into an empty directory
   outside the repository.
2. Copy the files in the table. Use the `rel` DLL, not `dev`: the development DLL draws debug overlays.
3. Check the signature: `Get-AuthenticodeSignature bin\nvngx_dlss.dll` must report `Valid`, signed by NVIDIA
   Corporation.
4. Regenerate `THIRD_PARTY_NOTICES.txt` from the new programming guide's notices sections, and update the versions
   here. The `ai-tree-dlss` regression golds depend on the DLSS version.
