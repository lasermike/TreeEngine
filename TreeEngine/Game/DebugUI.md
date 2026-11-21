# Debug UI vector controls

Settings are ordered Common, DXR, Raster. Inactive renderer options remain visible.

Vector rows provide draggable components, Ctrl-click/double-click text entry, and
Copy. Clipboard text includes the field name, object name where applicable, and
degrees or XYZW labels for rotations. Bounds and packed material diagnostics are
read-only and copyable.

Tools opens optional ImViewGuizmo controls. Camera position orbits a forward focus
point (initial distance 10 world units), with editable orbit distance, dolly, and
pan. Camera orientation rotates in place. Object movement uses camera-relative
pan/dolly; object rotation changes orientation around its origin. Light position
continues to aim toward scene center; direction edits preserve its distance.

Rotation rows use pitch/yaw/roll in degrees, with normalized quaternion editing in
the popup. Scale rows have optional proportional Link and a uniform-scale drag
area. Zero components must be unlinked before proportional edits. Zero object
scales are rejected; negative scales are retained.

UV edits apply when the drag or text edit finishes. Editing an instance's UV scale
makes that object independent, preserving the source and other instances. glTF
objects now support the existing optional `textureCoordScale` property (default
`[1,1]`) for persistence; old files require no migration.

Source-tree transforms now honor scale and rotate around their own origin. Scenes
with previously ignored source-tree scales can therefore look different, including
the fixed tree with scale 0.6 in the glTF demo.
