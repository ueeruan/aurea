# Aurea rebuild checkpoint — 2026-09-25

This tracks the attached **ULTIMATE STABILITY, PERFORMANCE & EFFECTS REBUILD**
request. It is not a declaration that the earlier phase-8 reports satisfy this
new request. Existing features must be checked against the requested behavior.

## Verified work and remaining scope

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

## Next performance work

The asset format already stores proxy path/dimensions, but there is no automatic
preview proxy generation/selection. Do not describe this metadata as working
proxies. Implement the actual editing path while keeping original media for
export, preserving VFR timing, rotation and color interpretation, validating
stale/missing cache behavior, and avoiding blocking UI/render threads. The same
shared policy must use the native encoder/decoder integrations on both platforms.
