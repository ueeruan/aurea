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

## Full-screen interaction increment (build 2110)

Curve entry points now open a dedicated full-screen editor on Android and iOS.
Easing, value and speed modes remain available; the preset bank is toggled from
Presets to keep the graph wide by default. Previous/next segment controls and
zoom controls have larger touch areas. Easing handles are inset horizontally
so targets at domain boundaries remain inside the graph.

Time Remap retains its effect-card editor and adds a 240 dp/point canvas,
full-screen expansion, zoom/Fit and pan on empty space. Android uses one contact
handler rather than competing tap/drag handlers. Key dragging preserves the
initial finger-to-point offset. Both platforms follow the index returned by the
core when a dragged remap key crosses another key, instead of editing the key
that took its old slot. View navigation does not write project keys.

Verified on 2026-09-25:
- `engine/build/android-p0/2110-debug-regression.log`: assembleDebug and
  testDebugUnitTest succeeded in 1m 58s.
- `build/android/app/test-results/testDebugUnitTest/TEST-com.aurea.aurea.editor.panels.TrackGraphMathTest.xml`:
  six tests, zero failures/errors. The three added cases exercise 48 dp hit
  targets at densities 1/2/3.5, nearby/coincident point selection, and hit mapping
  after zoom and pan. Existing value/velocity/viewport tests also passed.
- All 17 JVM suites in that run: 99 tests, zero failures/errors.
- iOS API/static-name audit: zero problems. This is not native Swift compilation
  or native touch validation.

Runtime acceptance still to record separately: enter through a timeline key and
an effect parameter, drag each easing handle and a value point, change segment,
open/close presets, pan/zoom/Fit, expand Time Remap, drag a point across another,
then undo and reopen the project. Confirm the visible graph and preview follow
the intended key. Exercise both orientations and safe-area sizes. The parent is
performing emulator UI validation; no device result is claimed by this document.

### Runtime QA follow-up: default smooth curve
The parent confirmed the larger full-screen graph in
`engine/build/android-p0/graph-fullscreen.png`. That exercise found an additional
input blocker: EaseInOut (the default text-animation interpolation) provided
handle coordinates but `hasHandles` excluded its type. Android/iOS now expose
those handles. Opening or tapping preserves the original piecewise quadratic
curve; conversion to a cubic happens only after a deliberate handle drag.
Android uses touch slop and iOS a 3-point movement threshold. Two new
`CurveEaseInteractionTest` JVM cases cover smooth handles without changing its
saved interpolation and the continuous built-in/hold distinction. These two
cases and the handle-drag runtime recheck await the next consolidated build.

## Latest requested layout: reference panel below timeline

The later explicit screenshot request supersedes the full-screen default above.
Android/iOS now keep Curve inside the contextual panel beneath the timeline,
including wide layouts. Preview and layer keys remain visible. The reference
arrangement uses a left back/invert/more rail, green curve on a subdued violet
grid, large white handles, lower Cubic Bezier Easing caption with segment arrows,
and two right columns for preset thumbnails and families. Value, Speed, copying,
pasting, saved presets, apply-to-all, overshoot and optional expansion remain
reachable through More. The full-screen editor is now optional.

Bounce/Elastic/4-step families use the shared evaluator's appended interpolation
IDs 7/8/9. Their thumbnails mirror the core formulas, including elastic
overshoot and four fixed plateaus; they do not expose unsupported Bezier handles.
Existing curve IDs remain unchanged. One additional JVM regression covers the
new thumbnail semantics. Static iOS API checks passed after this change; the
latest reference layout and family controls await consolidated compile/runtime
QA. Do not treat the earlier full-screen screenshot as proof of this new layout.

### Reference layout: Android runtime acceptance

The parent tested the installed APK and confirmed both white handles move the
real curve. The lower-handle edit survived save/reopen. Evidence:
`engine/build/android-p0/graph-reference-handle-drag.png` (before the final
header/guide polish) and `engine/build/android-p0/graph-upper-handle-drag.png`
(final layout). Bounce, Elastic and four-step presets apply their actual curves;
undo after Steps restores Elastic, recorded in `graph-bounce-applied.png`,
`graph-elastic-applied.png`, `graph-steps-applied.png` and `graph-undo-steps.png`
in the same directory. The footer now names these families and Hold instead of
calling them cubic Bezier; the reference title remains for Bezier editing.
This caption adjustment awaits the next consolidated build. These observations
are Android emulator validation, not native iOS touch or physical-device proof.
