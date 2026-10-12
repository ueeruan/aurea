# Missão ASTRA — RenderCore do export (12/10/2026)

Base: Beta 0.1.7 (build 2180, `codex/build-015` @ 88302f0d). Branch: `claude/astra-rendercore`.
Aparelho de medição: moto g52 (Snapdragon 680 / Adreno 610, Vulkan 1.1, 4 GB, classe LOW no motor).

## 1. Como o export funciona hoje (o que já existia)

O "RenderCore" pedido já existe em boa parte no motor, com outros nomes. Antes de
escrever código novo, cada componente da missão foi conferido no código:

| Componente pedido | Onde já está | Estado |
| --- | --- | --- |
| Render graph | `render/FrameGraph` (passes declarados, transientes reaproveitados) + `effects/EffectGraph` (efeitos neutros removidos, operações de cor fundidas num passe) | existente |
| Scheduler de quadros | `AureaRenderScheduler` + `export/OfflineFrameQueue` (slots de leitura alugados, produtor GPU × consumidor encoder) | **corrigido** (seção 3) |
| Memória de GPU | `MemoryManager`, `TrackedResourceBudget`, pool de transientes, admissão por pressão/calor | existente |
| Shader/pipeline cache | `ShaderLibrary::prewarm_effects` + cache de pipeline salvo em disco (`save_pipeline_cache`) | existente |
| 3D | `SceneRenderer` (sombras, PBR, partículas, degrau de custo 3D automático no export) | existente; **bug corrigido** (seção 4) |
| Media pipeline | `OfflineFrameRenderer` (decoders próprios, sem depender da prévia), `OfflineAudioSnapshot`, `FFmpegExportSink` (MediaCodec/VideoToolbox, OpenH264/Kvazaar por software) | existente |
| Export Recovery | `ExportRecovery` existia, mas **desligado nos dois apps** e sempre recomeçava do zero | **novo** (seção 2) |
| Detector de capacidade | `DeviceCapabilities` + `DevicePolicy` (tier, calor, profundidade do pipeline, tetos) | existente |
| Fallback de CPU | encoders por software (OpenH264/Kvazaar) e modo de segurança do encoder | existente |
| Tile renderer | só em pontos (upscale em blocos de 128, superfície de texto limitada) | **não feito** |

## 2. Export retomável (novo)

Antes: qualquer interrupção perdia tudo. Pior: Android e iOS chamam
`Engine::suspend()` quando o app vai para segundo plano, e a suspensão **cancela o
export** — trocar de app no meio de um export longo jogava fora o trabalho.

Agora (`enableResumableExport`, ligado no Android e no iOS):

- `export/ExportPacketLog` — registro durável dos pacotes de vídeo ao lado da saída
  (`<saída>.aurea-pkt`): cabeçalho com SPS/PPS e a chave da sessão, cada pacote com
  checksum, `fsync` antes de cada keyframe. A varredura valida tudo e corta no
  último GOP íntegro (nunca "conserta" um pacote).
- `FFmpegExportSink` — no modo registro, o encoder escreve no registro; no fim
  `end_video` solta o encoder do aparelho, `begin_assembly` abre o MP4, o motor manda
  o áudio inteiro (a mesma mixagem do caminho direto) e `finish` monta o arquivo,
  intercalado, com validação independente como antes.
- Chave da sessão = projeto congelado **sem** o que não muda o vídeo (data de
  gravação, zoom/rolagem/seleção da timeline, playhead) + configuração efetiva do
  export. Mesmo projeto + mesmas opções + mesma saída → continua; qualquer edição →
  recomeça do zero. Encoder diferente (SPS/PPS) → recomeça do zero.
- Suspensão do app ≠ cancelamento do usuário: a suspensão mantém o registro e o
  progresso volta com `kExportResumable`; o cancelamento do usuário apaga.
- Android (`Exporter.kt`): pausa ao ir para segundo plano e continua sozinho ao
  voltar; falha recuperável continua do trecho salvo (até 2 vezes); export
  interrompido com o app fechado aparece na tela Exportar ("Exportação interrompida
  em X% — Continuar / Descartar").
- iOS (`AureaModel.swift`/`ExportView.swift`): o mesmo comportamento.

Custo: o disco guarda o registro e o MP4 até a validação (o teste de espaço livre
conta os dois); a montagem relê o vídeo uma vez no fim.

## 3. Render e encoder em série (corrigido)

Medição na base (1080×1920, título estático, 300 quadros): render 36 ms/q,
encoder 34 ms/q, total **79 ms/q (12,7 qps)** — a soma, não o máximo. Causa: com
2 slots e 1 quadro "em voo" na GPU, o produtor segurava o slot do quadro pronto
enquanto esperava o encoder devolver o outro; render e encoder se revezavam.
Correção (`export_thread_main`): sem slot livre, o quadro pronto na GPU vai para o
encoder na hora. Mesmos quadros, mesma ordem, mesma qualidade.

O perfil de GPU por passe do export ("export perfil" no log) estava órfão desde a
troca de pipeline (nunca acumulava); voltou.

## 4. Correções encontradas pelo benchmark

- **Project 8 não exportava** na 0.1.7 ("nao foi possivel restaurar a fonte
  Original deste formato 3D"): formas/textos 3D procedurais caíam na regra de
  restaurar o modelo Original de arquivo. Eles não têm arquivo de origem — o modelo
  da prévia já é o original.
- A suíte de testes do host não compilava na 0.1.7 (string com quebra de linha
  literal em `test_export.cpp`).

## 5. Medições (moto g52, 1080×1920 ou resolução do projeto, 150 quadros, H.264 de hardware)

Mesmo APK de teste nas duas rodadas; app = 0.1.7 intacta (`apk-base2`) × esta branch (`apk-novo4`).
Brutos: `build/reference/rendercore-bench/base2*.json` e `novo4.json`.

| Cenário | 0.1.7 | Novo | Ganho | Fase final 0.1.7 / novo | Pico de RAM 0.1.7 / novo |
| --- | --- | --- | --- | --- | --- |
| Título estático (texto + forma) | 12,2 s (12,3 q/s) | 10,2 s (14,7 q/s) | 1,20× | 1,0 / 1,1 s | 384 / 414 MB |
| Slideshow de 3 fotos com zoom | 13,2 s (11,4 q/s) | 9,3 s (16,1 q/s) | 1,42× | 1,4 / 1,3 s | 423 / 438 MB |
| Vídeo 1080×1920 + legenda + áudio | 15,7 s (9,5 q/s) | 12,8 s (11,7 q/s) | 1,23× | 1,2 / 1,9 s | 398 / 422 MB |
| Vídeo + desfoque + glow + desfoque direcional | 25,4 s (5,9 q/s) | 19,1 s (7,9 q/s) | 1,33× | 1,3 / 1,8 s | 396 / 414 MB |
| Project 13 (2 modelos GLB grandes) | 101,3 s (1,5 q/s) | 101,4 s (1,5 q/s) | 1,00× | 1,0 / 1,0 s | 513 / 480 MB |
| Project 8 (pacote do iPhone) | **não inicia** | 7,6 s (19,9 q/s) | — | — / 1,1 s | — / 434 MB |

Leitura honesta:
- O ganho vem de render e encoder se sobreporem; não mudou pixel, amostra nem qualidade
  (testes `PipelinedOutputIsByteIdenticalToSerial` e afins continuam verdes).
- Project 13 é limitado pela GPU (640 ms/quadro de 3D): o pipeline não tem o que ganhar ali.
- A fase final do vídeo com som ficou ~0,6 s mais longa (montagem do MP4 a partir do
  registro + AAC do PCM guardado). Antes da correção do PCM lateral eram +2,3 s.
- Pico de RAM do processo (VmHWM) 7–30 MB maior nos cenários 2D: buffers do registro e
  da montagem; menor no Project 13.
- Perfil de GPU do título (novo): 28,3 ms/quadro, dos quais **22,7 ms são a conversão
  NV12** (`export-y` 15,4 + `export-cbcr` 7,3) e só ~5,6 ms a composição. É o próximo
  gargalo a atacar (compute escrevendo direto nos buffers de leitura, GLSL + Metal).

Ferramenta: `python tools/rendercore_bench.py --label X --install --apk-dir D --frames N`
roda `RenderCoreBenchmarkDeviceTest` cenário por cenário (processo novo por cenário,
mesma sondagem de memória/codecs do app) e junta as etapas do log do motor.

## 6. Testes executados

- Host (Windows, Vulkan real + FFmpeg): `ExportPacketLog` 5/5; ponta a ponta
  `SuspendedExportResumesFromLastGopWithSamePictures` (suspende no quadro ~70 de 120,
  continua no 60, quadros 0–59 idênticos bit a bit, PSNR mínimo 55,3 dB, áudio inteiro;
  projeto editado recomeça do zero; cancelar apaga o registro). Grupo "Export" inteiro:
  173 testes, só as 4 falhas que já existiam.
- Suíte completa do host: 1934 testes; 30 testes falham — **os mesmos 30, com o mesmo
  número de verificações falhando, na 0.1.7 intacta** (worktree `Aureabeta-base017`),
  ou seja, nenhuma regressão desta branch.
- Android JVM: 398 testes, 0 falhas. Aparelho (moto g52): `TimelineGesturesTest` 21/21
  (inclui o teste novo dos keyframes travados) + o benchmark acima.
- iOS: só checagens estáticas (api_swift, pbxproj, assets, contrato de efeitos, símbolos,
  tipos) — 0 problemas. Compilação/execução nativa só pela CI.

## 7. Timeline: keyframes de camada não selecionada (pedido de 12/10)

Como no Alight Motion: os losangos da linha de uma camada NÃO selecionada aparecem a 35%
e não pegam o dedo (o arrasto rola/faz scrub, o toque seleciona a camada). Trilhas de
propriedade abertas e o modo "escolher keyframes" continuam editáveis (pedido explícito).
Android: `KeyframeVisibility.editable`, `TimelineController`, `TimelinePainter`.
iOS: `KeyframeVisibility.editable`, `TimelineView`.

## 8. Pendências reais

- Reaproveitar quadros idênticos (título parado, foto parada): exige impressão
  digital completa do quadro avaliado (efeitos que usam o tempo, partículas, 3D);
  não feito para não arriscar um quadro congelado errado.
- Tile renderer geral, reescrita do render graph: não feitos (o grafo atual já
  reaproveita transientes e funde efeitos).
- Validação final decodifica o arquivo inteiro na CPU; em vídeos longos isso pesa
  no fim do export — candidato a validar em paralelo durante o render.
- Serviço em primeiro plano no Android (export continuar com o app em segundo
  plano em vez de pausar): não feito.
- iOS: compilação e execução só pela CI (`build-ipa.yml`); aqui só checagens estáticas.
- Nada disto rodou ainda num iPhone nem num aparelho fraco (LG K10).
