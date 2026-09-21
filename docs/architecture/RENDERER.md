# Renderer

Estado: **implementado** (Fase 2). Este documento descreve só o que existe no
código; o que ainda não existe está na seção "Fora do escopo atual".

## A regra

Nada acima de `GPUBackend` (`engine/include/aurea/render/GPUBackend.hpp`)
conhece Vulkan ou Metal. Renderer, FrameGraph e EffectGraph falam em
`TextureHandle`, `PipelineHandle`, `SamplerHandle` e `CommandList`. O backend
Vulkan (`engine/gpu/vulkan/`) é uma biblioteca separada (`aurea_vulkan`); o
núcleo (`aurea_core`) compila sem ele — os testes de timeline, animação e
serialização rodam sem GPU.

```
 Engine ── prepare (lock do modelo) ──► FrameSnapshot
        └─ render (sem lock) ──► Renderer ─► EffectGraph ─► FrameGraph ─► GPUBackend
                                                                          └─ vk::Backend
```

A plataforma cria o backend e o entrega ao motor (`EngineConfig::backend`); o
motor assume a posse. No Android isso acontece em `aurea_jni.cpp`; nos testes do
host, o mesmo `vk::Backend` roda na GPU da máquina (RTX 3050).

## Backend Vulkan

| Peça | Arquivo | O que faz |
|---|---|---|
| Loader | `VulkanLoader.*` | Carrega `libvulkan.so`/`vulkan-1.dll` em runtime (`VK_NO_PROTOTYPES`); tabelas de funções por X-macro. |
| Dispositivo | `VulkanDevice.cpp` | Exige Vulkan 1.1. Extensões: swapchain, superfície Android, `VK_ANDROID_external_memory_android_hardware_buffer`, `queue_family_foreign`, conversão YCbCr. Camada de validação só em debug e só se instalada. Preenche `GPUCapabilities` (um struct, sem checagens espalhadas). |
| Memória | `VulkanCommon.cpp` (`MemoryAllocator`) | Blocos de 64 MB (device) e 16 MB (host), sub-alocação first-fit com coalescência e alinhamento a `bufferImageGranularity`; alocação dedicada acima de meio bloco. |
| Anéis por frame | `HostRing` | Uniforms (256 KB, UBO dinâmico) e staging (4 MB) por frame em voo — nada é alocado durante o playback. |
| Frames em voo | `FrameContext` ×3 | Fence, semáforo de aquisição, pools de descritor por frame, query pool de timestamps, fila de destruição adiada. `BackendConfig::framesInFlight` limitado a 1..3. |
| Swapchain | `VulkanSwapchain.cpp` | FIFO, pré-rotação (a imagem sai girada no shader, o compositor do sistema não gasta um passe), ≥3 imagens, recriação por `OUT_OF_DATE`/`SUBOPTIMAL`/resize. No Android a extensão vem do `surfaceChanged` (o `currentExtent` fica um passo atrás quando o layout muda e a imagem sairia esticada). |
| Recursos | `VulkanResources.cpp` | Texturas, buffers, samplers, shaders, pipelines; destruição adiada até a GPU terminar (`defer_until_gpu_done`); caches de render pass, framebuffer e pipeline layout (um por sampler imutável). |
| Comandos | `VulkanCommands.cpp` | `CommandList` (barreiras explícitas vindas do FrameGraph, render pass, bind, push constants, draw/dispatch, timers, labels), begin/end de frame, submit esperando em `COLOR_ATTACHMENT_OUTPUT`, present. |

Layout universal de descritores (set 0): bindings 0–3 `sampler2D`, 4 UBO
dinâmico, 5–6 imagens de storage, 7 SSBO; push constants de 128 bytes. Render
passes com layout inicial = final `COLOR_ATTACHMENT_OPTIMAL` e dependências
externas; as transições são as barreiras planejadas pelo FrameGraph.

Cache de pipeline: `VkPipelineCache` persistido em
`<cacheDir>/aurea_pipeline_cache.bin` (gravação atômica por rename) no
`suspend` e no `shutdown`. O `ShaderLibrary` pré-aquece os pipelines na
inicialização e conta compilações depois de `mark_steady_state` (chamado após o
primeiro frame): compilar pipeline durante o playback vira aviso no log.

Perda de dispositivo: `is_device_lost` → `Engine::recover_device_locked`
recria backend e renderer e reabre as fontes de mídia.

## Zero-copy de vídeo (Android)

`MediaCodec → AImageReader (PRIVATE, GPU_SAMPLED_IMAGE) → AImage →
AHardwareBuffer → VkImage`. A importação (`import_external_image`):

- usa o **formato externo** do buffer (`VkExternalFormatANDROID`) e uma
  `VkSamplerYcbcrConversion` por (formato, matriz, faixa);
- a matriz e a faixa são **as do arquivo** (`ExternalImageDesc::matrix/fullRange`
  vindos de `VideoColorInfo`), nunca a sugestão do gralloc — muitos aparelhos, e
  o emulador, sugerem BT.601 cheio para qualquer vídeo;
- o componente de mapeamento e os offsets de croma são os sugeridos pelo driver;
- a amostra sai em RGB não linear; o shader `video/yuv_external.frag` só aplica
  curva de transferência, primárias e tone map;
- buffers que já são RGB (formato comum ou externo com sugestão `RGB_IDENTITY`)
  usam sampler sem conversão;
- a VkImage é cacheada por `AHardwareBuffer*` (o ImageReader recicla um
  conjunto fixo): em regime, importar custa zero; aquisição da fila FOREIGN com
  layout `UNDEFINED` e devolução no fim do frame.

Quirk conhecido: o emulador (gfxstream) anuncia as extensões mas amostra o
buffer YUV externo como bytes crus. Lá (`ro.boot.qemu`/`ranchu`) o motor usa os
planos pela CPU. **O caminho zero-copy ainda não foi validado em aparelho real.**

Fallback (sem importação de AHB ou no emulador): planos NV12/NV21/I420/P010 do
`AImageReader YUV_420_888` sobem por textura (uma por plano, persistentes por
layer) e o shader `video/yuv_planar.frag` faz a conversão com a mesma
matemática (`common/color.glsl`).

## Renderer (`src/render/Renderer.cpp`)

**prepare** (sob o lock do modelo, sem GPU): percorre a ordem da composição,
avalia transform e opacidade pelas trilhas (sem mexer no modelo; inclui a
cadeia de pais), pede os frames de vídeo ao `MediaManager` (modo Still, Scrub ou
Playback), sobe imagens, planeja os efeitos (`EffectGraph::plan`) e monta o
`FrameSnapshot`.

**render** (sem lock): `begin_frame` → uploads → por layer, a fonte (vídeo
zero-copy/planar, imagem por `rgba_to_linear`, sólido por clear, 1×1 quando não
tem efeito) → `EffectGraph::build` → UM passe de composição com blend de
hardware (Normal e Add) e push constants → passe de saída para o swapchain
(letterbox, pré-rotação, dither) → `compile`/`execute` do FrameGraph →
`end_frame`. Frames de vídeo usados são soltos com `defer_until_gpu_done`.

Espaço de trabalho: linear BT.709, pré-multiplicado, RGBA16F. SDR entra e sai
pela curva sRGB (os códigos fazem ida e volta sem mudar). HDR: PQ/HLG com branco
de referência de 203 nits e tone map de Reinhard na luminância.

## Preview adaptativo e ritmo

- Escala do preview FULL, 1/2, 1/4, 1/8 ou AUTO sem mudar coordenadas lógicas.
  AUTO: `AdaptiveResolutionController` pelas métricas reais do frame (orçamento
  16,67 ms ou 33,33 ms). Por layer, a densidade de texel é a potência de dois
  acima da escala na tela × fator do preview.
- O preview tem a **sua** thread de render (`Engine::render_thread_main`),
  acordada por comando, frame novo do decoder ou mudança de superfície. Com
  `render_frame(onlyIfChanged)` ela só redesenha quando o frame do playhead
  muda, chega comando/superfície nova ou chega um frame de vídeo que faltava —
  um vídeo de 30 fps redesenha 30 vezes por segundo, não na taxa do painel.
  Entre frames ela dorme até o próximo frame devido.

## Outras saídas

- `render_offscreen(target, w, h)`: o instante atual numa textura, esperando os
  frames EXATOS de vídeo (base do export e dos testes visuais).
- `capture_frame_rgba(maxDim, …)`: o frame em RGBA8 sRGB (miniatura do projeto
  na Home).

## Shaders

`engine/shaders/{common,video,composite,effects}/`, compilados para SPIR-V no
build pelo `glslc` do NDK (`cmake/AureaShaders.cmake`), embutidos no binário
(`ShaderId` gerado + blobs). Dependência nos `.glsl` incluídos: mudar um include
recompila quem o usa.

## Testes

`engine/tests/test_gpu.cpp` roda o backend Vulkan real: testes analíticos
(exposição, saturação, pilha fundida, níveis, curvas, tinta, energia/simetria do
blur, glow, nitidez, NV12 BT.709 limitado e BT.601 cheio, ida e volta SDR),
golden frames (transform, gaussian blur, glow, exposure, motion tile —
`engine/tests/golden/`, regerar com `AUREA_UPDATE_GOLDENS=1`), playback estável
sem criar textura, captura RGBA e o motor ponta a ponta com vídeo sintético.

## Fora do escopo atual

Metal (iOS), export de vídeo (reusará `render_offscreen`), modos de mesclagem
além de Normal/Add, 3D, texto e formas vetoriais.
