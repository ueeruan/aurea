The shadow candidate rotates the Vogel unit direction by the original shader's
float32 golden angle. It computes cos/sin of the pixel rotation once and resets
the direction independently for blocker search and PCF. It preserves the radial
sqrt, offset length, taps, texture samplers/LOD, bias, radius and MSAA settings.
There are no indexed constant arrays or new GPU resources.

`test_shadow_rotation.py --glslc <compiler> --output-dir <evidence-directory>`
compiles an isolated shadow call and the complete PBR/ground shaders with both
the original and rotated implementations. The reference macro is a test input;
normal builds use the rotated path. Optional `--essl-tool` also translates both
paths with the real GLES build tool.

The original 8,193-phase float32 coordinate audit across all counts 1–32 measured
maximum unit-disk component error 5.409121513e-6. At radius 0.12 on a 4,096 map,
that is at most 0.002659 texel; fixed PCF radius 1.5 texels has a much smaller
position error. This is a math proxy, not image equivalence: depth thresholds
can amplify coordinate changes. Native baseline/target A/B must compare the
same full RGBA frames with unchanged quality, taps and source assets, including
32-tap PCSS, floor/reflection/contact and first/middle/last frames. A pixel
regression rejects the candidate regardless of its timing improvement.
