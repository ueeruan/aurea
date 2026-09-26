# Playback audit — build 2119

Status: native/device validation in progress. This document does not certify physical iPhone/Samsung performance.

## Confirmed defects

- AudioEngine advanced mixPos and published ring chunks even when AudioBlockCache had not supplied PCM. A gated-decoder regression reproduced both false queued silence and lost samples after seek (2 failures before the fix).
- Audio output start/stop ran on the caller, including the render/model lock path. These operations now run on the audio mixer/control worker.
- Non-1x transport rates muted audio. Bounded mixer-side resampling now preserves the audio master clock at supported positive transport rates (varispeed, not pitch-preserving time stretch).
- Asset relinking could wait for the audio decoder mutex. Registration now uses a short metadata lock; the decoder worker replaces its reader and stale source revisions cannot enter the PCM cache.
- Android rendered preroll/late codec outputs into ImageReader images before discarding them. Codec-slot PTS lookahead now determines the true CFR/VFR interval before rendering; late intervals use releaseOutputBuffer(render=false).
- An in-flight video decode could populate the cache after seek. Playback generations invalidate pending output before presentation.

## Diagnostic path

Open the preview resolution menu and select AUREA RAW PLAYBACK TEST after importing video. It plays the first video source in the project at original resolution, with a neutral viewport and no proxies. The project is unchanged; export continues through the normal compositor.

RAW bypasses scene preparation, transforms, effect plans, masks, text/glyphs, particles, temporal effects and multicomp composition. The GPU records only native-image/YUV color conversion and display output. Duplicate source PTS are held on screen rather than rendered repeatedly. RAW scheduling follows source PTS intervals against the audio clock, independently of the composition FPS grid; display cadence is only the fallback polling deadline.

Transport uses the same play/pause/seek/loop/lifecycle controls. Audio uses the source clip without effect sends or automation. Diagnostic transport rate uses normal varispeed semantics.

Decode remains asynchronous. Video ownership is bounded by the decoder buffer capacity and shared memory budget. PCM uses a bounded SPSC ring (128 ms target lead); the callback copies prepared PCM without decoding, waiting, allocations or mutexes. Cache misses wait only on the mixer worker, without consuming timeline samples. Underruns clamp the audio clock until valid PCM resumes.

## Measurements

The visible RAW report includes source/codec/pixel format, decoder and reported acceleration, unique presentation FPS, repeated requests, estimated dropped CFR frames, decoded FPS, seek count, queue size/bytes, audio lead/underruns, decode/prepare/render/present timings, CPU upload time, GPU timing availability and video PTS age versus audio timeline. Dropped-frame estimates are approximate for VFR; the PTS/interval metrics remain authoritative. GPU-present success counts submitted frames, not physical display scan-out.

Android debug-only intent automation records RAW, seek, pause/resume and normal-compositor measurements with UI heartbeat and PSS. It accepts only a named file under the debug app's private files/playback directory. Release builds ignore this intent.

iOS native UI automation uses production AVFoundation/Metal/AVAudioEngine, records five real clips and checks process survival, playhead progress and bounded post-warmup memory. Existing iPhone performance reports include the playback diagnostics for physical-device reporting.

Fixtures are generated from owned FFmpeg test patterns and a 440 Hz AAC tone with tools/playback_matrix.py: H.264 720p30, 1080p30, 1080p60, HEVC 1080p30 and H.264 VFR. FFmpeg is a development tool and is not bundled in Aurea.

## Validation so far

- Gated audio regression: failed before the fix, passes after.
- Audio-related host battery: 20 tests, 98,319 checks, zero failures after rebuilding all affected objects consistently.
- Transport rates 0.5x/1x/2x/4x: waveform samples and audio clock verified.
- Seek cancellation: old in-flight video frame rejected, new target delivered.
- Initial Android emulator measurements exposed remaining repeated-frame/late-output work. Those initial rates are not release acceptance; repeat after the MediaCodec refactor and without concurrent compilation.
- Physical iPhone/Samsung and final RAW/compositor matrix remain to be recorded. AVAssetReader does not expose hardware use for its decoder instance, so iOS acceleration is reported unknown rather than guessed.


## Android emulator acceptance run (2026-09-26, commit 8a96ceea)

Five 20-second owned pattern/AAC files ran through RAW, seek, pause/resume and the normal compositor. No simultaneous native compilation. The Android emulator uses gfxstream with CPU YUV planes because its advertised external-YUV path is known to sample incorrectly. These numbers do not represent physical-device zero-copy performance.

| Source | RAW FPS / estimated drops (initial ~12 s) | Compositor FPS / estimated drops (~12 s) | Audio underruns, whole test |
|---|---:|---:|---:|
| H.264 720p30 | 30.01 / 5 | 29.64 / 9 | 0 |
| H.264 1080p30 | 27.42 / 35 | 29.84 / 8 | 0 |
| H.264 1080p60 | 6.86 / 689 | 21.94 / 445 | 1 |
| HEVC 1080p30 | 26.17 / 61 | 29.88 / 9 | 0 |
| H.264 VFR (30/60) | 15.61 / approximate 222 | 29.83 / approximate 196 | 0 |

**Performance acceptance is not complete.** 1080p60 fails the requested real-time target. The original-resolution CPU upload path frequently takes 20-75 ms in gfxstream. RAW's late-frame behaviour also needs further investigation under this backpressure. A/V frame age reached 333 ms at the final 60 FPS compositor sample; do not describe that case as synchronized. The 30 FPS cases remained close to the audio clock, with no observed audio underrun; physical lip-sync has not been measured.

At final compositor samples PSS was approximately 192-200 MiB. This short run cannot establish absence of long-term leaks. Maximum UI heartbeat gaps across import/start/play were 375-887 ms, so instant interaction is not certified.

Native decoder-only fixtures passed exact PTS/duration, retained-frame ownership, seeks and suspend/resume: 90 B-frames fixture frames and 63 VFR frames. This standalone probe fell back from goldfish to c2.android.avc.decoder; the full app matrix reported goldfish hardware decoding. No hardware result is inferred from the standalone probe.

Local evidence: `build/playback-matrix/*-android.jsonl`, `build/playback-matrix/android-summary.json`, `build/preview-bframes-new-decoder.log`, `build/preview-vfr-new-decoder.log`.

## iOS build delivery

Workflow: https://github.com/ueeruan/aurea/actions/runs/36260350168 . Build 2119 is an unsigned IPA for sideloading. Native compilation, bundle checks and Apple audio sample-rate conversion checks passed. Simulator playback results must be assessed separately from package validation.

The simulator job completed successfully: 14 native UI tests, zero failures (343.95 seconds). This includes all five RAW media cases and the 30-second video/text/captions test, plus video move, timeline dragging, add menu, effects and undo regressions. These assertions check survival, progression and bounded short-run memory; they do not enforce a 30/60 FPS performance threshold. Physical iPhone/Samsung acceptance remains outstanding.


## Follow-up 2120: one-frame cache seek loop

A physical-iPhone screenshot reported 22.4 preview FPS, 24.9 ms decode, one retained frame and 75 seeks; its installed build has not been confirmed. A shared-core regression reproduced a related defect independently of that clip: a one-frame cache retained the current frame while decode-ahead advanced three frames and discarded them, forcing a backwards seek on the next forward playback request. Thirty requested frames caused 30 seeks and 120 delivered frames.

Decode-ahead now respects measured frame size, local frame/byte limits and the remaining shared decoded-frame budget. The first retained frame triggers a capacity recheck. Normal cache capacity retains the existing prefetch window. Regression results for a frame-count limit, byte limit and shared-memory limit: each plays 30 frames with one seek and 30 delivered frames. All 18 VideoSource tests pass (603 checks). This is a reproduced/fixed code defect, not proof that all causes in the submitted iPhone clip are solved.
