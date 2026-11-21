# SMAA

Enhanced Subpixel Morphological Antialiasing reference implementation by Jorge Jimenez et al.,
from https://github.com/iryoku/smaa at commit 71c806a838bdd7d517df19192a20f0c61b3ca29d.
MIT licensed; see `LICENSE.txt`.

Unmodified copies of:
- `SMAA.hlsl` (from the repository root)
- `AreaTex.h` and `SearchTex.h` (from `Textures/`): the precomputed lookup textures as byte arrays.

`Game/Shaders/Smaa.hlsl` includes `SMAA.hlsl` through its `SMAA_CUSTOM_SL` porting macros and runs the
three passes as compute shaders. `RenderPlatform12.cpp` uploads the two lookup textures.
