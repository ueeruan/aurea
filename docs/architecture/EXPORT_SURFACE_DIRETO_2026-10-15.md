# Export direto na Surface do encoder (surface-direct) — 2026-10-15

Branch `claude/120-surface`. Três commits:

1. `408d3772` — escada de encoders na 1.2.0 (surface → buffer → legacy → software).
2. `abbddd0f` — a escada guarda o PCM do período de teste e ainda troca de degrau depois que o som entrou.
3. este — degrau `surface-direct`, **desligado por padrão**.

## O problema

No degrau `surface` o quadro faz GPU → CPU → GPU: o Vulkan converte para NV12
(`yuv_encode.frag`), o readback traz os planos para a CPU, o alimentador GLES
copia para a heap, sobe com `glTexSubImage2D`, converte NV12 → RGB num shader e
desenha na Surface do encoder. No moto g52 a conversão + readback eram 22,7 dos
28,3 ms de GPU por quadro (medição anterior).

## Desenho

Referência de comportamento: `build/reference/am-render-20261014/SPEC-RENDER.md`
(render direto na Surface do encoder, PTS = índice/fps por
`eglPresentationTimeANDROID`, alvo de 8 bits). Nenhum código, shader ou asset
de outro app foi usado.

- **Motor** (`Engine.cpp`): com `EngineConfig::enableDirectSurfaceExport` (ou
  `adb shell setprop debug.aurea.enc.direct 1`), sem upscale de IA e com
  `externalMemoryHardwareBuffer`, cada slot do export ganha um alvo RGBA8 com
  memória de AHardwareBuffer (`GPUBackend::create_shared_render_target`). O anel
  é o próprio conjunto de slots (2–4, a política de memória do aparelho manda;
  nada novo para 4 GB). Faltou um alvo = nenhum, e o export segue no NV12.
- **Vulkan** (`VulkanResources.cpp`): `AHardwareBuffer_allocate`
  (GPU_COLOR_OUTPUT | GPU_SAMPLED_IMAGE) → `VkImage` com
  `VkExternalMemoryImageCreateInfo` + import dedicado. A textura é a dona da
  referência e a solta depois do fence (regra de posse de sempre).
- **Renderer**: passe `export-rgb` (`shaders/export/rgb_encode.frag`) com a
  MESMA curva do `yuv_encode.frag` (`linear_to_srgb`, `texelFetch`, dither de
  1/2 LSB). O encoder do aparelho faz R'G'B' → Y'CbCr BT.709 de faixa limitada
  pelas etiquetas do MediaFormat (as mesmas do degrau `surface`).
- **Sink** (`FFmpegExportSink.cpp`): degrau `surface-direct` acima de
  `surface`. O motor entrega o AHardwareBuffer do quadro em
  `set_direct_surface_frame` antes de cada `write_video`.
- **Encoder** (`AndroidVideoEncoder.cpp`): o alimentador GLES importa cada
  buffer do anel uma vez como EGLImage (`eglGetNativeClientBufferANDROID` +
  `EGL_NATIVE_BUFFER_ANDROID`, com acquire próprio até o teardown), desenha um
  triângulo na Surface, `eglPresentationTimeANDROID(pts)` e swap. Sincronia: o
  motor já espera o fence do Vulkan antes de passar o slot ao encoder; depois do
  swap o alimentador faz `glFinish` (dentro do prazo dele) para o GLES ter
  terminado de ler antes de o motor reusar o slot. Sem fence fd entre APIs.
- **Período de teste**: enquanto o 1o pacote não é conferido, o NV12 continua
  sendo gerado e lido. Ele alimenta a checagem do 1o quadro (decode do pacote ×
  NV12) e os quadros guardados. Confirmado o degrau, o motor liga
  `OffscreenTarget::rgbOnly`: os passes NV12 e o readback param.

## Quedas (fail-safe)

`surface-direct` cai para `surface` NV12 sem mudar nada quando: falta extensão
EGL/GLES (`EGL_ANDROID_image_native_buffer`, `EGL_KHR_image_base`,
`GL_OES_EGL_image`), o EGLImage não é criado, o quadro chega sem buffer, a
abertura falha, a partida trava ou o 1o quadro é reprovado. Os quadros e o PCM
guardados vão para o degrau seguinte (`abbddd0f`). O degrau que falhou fica
marcado no processo e é pulado nos exports seguintes. Depois da confirmação não
há mais troca (como nos outros degraus).

Testes no aparelho: `debug.aurea.enc.skip=surface-direct` pula o degrau;
`debug.aurea.enc.fail=surface-direct-open|surface-direct-stall|surface-direct-verify`
força cada falha.

Diagnóstico: o campo `encoderPath` do JSON do export (anexado ao relato de bug
nas duas plataformas) mostra `surface-direct:<codec> ok` ou a falha.

## iOS

Fica no caminho atual. O export do iOS é o mesmo FFmpegExportSink com
`h264_videotoolbox`/`hevc_videotoolbox` recebendo NV12 da CPU (readback Metal),
não AVAssetWriter. O equivalente (Metal escrevendo num CVPixelBuffer do pool da
sessão VTCompressionSession, via CVMetalTextureCache) exige um sink de vídeo
próprio do iOS fora do FFmpeg e o mesmo período de teste. Não é pequeno e não
foi feito. O shader novo entra no glob do MSL, mas o iOS nunca liga o alvo
(`create_shared_render_target` devolve NotSupported fora do Android).

## O que foi medido, compilado e validado

| Item | Resultado |
|---|---|
| Host: `ExportDirectSurface` (3 testes) | ok: desligado não cria alvo; ligado, todo quadro chega com um buffer do anel, PTS exato, readback 6 cópias em 12 quadros (24 sem o degrau), alvos destruídos no fim; sem alvo, NV12 intacto |
| Host: `FFmpegExport.LadderFallbackAfterAudio…` | ok: 1o degrau reprovado no quadro 3 com som; 15 quadros na ordem, relógio exato e áudio estéreo contínuo no decode independente |
| Host: filtro `Export` | sem falha nova sobre a base (falhas restantes são da base ou de tempo, passam isoladas) |
| Host: `OfflineNativeGpu`, `ExportTransferDeadline` | ok |
| Android: `compileDebugKotlin` + nativo arm64 | ver relatório do commit |
| Android: `testUiTestUnitTest` x86_64 | ver relatório do commit |
| iOS: 5 checagens estáticas | 0 problemas |

**ms/quadro antes e depois: não medido.** O emulador local não sobe nesta
máquina (Hyper-V sem Windows Hypervisor Platform); a matriz fica com o CI do
GitHub.

## Não validado

- Nada do `surface-direct` rodou em Android: EGLImage de AHardwareBuffer,
  `eglPresentationTimeANDROID`, cor do arquivo (±1–2 níveis contra o NV12),
  ganho de tempo, comportamento em GPU Mali/Adreno/PowerVR.
- A escada (`408d3772`/`abbddd0f`) também só rodou no host.
- iOS: só checagens estáticas.

Por isso o degrau fica **desligado por padrão**. Para ligar na release: rodar a
matriz (vídeo simples, multicamada, efeitos, motion blur, 3D, projeto antigo)
com `debug.aurea.enc.direct=1`, conferir quadros/PTS/dimensões/cor com ffprobe e
medir ms/quadro contra o degrau `surface`.
