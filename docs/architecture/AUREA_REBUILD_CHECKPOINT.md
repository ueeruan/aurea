# Aurea rebuild checkpoint — 2026-09-25

This tracks the attached **ULTIMATE STABILITY, PERFORMANCE & EFFECTS REBUILD**
request. It is not a declaration that the earlier phase-8 reports satisfy this
new request. Existing features must be checked against the requested behavior.

## Verified work and remaining scope

### Build 2110 working checkpoint — user bug reports and revised curve reference

- Autosave now initializes Android's native file path at creation, persists
  empty projects, ignores cursor-only changes for debounce, and saves pending
  changes before closing. Android force-stop/reopen tests preserved both an
  added layer and deletion of the last layer.
- Native Android export corrected double application of encoded-packet offsets.
  Error progress preserves platform details. Actual H.264 landscape/portrait
  1080p+AAC and HEVC720p exports passed, including nonzero-offset remux and
  independently decoded video/audio equivalence.
- Time Remap commands, queries and interpolation now use the canonical remap
  track. Legacy tracks migrate with preserved recovery data. Graph controls live
  in the effect, not the video's speed panel; compact timeline children show
  labels/keyframes/curves. iOS effect insertion waits for actual insertion before
  closing the browser. Native iOS acceptance is still pending.
- Shared 3D includes creatable cameras/lights, null-depth keys, per-instance
  materials, static scene assembly with preview-only observer camera, and
  serialization validation. New text3D is static. A scene field formatting bug
  changed typed1100 to11000; focused numeric drafts now remain stable on both
  platforms. Native field QA is pending the updated APK.
- Real-ESRGAN animevideov3 runs in the shared core with bounded tiled2x/4x
  inference and cancellation, connected to actual video export including audio.
  This model targets anime/illustration; CPU performance and independent-frame
  flicker remain limitations. No general-photo/temporal model or GPU inference
  claim is made.
- Home and optional settings/export donations implemented on Android/iOS. The
  actual Android QR decodes to the user's exact Pix payload. No payment started.
- Consolidated host: **721 tests /4,554,235 checks /0 failures**,
  `engine/build/host/full-post-heartbeat-test.log` (before the additional easing
  families below). Native GLES: **182 tests /872,788 checks /0 failures**,
  `engine/build/android-p0/native-full-2110.log`; real MediaCodec and VFR proxy
  opt-ins passed separately. Debug/JVM: **99 tests /0 failures**,
  `engine/build/android-p0/2110-debug-regression.log`.
- User supplied a new curve-editor screenshot after the first full-screen
  design. Current work replaces default full-screen presentation with the
  reference's timeline-adjacent panel, green curve, white handles and preset
  rails, preserving larger hit targets. Real Bounce/Elastic/Steps families passed
  3 host tests/3,351 checks and 1 native GLES test/21 checks. Latest layout/native numeric
  fields still need emulator QA and final packaging.
- Build number2110 is set on both platforms. One preliminary Android32 release
  build passed (`2110-release-32.log`), but it predates the revised graph and
  is **not** the final deliverable. iOS CI and final32/64/IPA packaging remain.
- Reference-layout debug build passed in 1m22 with102 JVM tests. The optional
  expanded iOS menu and unsupported-inversion states were corrected afterward;
  their last focused JVM check remains for the final build.
- Actual app neural export completed63 frames at2x and published320x180 H.264
  21fps/3.00s to MediaStore. FFmpeg decoded the complete file without errors
  (`ai-app-export-decode.log`). This fixture has no audio; audio retention was
  independently checked by the shared export regression.
- The Samsung SECGujarati missing-glyph report is corrected in shared Text.cpp:
  an unsupported cluster now uses the configured default font while supported
  authored glyphs remain unchanged. Baseline had7 failures; fixed regression
  passed35 checks. Final Text/Text3D/GPU suite passed58 tests/77,998 checks.
  Physical Samsung retesting remains pending.
- Final reference-graph/font Android debug build passed with103 JVM tests,
  zero failures. The reference panel keeps the timeline, white drag handles,
  green curve, preset rails and a dotted white progress guide.
- First2110 iOS CI failed Swift type-checking in ExportView.options. The large
  builder was split into named sections with explicitly typed dimension math;
  native recompilation is required before declaring the IPA ready.
- The user explicitly cancelled recurring automation
  `aurea-implementa-o-e-paridade`; it was deleted in the app. Continue only this
  active task. Do not recreate the automation.

Evidence and limits are detailed in `P10_EXPORT_PACKETS_AND_AUTOSAVE_2026-09-25.md`,
`P9_NEURAL_UPSCALE_2026-09-25.md`, `P3_CAMERA_NULL_UI_2026-09-25.md`,
`P2_GRAPH_EDITOR_2026-09-25.md`, `P5_TIME_REMAP_EFFECT_2026-09-25.md` and
`P10_GLITCHIFY_2026-09-25.md`. Physical low-end/thermal, native iOS runtime,
real-footage tracking/flow/particle acceptance and live generation-service tests
remain open. The Android quality-resize complaint has not been reproduced in
the tested cases; do not call it conclusively resolved.

### Latest verified checkpoint (supersedes the older table below)

- Automatic preview proxies are implemented with native background encoding,
  VFR timing, bounded cache, cancellation and original-media export selection.
  Android content-URI codec regression passed at 960x540 from 1920x1080.
- Graph value/speed curves, timeline layer expansion (transforms, effects and
  individual animated tracks), and working text-animation creation now share
  Android/iOS behavior. Actual Android chevron and Add animation interactions
  were checked; the latter creates visible text reveal and timeline keyframes.
- Fixed parented 3D camera basis, mirrored normals/culling, morph shadows,
  camera-tracking consensus, remapped vector shutter travel and particle
  emission/counting. Added original Halation GPU effect.
- Generation UI/error/progress/upload handling improved on both platforms.
  AI video upscaler is still unimplemented; feasibility is documented, not a
  substitute for model/runtime integration and the video pipeline.
- Final host regression: 696 tests, 4,375,093 checks, zero failures, 197.49s.
  Native Android GLES: 166 tests, 693,307 checks, zero failures. Android JVM:
  90 tests, zero failures. Debug x86_64 and arm64 builds passed. iOS static
  checks passed; no native iOS build or execution was possible on Windows.
- Logs: engine/build/host/beta-expanded-final-ctest.log,
  engine/build/host/Testing/Temporary/LastTest.log,
  engine/build/android-p0/gles-expanded-full-test.log,
  engine/build/android-p0/gradle-expanded-final-arm64.log.
- Remaining: physical low-end/thermal measurements; graph multi-selection and
  horizontal key dragging; full 3D acceptance including alpha-mask shadows and
  animated skin bounds; planar tracking/stabilization real-footage acceptance;
  complete optical-flow occlusion/motion-blur acceptance; remaining Particulate
  acceptance; AI upscaler; live generation service validation; native iOS and
  whole-app device regression. Do not report the full rebuild as complete.

The table below records the earlier checkpoint and is retained as history.

| Phase | Evidence so far | Still required to close the phase |
|---|---|---|
| P0 preview/crashes/playback | VFR presentation fixes, bounded decoder recovery, software fallback, temporal sample retention, save revision race, asynchronous codec retirement; actual emulator preview/playback/export | Broader real-media/long-session and physical-device matrix; native iOS execution |
| P1 backend/performance/memory | Vulkan startup and ES fallback; exact mip accounting, bounded transient pool, real decoder upload regression, temporal RGB warmup; thumbnail trim/clear/relink/session admission fixes | Transparent editing proxies are **not implemented**; physical low-end measurements, thermal/battery/long-session budgets, remaining performance criteria |
| P2 keyframes/timeline/markers/gestures | Marker integrity/editing; preview-anchor tap marks current frame on both platforms; Bezier endpoint solve, duplicate key normalization, track-reference lifetime fixes | Native iOS interaction validation; remaining graph editor/evaluator/gesture acceptance. User explicitly rejected a separate marker strip |
| P3 3D/materials/lights/camera | Deferred environment lifetime fix; malformed animation channel protection; FBX constant-take preservation; parented XYZ storage regression | Complete requested 3D behavior and visual acceptance on Android/iOS |
| P4 tracking/stabilization | Existing implementation has not yet been fully audited against this request | Audit, implement missing behavior, real-footage validation |
| P5 time remap/flow/motion blur | Temporal RGB VFR export independently compared | Complete requested timing/optical-flow/motion-blur acceptance |
| P6 particles | Existing implementation has not yet been fully audited against this request | Audit, implementation and measured validation |
| P7 AI upscaler | Not closed | Model/runtime, resource and output-quality requirements |
| P8 generation UI | Not closed | Requested Android and iOS flows and integrations |
| P9 effects | Not closed | Compare complete requested catalog and controls with actual rendered/exported output |
| P10 parity/regression/polish | Shared C++ changes plus Android builds; iOS static checks | Native iOS build/run, physical devices, end-to-end parity and final acceptance |

## Evidence and constraints

- P0 details: `P0_PREVIEW_2026-09-25.md`, `P0_MEDIA_FOLLOWUP_2026-09-25.md`,
  `P0_VFR_MATRIX_2026-09-25.md` in this directory.
- P1 details: `P1_MEMORY_2026-09-25.md` and `P1_GLES_2026-09-25.md`.
- Follow-ups: `P1_THUMBNAIL_LIFECYCLE_2026-09-25.md`,
  `P2_KEYFRAME_EVALUATION_2026-09-25.md`, `P2_MARKERS_2026-09-25.md`,
  `P3_ANIMATION_INTEGRITY_2026-09-25.md`. Latest full host regression:
  681 tests, 4,210,839 checks, zero failures in 226.14 seconds.
- Emulator: `ANDROID_EMULATOR_LOCAL.md`; AVD Aurea_API35, API 35 x86_64,
  host GPU, headless. Its performance is not physical-phone performance.
- ES actual app export: 90/90 VFR temporal RGB frames matched independent
  FFmpeg presentation/channel reference. Full ES suite: 153 tests, zero failures
  before the additional RGB warmup regression; that regression passed separately.
- Debug GPU property has been restored to `auto`; Vulkan startup succeeded.
- Native iOS builds/tests require an Apple build environment. Static scope,
  shared-resource and effect-contract checks do not substitute for that.
- Preserve unrelated existing root fixtures and tracked `__pycache__` deletions.
  Stage only files belonging to each checkpoint. No remote publishing requested.

## Previous performance handoff (completed in latest checkpoint)

The asset format already stores proxy path/dimensions, but there is no automatic
preview proxy generation/selection. Do not describe this metadata as working
proxies. Implement the actual editing path while keeping original media for
export, preserving VFR timing, rotation and color interpretation, validating
stale/missing cache behavior, and avoiding blocking UI/render threads. The same
shared policy must use the native encoder/decoder integrations on both platforms.

## Build 2109 delivery follow-up

Transport now lands exactly on previous/next composition markers on Android/iOS. Final JVM count is 93 passing tests after this addition. User explicitly requested APK 32/64-bit and IPA through GitHub; this authorizes pushing the build branch to run the existing workflow, superseding the earlier no-publish constraint for this build only. No store/release publishing is requested.

## Delivered build 2109 — native iOS build now verified

Android release APKs armeabi-v7a and arm64-v8a compiled, package/version/ABI and v2/v3 signatures verified. Both include exact native frame seeking (aff4f70f). GitHub build branch codex/native-beta2-ipa is at dbf211ba; run https://github.com/ueeruan/aurea/actions/runs/36171107821 successfully compiled the iPhone app with Xcode 26.3, validated the Foundation bundle, compiled/linked all 66 Metal shaders, and packaged the IPA. Downloaded IPA passed local check_ipa.py, build 2109. Files are in build/releases/2109/. IPA is unsigned.

Simulator app also compiled, but UI/project capture and gesture tests were still running at the time of this entry: follow run 36171107821 to completion before claiming iOS runtime parity. Shared baseline remains 696 host / 166 native GLES / 93 JVM tests passing. Actual text export: 63/63 frames, 1280x720 H.264, 21fps decoded independently. Known Unity/WebView post-export crash is documented in P10_EXPORT_AD_WEBVIEW_2026-09-25.md. All earlier unclosed feature and physical-device criteria remain unclosed.
