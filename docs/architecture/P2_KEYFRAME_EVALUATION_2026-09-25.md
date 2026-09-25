# P2 — Deterministic keyframe evaluation (2026-09-25)

## Corrected root cause

The shared cubic Bezier inverse stopped when the absolute error in the time
coordinate was below `1e-5`. For a valid curve with a horizontal time tangent,
that does not bound the error in the animated value. With control points
`(0, 1), (0, 0)` and normalized time `0.000001`, the old evaluator returned
approximately `0.000003`, while the analytic inverse `t = cbrt(x)` gives
`0.029404`: a visible snap near a keyframe. The mirrored endpoint has the same
problem. It is independent of seeking order or platform.

`cubic_bezier` now uses double precision for inversion, keeps Newton steps
inside a root bracket, falls back to bisection for flat/out-of-bracket steps,
and measures parameter convergence. Value control points remain unrestricted,
so intentional overshoot/anticipation are preserved. Exact endpoints remain
exact. No keyframe data or serialization format changed.

The change applies to the shared evaluator used by Android and iOS, preview,
export, curve editing and expression sampling.

## Regression coverage

- `Track.FlatBezierTimeHandlesDoNotSnapNearEndpoints`: analytic inverse for
  both cubic time curves, including the first and last frames of a long track.
- `Track.BezierRandomReverseSeekPreservesCurveAndOvershoot`: independent
  double-precision bisection reference over six handle configurations and
  1,501 permuted frame positions each; includes overshoot, anticipation,
  steep/flat time curves and crossing into a Hold interval.

Build/test execution is pending the consolidated host/Android run. These are
numerical evaluation regressions, not claims of iOS native execution or
physical-device performance validation.

## Audit boundaries

The `lastIndex` lookup verifies both interval boundaries before using its
cached index, so random/reverse seeking alone does not produce a stale-index
result. Timeline edits and layer retiming are handled in a separate checkpoint.
The loader now normalizes legacy/imported tracks with a stable `O(n log n)`
sort when needed, then resolves duplicate times by preserving the last complete
serialized record, matching the existing preset importer. Previously an edit
could target one duplicate while evaluation used another, making the value
appear to reset. Valid unique tracks keep all keyframes unchanged, and the
original input file is not rewritten during load.

Additional regressions cover duplicate-time load, backward seeking, value
editing, keyframe movement, save/reopen and evaluation consistency; a reversed
32,768-key track verifies ordering and full value preservation without the old
quadratic insertion sort. The targeted serialization run passed 21 tests and
65,822 checks; the Track filter passed 32 tests and 9,169 checks.

## Track reference lifetime during parenting and tracking

`TrackSet` uses a contiguous vector. Several engine operations retained a
reference to X, inserted Y/Z, then wrote through the original reference. A
vector growth invalidates that reference, so parenting an animated layer or
applying stabilization could lose keys or access freed storage. Copied history
snapshots can trigger this with fewer than the default 16 tracks.

The point-tracking, stabilization, camera-track application and reparenting
paths now create all required tracks first, then acquire their references.
`Engine.ParentingAnimated3DLayerSurvivesTrackStorageGrowth` covers copied
single-track storage and the 15/16-track growth boundaries, checking all 31
world positions in reverse order, required XYZ tracks and unrelated effect
tracks. Consolidated host regression passed: 681 tests, 4,210,839 checks,
zero failures in 226.14 seconds (`engine/build/host/beta-final-ctest.log`).
Android arm64/x86_64 builds and unit tests passed. Native iOS execution is pending.
