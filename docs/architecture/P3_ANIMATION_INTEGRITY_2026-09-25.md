# Imported 3D animation integrity — 2026-09-25

This checkpoint changes the shared scene importer and pose evaluator used by
Android and iOS. It does not claim completion of the 3D roadmap or native Metal
validation.

## Invalid animation channels

The pose sampler returned without writing its output for empty/truncated
channels. The caller nevertheless assigned the uninitialized output, or reused
values from the previous channel, into translation, rotation, and scale. Invalid
morph channels also replaced the bind weights with zeros. A non-finite sample
time could reach the end iterator and read past the timestamp array.

Sampling now reports failure before accessing the key data. Failed channels
leave the current pose intact; invalid clip times resolve to the clip start.
Tests cover an animated parent with malformed child channels, forward/backward
seeks, truncated cubic morph data, and non-finite time.

## FBX constant animation takes

The FBX importer discarded every baked channel marked constant, and every
channel containing only one key. Constant means unchanged *within this take*,
not equal to the scene's bind transform. An imported take could consequently
reset a model's placement, rotation, or scale, or disappear entirely.

The importer retains populated constant channels, including single keys. The
authored `engine/tests/data/constant-take.fbx` fixture has bind translation X=0
and a constant animation take at X=5. Its regression imports the actual FBX,
samples the take at multiple times and after reverse seeking, then disables
animation and verifies that the bind position remains unchanged.

Validation: the consolidated host suite passed 681 tests and 4,210,839 checks
in 226.14 seconds, including these FBX and pose regressions. Android arm64 build
and unit tests passed. These focused regressions verify import and the shared
pose evaluator, not GPU output or physical-device performance. Native iOS
compilation and execution remain pending.
