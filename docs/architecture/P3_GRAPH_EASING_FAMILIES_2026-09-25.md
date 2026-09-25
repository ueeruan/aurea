# P3 — Real graph easing families, 2026-09-25

Shared Interpolation appends Bounce=7, Elastic=8 and Steps=9, preserving IDs0..6.
No keyframe field/layout change is needed: the existing interpolation byte is
saved and queried. Engine command validation, time remap, preset import and both
native bridges accept the appended values. Unknown modes still fall back to
Linear when reading. Older app versions cannot reproduce these new modes.

Bounce is an original piecewise ballistic curve: acceleration to the first
landing at half-time, followed by three shrinking rebounds. Elastic is a damped
cosine response normalized at the endpoint, and intentionally overshoots values.
Steps is explicitly a fixed four-step preset (end jumps), not an adjustable
step-count control. These modes do not use Bezier handles. Android/iOS graph
curves must draw the same formulas as core apply_easing, not substitute Beziers.

Tests added: endpoint/finite-value checks, rebound/overshoot/step thresholds,
project save/load sample parity, and actual rendered positions with preview vs
final-quality pixel parity. Host focused results pending below; no full-suite
rerun was requested for this bounded addition.

Focused host build succeeded; `engine/build/host/easing-families-test.log`:
**3 tests / 3,351 checks / 0 failures**, exit 0. The GPU test measured actual
animated shape positions for all three modes and matched preview with
final-quality render pixels. This is Windows/Vulkan validation; Android/iOS
runtime and UI validation are separate. No full-suite repeat was performed.

Final native Android GLES focal validation also passed: **1 test / 21 checks /
0 failures**, exit 0, `engine/build/android-p0/easing-gles-test.log`. This runner
includes the GPU test (all three modes), not the two host Track/Serialization
cases. NDK target rebuilt with -j4, executed on API35 x86_64 using a separate
pbuffer; editor UI and the ongoing real-app upscale export were not manipulated.
No native full-suite repeat was performed for the easing addition.
