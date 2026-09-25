# Camera and mirrored model render integrity — 2026-09-25

## Camera under a nonuniformly scaled parent

The camera view builder normalized each world-matrix axis independently. A
nonuniform parent scale followed by a child rotation introduces shear, which
individual normalization cannot remove. For parent XY scale (2,1) and camera
roll 45 degrees, the normalized right and up axes have dot product -0.6 instead
of zero. This distorts both scene models and 2D layers placed in 3D space.

The regression renders a 3D rectangular plane through a parented camera with
animated roll, comparing pixels against an independent rigid-camera reference.
It includes forward and reverse seeks and save/reopen. The analytical reference
retains the transformed camera up direction and forward direction, with roll
atan2(2 sin(angle), cos(angle)); projection must remain rigid.

The view now orthonormalizes forward/up, with stable fallback axes when a scale
collapses an axis. The Vulkan baseline reproduced three failed image checks
(mean error 4.3940 at frame 30, 2.2986 at frame 15). After the fix the camera GPU
regression passed all 39 checks, including save/reopen.

## Mirrored model front faces

Negative world determinants reverse triangle winding. The model draw pipeline
now follows that determinant, retaining authored front/back semantics, and the
corresponding pipeline variants are included in prewarming. The Vulkan GPU
regression passed 30 checks covering reflected model scale, parent reflection,
double reflection, back-face culling and save/reopen.

## Morph shadow silhouette

Shadow rendering previously used the original vertex buffer while color used
the deformed morph buffer. Morph preparation now precedes shadow construction;
both passes use the same deformed positions and skin offsets. Per-instance
morph shadow draws stay separate instead of being incorrectly instanced.

Authored glTF fixture pairs compare timeline-evaluated morph shadows against
the same geometry translated conventionally, with a separate check that the
scene actually has visible shadow pixels. A second case moves the caster far
outside its rest bounds; morph bounds are accumulated during deformation and
included in shadow-frustum fitting, without another vertex traversal. The host
Vulkan regression passed 38 checks: 324/306 visible shadow pixels in the normal/
large cases and zero mean image error against both references. Evidence:
`engine/build/host/morph-shadow-test.log`.

## Reflected tangent-space normal maps

The mesh vertex shader now adjusts tangent handedness by the signs of the
model and skin determinants. A dedicated GPU regression compares a transformed
reflection to independently baked reflected geometry with corrected tangent
handedness. Both use an authored constant tilted normal-map texture. The host
Vulkan regression passed 11 checks with zero mean image error. Evidence:
`engine/build/host/mirror-normal-test.log`.

Do not treat this checkpoint as completion of the 3D roadmap or native iOS
validation.

## Additional findings awaiting resolution

- Shadow fitting for large skeletal deformations still needs animated joint
  bounds; the morph-bound fix does not claim to resolve that separate case.
- Alpha-mask model shadow passes do not sample the material's alpha texture.
