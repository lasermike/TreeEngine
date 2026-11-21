# ImViewGuizmo

Vendored from https://github.com/Ka1serM/ImViewGuizmo at commit
`330a773ba890121eb32ecb68feb40a8a8ad6ac42`. The header and MIT license are unmodified.

`Game/DebugGizmoAdapter.h` supplies DirectXMath operations and column-matrix storage.
`Game/DebugVectorMath.h` converts between the engine's left-handed +Z-forward poses
and the library's right-handed -Z-forward poses. The implementation is included only
by `Game.cpp` through `DebugVectorEditor.h`.

The editor opens only one tools popup at a time because the library has a shared
interaction context. Axis snapping is immediate; animated snapping is disabled to
avoid interpolating antipodal directions through a zero vector.
