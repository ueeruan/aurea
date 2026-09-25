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
