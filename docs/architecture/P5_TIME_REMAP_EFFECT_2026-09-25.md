# Time Remap effect: canonical source-time curve

## Implemented
- Android and iOS render the real Time Remap graph and presets inside the `aurea.time.remap` effect card. Speed retains constant speed/reverse and frame blending.
- Generic key commands previously wrote both `TimeRemap` and the effect time parameter to `Layer.tracks`; playback reads `Layer.timeRemap`. Commands now resolve the canonical curve. The effect alias converts source seconds to composition frames; direct TimeRemap keys use frames.
- Layer/keyframe queries now include that curve, so timeline diamonds, selection, curve editing and dragging target actual playback data. Time Remap track labels open Effects.
- Effect enable/remove updates actual remap state. Smooth is EaseInOut rather than a misleading Linear alias.
- Presets include linear, smooth, slow middle, acceleration/deceleration, freeze at the current source frame and reversed source progression.

## Evidence
- `engine/build/host/remap-current-test.log`: 6 Remap tests, 139 checks, no failures, reported by scene3d agent. Includes canonical command writes, effect alias units, timeline query, enable/bypass, save/load, smooth/freeze/reverse.
- Android production Kotlin compilation passed in parent build; compact timeline regression passed separately in `timeline-compact-tests.log`.
- iOS API/static-name audit passed; this does not establish native Swift compilation or iPhone behavior.
- Added `ClipTime.MoveVideoToTwoSecondPlayheadPreservesSourceAndUndo` for the queued dock command at two seconds. Pending consolidated run. It has no GPU or AVFoundation decoder and does not reproduce/clear the reported physical iOS freeze.
- Added native XCTest `testAddingEffectClosesBrowserAndShowsAppliedCard`; requires Mac execution. Browser now observes effect-count confirmation after asynchronous submission rather than assuming immediate mutation.

## Limitations
Legacy remap migration now reads direct frame tracks or a uniquely identified effect time track in seconds. Composition double FPS converts values and velocity tangents; timestamps and normalized handles are preserved. Existing valid canonical curves win without merging conflicting timing. Old tracks move to lossless recovery metadata (Timeline section v23), outside active track queries; a persisted migration marker prevents deleted keys from returning after save/load. Unknown components, dangling effect IDs, expressions requiring unit reinterpretation, and multiple competing effect tracks without a canonical curve are left unchanged rather than guessed. Three Serialization.LegacyRemap regressions are added; consolidated compilation/tests pending. Encoded device export and physical touch interactions remain validation work; the source-time evaluator is shared with export.
