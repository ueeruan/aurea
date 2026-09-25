# Export packets, autosave and app UI — build 2110

## Export failure report

The user reported Android 1080p H.264 failing with only `erro de E/S`.
Inspection found a concrete native packet bug: the running muxer received
`data + info.offset` while `info.offset` was still nonzero. Android NDK applies
that offset itself, skipping the prefix twice. Pending packets already reset
the offset after copying and did not have this defect.

- Production `write_muxer_packet` now validates the buffer bounds and passes
  the original buffer and offset exactly once. Invalid packets are rejected.
- Export progress now retains `Status.detail()` rather than replacing every
  platform failure with the generic error category. Native diagnostics include
  the encoder, platform error code and last timestamp. Storage exhaustion is
  distinguished where the platform code/filesystem provides evidence.
- Source contract: https://android.googlesource.com/platform/frameworks/av/+/master/media/ndk/NdkMediaMuxer.cpp
  (`AMediaMuxer_writeSampleData` applies `info->offset`).

Real MediaCodec/AMediaMuxer tests on Aurea_API35 x86_64:

| Actual output | Result |
|---|---|
| H.264 1920×1080, 30 fps, AAC stereo | PASS: 30 video frames, 76 total packets |
| H.264 1080×1920, 30 fps, AAC stereo | PASS: 30 video frames, 76 total packets |
| HEVC 1280×720, 30 fps, no audio | PASS: 30 packets |

The test remuxes actual encoded packets with varying offsets 17–59, checks
out-of-bounds rejection, then re-extracts every packet and compares encoded
bytes and presentation timestamps. FFmpeg decoded the 1080p original and
remuxed MP4 to identical video/audio frame hashes. This exercises the same
production packet helper, without a fake muxer. The emulator selected AOSP
software encoders; this is not physical-phone encoder acceptance and does not
prove the screenshot's device has no additional fault.

Evidence: `engine/tests/android_export_probe.cpp`,
`engine/build/android-p0/export-real-1080.log`, `export-real-portrait.log`,
`export-real-hevc.log`, `aurea-export-1080.md5`,
`aurea-export-1080-offset.md5`. Host fault-injection regression also verifies
that a failed sink aborts output and the UI receives the actionable detail.

## Autosave

Android assigned a UI project path on creation without initializing the native
project's file path. The native checkpoint could therefore have no destination.
Creation now writes the initial project before entering the editor. Empty
projects can be saved, so deleting the last layer is persistent. Both platforms
base autosave debounce on model edits, not cursor motion, and bound dirty time
between gestures. Closing saves pending edits and remains open on save failure.

Actual app test: created `AureaAutosave2110`, added a square, allowed autosave,
force-stopped the process, reopened, and observed the square. Then deleted the
last layer, force-stopped/reopened again, and observed the empty composition.
Native file changed 611→1664→611 bytes. Evidence screenshots:
`engine/build/android-p0/autosave-reopen.png`, `autosave-empty-reopen.png`.
This test preceded the final close-project guard and is Android emulator only.

## Home and donations

Android/iOS Home now has direct create/import cards, a compact continue card,
library count/sorting and smaller project cards. Settings and export include an
optional donation row. No donation dialog opens automatically. The Pix payload
is exactly the one provided by the user; the rendered QR was independently
decoded and matched, including CRC. PayPal uses the supplied donation URL.
During export it copies the link instead of backgrounding the renderer.
No payment was initiated. Actual Android Home/settings/QR UI inspected;
iOS layout still requires its native build/run.
