# P9 — Shared neural upscale baseline, 2026-09-25

## Concrete implementation

`ai::Upscaler` embeds the original Real-ESRGAN animevideov3 weights and official
2x/4x ncnn graphs in the shared core. Pinned ncnn 20250503 runs on one CPU worker
without a Vulkan/Metal dependency. Android and iOS export use the same API and
weights; this is an anime/illustration model, not a general photoreal model.
The official 2x graph uses the learned 4x network followed by bicubic reduction.

Inference consumes SDR RGB8 and emits disjoint output tiles through a callback.
A 20-input-pixel halo covers the 18 spatial convolutions and bicubic footprint.
Activations and convolution workspaces share a 48 MiB allocator budget; bounded
tile vectors and model/pipeline allocations are additional. Cancellation is
checked between tiles; a tile already in inference completes before cancellation.
Model/vector allocation exceptions become Status failures at this component's
boundary; the rest of the engine retains its no-exceptions configuration.

Source/weights hashes and licenses are recorded in engine/assets/ai/README.md.
Xcode explicitly links libncnn because its existing static-library list does not
consume CMake target dependencies.

## Validation scope

Host regression compares whole-image inference with tiled inference on odd-size
RGB input at both scales, including partial edge tiles; checks transformed pixels,
allocator peak and cancellation. Export integration tests cover timing/audio,
pipeline determinism and cancel/recovery. Results are recorded below after runs.

An initial host access violation was symbolized from the Windows minidump:
ncnn::Mat::substract_mean_normalize creates an internal Scale layer. The reduced
operator registry originally omitted it; Scale and Bias are now included.
This was a runtime failure, not a successful inference validation.

## Remaining limits

No physical-device performance claim; CPU inference is expected to be slow.
No GPU inference acceleration, general-photo model or temporal consistency model.
Independent frame inference can flicker. Android native execution and iOS CI
must validate their actual toolchains/runtime before claiming platform readiness.

## Host inference result

`ai-inference-test.log`: 2 tests / 35 checks passed. For the 65x49 odd-sized
fixture, tiled and whole-image output matched exactly at both 2x and 4x
(maximum/mean byte error 0). Peak tracked activation/workspace allocation was
7.77 MiB. Whole+tiled runs took 335.8 ms (2x) and 330.4 ms (4x) on the Windows
host; these small-image timings are not mobile/video throughput measurements.
This first pass relinked the existing core with the corrected ncnn registry;
a consolidated build is required for concurrent scene/export changes.

Consolidated host build subsequently passed. `final-Upscaler.log` repeats the
2 tests / 35 checks on that consistent binary; `final-NeuralUpscale.log` passes
2 tests / 61 checks, including real inference in export, timing/audio retention,
pipeline determinism and cancellation/preview recovery. Platform validation is
still separate from these Windows-host results.

## Native Android execution (API 35 x86_64 emulator)

The existing `aurea_gles_tests` target now includes inference/color regression
sources and matches the core's no-RTTI/no-exceptions flags. The inference source
retains its separate exception boundary. Built with NDK 28.2 and executed from
/data/local/tmp without changing the running editor UI.

- `native-Upscaler.log`: 2 tests / 35 checks passed; both scales whole/tiled byte
  error 0; 7.77 MiB activation/workspace peak. Whole+tiled small-fixture timing
  526.7 ms (2x), 529.3 ms (4x), not a physical-mobile performance benchmark.
- `native-NeuralUpscale.log`: 2 tests / 61 checks passed, including inference in
  GLES export, audio/timing/output dimensions, deterministic pipeline depths,
  and cancellation/preview recovery (268.6 ms for this fixture).

These export regressions use the test capture sink to inspect exact output bytes
and audio; native MediaCodec/muxer correctness is covered by the separate real
export probe. iOS runtime and physical ARM devices remain separate validation.

Full native GLES runner: **182 tests / 872,788 checks / 0 failures**, exit 0,
`engine/build/android-p0/native-full-2110.log`. Optional benchmark/env-only cases
are reported as skipped by the existing runner, so this is not a claim that
benchmarks or unspecified media tests executed. The two media opt-ins were then
run explicitly against /data/local/tmp/proxy-vfr-1080.mp4:

- `native-real-media-2110.log`: actual MediaCodec decoded frame reaches GLES
  renderer, 1 test / 5 checks passed.
- `native-real-proxy-2110.log`: real decode/encode/redecode VFR proxy, 63 frames,
  960x540, preview selection/export-original contract, 1 test / 515 checks passed.

The shared CPU upload regression also passed on host (1 test / 40 checks) and
in the native GLES suite. Planar allocation failure now releases partial textures
and leaves dimensions unpublished so the next frame can retry; allocation-failure
injection itself was not exercised on a physical GPU.
