# Shared preview proxies

The media manager requests background proxies for video above the device
policy's short-side threshold. One background worker opens the platform's CPU
decoder, area-resamples the actual planes and feeds the platform H.264 encoder.
It bakes crop/rotation into the pixels, converts planar/NV21/NV12/P010 input into
NV12 and preserves the source matrix, primaries, transfer and range. HDR remains
HDR with 8-bit preview quantization; this is not an HDR mastering conversion.
Unsupported or failed proxy generation leaves original decoding available.

Original presentation timestamps are preserved, not replaced by a nominal FPS
grid. One-frame lookahead supplies actual durations to the iOS encoder; a
sidecar restores exact original presentation intervals on proxy decoding,
including the last frame. Original files and project asset paths are unchanged.
The renderer passes finalQuality into media selection: export reopens the
original even when preview was using a proxy. Audio always uses the original.

Disk cache keys include a source-version identity and target resolution. Android
gallery content URIs use the existing ContentResolver FD opener and fstat,
including file region offset/length; iOS resolves its stored document path.
Sources without a reliable version receive a session-only identity. Cache
entries are checked against readable video metadata, publication uses temporary
files, and changed source identities invalidate work before publication. The
512 MiB disk budget includes video and timestamp sidecars; live decoder entries
are pinned against eviction. Queue depth is 16 and only one job runs at a time.

Background, thermal pressure, playback and export are independent pause reasons.
Cancelling one reason cannot accidentally clear another. Generation pauses
while playing; completed proxies remain usable. Project changes invalidate work;
shutdown drains the worker. Cancellation releases resources after the current
platform codec operation returns, without joining the worker on the UI thread.
Transient failures retry after 30 seconds, rather than disabling an asset for
the entire project session.

Tests include crop/rotation/chroma ordering, limited/full-range P010 conversion,
and opt-in real Android codec generation/redecode using AUREA_PROXY_MEDIA. The
native regression exercises the content-URI FD path, checks frame timestamps,
color tags, pixel error, frame count and original selection for final-quality
rendering. Run results must be recorded separately; a successful build alone is
not native proxy validation. Native iOS execution and physical weak-device
performance/thermal measurements remain required.

## Executed validation checkpoint

The host conversion tests passed: 2 tests, 8 checks. The Android emulator native
run passed 3 tests and 522 checks, generating and decoding a real 960x540 proxy
with all 63 presentation frames from the 1920x1080 VFR fixture. It checked
timestamps, color metadata, pixel error and original-source selection for
final-quality rendering. Evidence:
`engine/build/android-p0/proxy-real-media-test.log`.

That run used a regular file path. The subsequent content-URI identity/opener
regression change has not yet been rebuilt or executed at this checkpoint.
This is emulator correctness evidence, not weak-phone performance measurement.

Final integrated verification: host 696 tests / 4,375,093 checks and native Android GLES 166 tests / 693,307 checks, all passing. The native suite includes the production content-URI proxy factory, original-media export selection, and Halation rendering. These are emulator results, not physical-device measurements.
