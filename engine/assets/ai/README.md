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

# Mapa de profundidade (MiDaS v2.1 small) — CONVERSÃO NOSSA

**Estes bytes NÃO vêm de um release ncnn oficial.** Não existe modelo de
profundidade publicado para ncnn (nem pela Tencent, nem por nihui). Os arquivos
`midas-v21-small-256.param` e `midas-v21-small-256.bin` foram gerados por nós, a
partir do ONNX oficial do MiDaS, pela receita abaixo. Qualquer pessoa com as
mesmas ferramentas chega aos mesmos hashes (a receita rodou duas vezes aqui e
deu os mesmos bytes).

## Origem

- Modelo: MiDaS v2.1 small (Intel ISL), licença **MIT** — texto em
  `LICENSE-MiDaS.txt` (também no texto de licenças do app).
- Arquivo: https://github.com/isl-org/MiDaS/releases/download/v2_1/model-small.onnx
- Tamanho: 66.764.249 bytes.
- SHA-256: `2d8c6cb8f415229daf1eb041024208e2608c9f98e17c81cc7c6ecb449c56fd58`
- Exportado pelo PyTorch 1.6, opset 10. Entrada `0` [1,3,256,256] RGB 0..1
  NCHW; o próprio grafo subtrai a média (0,485/0,456/0,406) e divide pelo
  desvio (0,229/0,224/0,225). Saída `797` [1,256,256]: disparidade relativa,
  sempre ≥ 0, **maior = mais perto**, sem escala nem deslocamento definidos.

## Receita (`tools/ai_depth_convert.py`)

Ferramentas: Python 3.12 com `onnx==1.23.0` e `pnnx==20250430` (pip; o pnnx
mais próximo do ncnn fixado, 20250503), e o `ncnnoptimize` compilado da árvore
ncnn fixada `305837fd4a722ebc47c5d72e72d8ec9ae970e932` (a mesma do motor):

```
cmake -S <ncnn-305837fd…> -B ncnn-tools -DNCNN_VULKAN=OFF -DNCNN_OPENMP=OFF -DNCNN_BUILD_TOOLS=ON -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF -DNCNN_SHARED_LIB=OFF
cmake --build ncnn-tools --config Release --target ncnnoptimize
python tools/ai_depth_convert.py model-small.onnx <saida> --pnnx pnnx --ncnnoptimize ncnn-tools/tools/Release/ncnnoptimize.exe
```

O script faz, nesta ordem:

1. **Devolve ao Resize a semântica do treino.** O opset 10 não sabe dizer
   `align_corners`: o export do PyTorch gravou os cinco Resize "linear" no modo
   assimétrico do opset 10, que não é o que a rede faz no treino (as quatro
   fusões usam `align_corners=True`; a cabeça, `False` — código do MiDaS). O
   grafo sobe para o opset 11 (`onnx.version_converter`) e cada Resize ganha o
   `coordinate_transformation_mode` do treino: os quatro primeiros
   `align_corners`, o último `half_pixel`. **Nenhum peso muda.** Conferido no
   onnxruntime: o mesmo grafo com o modo assimétrico reproduz o ONNX oficial
   com erro 0,0 — a única diferença é a semântica do Resize. Isto também é o
   que o ncnn consegue representar: o `Interp` bilinear do ncnn conhece
   `align_corners` e `half_pixel`, não o assimétrico. SHA-256 do ONNX
   intermediário: `e4341cea74e2588b1ebb22f6be2384b6df4cdfcac7d2a354ec598f81f51a933e`.
2. `pnnx midas_small_train.onnx inputshape=[1,3,256,256] fp16=0 device=cpu`.
3. **Corrige o pnnx 20250430:** ao fundir o `Pad` [topo 1, esquerda 1, baixo 2,
   direita 2] nas duas depthwise 5×5 de passo 2 (`convdwclip_4`,
   `convdwclip_17`), ele grava o recuo do topo como 0 (`14=0`). O script põe
   `14=1`. Sem isto, a altura do encoder cai de 8 para 7 e o mapa sai
   deslocado (erro médio de 2% do intervalo contra o onnxruntime).
4. `ncnnoptimize <param> <bin> midas-v21-small-256.param midas-v21-small-256.bin 65536`
   — pesos em **fp16** (33,2 MB). INT8 não serve: o ncnn não tem kernels int8
   no Vulkan.

## Saída

| Arquivo | Bytes | SHA-256 |
| --- | --- | --- |
| midas-v21-small-256.param | 15.127 | 25d14e8124e5c7861773724722a6782254f1fecd9182893455924c61e66369e4 |
| midas-v21-small-256.bin | 33.209.056 | f25da337508b8d518e5ac3fca6a577438b77b797c6f95a51433b814d5f1853f7 |

Grafo: 73 Convolution, 24 ConvolutionDepthWise, 29 BinaryOp, 27 Split,
7 ReLU, 5 Interp, 2 MemoryData, 1 Squeeze. Blobs `in0` → `out0` [256,256].

## Conferência (foto `android/app/src/main/assets/previa_efeitos.jpg`)

Contra o onnxruntime 1.30.0 no ONNX da etapa 1, mesma entrada 256² (erro
relativo ao intervalo min..max da referência):

| | máx | médio |
| --- | --- | --- |
| ncnn fp32 (antes do ncnnoptimize) | 3,3e-6 | 1,9e-7 |
| ncnn, pesos fp16 (o embutido) | 4,1e-3 | 3,8e-4 |
| motor, CPU (`DepthEstimator`, entrada do próprio motor) | 6,2e-3 | 4,3e-4 |
| motor, Vulkan (RTX 3050) | 6,2e-3 | 4,3e-4 |

CPU × Vulkan no motor: 4,6e-6 do intervalo. Contra o ONNX **oficial** (Resize
assimétrico) a diferença é de semântica, não de conversão: 3,8e-2 médio.

## No motor

`src/ai/DepthEstimator.cpp`: um worker, sem OpenMP, fp32 na conta (os pesos
fp16 viram fp32 no carregamento), Vulkan no Android/host com volta para a CPU,
CPU na Apple. Winograd ligado só na CPU (~40% mais rápido, mesmo resultado).
No Vulkan, `use_shader_local_memory = false`: os kernels com memória local do
ncnn fixado derrubam o dispositivo (`VK_ERROR_DEVICE_LOST`) na 1×1 816→232 do
fim do encoder (visto numa RTX 3050). O `Squeeze` final não tem kernel Vulkan
e roda na CPU (é só uma troca de forma de 256×256).
Tempo por quadro medido no host: ~80 ms na CPU (Ryzen 5 5500, 1 thread),
~15–24 ms no Vulkan (RTX 3050).

Embutido uma vez no núcleo: Clang/GCC (Android, iOS) pelo `.incbin` do
montador (`cmake/AiDepthModel.incbin.cpp.in`, sem array C++ gigante); MSVC
(host de testes) por tabela hexadecimal gerada (`cmake/EmbedDepthModel.cmake`).
