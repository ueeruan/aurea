# P9 — AI video upscaler feasibility audit (2026-09-25)

## Repository state

No super-resolution inference runtime or trained model was found in the
tracked engine/Android sources/assets or platform dependency configuration.
The existing remote generation client is not an upscaler. No fake UI option,
sharpening substitute, model download, dependency or inference claim was added
by this audit.

## Suitable implementation direction

Use a cancellable offline media-processing job that generates a new video
asset. Keep the source untouched. Import the completed file through the
existing Asset path so timeline, save/load, preview and export reuse normal
media behavior. Do not run expensive inference synchronously in playback or
call spatial sharpening neural super-resolution.

ONNX Runtime is a viable shared C++ integration candidate. Its official mobile
instructions cover Android/iOS; CPU/XNNPACK provides a baseline, with CoreML on
iOS and available Android execution providers evaluated against the actual
model. Availability is not evidence that every operator runs accelerated or
that performance is acceptable. [Official mobile deployment](https://onnxruntime.ai/docs/tutorials/mobile/)

The CoreML provider requires iOS 13+, can use CPU/GPU/Neural Engine, and has
platform-specific packaging/build requirements. It is independent of Aurea's
Metal renderer; routing inference through CoreML does not imply a new renderer
backend. [CoreML provider](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)

ncnn is an alternative native C++ runtime with mobile CPU optimization and
Vulkan acceleration. It has Android/iOS build support, but its advertised GPU
backend is Vulkan, not native Metal. An iOS GPU route would need additional
integration such as MoltenVK; a CPU fallback still requires measurement.
[ncnn upstream](https://github.com/Tencent/ncnn)

## Models and licensing

The official Real-ESRGAN model zoo includes true 2x/4x RRDB models and the
smaller `realesr-general-x4v3`. The compact general model is an appropriate
candidate for a bounded-memory first investigation; it is not a guarantee of
better detail or real-time performance. [Official model zoo](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md)

The official compact general configuration is 64 features, 32 body convolutions
and 4x pixel shuffle. The first/last convolutions make 34 spatial 3x3 layers;
from that architecture, a 34-input-pixel halo is a conservative way to compare
independent interior tiles against whole-image inference. This is an inference
from the network structure and must be tested numerically. [Model configuration](https://github.com/xinntao/Real-ESRGAN/blob/master/inference_realesrgan.py), [architecture](https://github.com/xinntao/Real-ESRGAN/blob/master/realesrgan/archs/srvgg_arch.py)

For the compact model, a 2x output can be produced by neural 4x followed by
resampling, as the official helper does for alternative output scales. That
must not be described as inference with a native 2x model. The official RRDB
2x checkpoint is available if that behavior is required instead. Official
tiling uses overlapping padded input and crops the output margins; copying
only disjoint input tiles would introduce seams. [Official inference helper](https://github.com/xinntao/Real-ESRGAN/blob/master/realesrgan/utils.py)

Real-ESRGAN publishes a BSD-3-Clause license. ncnn publishes BSD-3-Clause plus
listed third-party notices. Preserve notices with any vendored code/model
package and record the precise checkpoint source/version/hash; this audit did
not acquire or redistribute weights. [Real-ESRGAN license](https://github.com/xinntao/Real-ESRGAN/blob/master/LICENSE), [ncnn notices](https://github.com/Tencent/ncnn/blob/master/LICENSE.txt)

The upstream ONNX exporter targets the larger RRDB architecture; it cannot be
used unchanged for the compact SRVGG checkpoint. Conversion must instantiate
the matching architecture, preserve pretrained weights and compare output
against upstream reference inference before mobile integration. [Official exporter](https://github.com/xinntao/Real-ESRGAN/blob/master/scripts/pytorch2onnx.py)

## Smallest complete user-facing increment

1. Pin a real trained checkpoint and its license/provenance/hash. Convert it
   reproducibly, retain reference inputs/outputs and verify operator support.
2. Add the native runtime on Android and iOS, selecting an actually available
   execution provider and falling back explicitly when necessary.
3. Process SDR video off the render/UI threads, tile with verified overlap,
   bound all buffers, respond to cancel between tiles and remove partial files.
4. Preserve presentation times, duration, rotation/crop and audio alignment;
   negotiate output dimensions/codec against real encoder capabilities. Do not
   silently feed HDR/PQ content to an SDR model or mislabel transformed output.
5. Add real progress/cancel/import UI on both platforms only when the service
   works; import the finished media as a new asset so reopening is reliable.
6. Validate real video frames against reference inference, tile seams, audio
   sync, cancellation, save/reopen/export and memory/time on representative
   phones. Independent frame processing also needs temporal-flicker review.

A native inference prototype alone does not satisfy the full video feature.
A complete incremental implementation is technically viable, but cannot be
claimed delivered from the current dependency-free state or from a Windows
host benchmark. iOS native build/device verification remains unavailable in
this environment. No throughput/quality benchmark was performed by this audit.
