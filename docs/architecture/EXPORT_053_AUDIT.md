# Exportação 0.5.3 — auditoria (Fase A)

12/11/2026. Branch `claude/omega-0.3.2`, HEAD `e984f47f`, com 209 arquivos
modificados sem commit na árvore. **Fase A é só leitura**: nenhum arquivo de
código foi alterado, nada foi compilado e nada foi executado (outros agentes
estavam compilando e editando o motor). Este documento é o ponto de partida da
Fase B (implementação).

Pedido do dono para a 0.5.3: "Ultra otimização no app... para TUDO.... resolver
TODOS os problemas de exportação em TODOS os processadores..... e deixar
rápido".

## 0. Como ler

Cada afirmação leva uma etiqueta:

- **[código]** — visto no código desta árvore, com `arquivo:linha`.
- **[docs]** — relatado em documento ou mensagem de commit do repositório.
- **[hipótese]** — inferência (comportamento de plataforma ou de fabricante)
  que o código torna possível, mas que ninguém reproduziu.

Limites desta auditoria:

- `engine/src/engine/Engine.cpp` estava sendo editado durante a leitura: passou
  de 21.183 para 21.520 linhas em menos de duas horas. As referências a ele
  citam a **função**; a linha é a vista no fim da sessão e serve só de pista.
  Os demais arquivos citados não mudaram durante a leitura.
- Três partes foram lidas por subagentes (iOS; camada Kotlin fora do
  `Exporter.kt`; histórico git + docs secundários). Os itens que entraram como
  P0 foram reconferidos no código por mim; os demais levam a etiqueta que o
  subagente deu.
- Nenhum aparelho físico. A única família com execução real registrada em todo
  o histórico é Qualcomm (Moto g52, Snapdragon 680). Ver §2.3.
- `docs/architecture/EXPORT.md` está desatualizado (diz `NotImplemented`,
  encode por MediaCodec e mux só no FFmpeg) **[código]**; o estado real é o do
  §1. `STABILITY_IOS_2026-10-04.md` descreve um sink nativo do iOS que não
  existe mais **[código]**.

Abreviações de caminho:

| Sigla | Arquivo |
|---|---|
| `SINK` | `engine/src/export/FFmpegExportSink.cpp` |
| `AVE` | `engine/src/export/AndroidVideoEncoder.cpp` |
| `ENG` | `engine/src/engine/Engine.cpp` (linhas aproximadas) |
| `OFR` | `engine/src/export/OfflineFrameRenderer.cpp` |
| `ATT` | `engine/include/aurea/export/ExportAttempts.hpp` |
| `WD` | `engine/include/aurea/export/ExportWatchdog.hpp` |
| `RULES` | `engine/include/aurea/export/ExportRules.hpp` |
| `JNI` | `engine/platform/android/aurea_jni.cpp` |
| `EXP` | `android/app/src/main/java/com/aurea/aurea/state/Exporter.kt` |
| `K/` | `android/app/src/main/java/com/aurea/aurea/` |
| `AM` | `engine/platform/ios/app/AureaModel.swift` |
| `EV` | `engine/platform/ios/app/ExportView.swift` |
| `AE` | `engine/platform/ios/bridge/AureaEngine.mm` |

---

## 1. Mapa do pipeline de exportação hoje

```
tela Exportar ──► Exporter (escada de tentativas 0..6) ──► JNI / ponte ObjC
                                                              │
                              Engine::start_export_checked ◄──┘
                              (congela o projeto, checkpoint, disco, teto do aparelho,
                               abre o sink = escada de ENCODERS, slots de readback)
                                      │
   thread "aurea-export" (render)     │        thread "aurea-export-enc" (encoder)
   OfflineFrameRenderer ► GPU ► NV12 na GPU ► readback (1–4 slots) ► sink.write_video
   (decoders da mídia)                                   │           + mix do áudio (PCM → arquivo .pcm)
                                                         ▼
        Android: Surface(GLES) │ ByteBuffer │ FFmpeg+MediaCodec │ libopenh264/kvazaar │ software do sistema
        iOS:     h264/hevc_videotoolbox (NV12 │ I420) │ libopenh264/kvazaar
                                                         ▼
                     registro de pacotes (checkpoint por GOP) em disco
                                                         ▼
   fim: end_video → begin_assembly → AAC do PCM → mux MP4 (+faststart) → validação por decode → rename
                                                         ▼
        Android: cópia para o MediaStore (IS_PENDING)          iOS: Fotos (.addOnly)
```

### 1.1 Android — Kotlin (tela, serviço, vigia, publicação)

| Etapa | Onde | O que faz |
|---|---|---|
| Tela | `K/editor/ExportScreen.kt:137-141, 195, 241-265, 503-558` | 480p–4K, H.264/HEVC, fps Baixo/Reduzido/Cheio + 24–120 + livre 1–240, qualidade 20–100, encoder Auto/Compatível/Software, taxa Auto ou 5–60 Mbps. Pré-checagens: projeto vazio, < 1 quadro, > 3 h, limites de GIF/sequência **[código]** |
| Ausente na tela | idem | Sem comparação espaço livre × tamanho estimado (os dois só são mostrados, `:364, :382`); sem checagem de bateria/economia de energia; sem bloqueio quando a resolução passa do teto (só acinzenta); `HevcEncoderCheck.probe` não tem chamador em `src/main` **[código]** |
| Partida | `EXP:388-415` (`guardedExport`), `EXP:426-508` (`runExport`) | Inicia o serviço, marca o projeto (`ProjectGuard`), roda a **escada de tentativas** perguntando o próximo degrau ao motor (`nativeExportNextAttempt`) **[código]** |
| Serviço | `K/state/ExportForegroundService.kt:44-73, 83-94, 113, 121-152` | `startForegroundService` + `startForeground` com tipo `mediaProcessing` (API ≥ 35) ou `dataSync` (29–34); `PARTIAL_WAKE_LOCK "aurea:export"` de 6 h, só depois de o `startForeground` dar certo; `onTimeout` solta tudo e avisa; `START_NOT_STICKY` **[código]** |
| Manifesto | `android/app/src/main/AndroidManifest.xml:20-22, 125, 177-180`; `android/app/build.gradle.kts:126-127` | `foregroundServiceType="dataSync\|mediaProcessing"`, 3 permissões de FGS; `targetSdk 36`, `minSdk 26`. `POST_NOTIFICATIONS` **não** declarada **[código]** |
| Tela ligada | `K/MainActivity.kt:93` | `FLAG_KEEP_SCREEN_ON` permanente na Activity **[código]** |
| Progresso | `EXP:651-701` | `delay(100)` + `engine.exportProgress` na main thread; em paralelo, 1 Hz de `exportDiagnostics()` gravado com `AtomicFile` (`EXP:558-563`) **[código]** |
| Vigia da tela | `K/state/ExportStallWatchdog.kt:33-47, 51-53`; `EXP:658-680` | Observa só `framesDone` e `message`; 180 s sem mudança → `cancelExport`; +15 s → desiste e pede o próximo degrau (ou falha `expfail_stuck`). Relógio `elapsedRealtime` **[código]** |
| Térmico | `K/state/EditorStore.kt:1324-1331` → `JNI:2837` → `Engine::set_thermal` | Só repassa o estado (API 29+); o motor reduz quadros em voo e marca `kExportThermalReduced` (`ENG export_thread_main`). Não há pausa em estado crítico **[código]** |
| Memória | `EditorStore.kt:1225-1247` → `JNI:847` → `Engine::trim_memory` | `onTrimMemory` chega ao motor mesmo exportando; o export solta caches entre quadros **[código]** |
| Segundo plano | `EditorStore.kt:1130-1136, 1171-1184` | O motor **não** é suspenso no Android durante o export; `surfaceDestroyed` chama `detachSurface` bloqueante na main **[código]** |
| Publicação | `EXP:891-931, 980-990, 993-1058`; `K/state/GalleryPublish.kt:32-83` | Temp em `cacheDir/export`; API ≥ 29: MediaStore `IS_PENDING` + cópia de 1 MiB por vez; API ≤ 28: `Movies/Aurea` + MediaScanner (até 10 s). Falhou: cópia em `getExternalFilesDir`. Depois apaga o temp **[código]** |
| Limpeza | `K/state/CacheStorage.kt:116-130, 186-189`; `EditorStore.kt:1030` | A cada abertura do app: `cacheDir/export` inteiro é apagado e `cacheDir/motor` (fora prévias/PreRender/proxies) é aparado a 64 MB **[código]** |
| Morte do processo | `K/diagnostics/ExitDiagnostics.kt:156-171`; `EXP:437-453` | Só API ≥ 30 e só 4 motivos de saída (CRASH, CRASH_NATIVE, LOW_MEMORY, SIGNALED): procura checkpoint; senão começa num degrau mais leve **[código]** |
| Diagnóstico | `K/diagnostics/ExportDiagnosticReport.kt:15-25, 46-57`; `ProblemReport.kt:29-59, 85-90` | JSON de até 3.000 caracteres, 3 tentativas, em `noBackupFilesDir`; só sai do aparelho quando o usuário manda "Relatar um problema" **[código]** |

### 1.2 JNI

- `JNI:626-685` monta o `EngineConfig`: `exportSinkFactory = make_ffmpeg_export_sink`
  (`:677`), `enableResumableExport = true` (`:684`), `exportTemporaryDirectory =
  <cacheDir>/export` (`:666`), `enableTrackedGpuAdmission = true` (`:631`).
  **Não** define `memoryBudgetCapBytes` nem `enableDirectSurfaceExport` **[código]**.
- `JNI:469` entrega a JavaVM ao FFmpeg (`av_jni_set_java_vm`) — é por ela que o
  motor consulta `MediaCodecList` **[código]**.
- Entradas do export: `nativeStartExport` (`:5759`), `nativeExportResumeCandidate`
  (`:5773`), `nativeRestartExport` (`:5788`, sem chamador no Kotlin),
  `nativeExportNextAttempt` (`:5798`), `nativeExportBitrateBps` (`:5821`),
  `nativeCancelExport` (`:5975`), `nativeExportProgress` (`:6155`),
  `nativeExportDiagnostics` (`:6163`) **[código]**.

### 1.3 Motor

| Peça | Onde | O que faz |
|---|---|---|
| Admissão | `ENG Engine::start_export_checked` (~16408) | Prazos de partida: 20 s lock do render, 30 s dreno da prévia (`ENG:348-351`); cancela o PreRender; recusa se o render anterior segue preso no driver (`RenderHung`) **[código]** |
| Degrau | `ATT:86-96`; aplicado em `start_export_checked` | Degrau 0..6 viaja em `ExportSettings::safeMode`: codec, Baseline, teto de lado/fps, sem upscale, teto de motion blur, custo 3D, alinhamento a 16 e decode compatível **[código]** |
| Tamanho | `ATT:106-122`, `RULES:57-89` | Degrau 0 mantém o tamanho nativo par; degraus 1+ alinham o lado maior a 16 **[código]** |
| Teto do aparelho | `engine/src/platform/DeviceCapabilities.cpp:422-452`; `start_export_checked` (bloco "Teto do aparelho") | Do encoder **H.264** da tabela do Kotlin, limitado a 3840×2160; orçamento de memória < 6 quadros 4K RGBA16F → 1080p; 32 bits → 1080p; acima do teto o vídeo sai no maior tamanho que cabe **[código]** |
| Disco | `start_export_checked` (bloco "Espaço em disco") | Recusa com menos de metade da estimativa + 32 MB; confere a cada 300 quadros (< 16 MB livres → `StorageFull`) **[código]** |
| Checkpoint | `engine/src/export/ExportCheckpoint.cpp:189-199, 383-387`; `ExportCheckpoint.hpp:138-142` | Registro de pacotes + manifesto em `exportTemporaryDirectory` (Android: `cacheDir/export`; iOS: `<Caches>/export-checkpoints`); retoma no último GOP íntegro; sem espaço para o registro, exporta sem retomada **[código]** |
| Render | `ENG Engine::export_thread_main` (~17890), thread `aurea-export`; `OFR:120-309` | Um `Renderer` próprio do export (`set_startup_prewarm(false)`, `OFR:44-49`); espera o quadro exato da fonte em fatias de 10 ms; reabre a fonte, cai para decode compatível e, em último caso, usa o quadro mais próximo do trecho **[código]** |
| Readback | `start_export_checked` (bloco "Slots de leitura"); `engine/include/aurea/platform/AddressSpace.hpp:77-90` | NV12 convertido na GPU e lido em 1–4 slots mapeados (política do aparelho: 2 no perfil de entrada, 4 no topo, 2 em 32 bits, menos com calor); fila limitada a `min(64 MB, orçamento/8)` **[código]** |
| Encoder | `ENG Engine::export_encoder_main` (~18488), thread `aurea-export-enc` | `sink.write_video` e, na mesma thread, o mix do áudio do quadro (`write_export_audio`) **[código]** |
| Prioridade | as duas threads acima e `AVE:631` | As três threads do export só recebem **nome**; nenhuma chama `set_current_thread_priority` (`engine/src/core/Thread.cpp:17-37` existe e é usada por áudio, decode e prévia) **[código]** |
| Fim | `export_encoder_main` (trecho "finalizando / montando / validando") | `end_video` → `begin_assembly` → `replay_spilled_audio` (ou mix de novo) → `finish` → `validate_output` → rename **[código]** |
| Memória da escada | `ATT:300-428` (`ExportAttemptLedger`) | Mesmo tipo de falha no mesmo ponto em duas tentativas = determinístico (para); no máximo 4 tentativas que renderizaram **[código]** |

A escada de **tentativas** (`ATT:10-16, 86-96`):

| Degrau | Encoder | Codec | Lado menor | fps | Motion blur | Custo 3D | Decode |
|---|---|---|---|---|---|---|---|
| 0 | o pedido | o pedido | o pedido | o pedido | projeto | cheio | aparelho |
| 1 | aparelho, Baseline | H.264 | o pedido (alinhado a 16) | ≤ 60 | ≤ 32 | cheio | aparelho |
| 2 | software | H.264 | o pedido | ≤ 60 | ≤ 16 | 1 | compatível |
| 3 | aparelho, Baseline | H.264 | ≤ 720 | ≤ 30 | ≤ 16 | 1 | aparelho |
| 4 | software | H.264 | ≤ 720 | ≤ 30 | ≤ 8 | 2 | compatível |
| 5 | software | H.264 | ≤ 480 | ≤ 30 | ≤ 4 | 3 | compatível |
| 6 | software | H.264 | ≤ 480 | ≤ 30 | nenhum | 4 | compatível |

### 1.4 Sink e escada de ENCODERS (`SINK`)

Ordem montada em `SINK:1023-1101` (`build_rungs`):

| # | Degrau | Android | iOS |
|---|---|---|---|
| 0 | `surface-direct` | AHardwareBuffer RGBA → EGLImage → Surface do encoder. **Desligado** (`enableDirectSurfaceExport` falso; só `debug.aurea.enc.direct=1`) | — |
| 1 | `surface` | NV12 da CPU → textura GLES → RGB → Surface do encoder (`AVE:334-664`) | — |
| 2 | `buffer` | NV12 copiado no ByteBuffer com o layout que o encoder devolve (`AVE:760-829, 982-1022`) | — |
| 3 | `legacy` | FFmpeg `h264/hevc_mediacodec` com **todos** os encoders de hardware que passam em `isFormatSupported` (`SINK:277-425`) | `videotoolbox` NV12, depois `videotoolbox` I420 |
| 4 | `software` | `libopenh264` (H.264), `libkvazaar` (HEVC) | idem |
| 5 | `platform-software` | encoder de software do sistema via FFmpeg | — |

Regras **[código]**:

- Os degraus 0–2 usam só o **primeiro** encoder de hardware que
  `find_encoder` acha (`AVE:167-328`; API < 29 classifica pelo nome,
  `AndroidCodecName.hpp:18-26`). Segunda passada aceita encoder que anuncia o
  tamanho mas não a taxa em tempo real (`AVE:238-271`).
- "Liberdade condicional" (`SINK:1425-1438, 1928-1954`): até o 1º pacote, os
  quadros ficam guardados (3–8, `SINK:791-792`); sem 1º pacote em 5 s esperando
  o encoder (`startupStallNs`, `SINK:81`), ou 1º quadro reprovado
  (`verify_first_frame`, `SINK:1506-1597`: média > 3 ou bloco > 16 níveis),
  o próximo degrau recebe os mesmos quadros. Depois do 1º pacote não há troca
  dentro do sink.
- Degrau que falhou é pulado por 30 min **no processo** (`SINK:91-113`); só
  `surface-direct`, `surface` e `buffer` entram nessa memória (`SINK:1034, 1283`).
- Um pacote por quadro, na ordem, com PTS **idêntico** ao entregue
  (`SINK:1706-1707` no encoder do aparelho; `SINK:1892-1895` no FFmpeg); no
  modo registro o PTS tem de ser o do índice (`SINK:1853-1854`).
- Prazo no meio do vídeo: 30 s (`codecStallNs`, `SINK:76`). Surface que para no
  meio com o degrau `buffer` do mesmo encoder disponível devolve `Busy` e a
  tentativa recomeça pelo buffer (`SINK:1643-1653`).
- `OMX.qcom.*` recebe CBR no lugar do VBR (`AndroidCodecName.hpp:36-43`,
  `AVE:858, 929-937`, `SINK:930-934`).

### 1.5 Encoder nativo do Android (`AVE`)

- `MediaFormat` (`AVE:877-910`): `priority=1`, `operating-rate=32767`,
  `width/height`, `color-format`, `bitrate` (limitado ao teto anunciado,
  `RULES:109-112`), `bitrate-mode` só se anunciado, `frame-rate` inteiro ou
  float, `i-frame-interval` (2 s), `profile/level` **só** no Baseline,
  `color-standard/range/transfer`, `max-bframes=0` **[código]**.
- Configure recusado com as dicas offline → recria e configura sem elas, uma
  vez (`AVE:919-927`). `AMediaCodec_start` que falha **não** tem essa segunda
  chance (`AVE:945`) **[código]**.
- Surface (`AVE:396-479, 571-599`): contexto GLES2 próprio com
  `EGL_RECORDABLE_ANDROID`, numa thread própria (`aurea-enc-surface`);
  `eglPresentationTimeANDROID` + `eglSwapBuffers`; cada chamada tem prazo e a
  thread é abandonada se o driver não voltar (`AVE:695-715, 743-758`). No máximo
  3 quadros sem pacote (`kSurfaceInFlight`, `AVE:52, 1165-1167`).
- Buffer (`AVE:760-829`): layout pelo `image-data` (MediaImage) ou
  `stride`/`slice-height`; formato flexível sem descrição é recusado. Exige
  `AMediaCodec_getInputFormat` (API 28): em API 26–27 o degrau `buffer` não
  abre (`AVE:762-763`) **[código]**.
- EOS: Surface → `signalEndOfInputStream`; Buffer → buffer vazio com
  `ptsUs = 0` e a marca de EOS (`AVE:1089-1105`) **[código]**.
- Fechar: `stop/delete` numa thread com prazo de 3 s; passou, é abandonada
  (`AVE:1024-1059`) **[código]**.
- Modo síncrono (`dequeue*` com timeout), sem callbacks assíncronos **[código]**.

### 1.6 Áudio, mux, validação

- Áudio: mix do motor → PCM s16 num arquivo lateral (`<registro>.pcm`) durante o
  render; no fim, encoder **AAC nativo do FFmpeg** (software; nenhum MediaCodec
  nem AudioToolbox) (`SINK:1766-1813, 2371-2391`). Sessão retomada mixa tudo de
  novo no fim **[código]**.
- Mux: MP4 por `libavformat` com `+faststart` num temp
  `<saída>.aurea-ffmpeg-*.tmp` (`SINK:806-807, 860-870`); HEVC com tag `hvc1`
  (`SINK:849`); BT.709 limitado nas etiquetas **[código]**.
- Validação (`SINK:176-245, 2433+`): abre o MP4 e decodifica com os decoders de
  **software** do FFmpeg — tudo até 900 quadros, GOPs amostrados acima;
  compara amostras de imagem (1 a cada 15 quadros, até 240;
  `ExportContentCheck.hpp`: reprova média de luma > 24 ou croma > 16) e o nível
  do áudio. Modo tolerante: divergência de relógio vira aviso **[código]**.
- Publicação no motor: rename atômico (`SINK:2415-2423`) **[código]**.

### 1.7 Vigias e prazos (todos **[código]**)

| Vigia | Limite | Onde |
|---|---|---|
| Partida: lock do render / dreno da prévia / abertura de fonte | 20 s / 30 s / 10 s | `ENG:348-351`, `WD:86` |
| 1º pacote do encoder (por degrau) | 5 s esperando o encoder | `SINK:81` |
| Chamada EGL da Surface | 5 s (init e quadros em teste), 30 s no meio | `AVE:740`, `SINK:1669` |
| Encoder sem pacote no meio | 30 s | `SINK:76` |
| Worker do encoder sem batida | 45 s (10 s no abort, 3 s cancelado) | `WD:144-155` |
| Quadro exato da fonte | 4 s sem trabalho / teto 60 s; depois 1 s / 4 s; depois 250 ms / 1 s | `WD:92-96` |
| Camada sem imagem nenhuma | 4 s sem trabalho / teto 120 s | `WD:106-108` |
| GPU de um quadro | 120 s | `OFR:40`, `ENG wait_export_gpu` |
| Produtor (render) sem batida | 150 s (20 s cancelado) | `WD:165-171` |
| Tela (Android e iOS) | 180 s sem mudança + 15 s | `WD:183-184`, `ExportStallWatchdog.kt:51-53`, `AM:5721-5749` |
| `stop()` do encoder | 3 s | `AVE:1048` |

### 1.8 iOS

- Tela e partida: `EV:597-646` → `AM:5290-5336` (saída em
  `Documents/<nome do projeto>.mp4`) → `AM:5341-5487` (escada, 2 repetições por
  "ocupado") → `AE:6000-6011` → `Engine::start_export` **[código]**.
- Config: `engine/platform/ios/bridge/AureaBridge.mm:175` (sink FFmpeg), `:183`
  (retomável), `:187` (`memoryBudgetCapBytes` = 1 GiB) **[código]**.
- Render Metal → NV12 na GPU → blit para `MTLBuffer` compartilhado →
  `memcpy` para `AVFrame` (`SINK:2057-2062`) → o FFmpeg copia de novo para o
  `CVPixelBuffer` do VideoToolbox. **Não há** `AVAssetWriter` nem
  `VTCompressionSession` direto (grep vazio) **[código]**.
- Encoder: `h264_videotoolbox`/`hevc_videotoolbox` NV12 → I420 → `libopenh264`/
  `libkvazaar` (`SINK:1073-1087`); `allow_sw=0`, `realtime=0` (`SINK:921-924`).
  Degraus do VideoToolbox não entram na memória de degraus falhos **[código]**.
- Segundo plano: `scenePhase` → `AM:1784-1829`: salva o projeto, marca
  `exportPausedForBackground` e chama `Engine::suspend` (cancela com marca de
  suspensão; o checkpoint fica `Paused`); ao voltar, `AM:1873-1916` refaz do
  último GOP. `beginBackgroundTask` cobre só o salvar e o suspender; sem
  `UIBackgroundModes`/`BGTaskScheduler` **[código]**.
- `isIdleTimerDisabled` segue `exporting` (`AM:971`); térmico e memória chegam
  ao motor (`AM:1928-1963, 2003-2022`); disco só no motor **[código]**.
- Fotos: `requestAuthorization(.addOnly)` depois do export; negado → o arquivo
  fica em Documents (`AM:5929-5982`) **[código]**.
- Vigia da tela: `ExportStallWatch`, 180 s + 15 s (`AM:5721-5749`) **[código]**.
- CI: só H.264 360p/180p no simulador (`ParityExportProbe.swift:44, 142`);
  nenhum HEVC, 1080p/4K, segundo plano ou Fotos **[código]**.

---

## 2. Falhas conhecidas e situação

### 2.1 Tabela

Fontes: `docs/diagnostico/export-falhas.md` (EF), `export-falhas-0.3.0.md` (F3),
`export-engine-2.0.md` (E2), `docs/export-engine-0.5.0.md` (E5),
`docs/releases/*`, `docs/release/0.5.*`, `docs/architecture/*` e `git log --all`.
"No HEAD" = o commit é ancestral de `e984f47f` ou o código equivalente foi
conferido por identificador.

| # | Sintoma | Aparelho / SoC | Causa registrada | Correção | Situação |
|---|---|---|---|---|---|
| 1 | Morto no meio do export pelo monitor de GPU do HyperOS (quadro 95/590, "GpuMemory … 629145kB") | Redmi 12 (HyperOS) | Cache da prévia residente + orçamento de ¼ da RAM livre chegando a 1 GB | `1345f1a0` (**não** está no HEAD). Soltar o cache da prévia e `process_budget_bytes()` estão no HEAD; o **teto por fabricante (480 MB Xiaomi/Redmi/POCO, 1 GB nos demais) não está**: `memoryBudgetCapBytes` só é definido no iOS (`AureaBridge.mm:187`) | **Parcial — ABERTO** [docs + código] |
| 2 | Erro 6 quando a saída passa do teto (upscale 2× de 1440p) | Galaxy A05 | Tamanho acima do aparelho | `d9f86dc1`: sai no maior tamanho que cabe | Corrigido no HEAD [docs] |
| 3 | `VK_ERROR_DEVICE_LOST` no quadro 42/300 (3D pesado) | Moto g52 (SD 680, Adreno 610) | KGSL reinicia a GPU com submissão > ~2 s | Um passe por command buffer (`engine/gpu/vulkan/VulkanBackend.hpp:627-631`) | Corrigido no HEAD [docs] |
| 4 | Prazo de GPU (120 s) estourado por motion blur pesado | Moto g52 | 45–55 s de GPU por quadro | Teto de amostras e custo 3D por degrau (`ATT`) | Corrigido no HEAD, outro desenho [docs] |
| 5 | Encoder para de entregar pacotes em VBR (1–3 s) | Moto g52, `OMX.qcom.video.encoder.avc` — **medido** | VBR descarta quadros sem saída | `4e4af57b`: CBR para `OMX.qcom.*`; Surface parada → buffer | Corrigido só para `OMX.qcom.*`; `c2.qti.*` e os demais fabricantes **não testados** [docs] |
| 6 | "buffer do encoder menor que o quadro" em 854×480 | Vivo Y30 (Helio P35, PowerVR GE8320) | Passo alinhado a 16, buffer pela largura crua | `65ade5d0`: lado maior a 16, layout lido, software | Corrigido no HEAD, **nunca validado no aparelho** [docs] |
| 7 | Preso em "Preparando"; ANR com `LOW_MEMORY` | vivo V2026 (Helio P35, PowerVR GE8320) e vivo 1938 (Android 12, processo 32 bits) | Dezenas de segundos por shader no driver; main presa atrás de `nativeTrimMemory` | `2fe6fe20`, `15eb461a` | Corrigido no HEAD, não validado [docs] |
| 8 | "Preparando" para sempre e Cancelar morto | LG K61 (PowerVR GE8320) | Esperas da partida sem prazo | `f5c3bf1f` (0.5.2): prazos 20/30/30 s | Corrigido no HEAD, só testes de host [docs] |
| 9 | Para sempre em ~50% (quadro 19 de 38, nos 7 degraus) | Galaxy A23 | Decoder do aparelho nunca entrega o clipe que entra na transição | 0.4.6 decode compatível; 0.5.2 escada com memória | Corrigido no HEAD, não validado [docs] |
| 10 | "textura transitoria indisponivel" no mesmo quadro em todo degrau | Galaxy A23/M21, vivo V2120 | Degraus de compatibilidade renderizavam na densidade da composição | `f5c3bf1f` | Corrigido no HEAD, teste de host [docs] |
| 11 | Para no mesmo quadro (302; 46; 74%) | Poco X6 Pro, Poco M8 | Quadro exato nunca chega; causa do aparelho não provada | `7762e14a`: reabrir fonte, quadro mais próximo | Corrigido em código; emulador não reproduziu [docs] |
| 12 | iOS e Android ≤ 4 GB fecham no meio | iPhone 11/XR, Poco M | Export ignorava avisos de memória | `7762e14a`, 0.3.5 | Corrigido em código; jetsam/LMK nunca reproduzidos [docs] |
| 13 | **iOS fechando no meio em 4K 120 fps e 1080p 60 fps** | iPhone (modelo não informado) | Sem causa registrada | — | **ABERTO** (`docs/release/0.5.2-discord.md:79`: "em investigação") [docs] |
| 14 | "Não foi possível iniciar" com modelo 3D | iPhone 16 | Textura/HDRI ausente lido como "ainda carregando" | `d9f86dc1` | Corrigido no HEAD [docs] |
| 15 | Erro 26 na partida com mensagem de memória | Infinix X6820, Android 13, build 2151 | Não atribuída | — | **DESCONHECIDO** [docs] |
| 16 | ANR no export, marca `f=0/590` | Xiaomi 23028RA60L, Android 15, processo 32 bits | Main esperando `bindService` do Google; não simbolizado | `60e1b0c1` só documenta | **ABERTO / DESCONHECIDO** [docs] |
| 17 | Vídeo listrado (diagonal) | desconhecido | Passo de linha do encoder ≠ largura | `e2c709af`, `15e8502d` (caminho antigo); degrau `buffer` lê o layout | Corrigido nos degraus nativos; o degrau `legacy` (FFmpeg) continua presumindo o layout, mas o 1º quadro é conferido (`SINK:1477-1486`) [docs + código] |
| 18 | Vídeo verde | — | Quadro do decoder desenhado antes do fence | `aee589bb` (0.2.7) + conferência de conteúdo (0.5.0) | Corrigido no HEAD [docs] |
| 19 | 1080p H.264 com "erro de E/S" | — | Offset do pacote aplicado duas vezes | `1d767fe9` (caminho substituído) | Corrigido [docs] |
| 20 | Crash logo depois do export | Emulador, WebView 124 | WebView do anúncio (Unity) | — | **ABERTO**, sem correção validada (`P10_EXPORT_AD_WEBVIEW_2026-09-25.md`) [docs] |
| 21 | Erros 18, 5, 26, 28; "Quadro 0 de 522520" | todos | Assets mortos, duração sem teto, cópia de `content://` | `19118978` (0.2.4) | Corrigido no HEAD [docs] |
| 22 | Export morre ao sair do app / bloquear a tela | todos | Sem serviço em primeiro plano | `7276eef8`, `06730fa6`; 0.3.5; 0.3.6 | Corrigido no HEAD; ver R-02 e R-05 abaixo [docs + código] |
| 23 | Validação depois dos 100% confundida com travamento | todos | Mensagem parada | `7762e14a`, `f5c3bf1f` | Corrigido para a validação; **não** para a montagem do áudio e o `+faststart` (R-03) [docs + código] |
| 24 | Lado ímpar falha em todo degrau; "codec não suportado" pula para 720p; taxa acima do teto | relatos sem aparelho | Regras de tamanho e de tipo de falha | 0.3.6, 0.4.0, `e8f40207` | Corrigido no HEAD; o degrau `legacy` não limita a taxa (E5:73) [docs] |
| 25 | 32 bits e Android 8/9 | Helio de 3 GB com APK 32 bits; API 26–28 | Espaço de endereços; encoder de hardware não achado | `d9f86dc1` (0.2.6) | Corrigido no HEAD; **nenhum aparelho só de 32 bits foi testado** (só o Moto g52 rodando o APK v7a) [docs] |
| 26 | iOS: até 5 cópias por quadro, sem pool IOSurface, degraus falhos não lembrados | iPhone | Desenho do sink | — | **ABERTO** (E2:82-85, 211, 225-228) [docs + código] |
| 27 | Thread de render presa bloqueia novos exports até reabrir o app | qualquer driver que trave | Chamada de driver que não volta | — | **ABERTO por desenho** (E5:69) [docs] |
| 28 | Troca de encoder no meio do vídeo recomeça do quadro 0 | qualquer | O registro não continua com outro SPS/PPS | — | **ABERTO por desenho** [docs + código `SINK:1612-1620`] |
| 29 | CI do iOS "fechou o app antes do relatório final" | simulador | — | — | **DESCONHECIDO** (`docs/ios-parity.md:12-16`) [docs] |

Trabalho que ficou fora do HEAD (branches `claude/b2-export`, `claude/b2-surf`,
`claude/build-018*`, `claude/astra-rendercore`): a linha do HEAD passa por
`fafd12ee → 35bac8c0` e a escada foi **reimplantada por porte**, não por merge
(`408d3772` diz isso) **[docs]**. O que se perdeu no porte e ainda falta:

- teto de memória por fabricante no Android (item 1) **[código]**;
- vigia adaptativo por fase do `3b06e545` (20 s de preparação, `max(12 s, 8×
  média do quadro)`); o HEAD tem limites fixos de 45 s e 150 s (`WD:144-169`)
  **[inferência do subagente por grep]**.

### 2.2 Exceções por SoC / codec / fabricante que existem no código hoje

Não há tabela de SoC nem desvio por `Build.HARDWARE`, `SOC_MODEL` ou
`vendorID` da GPU: esses campos só vão para o diagnóstico **[código]**.

| Onde | O que faz |
|---|---|
| `engine/src/export/AndroidCodecName.hpp:36-43` | `OMX.qcom.*`: CBR no lugar do VBR |
| `AndroidCodecName.hpp:18-26` (repetido em `K/engine/DeviceProfile.kt:210-212` e `engine/platform/android/MediaCodecSource.cpp:350`) | Codec de software pelo nome (`OMX.google.`, `c2.android.`, `c2.google.`, `OMX.ffmpeg.`, `c2.ffmpeg.`, `OMX.SEC.sw.`, `.sw.`, `.sw`) |
| `SINK:1643-1653` | Surface parada no meio → mesma tentativa pelo buffer |
| `AVE:866-927` | `operating-rate=32767` e `priority=1`, com uma repetição sem as dicas |
| `RULES:36-38` | Lado maior múltiplo de 16 |
| `engine/include/aurea/platform/AndroidVideoCompatibility.hpp:10-59`; `JNI:761-787` | Samsung, SDK ≥ 26: planos legíveis ou vídeo por software quando o backend não importa AHardwareBuffer RGBA; nomes de decoder AOSP |
| `JNI:292-299, 765` | Emulador força vídeo por software |
| `engine/gpu/vulkan/VulkanBackend.hpp:627-631` | Um passe por submissão (Qualcomm KGSL) |
| `engine/gpu/vulkan/VulkanCommon.cpp:215-220`, `AddressSpace.hpp` | Regras de 32 bits (sem mapeamento antecipado, 4 fontes de vídeo, 2 slots de readback, 64 MB de cache do render) |
| `engine/src/render/Renderer.cpp:972`, `ENG:750`, `ShaderTranslate.cpp:150` | Queda Vulkan → GLES e correções de precisão (Huawei P30 lite, Mali-T820) |
| `engine/src/platform/DeviceCapabilities.cpp:501-539, 615-683` | Classe do aparelho por RAM, GPU, frequência de CPU e codecs; política por classe e por estado térmico |
| `AureaBridge.mm:187` | Teto de 1 GiB de orçamento — só iOS |

### 2.3 Evidência de campo por família de SoC

A maioria dos relatos traz só o **modelo**; o SoC não foi registrado. Abaixo só
entra numa família o que o documento ou o comentário do código diz
explicitamente. Atribuir os demais modelos a uma família seria dedução pelo
nome comercial (o ASTRA já recusou fazer isso).

| Família | O que existe |
|---|---|
| Qualcomm Adreno | Moto g52 (Snapdragon 680, Adreno 610) **medido**, APK de 64 e de 32 bits — o único aparelho com execução registrada. Codec2 da Qualcomm (`c2.qti.*`): **nunca exercitado** |
| MediaTek Helio + PowerVR | Vivo Y30 (Helio P35, `RULES:9`), vivo V2026 (Helio P35, PowerVR GE8320), LG K61 (PowerVR GE8320): só relatos e dumps de ANR (`build/reference/vivo-2185/`); nenhuma correção validada no aparelho |
| MediaTek Helio + Mali, Dimensity | **Nenhum** relato com o SoC registrado |
| Samsung Exynos (Mali / Xclipse) | Galaxy A51 5G (Exynos 980, `PROMPT03_A51_5G_2026-09-26.md`): crash de **importação**, mitigação nunca validada. **Nenhum** relato de export com o SoC registrado |
| Unisoc / Spreadtrum | **Nada** (só pesquisa pública nos docs ASTRA) |
| Google Tensor | **Nada** |
| Kirin | Huawei P30 lite: só prévia/Vulkan |
| 32 bits puro, chips de 2018, Android 8/9 | **Nada** em aparelho; só caminho de código (o Moto g52 rodou o APK v7a) |
| HEVC por hardware (qualquer fabricante) | **Nada** |
| Apple | iPhone 11, XR, 16: relatos; iOS nunca executado localmente |
| Modelos relatados **sem SoC registrado** | Redmi 12, Galaxy A05, Galaxy A23, Galaxy M21, vivo V2120, vivo 1938, Oppo A94, Poco X6 Pro, Poco M8, Poco M (sem modelo), Infinix X6820, Xiaomi 23028RA60L, Xiaomi 13T Pro (só flicker de prévia) |

E5:68 diz textualmente: "MediaTek, Qualcomm, Exynos e baixa RAM: não testados"
para a 0.5.0. A 0.5.2 também não registra execução em aparelho **[docs]**.

Os relatos de usuários ficam no D1 do Worker (`discovery/crash.js`,
`discovery/relato.js`); nenhum está exportado no repositório
(`build/reference/vivo-2185/reports.json` é uma consulta que falhou) **[docs]**.

---

## 3. Revisão do código: modos de falha que dependem do SoC / driver / sistema

Numeração `R-nn`. Cada item diz o que o código faz, por que pode falhar e a
etiqueta. A prioridade e o conserto estão no §5.

### 3.1 Ciclo de vida, serviço e arquivos (Android)

**R-01 — O checkpoint é apagado na abertura do app.** [código]
O registro de pacotes fica em `exportTemporaryDirectory` = `cacheDir/export`
(`JNI:666`; `ENG start_export_checked` chama `ckpt::directory(exportTemporaryDirectory, …)`;
`ExportCheckpoint.cpp:189-199`). Na criação do `EditorStore`
(`EditorStore.kt:1027-1030`) roda `storage.enforceLimits(exporting = false)`,
e `CacheStorage.kt:123` faz `trimToLimit(listFiles(export), 0L)`: tudo em
`cacheDir/export` sai. O comentário do próprio código diz "sobra de
export/legenda de uma sessão que morreu (crash, force kill) sai aqui". Ou seja:
depois de o sistema matar o processo no meio de um export (o caso para o qual
o checkpoint existe), a retomada não tem o que retomar. O comentário de
`ExportForegroundService.kt:76-82` promete o contrário ("a próxima exportação
continua de onde parou"). O teste `ExportCheckpointDeviceTest.kt` usa o mesmo
diretório (`:73`) mas não passa por uma reabertura do app.
*Não comprovado em execução:* a limpeza roda numa corrotina de IO; confirmar no
emulador com `am kill` no meio do export.

**R-02 — A retomada só é procurada em API ≥ 30 e para 4 motivos de saída.** [código]
`ExitDiagnostics.kt:157, 167-171`; `EXP:437-438` (`resumeCandidate` só `if (died)`).
Em Android 8–10 nunca há retomada nem partida num degrau mais leve.
[hipótese] Mortes por gerenciador de bateria do fabricante (MIUI/HyperOS,
ColorOS, Funtouch) chegam com outros motivos e também ficam de fora.
Além disso exige as mesmas opções e encoder Automático (`EXP:437`), e não há
tela "continuar exportação" na abertura (`engine.restartExport` sem chamador).

**R-03 — O vigia da tela pode cancelar um export saudável.** [código + hipótese]
`ExportStallWatchdog.observe` (`:33-47`) só vê `framesDone` e `message`.
- Na montagem, `replay_spilled_audio` (`SINK:2371-2391`) codifica **todo** o
  áudio em AAC antes de o contador "montando n/N" andar (a ordem é
  `begin_assembly` → `replay_spilled_audio` → `finish`, em
  `ENG export_encoder_main`). O `av_write_trailer` com `+faststart` reescreve o
  arquivo inteiro (`SINK:2269`; o próprio comentário em `SINK:612-616` registra
  que isso já passou de 45 s). Nesses dois trechos a mensagem não muda. O
  worker bate o coração (`SINK:617-621`), mas a tela não vê a batida.
  [hipótese] Vídeo de horas, ou arquivo de vários GB em eMMC lento, passa de
  180 s e é cancelado nos 100%.
- O relógio é `elapsedRealtime` (`EXP:658`). [hipótese] Se o processo ficar
  congelado (serviço recusado ou depois do `onTimeout`, sem wake lock), a
  primeira leitura depois vê um salto ≥ 180 s e cancela.
- Um único quadro mais lento que 180 s (acima dos 150 s do produtor) cai no
  mesmo caminho; o motor já teria falhado antes com `RenderHung`.
O iOS repete a mesma regra (`AM:5721-5749`).

**R-04 — O arquivo pronto é apagado quando a galeria e a cópia de reserva falham.** [código]
`EXP:899-900`: `fallbackCopy = … keepInAppFolder(…)` e, incondicionalmente,
`file.delete()`. Com `fallbackCopy == null` o estado vira `Failed`
(`EXP:928`) e o vídeo renderizado não existe mais. [hipótese] Gatilho
provável: `ENOSPC` na cópia para o MediaStore — a reserva copia para o mesmo
volume (`EXP:986-988`).

**R-05 — `stopSelf()` depois de um `startForeground` recusado.** [código + hipótese]
O serviço é iniciado com `startForegroundService` (`ExportForegroundService.kt:129`);
se o `startForeground` lança (`:49-63`), o código registra e chama `stopSelf()`.
[hipótese, comportamento documentado da plataforma] Parar um serviço iniciado
com `startForegroundService` sem ter chamado `startForeground` faz o sistema
derrubar o app ("did not then call Service.startForeground()" /
`ForegroundServiceDidNotStartInTimeException`). Casos em que o
`startForeground` é recusado: orçamento diário de `mediaProcessing`/`dataSync`
esgotado no Android 15+, início a partir de segundo plano no Android 12+,
tipo/permissão divergente. Também: `running` continua verdadeiro depois da
recusa (`:57-63, 128-131`), e ninguém avisa o `Exporter`; sem serviço não há
wake lock (`:64-69`).

**R-06 — Sem serviço, o export fica sem wake lock e sem aviso.** [código]
`onDestroy` só solta o wake lock (`:96-99`); não há `onTaskRemoved`. Arrastar o
app para fora limpa o ViewModel, que cancela o export
(`EditorStore.kt:1341-1348`). O teto do wake lock é 6 h (`:113`): um render
mais longo que isso em chip lento fica sem ele.

**R-07 — O export precisa de ~2× o tamanho final em disco (até ~3× com checkpoint) e nada confere antes.** [código]
Registro de pacotes + PCM + MP4 montado + cópia no MediaStore coexistem
(`ExportCheckpoint.cpp:383-387` exige só metade; `ENG` recusa abaixo de metade
da estimativa + 32 MB). A tela mostra livre e estimado e não compara
(`ExportScreen.kt:250-263, 364, 382`). [hipótese] O sistema pode limpar
`cacheDir` sob pressão de armazenamento durante o export ("Export produced no
file", `EXP:994`).

**R-08 — Cancelar pode levar 195 s e terminar como "travou".** [código]
O "desiste 15 s depois de cancelar" só existe na partida (`EXP:813-818`). No
laço principal um cancelamento espera `progress.finished`; se não vier, o
caminho é o do vigia e termina em `Failed/expfail_stuck` (`EXP:675-676`), não
em `Cancelled`. `Exporter.cancel()` chama `engine.cancelExport()` na main
(`EXP:853-859`); o caminho do vigia usa IO (`EXP:662`).

**R-09 — A Activity recriada esconde um export em andamento.** [código]
`configChanges` não cobre `locale|layoutDirection|fontScale|keyboard|navigation`
(`AndroidManifest.xml:125`); `EditorUi.Saver` não guarda `exporting`
(`K/editor/EditorScreen.kt:138-148`) e nada reabre a tela quando
`exporter.busy`. O export continua (ViewModel), mas sem progresso nem Cancelar
na tela.

**R-10 — Segundo plano durante o export.** [código + hipótese]
`surfaceDestroyed` chama `engine.detachSurface()` bloqueante na main sob
`lifecycleLock` (`EditorStore.kt:1130-1136`); `saveOnLeave` + miniatura rodam
(`:1176-1178`); `onTrimMemory` chama `engine.trimMemory` sem olhar
`exporter.busy` (`:1225-1247`). [hipótese] ANR em segundo plano se o detach
esperar um quadro do export; miniatura disputando a GPU.

**R-11 — `POST_NOTIFICATIONS` não é declarada nem pedida.** [código]
Android 13+: a notificação de progresso e a de tempo esgotado nunca aparecem.

**R-12 — Marca de saída velha depois de um job do Hyper.** [código]
`startHyper` passa `guardedExport(null)` (`EXP:307`); a marca por percentual
(`EXP:685`) não é limpa por `ProjectGuard.end`. [hipótese] Um crash sem
relação faz o próximo export começar num degrau mais leve (720p).

### 3.2 Seleção e configuração do encoder (Android)

**R-13 — Pacote com PTS diferente ou quadro omitido mata a tentativa.** [código + hipótese]
`SINK:1706-1707`: qualquer pacote cujo `ptsUs` não seja o do quadro mais antigo
pendente vira `EncodeFailed` ("omitiu, duplicou ou reordenou quadro"); no modo
registro o PTS tem de ser o do índice (`SINK:1853-1854`); no fim,
`videoPackets_ != videoFrames_` falha (`SINK:1750-1751`). Depois do 1º pacote
não há troca de degrau no sink: a tentativa inteira falha e a escada sobe —
degrau 1 (o **mesmo** encoder, Baseline, mesmo modo de taxa) e depois software.
O único encoder com contorno é `OMX.qcom.*` (CBR). [hipótese] Controle de taxa
que descarta quadros existe em outros encoders de aparelho (o caso medido foi o
OMX da Qualcomm; `c2.qti.*`, `c2.mtk.*`, `OMX.MTK.*`, `c2.exynos.*`,
`OMX.Exynos.*`, `c2.unisoc.*`/`OMX.sprd.*` nunca rodaram), em especial com taxa
baixa (as opções de fps Baixo/Reduzido geram 7,5–15 fps, `RULES:182-202`).
Resultado esperado nesses aparelhos: export que termina, mas no software
(minutos em vez de segundos) ou em 720p.

**R-14 — Dicas offline só têm segunda chance no `configure`.** [código + hipótese]
`AVE:881-884, 919-927`. Se o encoder aceita o `configure` com
`operating-rate=32767`/`priority=1` e falha no `AMediaCodec_start` (`AVE:945`)
ou nunca entrega o 1º pacote, os degraus `surface` e `buffer` caem com as
mesmas dicas e ficam marcados como falhos por 30 min. [hipótese] O Media3
Transformer mantém uma lista de SoCs que precisam de taxa de operação menor
(Snapdragon e Unisoc); o comentário em `AVE:870-875` cita o Media3 como
referência, mas o valor e as exceções dele não foram conferidos contra a
versão atual da biblioteca. `priority=1` também pode fazer o fabricante
agendar o encoder como trabalho de fundo.

**R-15 — Só o primeiro encoder de hardware usa os degraus nativos.** [código]
`find_encoder` devolve o primeiro da `MediaCodecList` (`AVE:240-320`).
Aparelhos com dois encoders de hardware para o mesmo tipo (Codec2 e OMX do
mesmo fabricante) só testam o segundo pelo degrau `legacy` (FFmpeg +
MediaCodec por buffer), que não lê o passo de linha, não limita a taxa ao teto
anunciado (E5:73) e exige que o modo de taxa pedido seja anunciado
(`SINK:379`). O degrau `legacy` tem o 1º quadro conferido (`SINK:1477-1486`),
então layout errado não vira arquivo errado — vira software.

**R-16 — Teto do aparelho calculado com máximos independentes e só do H.264.** [código]
`K/engine/DeviceProfile.kt:167-177` grava `supportedWidths.upper` e
`supportedHeights.upper` separados, sem fps; `DeviceCapabilities.cpp:426-427`
usa `max` como lado maior e `min` como lado menor. Um encoder 1920×1088 que
anuncia 1920 nos dois eixos passa por "1920×1920". O degrau 0 então não acha
encoder para o tamanho (`isSizeSupported` falso) e o export vai para o
software no tamanho cheio. `hevcEncodeMaxShort` existe no relatório
(`K/engine/DeviceReport.kt:45-67`) e não é usado em `src/main`: HEVC acima do
que o encoder HEVC faz é oferecido e termina em H.264 (degrau 1).

**R-17 — Degrau 0 no tamanho nativo não alinhado.** [código + hipótese]
`ATT:117-121`: o degrau 0 mantém, por exemplo, 854×480 ou 1080×1350. O degrau
`buffer` lê o layout e recusa o que não cabe (`AVE:796-801, 988-991`), então o
caso do Vivo Y30 não vira arquivo errado; [hipótese] em encoders que exigem
múltiplo de 16 a tentativa 0 inteira falha e recomeça alinhada no degrau 1,
com o custo da partida.

**R-18 — Latência do encoder × limite de 3 quadros em voo na Surface.** [código + hipótese]
`SINK:1636-1659`: com 3 quadros sem pacote o sink espera; em teste, 5 s.
[hipótese] Encoder que só entrega o 1º pacote depois de mais de 3 quadros de
entrada nunca passa pela Surface: 5 s perdidos e degrau marcado por 30 min; o
`buffer` (sem limite) assume. Não é falha — é custo de partida e perda do
caminho rápido.

**R-19 — Degrau `buffer` indisponível em API 26–27.** [código]
`AVE:762-763` (`AMediaCodec_getInputFormat` é API 28). Android 8.0/8.1 ficam
com `surface` → `legacy` → software.

**R-20 — EOS por buffer com `ptsUs = 0`.** [código + hipótese]
`AVE:1099-1100`. [hipótese] Encoder que valida a ordem dos tempos pode
reclamar de um tempo que volta a zero; o buffer é vazio, então o risco é baixo.

**R-21 — `frame-rate` e `i-frame-interval` como float.** [código + hipótese]
`AVE:890-897` usa float quando o valor não é inteiro (29,97; 7,5; GOP de 2 s
com taxa fracionária). [hipótese] Encoder antigo que só lê inteiro recusa o
`configure`; isso cai no degrau seguinte.

**R-22 — Perfil do H.264.** [código]
Fora do modo de segurança nenhum `profile`/`level` é pedido (`AVE:898-901`):
vale o padrão do encoder. [hipótese] Em vários encoders de aparelho o padrão é
Baseline/Main; a mesma taxa rende menos qualidade que o High. Não é falha.

### 3.3 Cor e imagem

**R-23 — A conversão de cor da Surface só é conferida no 1º quadro.** [código + hipótese]
No degrau `surface` o quadro vai em R'G'B' e é o **encoder** que converte para
Y'CbCr (`AVE:80-117, 902-904`). A conferência (`SINK:1506-1597`) compara só o
primeiro quadro guardado, com limites de 3 (média) e 16 (bloco). Um primeiro
quadro preto ou cinza (fade-in, cartela) tem croma neutro e passa com qualquer
matriz. A conferência final (`ExportContentCheck.hpp`) reprova só acima de 24
(luma) / 16 (croma): pega vídeo verde ou preto, não uma troca BT.601 ↔ BT.709.
[hipótese] Em encoders que ignoram `color-standard` na entrada por Surface, o
arquivo sai com as cores levemente deslocadas e etiquetado como BT.709, sem
aviso. O degrau `buffer` não tem esse risco (entrega Y'CbCr pronto).

**R-24 — Quadro aproximado e recurso omitido.** [código]
Decoder que não entrega o quadro exato: depois dos prazos sai o quadro mais
próximo do mesmo trecho (`OFR:134-141, 199-209`), com aviso
(`kExportFrameFallback`). Recurso que nunca chega: o quadro sai sem ele
(`OFR:183-191, 281-289`). É decisão registrada; entra aqui porque é saída
diferente da prévia e depende do decoder do aparelho.

**R-25 — Avisos da validação tolerante não chegam à tela.** [código]
`verifyWarnings`, `retrySoftwareDecode`, `softwareDecode`
(`K/engine/EnginePods.kt:551-555`) e `ExportStallWatchdog.nextAttempt` não têm
chamador em `src/main`.

### 3.4 Memória, 32 bits, calor

**R-26 — Android sem teto de orçamento por fabricante.** [código + docs]
Ver item 1 do §2.1. O monitor de GPU do HyperOS matou o Redmi 12 com ~629 MB.

**R-27 — Decoders + encoder simultâneos.** [código + hipótese]
`MediaCodecSource.cpp` não trata `AMEDIA_ERROR_INSUFFICIENT_RESOURCE` nem
codec "reclamado" de forma específica (grep vazio); há a queda genérica para
software (`:490`). [hipótese] Aparelho de entrada com poucas instâncias de
codec (várias camadas de vídeo 1080p + encoder 1080p) perde um decoder no meio;
o caminho é reabrir a fonte e, depois, decode compatível (`OFR:244-272`).

**R-28 — 32 bits.** [código]
2 slots de readback, 64 MB de cache do render, teto 1080p, 4 fontes de vídeo.
Coerente; nunca rodou num aparelho só de 32 bits.

**R-29 — Calor.** [código]
O motor só reduz quadros em voo (`ENG export_thread_main`, `thermalDegrade_`);
não pausa em `CRITICAL`/`.critical`. [hipótese] Em chip sem dissipação, um
export longo pode ser morto pelo sistema por temperatura; com R-01 aberto, o
progresso se perde.

### 3.5 iOS (subagente; itens P0 reconferidos)

**R-30 — Corrida ao ir para segundo plano.** [código + hipótese]
`AM:1800-1808`: `saveOnLeave` roda **síncrono na main** antes de o `suspend`
ser enfileirado (`AM:1819-1828`); o caminho do render não chama
`engine.pauseExport()` antes (só o da preparação, `AM:5663`). Nessa janela o
export continua submetendo trabalho de Metal. Erro de command buffer vira
`InvalidState` (`engine/gpu/metal/MetalCommon.mm:22-37`), e `suspendedExport`
exige `result == Cancelled` (`ENG export_thread_main`). [hipótese] Um erro de
"GPU não permitida em segundo plano" termina a sessão como falha de render →
tipo Memory → degrau 3 (720p, 30 fps) em vez de pausar.

**R-31 — Nome de arquivo do export não é sanitizado.** [código]
`AM:5326-5327` monta `Documents/\(projectName).mp4`; `projectName = name`
(`AM:2987`) guarda o título cru, enquanto o arquivo do projeto usa
`uniqueProjectURL` (`AM:3156-3158`, troca `/ \ :`). Com "/" no título o caminho
aponta para uma pasta que não existe; no modo checkpoint o contêiner só abre na
montagem (`SINK:860-862`), então o render inteiro termina e aí falha com erro
de E/S, que não tem nova tentativa (`ATT:170-172`). Vale só na sessão em que o
projeto foi criado (ao reabrir, o nome vem do arquivo já sanitizado).

**R-32 — Sem contrapressão por bytes no VideoToolbox.** [código + hipótese]
`pendingLimit_ = 256` quadros (`SINK:74, 793, 2007`). [hipótese] Se o
`VTCompressionSessionEncodeFrame` não bloquear, até 256 pixel buffers ficam
dentro do VideoToolbox (≈ 12,4 MB cada em 4K): candidato para o item 13 do
§2.1 (4K120 / 1080p60 fechando).

**R-33 — "Export anterior morreu" falso.** [código]
A marca em UserDefaults só é limpa quando `exporting` vira falso (`AM:963-967`);
um export pausado a mantém (`AM:1811-1813`). App suspenso e recolhido pelo iOS
→ o próximo export automático começa no degrau de memória (720p)
(`AM:5307-5317`).

**R-34 — Temp do MP4 vaza em Documents.** [código]
`<nome>.mp4.aurea-ffmpeg-*.tmp` só é removido pelo `abort()` do próprio sink
(`SINK:742-748`); morte na montagem/validação deixa um arquivo do tamanho do
vídeo, que a tela de Armazenamento não lista
(`engine/platform/ios/app/StorageView.swift:153-161`).

**R-35 — Outros do iOS.** [código]
Salvar ao sair na main no caminho do render (`AM:1800-1808`; só a preparação
saiu da main); espaço livre da tela (`volumeAvailableCapacityForImportantUsage`,
`EV:202`) ≠ o do motor (`std::filesystem::space`); aceleração do encoder
reportada como `Unknown` (`SINK:1000-1004`); 4K e fps livre até 240 sem
bloqueio (`EV:64, 446-448`); 1º pacote em 4K com só 5 quadros guardados +
5 s (`SINK:791-792`) e degraus do VideoToolbox não lembrados (`SINK:1034`);
montagem, validação e Fotos não usam GPU, mas o `suspend()` sempre cancela e o
último GOP é refeito ao voltar.

### 3.6 Conferido e sem problema

- PTS monotônico e sem B-frames: `max-bframes=0` / `max_b_frames=0`; a base de
  tempo é múltipla exata da taxa e ≤ 1 MHz para sobreviver à ida e volta em
  microssegundos do MediaCodec (`SINK:797-802`) **[código]**.
- Duração do último quadro e do áudio: `stts` do vídeo fecha em
  `frame_pts(N)`; o último pacote AAC recebe a duração real e o padding
  (`SINK:2244-2267`) **[código]**.
- Ordem das faixas no muxer: não há `MediaMuxer`; vídeo e áudio entram num
  único `avformat_write_header` (`SINK:835-874`) — sem corrida de início
  **[código]**.
- EOS que não chega: prazo de 5 s/30 s (`SINK:1689-1695`); trailer e fechamento
  conferidos (`SINK:2269-2274`) **[código]**.
- `EGL_RECORDABLE_ANDROID` e `eglPresentationTimeANDROID` presentes
  (`AVE:405-421`) **[código]**.
- Armazenamento cheio: `ENOSPC`/`EDQUOT` → `StorageFull` (`SINK:626-629`),
  mensagem própria (`EXP:732`) **[código]**.
- Partida dupla: protegida por `busy`/`sessionActive` (`EXP:238, 329, 412`)
  **[código]**.
- Áudio independente do aparelho: AAC por software do FFmpeg nas duas
  plataformas **[código]**.
- Textos `expfail_*` e `export_*` existem nos 6 idiomas **[código, subagente]**.

---

## 4. Velocidade: onde o tempo vai e o que dá para ganhar

Único número medido em aparelho que existe no repositório: no Moto g52, a
conversão para NV12 + readback eram **22,7 dos 28,3 ms de GPU por quadro**
(`EXPORT_SURFACE_DIRETO_2026-10-15.md`) **[docs]**. Todo o resto abaixo é
estimativa **[hipótese]** até ser medido; o motor já publica
`decodeWaitMs / renderMs / readbackMs / encodeMs / audioMs` por quadro no
progresso (`ENG export_encoder_main`), então a medição não precisa de código
novo.

O caminho de um quadro hoje no degrau `surface` (Android) **[código]**:
render RGBA16F → passe NV12 na GPU → readback para a CPU (fence + mapa) →
`memcpy` para a heap do alimentador (`AVE:969-977`) → `glTexSubImage2D` (2
planos) → shader NV12→RGB → `eglSwapBuffers` → o encoder converte RGB→YUV de
novo. São duas conversões de cor e três cópias de quadro que o
`surface-direct` elimina.

No fim do export **[código]**: os pacotes foram gravados no registro (1ª
escrita); a montagem lê o registro e grava o MP4 (2ª); o `+faststart`
reescreve o MP4 para mover o `moov` (3ª); a validação lê e decodifica por
software; o Kotlin copia o arquivo para o MediaStore (4ª). O áudio inteiro é
codificado em série nesse fim.

| # | Oportunidade | Hoje [código] | Ganho estimado [hipótese] | Risco |
|---|---|---|---|---|
| S1 | Ligar o `surface-direct` (zero cópia até o encoder) | Implementado e desligado; nunca rodou em Android (`EXPORT_SURFACE_DIRETO`); a conferência de conteúdo não tem NV12 para amostrar nesse degrau (`SINK:1958`) | Até ~5× menos GPU por quadro em projeto simples no Adreno 610 (22,7 de 28,3 ms somem); 1,5–3× no tempo total quando o gargalo é esse | **Alto**: EGLImage de AHardwareBuffer em Mali/PowerVR/Xclipse nunca testado; cor ±1–2 níveis; precisa de matriz de aparelhos e de chave para desligar |
| S2 | Lembrar a escada entre aberturas do app (degrau bom e degraus falhos por aparelho + codec + tamanho) | Memória de 30 min só no processo (`SINK:91-113`); iOS não lembra o VideoToolbox | Tira 5 s por degrau falho + aberturas recusadas em **todo** export nos aparelhos onde o degrau 0 falha | Baixo (validade por `Build.FINGERPRINT`/versão do app; expirar) |
| S3 | Prioridade das threads do export e dica de desempenho ao sistema | Nenhuma prioridade (`ENG`, `AVE:631`); `Thread.cpp:17-37` já tem a função | 10–30% em big.LITTLE quando o export cai nos núcleos pequenos; mais em segundo plano | Baixo; medir calor. `PerformanceHintManager` (API 31+) é opcional |
| S4 | Fim sem reescrever: reservar o `moov` (o tamanho de cada amostra já é conhecido pelo registro) em vez de `+faststart` | `SINK:864`, `av_write_trailer` reescreve tudo | Segundos a minutos em arquivo grande / eMMC lento; também tira a causa de R-03 | Médio (cálculo do tamanho do `moov`; manter a queda para `+faststart`) |
| S5 | Fim sem a 4ª escrita: montar o MP4 direto no item pendente do MediaStore, ou cópia pelo kernel | `EXP:1008-1032`, cópia de 1 MiB por vez; `K/state/GalleryPublish.kt` | Metade do tempo de "100%" em arquivo grande; metade do disco exigido (R-07) | Médio (API ≤ 28 e a reserva continuam com cópia) |
| S6 | Validação mais barata | Decode por software de tudo até 900 quadros, GOPs amostrados acima (`SINK:176-245`); 2–4 threads | 5–40 s por export (1080p–4K em chip fraco) | Médio: é a rede de segurança. Proposta: sempre amostrado quando o degrau foi confirmado no 1º pacote, mantendo início, fim e amostras de imagem |
| S7 | Áudio fora do caminho do vídeo | Mix na thread do encoder a cada quadro (`ENG export_encoder_main`); AAC inteiro em série na montagem (`SINK:2371-2391`) | `audioMs` por quadro sai do caminho crítico; a montagem perde 1–3% da duração do vídeo em tempo de AAC | Baixo (thread própria; AAC codificado durante o render para um registro lateral em vez do PCM — também reduz disco) |
| S8 | Não renderizar de novo quadro idêntico (foto parada, texto parado, cartela) | Todo quadro é renderizado e lido (`ENG export_thread_main`); o PreRender não é reaproveitado no export (`PRERENDER_INTEGRATION_2026-10-13.md`) | Enorme em projeto de fotos/texto (o render some nos trechos parados); zero em vídeo | Médio-alto: precisa de uma assinatura do quadro que prove igualdade (partículas, efeitos com tempo, expressões) |
| S9 | Latência da Surface e encoders extras | Limite fixo de 3 em voo (`AVE:52`); só o 1º encoder nos degraus nativos (R-15, R-18) | Recupera o caminho rápido em encoders com latência > 3; evita o `legacy` | Baixo-médio |
| S10 | Software mais rápido | `thread_count = 4` fixo e fatias por thread (`SINK:912, 969-971`); kvazaar `ultrafast`, 4 threads (`SINK:943-944`) | 1,3–2× nos degraus de software em 8 núcleos | Baixo (usar o número de núcleos grandes; conferir memória em 32 bits) |
| S11 | Decode do export | `DecodeMode::Playback`, espera em fatias de 10 ms (`OFR:302-307`) | Depende de `decodeWaitMs` medido; relevante em projeto de vídeo com cortes | Médio (mexe no caminho que tem mais contornos por aparelho) |
| S12 | Partida em paralelo | O `Renderer` do export não pré-aquece (`OFR:44-49`) e compila no 1º quadro; o encoder abre antes | 1–3 s na partida; mais em PowerVR (dezenas de segundos por shader, item 7 do §2.1) | Baixo-médio (compilar o 1º quadro enquanto o encoder abre) |
| S13 | Kotlin | `fsync` do diagnóstico a 1 Hz (`EXP:558-563`), leitura do progresso a 10 Hz na main (`EXP:651-654`) | Pequeno; menos disputa de E/S com o muxer em eMMC | Baixo |
| S14 | iOS zero cópia (Metal → `CVPixelBuffer` do pool → VideoToolbox) | 2 cópias de CPU por quadro (~25 MB em 4K) + a do período de teste | Grande em 4K; também ataca R-32 | **Alto**: sink de vídeo próprio fora do FFmpeg; só valida em iPhone |
| S15 | Passar adiante clipe intocado (sem recodificar) | Não existe | 10× ou mais em projeto que é só corte | **Alto** e fora do tamanho da 0.5.3: GOP, SPS/PPS diferentes por trecho, taxa, cor. **Não recomendado agora** |
| S16 | Modo assíncrono do MediaCodec | Síncrono, com esperas de 1–2 ms (`SINK:1658, 1435`) | Pequeno (a fila já faz a contrapressão) | Médio. **Não recomendado agora** |

Já está certo e deve ficar: `operating-rate`/`priority` como dica (com R-14
corrigido), encoder solto antes da montagem (`SINK:2336-2337`), profundidade do
pipeline pela política do aparelho, fatias do OpenH264 e NEON (E5).

---

## 5. Lista priorizada

Regras para a Fase B:

- Paridade (AGENTS.md): toda mudança de comportamento entra em Android/Kotlin e
  iOS/Swift no mesmo trabalho; regra no motor C++ sempre que possível.
- Registrar em separado: implementado / compilado / validado em aparelho.
- Sem lista de fabricantes sem evidência (posição do ASTRA,
  `ASTRA_EXPORT_INVESTIGATION_2026-10-13.md`): preferir detecção por
  comportamento + memória persistida do que funcionou.
- "Não verificável sem aparelho" = só um aparelho daquela família prova.

### P0 — o export falha, trava ou perde o resultado (12)

| ID | O que | Onde mexer | Como testar | Não verificável sem aparelho |
|---|---|---|---|---|
| **P0-1** | Checkpoint sobrevive à abertura do app e a retomada é procurada sempre (R-01, R-02) | `K/state/CacheStorage.kt` `enforceLimits` (`:116-123`): não apagar registro com manifesto válido (ou mover o checkpoint para fora de `cacheDir/export`: `JNI:666` + `ExportCheckpoint.cpp:189-199`); `EXP:437-453` procurar `resumeCandidate` em qualquer API e motivo; `ExitDiagnostics.kt:156-171` marca em arquivo para API < 30. iOS: conferir que `<Caches>/export-checkpoints` não é varrido (`StorageView.swift:67-81`) | JVM: regra da limpeza. Instrumentado no emulador (API 28, 30, 35): export longo → `am kill` → reabrir → continua do GOP (estender `ExportCheckpointDeviceTest`). Host: `ExportCheckpoint.inl` já cobre o motor | Morte por LMK/gerenciador de bateria real (MIUI, ColorOS, One UI) |
| **P0-2** | Nunca apagar o vídeo pronto sem uma cópia confirmada (R-04) | `EXP:891-931` `publish`, `EXP:980-990` `keepInAppFolder`: se as duas cópias falham, mover (`renameTo`) para `filesDir/exports` e terminar como `Done` com `savedToGallery=false`; `res/xml/export_paths.xml` | JVM (decisão extraída) + instrumentado com resolver falso | Comportamento do MediaStore de cada fabricante com disco cheio |
| **P0-3** | Vigia da tela não cancela export vivo (R-03) | Motor: `Engine::export_progress` publica um contador de vida do fim (bytes gravados / amostras de áudio) ou a idade da última batida; `SINK:2371-2391` atualiza o contador. `K/state/ExportStallWatchdog.kt:33-47` e `AM:5721-5749` (iOS, paridade): observar fase (`flags`) e o contador; zerar quando o intervalo entre leituras passa de ~5 s | Host: `ExportWatchdog.inl` (fim longo simulado). JVM: `ExportStallWatchdogTest` (salto de relógio, mudança de fase). Swift: mesma tabela de casos | Tempo real de `+faststart` e AAC em eMMC lento |
| **P0-4** | Serviço recusado não derruba o app e o `Exporter` fica sabendo (R-05, R-06) | `K/state/ExportForegroundService.kt:44-63, 96-99, 121-131`: nunca `stopSelf()` antes de um `startForeground` aceito (tentar o outro tipo declarado; se recusar, ficar como serviço comum ou não usar `startForegroundService`); expor `refused`; limpar `running`; `onTaskRemoved`. `EXP:388-415` avisa a tela | Instrumentado no emulador API 34/35/36 (esgotar o orçamento com `adb shell cmd activity`/`device_config`); JVM para a máquina de estados | Políticas de FGS alteradas por fabricante |
| **P0-5** | Encoder que omite quadro ou muda o PTS não custa a tentativa inteira (R-13) | `SINK:1682-1722` `drain_native`, `SINK:1827-1868` `video_packet`: reconhecer "pacote do quadro seguinte" (omissão) como caso próprio; fora do período de teste devolver `Busy` e reabrir o **mesmo** encoder em CBR (como já faz Surface→buffer, `SINK:1643-1653`), continuando do checkpoint; lembrar por codec (junto com S2). `AndroidCodecName.hpp:36-43` deixa de ser a única regra. `ATT:145-201` se precisar de um tipo novo | Host: `test_ffmpeg_export.cpp` com encoder falso que omite/duplica; novo token `debug.aurea.enc.fail=drop` para o emulador (`AVE`, `debug.aurea.enc.allowsw=1`) | Quais encoders reais descartam quadros (MTK, Exynos, Unisoc, `c2.qti`) e se o CBR resolve neles |
| **P0-6** | Dicas offline com segunda chance completa (R-14) | `AVE:919-948`: repetir sem `operating-rate`/`priority` também quando o `start` falha; `SINK:1425-1438` e `1627-1680`: 1º pacote que não chega com as dicas → reabrir o mesmo degrau sem elas antes de condená-lo; lembrar por codec | Host: `test_export_sink_startup.cpp`; emulador: token `debug.aurea.enc.fail=hints-start` / `hints-stall` | Quais SoCs recusam ou degradam com as dicas |
| **P0-7** | Teto de orçamento de memória no Android (R-26) | `JNI:626-631`: `config.memoryBudgetCapBytes` (o valor do `1345f1a0`: 480 MB em Xiaomi/Redmi/POCO, 1 GB nos demais — decisão a confirmar com o dono, porque é regra por fabricante); `ENG:1123` já aplica | Host: teste do orçamento. Emulador: `debug.aurea.memory_budget_mb` | O limite real do monitor de GPU do HyperOS/MIUI por modelo |
| **P0-8** | Cancelar sempre termina (R-08) | `EXP:651-680, 853-859`: 15 s depois do cancelar do usuário no laço principal → `Cancelled`; `cancelExport` fora da main. iOS: conferir o mesmo no `AM:5525-5535, 5807-5840` | JVM + `ExportLifecycleStabilityTest` no emulador | — |
| **P0-9** | iOS: pausar antes de salvar ao ir para segundo plano (R-30) | `AM:1784-1829`: `engine.pauseExport()` no topo, salvar fora da main; `ENG export_thread_main`: falha vista com a marca de suspensão conta como suspensão; `engine/gpu/metal/MetalCommon.mm:22-37`: código próprio para "GPU não permitida em segundo plano" | Host: `ExportCheckpoint.inl` com suspensão no meio de um quadro que falha. CI do iOS só compila | Tudo: quando o Metal passa a recusar trabalho em segundo plano |
| **P0-10** | iOS: nome do arquivo do export sanitizado + sonda de escrita na pasta (R-31) | `AM:5326-5327` (a regra de `AM:3157`); `Engine::start_export_checked` (perto do bloco de disco): criar/apagar um arquivo de teste na pasta de saída. Android: conferir que `EXP:331` já usa nome seguro | Host: teste de `start_export` com pasta inexistente; teste Swift do nome | — |
| **P0-11** | iOS: fila pendente limitada por bytes (R-32, item 13 do §2.1) | `SINK:793, 2007`: no Apple, `pendingLimit_` por bytes de NV12 (~96 MB) em vez de 256 quadros | Host: teste do limite. | Tudo: se o VideoToolbox acumula; se isso é a causa do 4K120/1080p60 |
| **P0-12** | Espaço em disco conferido antes, com o tamanho real exigido (R-07) | `K/editor/ExportScreen.kt:241-265` e `EV:597-646`: bloquear com mensagem quando livre < exigido; a conta vem do motor (`export_checkpoint::log_fits` + cópia final) por uma função única exposta nas duas pontes | JVM/Swift da regra; `ExportPreflightDeviceTest` no emulador | — |

### P1 — o arquivo sai diferente do pedido, ou a pessoa fica sem saber (12)

| ID | O que | Onde mexer | Como testar | Não verificável sem aparelho |
|---|---|---|---|---|
| **P1-1** | Conversão de cor da Surface conferida de verdade (R-23) | `SINK:1506-1597`: conferir num quadro com croma suficiente (manter o período de teste até aparecer um, com teto) ou enviar um padrão de barras de cor numa sessão descartável na abertura; guardar o veredito por codec (com S2). Apertar o limite de croma da conferência final só para esse veredito | Host: decoder falso com matriz 601; emulador com `debug.aurea.enc.fail=surface-verify` | Quais encoders ignoram `color-standard` na Surface |
| **P1-2** | Teto do aparelho pelo que o encoder aceita de fato (R-16) | `K/engine/DeviceProfile.kt:147-190`: gravar `isSizeSupported`/`areSizeAndRateSupported` para os tamanhos oferecidos (subir `SCHEMA`); `DeviceCapabilities.cpp:422-452`; `K/editor/ExportScreen.kt:195, 503, 552` usar `hevcEncodeMaxShort`, não oferecer o que o encoder não faz, não escolher upscale por padrão. iOS: `EV:64, 446-448` | JVM `DeviceReportTest`; host `DeviceCapabilities`; `ExportLadderDeviceTest` | A tabela real de cada SoC |
| **P1-3** | Diagnóstico que permita triagem por SoC | `K/diagnostics/ExportDiagnosticReport.kt:48-58`, `ProblemReport.kt:29-43`: GPU (renderer/driver, `engine.deviceSummary()`), SoC em API < 31 (`ro.board.platform`, `/proc/cpuinfo`), RAM total/livre, disco livre, térmico, caminho do encoder (`nativeLastExportEncoderPath`, `JNI:5880`), degraus tentados. Envio automático **com consentimento** quando o export falha. iOS: `lastExportEncoderPath` (`AE:5845`) no relato | JVM `ProblemReportExportTest`; revisão de privacidade | — (é o que substitui o aparelho que não temos) |
| **P1-4** | Export em andamento continua visível (R-09) | `AndroidManifest.xml:125` (`fontScale\|locale\|layoutDirection\|keyboard\|navigation`); `K/editor/EditorScreen.kt:138-148, 419`: reabrir a tela quando `exporter.busy` | Emulador: mudar `font_scale` no meio do export | — |
| **P1-5** | Notificação de progresso aparece (R-11) | `AndroidManifest.xml` + pedido de `POST_NOTIFICATIONS` no 1º export (`ExportScreen.kt:224-227`) | Emulador API 33+ | — |
| **P1-6** | Marcas de "o app morreu" sem falso positivo (R-12, R-33) | `EXP:402-414` (jobs do Hyper); `AM:963-967, 1811-1813` (valor "pausado") | JVM / Swift | — |
| **P1-7** | Avisos da validação e do decode compatível chegam à tela (R-25) | `EXP:1120-1129` `noticeFor`; `EnginePods.kt:551-555`; iOS equivalente | JVM / Swift | — |
| **P1-8** | iOS: sobras e números coerentes (R-34, R-35) | Varredura de `*.aurea-ffmpeg-*.tmp` (`StorageView.swift:67-81` ou `ENG` na abertura); mesmo número de espaço livre na tela e no motor; encoder real no relato | Swift + host | Fotos com arquivo grande |
| **P1-9** | Perfil High quando anunciado (R-22) | `AVE:167-328` (ler High e nível), `AVE:898-901` | Host + `AstraExportMatrixDeviceTest` (ffprobe do perfil) | Encoders que anunciam High e falham com ele |
| **P1-10** | Robustez barata do encoder (R-19, R-20, R-21) | `AVE:1099-1100` (EOS com o último PTS + duração); `AVE:890-897` (repetir com `frame-rate` inteiro se o float for recusado); `AVE:760-829` (API 26–27: layout por `stride` do formato de saída ou seguir sem o degrau) | Host + emulador API 26 | Encoders antigos reais |
| **P1-11** | Segundo plano não disputa com o export (R-10) | `EditorStore.kt:1130-1136, 1176-1178, 1225-1247`: sem miniatura e com `trim` mais leve enquanto `exporter.busy`; conferir com o motor se o `detachSurface` espera o export | `ExportLifecycleStabilityTest` | ANR real em aparelho de pouca RAM |
| **P1-12** | Pausa por calor crítico (R-29) | `Engine::set_thermal` + `export_thread_main`: em crítico, pausar com checkpoint e avisar; telas mostram "esperando esfriar". Depende de P0-1 | Host: `ExportCheckpoint.inl` + térmico injetado | Curva térmica de cada chip |

### P2 — velocidade (14)

Na ordem em que eu faria; ganho e risco no §4.

| ID | O que | Onde mexer | Como testar | Não verificável sem aparelho |
|---|---|---|---|---|
| **P2-1** | S2: memória persistente da escada | `SINK:91-113, 1023-1101` + armazenamento pequeno (arquivo em `cacheDirectory` com versão do app e `Build.FINGERPRINT`); iOS inclui os degraus do VideoToolbox | Host: `test_export_sink_startup.cpp` | — |
| **P2-2** | S3: prioridade das threads | `ENG export_thread_main`, `export_encoder_main`, `AVE:631`: `set_current_thread_priority`; iOS QoS `userInitiated` | Medir `renderMs/encodeMs` antes e depois | Ganho real em big.LITTLE; calor |
| **P2-3** | S4 + S5: fim sem reescrever e sem a cópia extra | `SINK:835-874` (reserva do `moov`), `SINK:2393-2413`; `EXP:1008-1032` + `GalleryPublish.kt` | Host: MP4 com `moov` na frente conferido por ffprobe; emulador API 29/30/35 | eMMC lento real |
| **P2-4** | S7: áudio em thread própria e AAC durante o render | `ENG write_export_audio`, `export_encoder_main`; `SINK:2130-2221, 2371-2391` | Host: `test_offline_audio.cpp`, `ExportAudioProgress.inl` (áudio idêntico amostra a amostra) | — |
| **P2-5** | S6: validação amostrada para degrau confirmado | `SINK:176-245, 2433+` | Host: `test_export_content.cpp`, `FFmpegExport` | — |
| **P2-6** | S12: partida em paralelo | `Engine::start_export_checked`, `OFR:34-53` | Host: `ExportStartPhase.inl`, `ExportSourceStartup.inl` | PowerVR/Mali antigos |
| **P2-7** | S9: latência da Surface adaptável e degraus nativos para todo encoder de hardware | `AVE:52, 167-328, 1165-1167`; `SINK:1045-1072, 1636-1659` | Host + emulador com `allowsw` | Encoders com latência alta |
| **P2-8** | S10: software mais rápido | `SINK:906-979` | Medir no emulador (libopenh264) e no g52 | arm64/armv7 reais |
| **P2-9** | S13: Kotlin | `EXP:558-563, 651-701` | JVM | — |
| **P2-10** | S1: ligar o `surface-direct` | `JNI:626-685` (`enableDirectSurfaceExport`), com chave remota e por aparelho aprovado; amostra de conteúdo para o degrau (`SINK:1958`) | Host `ExportDirectSurface.inl` (já existe); emulador com `debug.aurea.enc.direct=1`; **matriz de aparelhos obrigatória** | Tudo: EGLImage/AHardwareBuffer por GPU, cor, ganho |
| **P2-11** | S8: quadro idêntico não é renderizado de novo | `ENG export_thread_main`, `OFR`, assinatura no `Renderer`/`FrameSnapshot` | Host: golden frames + comparação quadro a quadro com o caminho atual | — |
| **P2-12** | S11: decode do export | `OFR:120-309`, `engine/platform/android/MediaCodecSource.cpp` | Host `ExportSlowMedia.inl`; aparelho | Decoders reais |
| **P2-13** | S14: iOS zero cópia | sink de vídeo próprio do iOS | Só iPhone | Tudo |
| **P2-14** | S15/S16: passar adiante clipe intocado; MediaCodec assíncrono | — | — | **Não fazer na 0.5.3** |

### Contagem

- **P0: 12** (8 Android/motor, 3 iOS, 1 nas duas telas).
- **P1: 12**.
- **P2: 14** (2 marcados "não fazer agora").

---

## 6. Como testar e o que fica sem prova

### 6.1 O que existe

- **Host (motor)**: `engine/tests/test_ffmpeg_export.cpp`, `test_export_attempts.cpp`,
  `test_export_sink_startup.cpp`, `test_export_content.cpp`,
  `test_export_packet_log.cpp`, `test_export_recovery.cpp`,
  `test_offline_audio.cpp`, `test_offline_frame_queue.cpp` e os `.inl`
  `ExportWatchdog`, `ExportCheckpoint`, `ExportDirectSurface`,
  `ExportStartPhase`, `ExportDensityPressure`, `ExportSlowMedia`,
  `ExportTransferDeadline`. E5 registra 255 testes no grupo `Export` com 1
  falha intermitente (`ExportWatchdog.inl:140`) **[docs]**. Atenção da memória
  do projeto: no MSVC em pt-BR nenhum diretório de build do host é confiável
  depois de mudar header — apagar os `.obj` antes de confiar numa falha.
- **JVM Android**: `android/app/src/test/.../state/` (`ExportStallWatchdog`,
  `ExportStartupFailure`, `VideoExportRules`, `GalleryPublish`,
  `HevcEncoderCheck`), `engine/DeviceReportTest`,
  `diagnostics/ExitDiagnosticsTest` — `:app:testUiTestUnitTest` **[código]**.
- **Instrumentado**: `android/app/src/androidTest/.../editor/` —
  `ExportCheckpointDeviceTest`, `ExportLadderDeviceTest`,
  `ExportLifecycleStabilityTest`, `ExportPreflightDeviceTest`,
  `AstraExportDeviceTest`, `AstraExportMatrixDeviceTest`,
  `AudioExportFailureDeviceTest`, `GalleryVideoExportDeviceTest` e outros;
  `:app:connectedUiTestAndroidTest -PaureaAbi=x86_64`; vários se pulam sem
  argumento do runner (`aureaAstra`, `aureaFieldVideo`, `aureaGalleryVideo`,
  `aureaExportQuality`) **[código]**. Nenhum teste cobre
  `ExportForegroundService`, `onTimeout`, `retryGallery`, `keepInAppFolder` ou
  `consumeExportDeath` **[código]**.
- **Injeção de falha no aparelho/emulador** (`AVE:152-165`, `AndroidVideoEncoder.hpp`):
  `debug.aurea.enc.allowsw=1` (o emulador não tem encoder H.264 de hardware —
  sem isto os degraus nativos não rodam, E5), `debug.aurea.enc.skip=surface,buffer,legacy`,
  `debug.aurea.enc.fail=surface-open|surface-stall|surface-hang|surface-verify|buffer-open|buffer-stall|buffer-verify`,
  `debug.aurea.enc.direct=1`, `debug.aurea.memory_budget_mb`, `debug.aurea.gpu=gles`.
  A Fase B deve acrescentar `drop`, `hints-start`, `hints-stall`.
- **CI**: `.github/workflows/host-export-check.yml`, `android-export-verify.yml`,
  `build-phone-tests.yml`, `build-apks.yml`, `astra-native-ios.yml`,
  `build-ipa.yml`. iOS só compila e roda o `ParityExportProbe` no simulador.

### 6.2 Matriz mínima de aparelhos para chamar a 0.5.3 de "todos os processadores"

Para cada um: H.264 1080p30, H.264 1080p60, HEVC 1080p30, 4K30 se oferecido,
480p, fps Baixo (7,5), projeto com 2 vídeos + transição, projeto de fotos +
texto, 3D com motion blur, export de 10 min em segundo plano com tela
apagada, matar o app no meio e reabrir. Conferir com ffprobe: quadros, PTS,
dimensões, perfil, cor; e o `encoderPath` do diagnóstico.

A coluna "exemplo" é sugestão de mercado, não dado do repositório; o que
importa é o encoder (o inventário que o `AstraExportMatrixDeviceTest` grava) e
a GPU de cada aparelho escolhido.

| Família | Exemplo | Por quê |
|---|---|---|
| Qualcomm OMX | Moto g52 (existe) | Base medida |
| Qualcomm Codec2 (`c2.qti`) | um Snapdragon recente | R-13/R-14 nunca rodaram em Codec2 |
| MediaTek Helio + PowerVR | Helio P35 (Vivo Y30; LG K61 tem a mesma GPU) | Itens 6–8 nunca validados; caminho GLES |
| MediaTek Helio + Mali | um Helio G de entrada com 4 GB | Teto de memória (item 1), LMK |
| MediaTek Dimensity | qualquer | Nenhum dado |
| Exynos Mali | um Galaxy A com Exynos | Nenhum dado de export; regra de planos da Samsung |
| Exynos Xclipse | um Galaxy S com Exynos 2200+ | Nenhum dado |
| Unisoc | um Tiger T6xx ou SC9863A | Nenhum dado |
| Tensor | um Pixel 6 ou mais novo | Nenhum dado |
| 32 bits puro / Android 8–9 | qualquer de 2018 | R-19, 32 bits real |
| iPhone | 11/XR (4 GB) e um recente | Itens 12, 13; P0-9 a P0-11 |

### 6.3 O que **não** dá para afirmar sem esses aparelhos

1. Que algum encoder além do `OMX.qcom` descarta quadros ou altera PTS (R-13),
   e que o CBR resolve nele.
2. Que `operating-rate=32767`/`priority=1` é aceito ou degrada (R-14).
3. Que a Surface de cada encoder respeita `color-standard` (R-23).
4. Que o `surface-direct` funciona (EGLImage de AHardwareBuffer) em
   Mali/PowerVR/Xclipse/Adreno, e quanto ganha (S1).
5. Os limites reais de memória que fazem o sistema matar o app (HyperOS,
   One UI, jetsam do iOS).
6. O comportamento do serviço em primeiro plano em ROMs de fabricante.
7. Tudo do VideoToolbox: latência do 1º pacote, se acumula quadros, limites de
   4K/fps por modelo, sessão invalidada em segundo plano.
8. O ganho real de qualquer item do §4 (só o do Moto g52 foi medido, e antes
   do `surface-direct`).

O que **dá** para provar sem aparelho: P0-1, P0-2, P0-3, P0-4 (emulador),
P0-8, P0-10, P0-12, a lógica de P0-5/P0-6 (com injeção de falha), P1-2 a P1-7,
P2-1, P2-3 a P2-6, P2-9.

---

## 7. Notas para continuar na Fase B

1. **Ordem sugerida**: P0-1 → P0-2 → P0-3 → P0-4 → P0-8 (Kotlin/Swift, baixo
   risco, testáveis aqui) → P1-3 (diagnóstico: é o que vai dizer o que
   acontece nas famílias sem aparelho) → P0-5/P0-6 com P2-1 (a memória
   persistida serve aos três) → P0-7, P0-12 → iOS P0-9/10/11 → P1 → P2.
2. **Conflitos**: `Engine.cpp` está em edição por outros agentes; os itens que
   tocam nele (P0-3, P0-9, P0-10, P1-12, P2-2, P2-4, P2-6, P2-11) esperam o
   aviso de que o motor está livre. `FFmpegExportSink.cpp`,
   `AndroidVideoEncoder.cpp`, `Exporter.kt`, `ExportForegroundService.kt`,
   `CacheStorage.kt`, `ExportStallWatchdog.kt` e `AureaModel.swift` não
   mudaram durante esta leitura, mas conferir `git status` antes de editar.
3. **Decisões que são do dono**: (a) teto de memória por fabricante (P0-7) é
   uma regra por marca — o projeto evitou isso até hoje; (b) envio automático
   de diagnóstico de falha com consentimento (P1-3); (c) ligar o
   `surface-direct` sem matriz de aparelhos (P2-10) — recomendação: não ligar
   por padrão na 0.5.3 sem pelo menos um aparelho por família de GPU.
4. **Documentos a atualizar junto**: `docs/architecture/EXPORT.md` (está
   descrevendo um estado que não existe), `STABILITY_IOS_2026-10-04.md`
   (sink nativo removido), o comentário de `CacheStorage.kt:16` ("ao fim de
   cada export", sem chamador) e o de `ExportForegroundService.kt:76-82`
   (promete retomada que R-01 impede).
5. **Não confundir as duas escadas**: a de **tentativas** (0..6, `ATT`, decide a
   tela + motor, recomeça o export) e a de **encoders** (`SINK build_rungs`,
   troca só até o 1º pacote, sem recomeçar). Os itens P0-5 e P0-6 existem para
   que uma falha de encoder depois do 1º pacote seja resolvida na segunda sem
   passar pela primeira.
6. **O que não foi lido linha a linha** (só por grep/trechos): o corpo inteiro
   de `Engine::start_export_checked` e `export_thread_main`,
   `SINK:2433-2783` (`decode_validate`), `ExportRecovery.cpp`,
   `ExportPacketLog.cpp`, `ImageEncode.cpp`, `GlVideoBridge.cpp`,
   `MediaCodecSource.cpp` (decode) e o caminho do Hyper
   (`export_encoder_hyper_loop`). A exportação de imagem/GIF/sequência ficou
   fora do escopo.

---

## Fase B — feito

12/11/2026, mesma branch, nada commitado. Escopo final (dono): **os 12 P0**
(o export falha, trava ou perde o resultado). P1/P2 só ficaram os que já
estavam prontos e testados; os não testados foram desfeitos (lista abaixo).
Regras seguidas: nenhuma mudança visível no Android nem no iOS (só mensagens
que já existiam), `surface-direct` continua desligado, nenhuma regra por
fabricante, sem `try/catch` novo no motor.

Legenda de "Verificado":
**teste** = teste novo/existente rodou e passou (host `aurea_tests`, JVM
`:app:testUiTestUnitTest`); **NDK** = compilado com o comando exato do NDK
(arm64-v8a e armeabi-v7a) a partir de `android/app/.cxx/RelWithDebInfo/3l24xg6o`,
objetos em pasta temporária; **compilado** = compilado no host/Kotlin, sem
teste do caminho; **lido** = só revisão estática (Swift/ObjC: precisa do CI do
iOS). **Nada rodou em aparelho físico nem no emulador** (o emulador não foi
usado: ele é compartilhado com o dono e o export de vídeo do motor precisa do
APK nativo inteiro, fora do tempo pedido).

### Decisões do dono aplicadas

1. **Teto de memória sem marca (P0-7)** — `export/ExportMemory.hpp`: até 3 GB →
   384 MB, 4 GB → 512 MB, 6 GB → 768 MB, acima → 1 GiB; processo de 32 bits →
   no máximo 512 MB. Vale só para o orçamento do EXPORT (fila, caches do render,
   quadros em voo), a prévia não muda. A cada export que o sistema matou, o
   próximo (inclusive a continuação do checkpoint) começa um degrau abaixo:
   100 → 75 → 50 → 35 %, piso 128 MB; volta a 0 no primeiro que termina.
2. **Diagnóstico só local e completo** — nada é enviado sozinho; o arquivo do
   relato que já existe ("Relatar um problema") ganhou GPU/driver, SoC (também
   antes do Android 12: `Build.HARDWARE/BOARD`, `ro.board.platform`,
   `ro.hardware`, `ro.soc.model`, `/proc/cpuinfo`), RAM total/livre/`lowRam`/
   `memClass`, disco livre, calor, economia de energia, estado do serviço em
   primeiro plano, degrau de memória, cada tentativa da escada (degrau, código,
   motivo, quadro) e o caminho do encoder com cada degrau que falhou, foi
   pulado ou aprendeu algo (CBR, sem dicas, tempo rearredondado).
3. **`surface-direct`** desligado, não mexido.

### P0 — o que mudou

| ID | O que mudou | Verificado | Arquivos |
|---|---|---|---|
| **P0-1** checkpoint apagado na abertura / retomada só no Android 11+ | A limpeza da abertura apaga a pasta do export **menos** o registro e o manifesto do checkpoint (o motor já os envelhece). Marca própria "export de vídeo em curso" no disco (qualquer Android, qualquer motivo de morte); o checkpoint é procurado em todo export Automático, não só depois de uma morte vista pelo sistema. | teste (JVM `ExportCheckpointCleanupTest`, 4) + compilado | `CacheStorage.kt`, `ExportKillMemory.kt` (novo), `Exporter.kt` |
| **P0-2** vídeo pronto apagado se galeria e reserva falham | O temporário só sai com uma cópia confirmada; senão é **movido** (rename, sem espaço extra) para `filesDir/exports`, compartilhável pelo mesmo FileProvider, e o resultado é o "ficou na pasta do app" de sempre. Entra na conta e na limpeza do Armazenamento. | teste (JVM `ExportPublishRulesTest`) | `Exporter.kt`, `GalleryPublish.kt`, `AppStorage.kt`, `res/xml/export_paths.xml` |
| **P0-3** vigia da tela cancela export vivo nos 100 % | Motor: em "finalizando/montando/validando" a mensagem leva o segundo da última batida do worker (muda enquanto o AAC/trailer/validação andam, para se travar). Telas: o tempo em que ninguém leu o progresso (processo congelado, app suspenso) não conta (intervalo > 5 s sai da conta). | teste (JVM `ExportStallWatchdogTest`, 13); motor compilado + NDK; iOS lido (harness atualizado) | `Engine.cpp` (`export_progress`), `ExportStallWatchdog.kt`, `Exporter.kt`, `AureaModel.swift` (`ExportStallWatch`) |
| **P0-4** `startForeground` recusado | Tenta os dois tipos declarados (Android 15+: `mediaProcessing`, depois `dataSync`); recusado, `running` deixa de mentir, a recusa vai para o relato, e um wake lock parcial **do processo** segura a CPU desde o início do export até o fim (o do serviço o substitui quando ele aceita). `onDestroy` com export em curso volta ao wake lock de reserva. | teste (JVM `ExportForegroundServiceTest`, 3) + compilado | `ExportForegroundService.kt`, `Exporter.kt` |
| **P0-5** encoder que descarta quadro / muda o tempo derruba a tentativa | `export/EncoderPacketTiming.hpp`: pacote com o tempo rearredondado (até meio quadro) é o mesmo quadro e vai no relógio do export. Quadro descartado: no período de teste, o **mesmo** degrau de novo em CBR; depois dele, "refazer" (Busy) → a **mesma tentativa** continua do último GOP do checkpoint em CBR (aprendido para aquele encoder, persistido), sem descer para o software. Em CBR e descartando de novo, o degrau falha e fica pulado. Vale para os degraus nativos, o FFmpeg/MediaCodec e o VideoToolbox. A escada nativa agora tenta **todos** os encoders de hardware do tipo (antes só o primeiro); HEVC com hardware que falha segue para o H.264 do aparelho, não para o HEVC por software. | teste (host: `Export053.PacketTime…`, `FFmpegExport.RetimedEncoderPacket…`, `FFmpegExport.DroppedPictureAfterTheStart…`); Android NDK | `EncoderPacketTiming.hpp` (novo), `FFmpegExportSink.cpp/.hpp`, `AndroidVideoEncoder.cpp/.hpp` |
| **P0-6** dicas offline só tinham 2ª chance no `configure` | Recusa no `configure` relaxa uma coisa por vez (dicas, taxa fracionária); `start` recusado com as dicas → reabre uma vez sem elas; 1º pacote que não vem (ou erro) com as dicas → o mesmo degrau sem elas antes de trocar de encoder; o que funcionou fica aprendido por encoder. EOS por buffer leva o tempo do último quadro (antes 0). | NDK (arm64 e v7a), lido | `AndroidVideoEncoder.cpp/.hpp`, `FFmpegExportSink.cpp` |
| **P0-7** teto de memória perdido no Android | Ver decisão 1. `Engine::set_export_memory_step`, `export_budget_bytes`; ponte JNI e iOS; Kotlin e Swift sobem o degrau depois de uma morte e zeram no sucesso. | teste (host `Export053.DeviceMemoryCap…`, `…MemoryStepLowers…`, `…MemoryStepChangesTheBudgetButNeverThePixels…` — imagem e som idênticos no degrau 0 e 3); JVM; iOS lido | `ExportMemory.hpp` (novo), `Engine.hpp/.cpp`, `aurea_jni.cpp`, `AureaEngine.kt`, `Exporter.kt`, `ExportKillMemory.kt`, `AureaEngine.h/.mm`, `AureaModel.swift` |
| **P0-8** Cancelar podia levar 195 s e virar "travou" | 15 s depois do Cancelar da pessoa, a tela encerra como cancelado; `cancelExport` fora da main. | teste (JVM `userCancelExpired` em `ExportStallWatchdogTest`) + compilado | `Exporter.kt`, `ExportStallWatchdog.kt` |
| **P0-9** iOS: corrida ao ir para segundo plano | iOS pausa o export (`pauseExport`) **antes** de gravar o projeto; motor: falha que chega com a marca de suspensão posta (erro do Metal em segundo plano) é tratada como pausa (checkpoint fica, continua ao voltar). | motor compilado + NDK; iOS lido | `Engine.cpp` (`export_thread_main`), `AureaModel.swift` |
| **P0-10** pasta de saída inválida só falhava no fim | Partida grava e apaga um arquivo de prova ao lado da saída; sem permissão/pasta → falha na hora (E/S, mensagem que já existia). iOS: o nome do arquivo usa a mesma higienização do nome do projeto. | teste (host `Export053.OutputFolderThatRefusesFiles…`); iOS lido | `Engine.cpp` (`start_export_checked`), `AureaModel.swift` |
| **P0-11** iOS: até 256 quadros 4K dentro do VideoToolbox | No Apple, a fila de quadros pendentes do sink tem teto de ~96 MB de NV12 (mínimo 8 quadros). | lido (código só compila no Apple) | `FFmpegExportSink.cpp` (`open`) |
| **P0-12** disco cheio na cópia final | Antes de copiar para a galeria, confere o espaço do armazenamento compartilhado (arquivo + 32 MB); não cabe → nem galeria nem reserva são tentadas e o vídeo vai direto para a pasta privada (P0-2), sem cópia. iOS: falha das Fotos já mantinha o arquivo em Documents. | teste (JVM `aCopyThatCannotFitIsNotStarted`) | `GalleryPublish.kt`, `Exporter.kt` |

### P1/P2 que ficaram (prontos e testados ou triviais)

| Item | O que | Verificado |
|---|---|---|
| P1-2 | Teto do aparelho pelo maior tamanho que o encoder **aceita** (`isSizeSupported`), não pelos máximos de largura/altura separados; sondagem refeita (SCHEMA 4) | teste (JVM `DeviceProfileSizeTest`, 3) |
| P1-3 | Diagnóstico local completo (decisão 2) | teste (JVM `ExportDiagnosticReportTest` 7, `ProblemReportExportTest` 3) |
| P1-4 | Activity recriada com export do editor em curso: a tela Exportar (já existente) volta | compilado |
| P1-5 | `POST_NOTIFICATIONS` só **declarada** (nenhum pedido/diálogo novo) | compilado |
| P1-6 | Marca de saída do job do Hyper desfeita no fim (Android); iOS: pausado ≠ "morreu exportando" | compilado; iOS lido |
| P1-8 | iOS: sobra `*.aurea-ffmpeg-*.tmp` limpa na abertura; caminho do encoder no relato | lido |
| P1-11 | Sem render da capa da Home enquanto exporta | compilado |
| P2-1 | Memória da escada de encoders entre aberturas do app (arquivo por aparelho + sistema + versão do app; degrau que falhou 2× em 3 dias é pulado; confirmado apaga as falhas) | teste (host `Export053.EncoderLadderMemory…`) |
| P2-3 | Montagem a partir do registro com o `moov` reservado na frente (sem regravar o MP4 inteiro no `+faststart`); cópia para a galeria pelo kernel (`FileUtils.copy`) com volta ao jeito antigo | teste (`Checkpoint` 16/16 e `Export` do host com o FFmpeg real; ordem dos átomos conferida; JVM `ExportPublishRulesTest`) |

### Desfeito (não testável a tempo) ou não feito

- **Desfeitos agora** (código voltou ao comportamento anterior): reprovar o
  arquivo por cor trocada no degrau Surface (P1-1; ficou só o log da medida),
  perfil H.264 High (P1-9; o encoder segue no perfil padrão dele), validação
  amostrada para degrau confirmado (P2-5), pausa por calor crítico (P1-12) e
  prioridade das threads do export (P2-2: com ela,
  `ExportSourceStartupGpu.TwoRetiringSourcesBoundAdmissionAndReleaseAllowsRetry`
  falhou 1 de 3 vezes rodando sozinho; sem ela, 5 de 5 passaram).
- **Não feitos:** P1-7 na tela (avisos só no relato, para não mudar a tela),
  P1-10 (layout do buffer no Android 8.0/8.1), P2-4 (mix/AAC em thread
  própria: exige o sink seguro entre threads; risco alto sem aparelho),
  P2-6…P2-14.

### Testes que rodaram

- Host (`engine/build/host-exp`, do diretório `engine/`): `Export053` 6/6
  (71 verificações), `FFmpegExport` 22 testes / 8776 verificações / 0 falhas
  (o caso "descarta o 1º quadro" se pula sem libopenh264 **e** libx264 no
  FFmpeg do host), grupo `Export` inteiro: 289 testes, 225 058 verificações, 0 falhas (depois de desfazer P2-2); `Checkpoint` 16/16 (524). Os MP4 montados pelo checkpoint saem com `ftyp moov free mdat` (índice na frente, sem regravar o arquivo); reserva medida: moov de 4,7 KB + 76 KB livres num vídeo de 100 quadros.
- JVM `:app:testUiTestUnitTest` (`state.*`, `diagnostics.*`,
  `DeviceProfileSizeTest`, `DeviceReportTest`): 27 suítes, 0 falhas.
- Kotlin `:app:compileDebugKotlin -PaureaAbi=arm64-v8a`: BUILD SUCCESSFUL.
- NDK (arm64-v8a, depois da última mudança): `AndroidVideoEncoder.cpp`, `FFmpegExportSink.cpp`,
  `aurea_jni.cpp`, `Engine.cpp` sem erro (só avisos antigos); armeabi-v7a:
  `AndroidVideoEncoder.cpp`, `FFmpegExportSink.cpp` sem erro.
- iOS estático (Windows): `check_symbols` (0 faltando), `check_api_swift`,
  `check_export_startup_async` (parte estática), `check_project_lifecycle`,
  `check_pbxproj`: 0 problemas. Swift **não** compilado.

### Edições do iOS para o CI conferir

| Arquivo | Onde | Risco de compilação |
|---|---|---|
| `bridge/AureaEngine.h/.mm` | `setExportMemoryStep:`, `+configureExportRungMemory:identity:`, `+exportRungMemory` | baixo (espelham `setThermalLevel`/`lastExportEncoderPath`) |
| `app/AureaModel.swift` | `enterBackground` (pausa antes de gravar), `safeFileName` + `startExport`, `ExportStallWatch.observe`, marca "pausado" (`previousExportMark`/`markExportInFlight`), `ExportMemoryStep`, `ExportReportLog`, `exportReportState/Context`, `configureExportRungMemory`, chamadas em `prepareExport` | médio: inicializadores `static let` entre `nonisolated`/main actor; nome importado `setExportMemoryStep(_:)`; `[String: Any]` heterogêneo |
| `app/ProjectTransfer.swift` | `ProblemReport.freeDiskMB`, `deviceState`, `noteExport(_:context:)`, `recordExportDiagnostics` | médio: tupla rotulada, `attributesOfFileSystem` |
| `app/StorageView.swift` | `sweepStagedExports`, `launchCleanup` | baixo |
| `verify/check_export_startup_async.py` | stubs novos e casos executados novos (nunca rodaram) | os casos podem falhar por pré-condição de tempo |

Atenção para o dono: os campos de disco no relato do iOS usam API de
"required reason" da Apple; o `PrivacyInfo.xcprivacy` não declara DiskSpace
(já não declarava para a tela Exportar). Conferir antes de enviar à App Store.

### O que só aparelho prova

Tudo que depende do encoder/driver real: quais encoders descartam quadros e se
o CBR resolve; se `operating-rate`/`priority` são aceitos; o 2º encoder de
hardware entrando quando o 1º falha; a recusa do serviço em primeiro plano em
ROMs de fabricante e se o `stopSelf` após recusa derruba o app (o código mantém
o comportamento anterior nesse ponto); os limites reais de memória que matam o
app (HyperOS, One UI, jetsam); o VideoToolbox com a fila limitada e a pausa em
segundo plano; o ganho de tempo do `moov` reservado em armazenamento lento.
