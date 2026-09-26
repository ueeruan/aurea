# Embedded anime super-resolution model

The two parameter graphs and the shared trained weights are unmodified files
from the official Real-ESRGAN release `v0.2.5.0`:
https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip

Archive SHA-256: `abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d`.

| File | SHA-256 |
| --- | --- |
| realesr-animevideov3.bin | 548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d |
| realesr-animevideov3-x2.param | b88ff4f00ebf019a7fdac17fdd45a7fd3665d37509efc5baf2e4da2e24420a04 |
| realesr-animevideov3-x4.param | 850a248e7c14c27e5bd8cf7265113a9441036a7db63963bb8aa5169d788a435e |

Upstream's x2/x3/x4 `.bin` files are byte-identical. We store that 1,247,368-byte
file once under the shared name above. The x4 graph performs trained 4x neural
inference. The x2 graph is the same trained 4x network followed by upstream's
bicubic reduction, not a separately trained native 2x model.

This is **animevideov3**, intended for animation/illustration. It is not a
general photoreal restoration claim. Conservative temporal luma stabilization
now reduces changes in stationary detail and rejects detected motion, cuts,
fades and discontinuities. It is not a learned temporal model and cannot
guarantee elimination of flicker. Inputs must be SDR RGB; do not feed PQ/HLG without appropriate
conversion. No conversion or retraining of weights was performed here.

The network contains 18 padded spatial 3x3 convolutions, PReLU, pixel shuffle,
nearest-neighbor residual, sum, and (x2 only) bicubic resize. Aurea uses a
20-input-pixel tile halo and crops each output tile to its disjoint interior;
the whole-image/tiled test verifies seams and image edges numerically.

Runtime: Tencent/ncnn commit `305837fd4a722ebc47c5d72e72d8ec9ae970e932`
(20250503), Vulkan on Android/host, CPU fallback and CPU on Apple builds;
OpenMP disabled, one inference worker. The final x2 bicubic operation runs on
CPU because its pinned Vulkan implementation failed pixel parity tests. Neural
convolutions remain on Vulkan. Software Vulkan devices are excluded from Auto.
`AUREA_AI_VULKAN=OFF` builds the CPU-only path. CMake pins its
archive hash. Model bytes are embedded once in the shared core so Android and
iOS use the same weights and graphs without an external model download.

Licenses are preserved in `LICENSE-Real-ESRGAN.txt` and `LICENSE-ncnn.txt`.
The Vulkan shader compiler is nihui/glslang revision
`a9ac7d5f307e5db5b8c4fbf904bdba8fca6283bc`, with its archive hash pinned in
CMake and license shipped in Android assets/licenses/LICENSE-glslang.txt.
Primary sources: https://github.com/xinntao/Real-ESRGAN,
https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan,
https://github.com/Tencent/ncnn.
