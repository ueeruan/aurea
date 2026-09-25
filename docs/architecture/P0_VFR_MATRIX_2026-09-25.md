# Presentation timing and decoder recovery

The shared frame cache and renderer now use actual presentation intervals for
decoders that provide precise timing. Nominal FPS remains a compatibility path
for older sources. Android derives intervals with one-frame lookahead; iOS uses
sample durations and a lazy sample-reference timing index for random seeks.
Transient decoder failures receive at most two delayed retries, including when
the requested playhead does not change.

The API 35 emulator reproduced a goldfish H.264 timeout after decoder recreation.
Android now retries that failed hardware session with its platform software
decoder while preserving the requested presentation time. This does not claim
that every device decoder failure has the same cause.

## Executed validation

- Full host suite: 656 tests, 4,135,464 checks, zero failures (161.57 seconds).
- Android arm64 Debug assembly and Kotlin unit tests passed.
- Native Android probe checked sequential timestamps, duration intervals, EOS,
  random seeks into intervals, luma hashes, retained frames and suspend/resume.
- Probe passed H.264 B-frame and variable-rate fixtures, H.264 720p60, 4K24,
  portrait 720x1280, 360p at 24/25/30/50/120 fps, HEVC 1080p30, VP9 and AV1.
- Actual editor preview held source frame 0 at composition frame 5, then source
  frame 10 at composition frame 12 for the VFR fixture, as expected.

Reproduce the generated codec cases with `tools/android_media_matrix.py` and the
`aurea_media_probe` target (Android CMake option `AUREA_ANDROID_MEDIA_PROBE=ON`).
Reference timestamps come from independent FFmpeg decoding of generated patterns.
Local evidence is under `engine/build/android-p0/codec-*` and
`engine/build/host/Testing/Temporary/LastTest.log`.

These are emulator decoder tests, not measured phone preview/export performance,
HDR/10-bit coverage or zero-copy validation. Native iOS compilation and execution
remain unverified on this Windows host; its opt-in simulator regression now also
includes the VFR fixture and interval-interior seeks. P1–P10 are not complete.

## Editor export follow-up

The actual Android editor exported the VFR project (21 fps composition) as H.264,
1280x720 at 30 fps. Independent FFmpeg decoding found 90 complete frames over
three seconds, SDR BT.709 tags and no audio track. Comparing sampled RGB pixels
against every source fixture frame matched the expected presentation interval
for all 90 output frames, including holds on the composition frame grid. The
reproducible comparator is `tools/verify_video_presentation.py`; this test pattern
comparison is not a general perceptual quality metric.

After the autosave and transient texture retention changes, the complete host
suite passed: 659 tests, 4,135,017 checks, zero failures, 197.10 seconds. The first
overnight invocation was interrupted; this result is the subsequent complete run.

## Temporal RGB export and finite decoder images

The actual Android VFR project exposed two additional failures: repeated base/R/G/B
requests superseded each other on the single decoder, and CPU-plane frames were
retained across GPU submissions even after staging had copied their pixels. The
second fault exhausted the ImageReader during export. Export also incorrectly
continued with incomplete samples after its four-second decode deadline.

The renderer now requests one missing temporal sample at a time, protects the
bounded required sample set in the cache, and releases CPU images after recording.
External images remain retained until GPU completion. Export drains completed
GPU work when waiting for decode and aborts with Timeout when exact samples never
arrive. Cache reclamation invalidates repeated requests. Decoder metadata is
published as a locked value snapshot so runtime fallback cannot race the UI.

Validation after these changes:

- Complete host suite: 664 tests, 4,135,114 checks, zero failures, 203.12 seconds.
- Regression covers finite decoder image leases, missing-frame export failure,
  distant temporal samples under memory pressure, and GPU RGB pixels after save/reopen.
- Android x86_64 and arm64 Debug builds and Kotlin unit tests passed.
- Actual API 35 editor export: 1280x720 H.264, 30 fps, 90/90 frames, three frames
  in flight; 6.7 seconds from encoder startup to completion, software encoder.
  No ImageReader exhaustion or decoder failure occurred during this run.
- Independent FFmpeg comparison verified all three channels with Timewarp RGB
  offsets +3/0/-3 composition frames at 21 fps: zero mismatches across 90 outputs.
  Reproduce with `verify_video_presentation.py --rgb-offset-frames 3 0 -3`.
- iOS API names, effect parameter types, shared resources and scope checks passed.
  These are static checks; native Metal/iOS execution remains unverified.

Local evidence: `engine/build/android-p0/vfr-rgb-export.mp4`,
`vfr-rgb-presentation.json`, `temporal-retention-native-export.log`, and
`engine/build/host/temporal-retention-ctest.log`. Emulator timings are not phone
performance measurements. This checkpoint does not complete P1–P10.
