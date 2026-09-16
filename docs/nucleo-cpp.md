# Nucleo C++ do Aurea — arquitetura e estado (16/09/2026)

Pedido do dono: Flutter fica com a UI; o trabalho pesado (video, composicao,
efeitos, animacao, 3D, preview, exportacao) vai para um nucleo C++ em
Metal/Vulkan. Sem mudar a interface, sem migracao as cegas.

## O que a auditoria mostrou

- O palco reconstroi a arvore inteira a cada quadro na thread da UI; texto
  animado faz layout por letra; estilos de camada duplicam o conteudo; blend e
  mascara tiram snapshots.
- A exportacao e o pior caminho: video extraido para PNG pelo FFmpeg e
  decodificado de volta; composicao lida da GPU; 5+ copias por quadro; RGBA
  para NV12 pixel a pixel em Kotlin/Swift; render e codificacao em serie.
- `native/` (aurea_native) e codigo morto (stubs; nao compila no iOS).
- O caminho de build que funciona nas tres pontas (iOS no CI, Android release,
  testes no Windows) e o **build hook do Dart** (`native_toolchain_c`), como
  `aurea_timecore`, `aurea_meshopt`, `aurea_tracker2`.

## Arquitetura alvo (da pesquisa com fontes)

```
Dart (thread principal)  UI ── Texture(id) ◄──────────── raster amostra sem copia
  EngineClient: submit(buf.address, folha) · poll(stats, folha) · listener (raro)
════════════════ FFI (sem isolates, sem JSON) ══════════════════════════════
C++  Fila de comandos (SPSC) → thread do documento → ProjectSnapshot imutavel
     Avaliador(snapshot, t) → RenderGraph (o MESMO para preview e exportacao)
     Relogio (audio mestre)   Render (dono do contexto GPU)
        2D: Impeller standalone (mesmo renderizador, mesmos .frag do impellerc,
            mesmo SkParagraph) — plano B: Skia Ganesh com impellerc->SkSL
        3D: Filament + gltfio + ufbx (FBX), device proprio, quadros por
            IOSurface/AHardwareBuffer
     Midia: VideoToolbox/AVAssetReader | AMediaCodec→AImageReader(AHB)
     Saidas: iOS IOSurface BGRA → FlutterTexture.copyPixelBuffer
             Android SurfaceProducer → ANativeWindow → swapchain Vulkan
             Exportacao: AVAssetWriter | MediaCodec input Surface + AMediaMuxer
```

Rejeitados para 3D: OGRE-Next (Android so Vulkan, glTF para 2.1), bgfx (sem
importador), Diligent (Metal pago), The Forge (glTF offline), LibGodot
(mobile experimental), wgpu (interop so em Rust).

## Estagios (cada um atras de chave, com volta automatica)

| Estagio | Escopo | Portao |
|---|---|---|
| S0 | Ponte, telemetria, harness de quadros de referencia | goldens estaveis; FFI p99 < 50 us |
| S1 | Decodificacao nativa + cache de quadros, como Texture no compositor ATUAL | scrub p95 < 100 ms aprox./< 300 ms exato; perdas < 1% |
| S2 | Avaliador C++ em sombra contra o Dart | 0 divergencias no corpus |
| S3 | Compositor nativo v1 (video, imagem, transform, mascara, blends) | diff <= 2/255 fora de bordas; p95 <= 12 ms no iPhone 13 |
| S4 | Exportacao nativa do mesmo grafo | exportacao = golden do preview |
| S5 | Efeitos + blends proprios | testes por efeito em Metal, Vulkan, GLES |
| S6 | Texto e formas | goldens de texto (RTL, emoji, fallback) |
| S7 | 3D por Filament | goldens 3D; 30 min sem reset de GPU/jetsam |
| S8 | Padrao ligado; Flutter so para GLES/lista negra | 2 betas em paridade |

## Feito nesta sessao

- **Bancada A-E** (`bancada_do_nucleo.dart`, tela de estresse "Bancada A-E"):
  o "antes" no aparelho, com UI x raster, quadros perdidos, travadas, scrub e
  ciclos de memoria. Linha de base do PC em `test/bancada_do_nucleo_test.dart`.
- **`packages/aurea_core`** (build hook): primeira fatia, o codificador de video
  do Android em C++ (AMediaCodec + AMediaMuxer, fila de 2 quadros numa thread
  nativa, quadro por FFI, NV12/I420/mapa MediaImage2), conectado ao
  `PlatformEncoder` real atras de `PlatformEncoder.nucleoLigado` (DESLIGADO).
  - PC: conversao de cor identica byte a byte ao Kotlin.
  - Emulador (API 34, Codec2 de software): o `AMediaCodec_configure` e recusado
    com os formatos 21, 19 e Flexible, com os mesmos parametros que o Kotlin
    usa com sucesso; o fluxo volta ao Kotlin sem quebrar a exportacao. Causa
    ainda nao encontrada. Proximo passo: testar em aparelho com codificador de
    hardware e, se o Codec2 continuar recusando, trocar a entrada para
    `AMediaCodec_createInputSurface` (API 26), que e tambem o caminho do S4.
  - `integration_test/nucleo_codificador_test.dart` compara os dois caminhos e
    exige que o nucleo tenha sido de fato usado.

## Proximo

1. Relatorios da Bancada A-E no iPhone 17, iPhone 13 e Android (o "antes").
2. Destravar o codificador do nucleo em aparelho (ou Surface de entrada).
3. S0/S1: ponte de comandos + decodificacao nativa como Texture.
