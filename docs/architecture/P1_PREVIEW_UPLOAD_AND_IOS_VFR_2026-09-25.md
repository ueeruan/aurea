# Preview upload and iOS VFR follow-up

## Changes

- Shared CPU video fallback now uploads planes only when decoded frame content changes. Previously every render uploaded them again, including a paused frame, repeated display refreshes, and a preview-resolution change. A monotonic `DecodedFrame::content_id()` avoids pointer/PTS reuse after seeks and does not retain CPU frame memory in GPU caches. New content and partially failed uploads still upload every required plane. Zero-copy and export resolution are unchanged.
- iOS seek keeps the container's exact rational `CMTime`. Converting PTS through rounded microseconds could start `AVAssetReader.timeRange` just after a requested sample (2/3 seconds becomes 666667 microseconds), excluding that sample. Indexed VFR presentation intervals use the next PTS, avoiding discard based on a shorter nominal decoded-sample duration. The existing production decoder regression now reports failing mode/index/target/PTS/duration; its assertions remain intact.

## Validation

Host RelWithDebInfo build succeeded. `VideoPreviewScaleKeepsGeometryAndReusesPlaneUpload` passed 1 test / 40 checks on Vulkan. It renders NV12 through the production GPU path at Full, half, quarter, eighth, Full, and full-quality export. Logical interior/exterior pixels remain in place; four scale changes plus same-frame export add **zero plane upload bytes**. A new frame and decoder reopening at the same PTS each require a new upload. Log: `engine/build/host/preview-scale-upload-test.log`.

iOS correction requires the next native Mac CI run. Previous build 2109's real VFR regression failed `Seek returned wrong pixels/PTS`; B-frame regression completed before that failure. The test uses `MediaPriority::Preview`, so the Thumbnail-only CPU NV12 path is not the failing path.

## Existing build 2109, real Android emulator UI

Aurea_API35 was booted headless. Tested actual gallery imports, not synthetic decoder replacements:

- 160×90 VFR clip: AUTO to 1/8 and Full; framing unchanged.
- 1920×1080 VFR fixture imported through gallery content URI: Full to 1/8; framing unchanged.
- Same landscape source with composition changed to 1080×1920: 1/8 to Full; framing unchanged, including existing source placement/crop.

Captures: `engine/build/android-p0/preview-quality-{before,after,full}.png`, `preview-hd-eighth.png`, `preview-portrait-{eighth,full}.png`. The earlier `preview-hd-full.png` capture may still include selection/menu state; portrait pair is the clean comparative pair.

Baseline HUD while HD plays at project 21 fps, Full portrait composition: preview 20.9 fps, UI 60 fps, CPU frame 8.9 ms (prepare 0.1, record 8.8), GPU 4.7 ms, decode 13.8 ms, present 1.0 ms, acquire 0.5 ms, zero dropped/stale frames, one seek. Pacing p50 47.8 / p95 48.8 / p99 49.4 ms over 21 samples. Recorded in `engine/build/android-p0/preview-hd-baseline-hud.txt`. Initial HD startup was much slower and must not be confused with steady playback.

These measurements use the **previous APK**, so they are a baseline, not an APK before/after performance claim for the upload change. They do not validate physical weak devices, iOS performance, or Android hardware zero-copy. The reported enormous preview after changing quality was **not reproduced** in these three scenarios and remains unresolved; no speculative geometry patch was applied.
