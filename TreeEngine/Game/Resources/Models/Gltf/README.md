# Khronos material test models

The four test models below were downloaded from the Khronos glTF Sample Assets
repository on 2026-10-20. Their matching scenes are in `../../Scenes/`.

| File | Source | Credit and license |
| --- | --- | --- |
| `MetalRoughSpheres.glb` | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MetalRoughSpheres | © 2017 Analytical Graphics, Inc.; Ed Mackey. CC BY 4.0. |
| `MetalRoughSpheresNoTextures.glb` | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MetalRoughSpheresNoTextures | © 2019 Public. CC0 1.0. |
| `SpecularTest.glb` | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularTest | © 2021 Analytical Graphics, Inc.; Ed Mackey. CC BY 4.0. |
| `NormalTangentMirrorTest.glb` | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/NormalTangentMirrorTest | © 2018 Analytical Graphics, Inc.; Ed Mackey. CC BY 4.0. |

`MetalRoughSpheres.glb` is adapted from the Khronos binary: its sampler's
`minFilter` was changed from `NEAREST_MIPMAP_LINEAR` to `LINEAR_MIPMAP_LINEAR`
because TreeEngine does not support nearest texel filtering.

`NormalTangentMirrorTest.glb` is adapted from the Khronos binary: its material's
`occlusionTexture` reference was removed because TreeEngine does not import
occlusion textures. The normal map, tangent data, and other material fields are
unchanged. The adaptation affects ambient shading, so compare normal directions
and highlight shapes rather than exact brightness against the source preview.

CC BY 4.0: https://creativecommons.org/licenses/by/4.0/
CC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/
