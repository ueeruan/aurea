# P0 media follow-up — 2026-09-25

This is a checkpoint, not completion of the P0–P10 rebuild request.

## Implemented

- Shared decoded-frame accounting now uses 4 bytes/pixel for RGBA8 and 8 for
  RGBA16F. Previously both were accounted as 4:2:0, understating the iOS BGRA
  cache in particular. Odd 4:2:0 dimensions round chroma dimensions up.
- iOS video decoding uses AVAssetReader's decoded output in presentation order.
  The previous compressed-sample path fed decode-order samples into a single
  VideoToolbox callback slot, ignored the callback PTS, and did not drain delayed
  frames. New frames retain their CVPixelBuffer independently of reader lifetime.
  EOS/error handling is explicit. The live-frame ceiling allows shared prefetch
  instead of leaving only one frame after the scheduler's reserved buffers.
- Shared video import explicitly records zero audio channels/sample rate when
  the platform probe reports no audio. Previously the stereo defaults made
  silent videos schedule audio/waveform work and offer audio extraction.
- Shared decode failures log their backend detail and requested time, identifying
  thumbnail versus preview/export decode, instead of only generic timeout text.

## Native regression coverage added

The opt-in iOS `export-render` simulator scene now verifies the production decoder
against a separate AVAssetReader for both its real export and the generated
`engine/tests/data/preview-bframes.mp4` (90 frames, 65 B-frames). It compares
pixel hashes and presentation timestamps in CPU and IOSurface modes, checks
draining to EOS, seeks backward/forward, suspend/resume and retained-frame lifetime.
The function is absent in Release. The fixture generator is documented beside it.

**This native iOS regression has not executed on this Windows host.** Static Swift
API/project/effect-contract checks are not an Apple compile, Metal run, or native
decoder test. The existing macOS GitHub workflow invokes the expanded scene.
AVFoundation does not expose the selected decoder's hardware status; reporting
therefore no longer claims a measured hardware decoder merely from the API name.

## Executed checks

- New cache regressions failed before the accounting fix, passed afterwards.
- Full host suite after cache changes: 652 cases, 4,135,481 checks, zero failures,
  196.20 seconds. Optional device/benchmark cases retain their existing limits.
- Android arm64 Debug build and Kotlin unit tests passed.
- Android x86_64 Debug build installed successfully on `Aurea_API35`.
- H.264 fixture imported and visibly advanced in the emulator. First run logged
  one generic decode timeout; a fresh single-video import/playback/scrub after
  enabling detailed errors did not reproduce it. Its root cause is **not yet
  established**, so this is not evidence that all preview timeouts are resolved.
- Silent-video regression reproduced 10 failed checks before the import fix.
  It covers mixer participation, extraction, waveform and save/reopen, with an
  audio-bearing video as the positive control.
  After the fix all 34 checks passed. The updated emulator import displayed
  through frame 89 (2.967 seconds), removed inappropriate audio controls, and
  produced no audio/waveform-open warnings in its captured process log after
  playback/scrub and background/foreground.
- Final arm64 build passed; Kotlin: 83 tests, zero failures/errors/skips.
- Final host suite including silent-video coverage: 653 cases, 4,135,459 checks,
  zero failures, 201.42 seconds. Check totals vary in existing timed stress tests.

Logs and captures are in ignored `engine/build/host` and
`engine/build/android-p0`. The emulator uses a host-assisted goldfish H.264
decoder and Vulkan CPU plane uploads; it does not establish phone zero-copy
performance, HEVC/HDR/AV1 compatibility, or weak-device frame rates.

## Remaining work

P0 remains active: investigate the intermittent emulator timeout, variable-frame-
rate scheduling/selection, the broader codec/size/FPS matrix, real Android devices
and native iOS execution. The requested P1–P10 work has not been declared complete.
In particular, passing existing effect/3D/tracking tests does not prove the user's
entire new feature list or the full UI→save→render→export→reopen path.

The decoded-output ordering guarantee is documented by Apple under
[AVAssetReaderTrackOutput.outputSettings](https://developer.apple.com/documentation/avfoundation/avassetreadertrackoutput/outputsettings).
