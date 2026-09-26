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

RAW bypasses scene preparation, transforms, effect plans, masks, text/glyphs, particles, temporal effects and multicomp composition. The GPU records only native-image/YUV color conversion and display output. Duplicate source PTS are held on screen rather than rendered repeatedly. VFR polling follows display cadence, not the file's average frame rate.

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
