# P2 — Curve editor state and gesture correctness (2026-09-25)

## Implemented

Android previously kept Bezier handles in a process-global session map keyed
by layer/property/frame, because the timeline POD omits handles. Reopening a
project therefore displayed default handles, and undo, moved keys or imported
curves could display/save stale handles. The existing shared
`query_keyframe_easing` API is now exposed through a narrow JNI/Kotlin query.
The curve panel and preset export read authoritative project handles. A graph
revision invalidates the view even when timeline row bytes remain identical
(the intentional KeyframeSnapshot optimization excludes handles). iOS already
used the core query and retains that behavior.

Both platforms now draw with the same safeguarded double-precision Bezier
inverse as the shared evaluator. This fixes distorted endpoint samples for
flat time tangents, while preserving overshoot.

Both platforms also require an initial touch within 24 points/dp of a handle,
preserve the offset from the touch to its center throughout dragging, and
freeze the plot range during the gesture. Touching empty graph space no longer
rewrites the nearest handle. Imported extreme overshoot handles fit the plot.
Quadratic Ease In/Out curves expose their mathematically exact handles.
Android closes the undo group even when a drag coroutine is cancelled; iOS
already closes on cancellation/disappearance.

## Validation

Added `CurveMathTest` JVM regressions for analytic cubic inverse at both flat
endpoints, exact endpoints, intentional overshoot and 1,001 linear samples.
`git diff --check` passes. Consolidated Android compilation/unit tests passed; see the result below.
iOS changes have not been compiled/executed on macOS or hardware in this run.

## Value and speed track graphs

A second increment adds Value and Speed modes on Android and iOS. Both read
`query_track_curve` from the shared core with the complete property/effect/
parameter identity, so they display the actual saved keyframe values rather
than only a normalized easing thumbnail. Sampling deliberately represents the
keyframe curve before expressions, matching the keyframe editor's contract.

Speed displays signed finite differences in property units per second, using
the composition frame rate and actual integer frame intervals returned by the
core sampler. Repeated integer samples are removed before differentiation;
otherwise a short segment oversampled to 160 points would show alternating
false zeroes and spikes. The displayed rate is a sampled interval average,
not a claim of an analytic continuous derivative.

Both platforms provide zoom buttons, pan on empty graph space, and Fit. Android
also accepts two-finger pinch/pan. Value-mode keyframes have 48-point/dp hit
targets. Selecting and dragging a key vertically submits the existing
KeyframeSetValue command to its actual track, within one undo group. Timeline,
preview, project save/load and export therefore consume the edited value via
the existing shared command/evaluation path. Graph zoom/pan is view state and
never changes project keys. Speed is derived and is not presented as an
independent editable value.

Three additional JVM tests cover deduplicated velocity samples, signed and
hold motion, frame-rate units, anchored zoom and pan. Android compilation plus 89 JVM tests completed with zero failures in
`engine/build/android-p0/gradle-graph-ai-tests.log`. iOS static API/scope checks
also passed (not native compilation or execution). Runtime UI exercise remains
pending. The preceding
handles/JNI correction passed Android arm64 compilation and unit tests in
`engine/build/android-p0/gradle-graph-arm64.log` (parent-run verification).

## Scope remaining

Graph-specific multi-key selection, horizontal key movement and value/speed
Bezier-handle editing remain separate work. Native iOS compilation and touch
interaction validation are still required on macOS/device. Existing easing
handles, presets, curve copying and apply-to-all controls remain available.
