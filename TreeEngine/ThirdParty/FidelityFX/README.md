# AMD FidelityFX SDK (FSR upscaling)

A subset of the [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), **v2.4.0**
(released October 28, 2026), for the FSR upscaler backend (`Game/UpscalerFsr.cpp`; see `Game/UPSCALING.md`).
The folders mirror the SDK's `Kits/FidelityFX`, because the headers include each other by relative path.

| Path | From the SDK | What it is |
|---|---|---|
| `api/include/ffx_api.h`, `ffx_api_types.h`, `ffx_api_loader.h` | `Kits/FidelityFX/api/include/` | The FidelityFX API: five functions, descriptor structs, and a loader helper for run-time loading |
| `api/include/dx12/ffx_api_dx12.h` | `Kits/FidelityFX/api/include/dx12/` | The DX12 backend descriptor and `ffxApiGetResourceDX12` |
| `upscalers/include/ffx_upscale.h` | `Kits/FidelityFX/upscalers/include/` | The upscaler's create, dispatch and query descriptors |
| `signedbin/amd_fidelityfx_loader_dx12.dll` | `Kits/FidelityFX/signedbin/` | The loader (2.3.0.4555), which the engine loads with `LoadLibraryW` |
| `signedbin/amd_fidelityfx_upscaler_dx12.dll` | `Kits/FidelityFX/signedbin/` | The upscaler providers (4.1.2.4555): FSR 4.x on supported AMD GPUs, FSR 3.1.x and 2.3.x on all others |
| `license.md` | `Kits/FidelityFX/docs/license.md` | The SDK's licenses; these headers and DLLs are under its permissive (MIT-style) license, whose notice must ship with them |

Both DLLs are signed by Advanced Micro Devices (checked with `Get-AuthenticodeSignature`) and stored in Git LFS.
The `TreeClassic` build copies them next to the executable, where the loader finds the upscaler DLL. Without
them FSR is reported as unavailable and the other anti-aliasing modes are unaffected.

## Updating

1. Download the release archive (`https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/<tag>.zip`)
   into an empty directory outside the repository.
2. Copy the files in the table from `Kits/FidelityFX`. Keep the folder layout.
3. Check the signatures: `Get-AuthenticodeSignature signedbin\*.dll` must report `Valid`, signed by Advanced Micro
   Devices.
4. Update the version numbers here. If `FFX_UPSCALER_VERSION` in `ffx_upscale.h` changed, check the release notes for
   descriptor changes before rebuilding.
