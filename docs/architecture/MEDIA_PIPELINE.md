# Pipeline de mídia

Estado: **implementado para vídeo e imagem** (Fases 2 e 3). Áudio e export
ainda não existem.

## Visão

```
Importar ─► VideoSourceFactory::probe ─► Asset + Layer (motor)
Preview  ─► MediaManager (uma fonte por layer) ─► VideoSource (thread de decode)
                                                  └─ VideoDecoderBackend (MediaCodec no Android)
                                                  └─ DecodedFrameCache
Renderer ─► frame_for(instante) ─► textura (zero-copy ou planos)
Timeline ─► ThumbnailService (decoder próprio, baixa prioridade)
```

## Android: `engine/platform/android/MediaCodecSource.cpp`

- **Origem**: `fd:<n>[:off:len]`, `content://…` (o motor chama
  `AureaEngine.openContentFd` pelo JNI e recebe um descritor novo — é o que
  permite reabrir o vídeo de um projeto salvo) ou caminho comum.
- **Sondagem**: `AMediaExtractor`: tamanho, rotação (`rotation-degrees`), fps
  (chave ou mediana dos intervalos das primeiras amostras), duração, perfil →
  profundidade de bits, cor (`color-standard/range/transfer`).
- **Decoder**: `AMediaCodec` (hardware primeiro; se o hardware recusa ou esgota,
  o decoder de software do sistema), prioridade tempo real, saída para um
  `AImageReader` de 12 imagens:
  - zero-copy: `AIMAGE_FORMAT_PRIVATE` + `GPU_SAMPLED_IMAGE` → `AHardwareBuffer`;
  - planos: `YUV_420_888` → NV12/NV21/I420 (layout exótico é compactado para NV12).
  O frame segura o `AImage` e o dono do leitor por referência: o leitor só morre
  quando o último frame em voo na GPU é solto.
- **Cor**: a do bitstream (formato de saída do codec) vence a do container. Sem
  metadado, BT.601 abaixo de 720 linhas e BT.709 acima — nunca "tudo BT.709
  limitado". A rotação do container é aplicada no shader (a chave é zerada no
  `configure` para o codec não marcar o buffer).
- **Segundo plano**: `suspend` devolve o codec ao sistema; `resume` recria.

## Motor: `VideoSource` (`src/media/VideoSource.cpp`)

Uma thread por fonte, com UM pedido pendente (`DecodeRequest`: instante, modo,
direção, velocidade):

- **Coalescência**: pedido novo substitui o anterior ainda não atendido; se o
  alvo novo está à frente e ao alcance de um GOP, a thread segue andando em vez
  de fazer seek (`forwardRetargets`).
- **Scrub**: frames intermediários são decodificados e descartados sem render;
  arrastando para a frente, deixa dois frames prontos adiante.
- **Playback**: mantém de 3 a 4 frames à frente do playhead (prefetch).
- **Still**: o frame exato.

`DecodedFrameCache`: orçamento por quantidade e bytes; despejo temporal (frames
atrás da direção do movimento custam 3×). O cache segura no máximo
`imagens do leitor − 5` (sobram os frames em voo na GPU e uma folga para o codec
não travar). `FrameRef` é contagem de referência intrusiva; o renderer solta o
frame depois que a GPU termina (`defer_until_gpu_done`).

`MediaManager`: uma fonte por layer (duplicar uma layer abre outra fonte),
coleta das ociosas, `suspend_all/resume_all`, estatísticas agregadas (seeks,
coalescidos, descartados, decode médio, último seek, decoder e se é hardware).

## Reprodução (`src/playback/Playback.cpp`)

`PlaybackClock` com `MasterClock` trocável (o relógio de áudio entra aqui quando
existir), `PlaybackController` (play, pause, toggle, seek, scrub begin/move/end,
passo, loop, velocidade) e `FrameScheduler` (frames perdidos). O modo do
controller decide o modo do decode.

## Miniaturas (`src/media/ThumbnailService.cpp`)

Thread própria em prioridade de fundo, decoder próprio sempre em modo CPU,
quadro-chave mais próximo (baldes de 250 ms), conversão YUV→RGBA8 reduzida com a
matriz/faixa e a rotação do vídeo, cache LRU de 900 miniaturas, fila com o pedido
mais recente primeiro. A UI pede por `query_thumbnail` e redesenha quando
`thumbnailGeneration` muda no status.

## Imagens

A plataforma decodifica (JPEG/PNG/HEIC) em RGBA8 com alfa reto e entrega ao
motor com a origem (`import_image(…, sourcePath)`). O projeto salva a origem; ao
reabrir, o motor pede os pixels de volta pelo `EngineConfig::imageLoader`
(JNI → `AureaEngine.decodeImage`).

## Importação de vídeo

`Engine::import_video`: sonda fora do lock, cria asset e layer no topo; o
primeiro clipe faz a composição adotar tamanho (teto do aparelho aplicado com UMA
escala, preservando a proporção), fps e duração; a layer é ancorada no centro e
encaixada.

## Fora do escopo atual

Decodificação de áudio (waveforms, relógio de áudio), export, proxies.
