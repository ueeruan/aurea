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

## 17/09 — RenderCore: fundacao, e o Vulkan no aparelho

Pedido do dono: o mesmo, executado. `packages/aurea_render` (build hook do
Dart, C++20) com a primeira fatia de verdade atras da UI.

**O que existe.** Fila de comandos sem trava entre UI e render; relogio de
quadro com p50/p95/max e orcamento; gerenciador de recursos com dono, alca,
orcamento em bytes e despejo LRU (300 quadros, uma alocacao de alvo);
gerenciador de shaders com cache, pre-aquecimento e deteccao de fonte
trocada; avaliador de timeline que e copia fiel do `AnimatedDouble.valueAt`
do Dart; compositor 2D com ancora, escala, rotacao, opacidade, oito modos
de mistura em Porter-Duff e supermuestreamento, sobre um backend de
REFERENCIA em CPU (o gabarito do Metal e do Vulkan que vierem); nucleo com
thread propria e qualidade adaptativa por degraus.

**A ponte.** `api.cpp`: todo simbolo fecha o corpo num `catch (...)`, as
estatisticas saem num vetor de `double` (struct com `double` e `int`
misturados tem preenchimento, e preenchimento faz os dois lados lerem o
campo do vizinho). Publicar 20 camadas custa p50 0,012 ms / p99 0,088 ms.

**A REGRA DO ZERO-COPY VIROU CODIGO.** `ler_pixels` RECUSA num nucleo
aberto com thread propria — que e o de producao. Comentario nao impede
nada; a linha impede.

**O QUE AINDA NAO EXISTE, E O RELATORIO DIZ.** Video, texto, efeitos, 3D e
particulas nao estao no compositor. O backend de GPU nao esta escrito:
`abrir` RECUSA Vulkan em vez de devolver um motor que diz ser o que nao e —
um nucleo que se diz de GPU e compoe na CPU seria a camada falsa sobre o
renderizador antigo.

### V0 — o dispositor, medido no emulador

`flutter test integration_test/nucleo_vulkan_test.dart -d emulator-5554`:

```
Vulkan 1.2.0 | SwiftShader Device (LLVM 10.0.0) | driver 5.0.0
1 dispositivo(s) | fila grafica: sim | swapchain: sim
textura max 16384 | AHardwareBuffer: sim
```

A sonda sobe a instancia, escolhe o dispositivo fisico, acha a familia de
fila grafica, cria o DISPOSITOR LOGICO (onde o driver real recusa) e
desce. **Isto e SOFTWARE Vulkan (SwiftShader) num emulador**: prova o
caminho da API, nao o desempenho de GPU num celular. NUMERO DE APARELHO:
NAO TESTADO — REQUER DISPOSITIVO.

### O portao que pegou, e o build do Android

- **arm64 recusou `std::jthread`**: a libc++ do NDK 28 nao o tem (nem
  `std::stop_token`). Trocado por `std::thread`; a parada ja era explicita.
  Provado: `libaurea_render.so` ELF64 AArch64, 21 simbolos, ligado contra
  `libvulkan.so`.
- **O build do Android estava quebrado, e nao era o RenderCore** (medido:
  sem o pacote, a falha e identica). O daemon do Kotlin guarda os `.tab` do
  cache incremental num mapa global e estatico, e plugins que aplicam o KGP
  por conta propria registram o mesmo caminho duas vezes. `flutter clean`
  nao resolvia porque nao era cache velho. Corrigido com
  `kotlin.incremental=false` em `android/gradle.properties`, que desliga o
  COMPONENTE que falha e nao um recurso.

### Proximo

1. V1: superficie pelo `ANativeWindow` do `SurfaceProducer`, swapchain e um
   clear na tela.
2. V2: o compositor em shader — e o unico ponto em que `abrir(backend: 2)`
   pode passar a responder sim.
3. Metal depende de uma maquina Apple: nao compila no PC do projeto.
