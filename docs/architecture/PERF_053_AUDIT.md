# Auditoria de desempenho — AUREA 0.5.3 (Fase A, só leitura)

Data: 12/11/2026. Ramo: `claude/omega-0.3.2` (HEAD `e984f47f`, com muito trabalho
sem commit). Pedido do dono: "Ultra otimização no app... para TUDO.... resolver
TODOS os problemas de exportação em TODOS os processadores..... e deixar rápido".
A exportação está em outra auditoria (`EXPORT_053_AUDIT.md`); este documento cobre
o resto: abertura do app, abertura de projeto, resposta do editor, timeline,
prévia, scrub, painéis, miniaturas e formas de onda, memória, armazenamento,
bateria e calor, e tamanho do pacote, em Android, iOS e PC.

**Esta fase não alterou código, não compilou e não executou nada.** Os números
citados vêm de documentos e logs que já existiam. Restrições para a Fase B: nenhuma
mudança visível de interface no Android/iOS, só otimização; o vídeo exportado não
pode mudar (testes de imagem dourada e de Export sem atualizar referência).

## 0. Como ler

Cada afirmação leva uma marca de evidência:

| Marca | Significado |
|---|---|
| **[código✓]** | Lido e reconferido diretamente no arquivo nesta sessão. |
| **[código]** | Lido no código por um dos cinco levantamentos paralelos desta sessão (Kotlin, render, mídia, iOS, memória/PC); não reconferido linha a linha. |
| **[medido: onde]** | Número de um log, relatório ou artefato existente; a origem vai junto. |
| **[doc]** | Afirmado em documento do repositório, sem reconferir. |
| **[hipótese]** | Inferido; falta medir. Todo custo em ms sem origem é hipótese. |

Números de linha são de 12/11/2026 e vão deslizar (outros agentes estão editando o
motor); o nome da função vai sempre junto. Caminhos relativos a
`D:\Users\Ruan\Documents\Aureabeta\`. Abreviações: `K/` =
`android/app/src/main/java/com/aurea/aurea/`, `I/` = `engine/platform/ios/`,
`E/` = `engine/src/`.

Limite que vale para o documento inteiro: só há um emulador Android (x86_64,
decoder de software, GPU do host por gfxstream) e este PC (Ryzen 5 5500, RTX 3050).
Nenhum aparelho está plugado e o iOS só compila na CI. Ver a seção 5.

---

## 1. O que já se sabe

### 1.1 Documentos

| Documento | O que estabelece |
|---|---|
| `docs/architecture/PERFORMANCE.md` | Orçamento 16,67/33,33 ms; `AdaptiveResolutionController` (AUTO) desce a prévia para 1/2, 1/4, 1/8; a prévia redesenha na taxa do conteúdo; `bridge::PerfPOD` e painel DEV. Garantias com teste: playback estável não cria textura, nenhum pipeline depois do aquecimento, 12 posições de scrub = no máximo 3 seeks. Pendente declarado: medir em aparelho real. |
| `docs/performance/BENCH-2026-11-07.md` | Bancada `diagnostics/PlaybackBench.kt` (só debug) e o antes/depois dos commits Desempenho 1–3, no emulador. Lista o que **não** foi aplicado: mistura "Somar" pelo hardware (muda o alfa), `operating-rate` e decoder compartilhado entre cortes (só validável com decoder de hardware), pré-render em RGBA8 (degraus), iOS HDR pela CPU e `AVAssetReader` novo por busca. |
| `docs/performance/PHASE_8_REPORT.md` + `baseline_host.json` | Benchmarks `PERF_*` no host (setembro) com verificador de regressão (+40 % no p50). Tabela de operações: ver 1.6. |
| `docs/architecture/PLAYBACK_2119.md` | Matriz de playback no emulador: 1080p60 H.264 **não** atinge tempo real (RAW 6,86 fps, compositor 21,94 fps); upload de planos pela CPU leva 20–75 ms no gfxstream; intervalos da thread de UI de 375–887 ms entre importar e tocar. Declara que a aceitação de desempenho não está completa. |
| `docs/architecture/FULL_AUDIT_2026-10-04.md` | Orçamentos de memória por importação, monitor de pressão a 1 Hz, trim. Ensaio `HeavyStress` (97 camadas, 176 efeitos, FBX) **interrompido por lentidão** no emulador; PSS máximo ~389 MiB. Aparelho físico: não executado. |
| `docs/architecture/FULL_AUDIT_IOS_2026-10-04.md` | Correções de estabilidade iOS verificadas só por leitura; sem compilação Apple nem aparelho. |
| `docs/architecture/STABILITY_EXPORT_PREVIEW_2026-10-04.md` | Buffer de prévia em RGBA16F, preparação antes do Play, linha azul na régua. |
| `docs/architecture/PRERENDER_INTEGRATION_2026-10-13.md` | PreRender 540p em disco; qualquer revisão do modelo invalida a sessão. |
| `docs/release/0.5.*` | A 0.5.2 trouxe a tela de Armazenamento, o teto de 4 GB do PreRender e a limpeza de órfãos. |

### 1.2 O ramo `perf-2018`

- **[código✓]** `perf-2018` aponta para `f5c3bf1f` (o commit da 0.5.2), sem nenhum
  commit próprio: `git diff claude/omega-0.3.2...perf-2018 --stat` é vazio. O HEAD
  atual está 4 commits à frente (2217 e três acertos de iOS/CI).
- O plano de fases não está em nenhum arquivo do repositório nem do ramo; a busca
  por "Fase 0" em `docs/`, `engine/`, `android/`, `desktop/` e `tools/` só acha
  comentários sem relação. Ele está na transcrição da sessão de 10/11
  (`1f46513c-…jsonl` na pasta de projetos do Claude). Resumo do pedido original,
  para aparelhos de 2018 (Snapdragon 660/845, Helio P60, 3–4 GB, Android 8–10,
  inclusive 32 bits):
  - Fase 0: levantamento, sem código. **Feita em 10/11, só por leitura.**
  - Fase 1: medição, classe de aparelho gravada, governador de quadro (1, 1/2, 1/3,
    1/4 com FSR), interface livre (nada pesado na thread principal, cache de
    pipelines em disco, scrub cancela trabalho antigo).
  - Fase 2: proxies, cache de render por camada, versão leve dos efeitos.
  - Fase 3: teto de memória por classe, 8 bits onde 16 não muda a prévia, calor.
  - Regra do dono: "Não afirme ganho sem número."
- O dono pausou tudo logo depois da Fase 0 ("paramos isso por enquanto") para fazer
  o Aurea Alpha. **Nenhuma das fases 1–3 foi implementada.** A Fase 0 não mediu
  nada: a ordem dos suspeitos foi inferida do código.

Os 10 suspeitos da Fase 0, reconferidos hoje contra o código atual:

| # | Suspeito (10/11) | Hoje |
|---|---|---|
| 1 | Pipelines compilados dentro do quadro; cache em disco só gravado ao fechar | **Ainda vale** → P0-1 |
| 2 | Leitura de disco na thread de render com o modelo travado (HDRI, LUT, fontes) | **Ainda vale** → P0-2, P0-3 |
| 3 | Chamadas da thread principal do Android que esperam o quadro inteiro | **Parcial**: `setText3d` e `setImage3d` ganharam debounce; o problema maior é outro, a releitura do modelo inteiro → P0-4 |
| 4 | Decodificadores de hardware sem limite em 64 bits | **Ainda vale**, com uma correção: a queda para software é imediata, não depois de 5 tentativas → P0-6 |
| 5 | Orçamento = 1/4 da RAM livre na abertura, nunca recalculado; tudo RGBA16F | **Ainda vale**; "uma alocação Vulkan por textura" é falso (há subalocador por blocos) → P1-14 |
| 6 | Governador mede `max(cpu, gpu)` e apaga o cache ao mudar de nível | **Ainda vale** → P1-1, P1-2 |
| 7 | Cena 3D refeita a cada quadro, sombra sem cache | **Ainda vale** → P1-6 |
| 8 | Cada edição apaga todo o cache de composição | **Ainda vale**: `previewRangeEdits_` e `pendingPreviewRanges_` não têm quem escreva **[código✓]** → P1-3 |
| 9 | Fluxo óptico com 2 quadros de cache; Rotobrush sem nível de prévia | **Ainda vale** → P2 |
| 10 | Pool de tarefas ocioso, uma thread por fonte de vídeo, sem lista de drivers ruins, cópia extra no GLES | **Ainda vale** (o despertar a cada 10 ms do PreRender foi corrigido) → P1-9, P2 |

### 1.3 O que os commits "Desempenho 1–3" (07/11) já fizeram

- `2d44da9e`: `read_status` usa `try_lock` e o que o último quadro publicou; leitores
  do PreRender dormem sem pedido; tempos por passe na GPU só com o painel aberto;
  refino e rascunho passam ao lado do cache; pipelines novos compilam fora do quadro
  quando parado depois de uma edição; `PlaybackBench`.
- `608ba091`: cabeçote fracionário sem lock (`playhead_precise`) para a timeline
  deslizar a cada vsync; menos recomposição no Android (transporte, palco, barra de
  seleção); o scrub do Android não relê o inspetor a cada passo.
- `2b9c2bad`: AAudio com 4 bursts; cache de quadros decodificados pela RAM do aparelho.
- **[medido: BENCH-2026-11-07, emulador]** −31 % de amostras de CPU fora das threads
  nomeadas tocando 7 s; `readStatus` p99 de 1,5–6,1 ms para 0,6–4,0 ms; Play até o
  cabeçote andar 36–44 ms. Os intervalos da UI no emulador não mudaram (130–135
  intervalos acima de 33 ms em ~330), atribuídos à composição de tela do emulador.

Paridade iOS desses commits: `playhead_precise` e a lista de camadas igual existem
no iOS; a correção do scrub **não** chegou ao iOS (P0-5); `PlaybackBench` e
`setDetailGpuTimers` não têm equivalente.

### 1.4 Classe de aparelho, orçamento e calor

**[código]** `classify_device` (`E/platform/DeviceCapabilities.cpp:559-590`): a classe
é o **pior** eixo.

| Eixo | LOW | MID | HIGH | ULTRA |
|---|---|---|---|---|
| RAM total | < 4608 MiB | < 7168 | < 10752 | resto, ou não medida |
| RAM na abertura | desce um degrau se livre < 512 MiB ou < total/8 | | | |
| GPU (limites declarados) | sem compute, textura < 4096, workgroup < 256 ou Vulkan < 1.1 | workgroup < 512, sem fp16, textura < 8192 ou GPU desconhecida | resto | workgroup ≥ 1024, Vulkan ≥ 1.3, textura ≥ 16384, fp16 |
| CPU | < 4 núcleos ou < 2,2 GHz | < 2,4 | < 2,95 | resto |
| Codecs | sem H.264 por hardware ou máx < 1080 | sem HEVC por hardware ou < 2160 | — | resto, ou tabela ausente |

**[código]** `device_policy` (`DeviceCapabilities.cpp:612-694`):

| | LOW | MID | HIGH | ULTRA |
|---|---|---|---|---|
| Escala inicial da prévia | 1/4 (1/2 se lado menor < 720) | 1 | 1 | 1 |
| Escala dos efeitos pesados / partículas | 0,5 / 0,5 | 1 / 1 | 1 / 1 | 1 / 1 |
| Mapa de sombra | 1024 | 2048 | 2048 | 2048 |
| Proxy | 540p para tudo acima de 720p | 720p acima de 1080p | idem | idem |
| Fatia para quadros decodificados | 12 % | 26,5 % | 26,5 % | 26,5 % |
| Lado da prévia de efeito | 160 | 320 | 512 | 512 |

Fatos sobre a classe e o orçamento:

- **[código]** Orçamento de memória = RAM **livre** / 4 no instante da abertura
  (`memory_budget_bytes`, `DeviceCapabilities.cpp:355-365`); `refresh_dynamic()` não
  tem chamador; `Engine::apply_memory_budgets` só roda em `initialize`. Tetos só
  existem para 32 bits (256 MiB, `AddressSpace.hpp`), iOS (1 GiB) e aparelho não
  medido (384 MB). **Android 64 bits não tem teto.**
- **[medido: `desktop/run-*.log`, este PC]** O mesmo PC registrou `orcamento_mb` de
  712 a 4114 conforme a RAM livre na hora.
- **[código]** A classe não é gravada e não há teste de GPU; `estimatedFillRatePerSec`
  é só declaração (`DeviceCapabilities.hpp:80`).
- **[medido: `desktop/run-*.log`]** O PC saía como `classe=LOW` nas rodadas 2–5 de
  11/11 por `decode_h264=nao` (eixo de codecs, não RAM). **Já corrigido**: da rodada
  7 em diante registra `classe=ULTRA` com `decode_h264=hw`.
- **Calor [código]**: Android por `EditorStore.startThermalWatch`
  (`K/state/EditorStore.kt:1324-1331`) → `nativeSetThermal` → `Engine::set_thermal`
  (`Engine.cpp:13265`), só API 29+; **sem heurística abaixo do Android 10**. iOS por
  `observeThermalState` (`I/app/AureaModel.swift:1928-1951`). WARM: pausa de fundo
  150 ms. HOT: pesados ×0,5, sombra pela metade. CRITICAL: piso 1/4, proxies parados.
- **[código✓]** `DevicePolicy::minPreviewDenominator` (HOT = 2, CRITICAL = 4) é
  calculado em `DeviceCapabilities.cpp:667/678`, mas
  `AdaptiveResolutionController::set_quality_floor` (`RenderScheduler.cpp:253`) **não
  tem nenhum chamador**: o piso de HOT não chega ao governador.

### 1.5 Caches e seus tetos **[código]**

| Cache | Teto | Despejo |
|---|---|---|
| Quadros decodificados | 26,5 % do orçamento (LOW 12 %) | orçamento; trim estágio 3 |
| Prévia guardada na GPU (`RenderedFrames`) | 32/64/320/512 MiB pela RAM total, ≤ orçamento/4, ≤ 300 quadros (`PreviewCachePolicy.hpp:27`) | pressão ≥ 0,85 zera; trim estágio 3 |
| `TransientTexturePool` | retém 7 % do orçamento; tamanhos antigos ficam 120 quadros | limpo no estágio 4 |
| PreRender | RAM 16 MiB / ≤ 8 quadros; disco 4 GiB (móvel) e 48 GiB (Windows) | `sweep_orphans` na abertura |
| Proxies | 2 GiB em disco, deixa 1 GiB livre | por idade |
| Miniaturas do motor | 1,5 % do orçamento, ≤ 900 | LRU, só memória |
| Miniaturas Kotlin / prévias de efeito | heap/16 entre 8 e 24 MB / 16 MB | `onTrimMemory` |
| Formas de onda / blocos de áudio | 1 % / 4 % | estágios 2 / 3; a onda tem cache em disco |
| Atlas de glifos | um de 2048² | reinício |
| Imagens, modelos e HDRI na CPU | min(orçamento/3, 256 MiB) | nenhum com o projeto aberto |
| **Originais de imagem na GPU** | **sem teto** | só trim estágio ≥ 4 |
| **Fluxo, máscara, planar, LUT, TimeSlice, espectros** | **sem teto em bytes** | idade de 240 quadros (`collect_resources`, `Renderer.cpp:10354`) |
| **LUT `.cube` do motor** | 8 entradas | **apaga todas ao chegar a 8** **[código✓]** (`Engine::cube_lookup`, `E/engine/EngineMedia.cpp:25-35`) |
| Mapas de profundidade | 48 | LRU |
| Desfazer | entre 16 e 128 MB (orçamento/16), 1000 entradas | mais antigo primeiro |
| `faceCache` de fontes (Kotlin) | **sem teto** (`K/editor/FontBrowser.kt`) | nenhum |

Trim: nível 10/20 do sistema → estágio 3; nível 15/80 → estágio 7 (tudo). Android
consulta `ActivityManager.MemoryInfo` fora da thread principal a no máximo 1 Hz
(`checkMemoryPressure`, `EditorStore.kt:1255`); iOS usa `os_proc_available_memory`
a 1 Hz mais o aviso do sistema. O custo dos dois monitores é desprezível. Não foram
lidos os tetos em bytes de layout de texto, Rotobrush e fluxo óptico.

### 1.6 Números que já existem

| Medida | Valor | Origem |
|---|---|---|
| Abertura do motor, quente | 255–372 ms (gpu 168–263, renderer 32–60, 18 pipelines) | **[medido: `desktop/run-*.log`, este PC]** |
| Abertura do motor, cache de pipelines inválido | 1587 ms (gpu 720, renderer 534); outra rodada 2133 ms (renderer 1505) | **[medido: `desktop/run-21.log`, `build/*.log`]** |
| Pipelines criados durante o playback numa sessão | 32, dez `video/flow_*` no mesmo quadro | **[medido: `desktop/run-19.log`]** |
| Play até o primeiro quadro (PC) | 87 % abaixo de 5 ms, máximo 184 ms, em 429 Plays | **[medido: `desktop/run-*.log`]** |
| Salvar projeto pesado / 1000 camadas | 11,6 / 16,8 ms com o lock do modelo | **[medido: PHASE_8_REPORT, host, setembro]** |
| Abrir projeto pesado / 1000 camadas | 0,11 / 20,2 ms | idem |
| Edição com desfazer, 1000 camadas | 0,93 ms | idem |
| Primeiro quadro exato depois de abrir | 57,4 ms | idem |
| Miniatura: primeira / 12 prontas | 13,3 / 156,2 ms (1080p → 90 px) | idem |
| Forma de onda de 60 s | 108,1 ms | idem |
| `PERF_1080_BASIC` | GPU p50 0,72 ms; CPU p50 0,41 ms | **[medido: baseline_host.json]** |
| Emulador, 1 vídeo 720p30 | CPU do quadro 9,6 ms, decode 2,2 ms, seek 1,4–4,3 ms | **[medido: PERFORMANCE.md]** |
| Emulador, 1080p60 | compositor 21,94 fps, 445 quadros perdidos em ~12 s | **[medido: PLAYBACK_2119.md]** |
| PSS tocando (emulador) | 192–200 MiB; ensaio pesado 389 MiB | **[medido: PLAYBACK_2119, FULL_AUDIT]** |

Nenhum desses números é de celular. Os do host são de uma GPU de mesa e servem para
comparar antes e depois, não para afirmar fps em aparelho.

---

## 2. Pontos quentes por área

### 2.1 Abertura do app

**Android** (`K/MainActivity.kt`, `K/state/EditorStore.kt`, `K/ads/`):

- **[código]** O motor não bloqueia o primeiro quadro: a Home desenha e espera
  `engineReady`; a splash é só `windowBackground`. Faturamento e Firebase nascem sob
  demanda; a coleta de crash espera 3 s em IO.
- **[medido: manifesto mesclado do release de 11/11]** Antes de
  `AureaApplication.onCreate` rodam na thread principal: `androidx.startup`
  (5 inicializadores), `MlKitInitProvider`, `MobileAdsInitProvider` (embora o
  provedor em uso seja o LevelPlay), `FirebaseInitProvider` e dois provedores do
  IronSource. O custo de cada um é **[hipótese]**.
- **[código]** Cerca de 9 leituras do Keystore (com `KeyStore.load` + AES-GCM) na
  thread principal entre `onCreate` e `onStart` para quem está logado:
  `captions/Captions.kt:28-59` (`KeyVault.secret/get`), `conta/ContaViewModel.kt:31,
  62-73, 92`, `hyper/Entitlements.kt:162-168`, chamadas de `MainActivity.kt:100, 112,
  397`.
- **[código]** `WebView.getCurrentWebViewPackage()` na thread principal em `onCreate`
  (`ads/AureaAds.kt:20`, de `MainActivity.kt:342`); `LevelPlay.init` roda na
  principal (`ads/LevelPlayAdsBackend.kt:60-77`), enquanto o backend do AdMob usa
  thread.
- **[código]** 8,8 MB de fontes (Roboto 2,4, NotoSansJP 4,5, NotoSansKR 1,8,
  Naskh 0,1) são **recopiados dos assets a cada abertura fria**, antes de
  `engine.initialize` (`EditorStore.kt:931-975`, thread `aurea-ciclo`). Atrasa o
  `engineReady`, não o primeiro quadro.
- **[código]** `System.loadLibrary("aurea")` na principal (`engine/AureaEngine.kt:
  80-90`, disparado pelo inicializador de campo em `EditorStore.kt:246`). A `.so` tem
  113 MB (ver 2.6); é `mmap`, mas as realocações rodam ali.
- **[código]** Verificação ECDSA da configuração remota em cache na principal
  (`config/AppConfig.kt:198-208`); bloco pós-`engineReady` na principal com
  `deviceReport`, `refreshPlugins` e `readCatalog` (catálogo inteiro de efeitos,
  `EditorStore.kt:995-1012`).
- **[medido: AAB de 11/11]** Não há perfil de base do app: o `baseline.prof`
  embutido tem 12,5 KB e vem só das bibliotecas (o `profileinstaller` entra por
  dependência transitiva). `settings.gradle.kts` só inclui `:app`; não há módulo de
  macrobenchmark. Com 22 MB de dex, as primeiras execuções depois de cada
  atualização rodam interpretadas/JIT **[hipótese]** de ser o maior ganho de
  abertura fria.
- Não existe `reportFullyDrawn` nem seção de `Trace`.

**iOS** (`I/app/AureaApp.swift`, `AureaModel.swift`):

- **[código]** `AureaApp.body` `.onAppear { model.start() }` (`AureaApp.swift:47`) →
  `AureaModel.start` (`AureaModel.swift:1758`) → `Host::initialize`
  (`I/bridge/AureaBridge.mm:124`) → `Engine::initialize`: abertura da GPU e
  pré-aquecimento dos ~18 pipelines **síncronos na thread principal**.
- **[código]** `AureaModel.init` (`:1133`) também roda `HyperPlatform.install()` →
  `MLComputeDevice.allComputeDevices` (`HyperModel.swift:418`) e a primeira montagem
  da tabela de textos.
- **[código]** A ponte iOS não expõe `startup_.{jobsMs,gpuMs,rendererMs,totalMs}`
  (`Engine.cpp:720-842`), então nem o simulador da CI mede isso hoje.
- Já bom: shaders em `.metallib` pré-compilado e cache `MTLBinaryArchive`.

**PC** (`desktop/src/main/kotlin/pc/Main.kt`):

- **[código]** Pré-carga das DLLs do FFmpeg, `Application.onCreate` e
  `MainActivity.create()` antes de a janela aparecer. JVM com `-Xms512m -Xmx4g`, ZGC
  geracional, sem AppCDS e sem ProGuard (`desktop/build.gradle.kts:225-236`).
- **[medido]** Abertura do motor 255–372 ms quente e 1,6–2,1 s com o cache de
  pipelines inválido.

### 2.2 Interface: Compose e SwiftUI

**Despacho no Android [código]**: não existe uma thread do motor. Transporte, scrub
e transformações vão por `send {}` (`EditorStore.kt:3716-3725`, lote sem bloqueio).
Mas há 728 chamadas `engine.` em `EditorStore.kt`, e a maioria das leituras e muitas
escritas são JNI direto na thread principal. Toda `query_*` aberta usa
`lock_guard(modelMutex_)` bloqueante; só `read_status` usa `try_to_lock`
(`Engine.cpp:14437`). A thread de render segura `modelMutex_` durante a drenagem de
comandos e todo o `Renderer::prepare` (`Engine.cpp:12693-12961`).

- **[código✓] Releitura do modelo inteiro a cada revisão.** `publish()`
  (`EditorStore.kt:1517-1580`) → `refreshModel()` (`:1642`) → `refreshDetail()`
  (`:1735`): cerca de 43 consultas bloqueantes mais ~20 em auxiliares, na thread
  principal. Em um arrasto, cada comando sobe a revisão, então isso roda uma vez
  por vsync, e cada chamada pode esperar o `prepare` do render. Tocando,
  `refreshDetail` + `refreshEffectParams` rodam a 10 Hz (`DETAIL_PLAYBACK_NS`).
- **[código✓] 67 ocorrências de `refreshDetail()`** em `EditorStore.kt`; cerca de 56
  são setters de controle deslizante e digitação que chamam a cada evento de
  ponteiro, por fora da guarda `gestureDepth` (que só existe em `refreshNow`,
  `:3698-3712`). Exemplos: `setTextContent :5484` (a cada tecla), `setTextSize :5496`,
  `setTextStyleValue :7066`, `setTextAnimatorValues :7334`, `setShapeParam :9233`,
  `setAudioGain :9459`, `dragPoseIk :2845`. Vários também escrevem por JNI
  bloqueante (`setTextStyle`, `setTextTexture`, `setTextAnimator`,
  `setShapeParamAnim`). E a leitura sai velha: o comando só é aplicado no quadro
  seguinte do motor.
- **[código]** Ainda diretos na principal: `applyTextFont :7811`
  (`Engine::set_text_font`), `addShape3D`/`addText3D` (`:3101`, `:3396`, pegam
  `sourceImportMutex_` e depois `modelMutex_`), `pushText3D :3554` (monta a malha na
  thread chamadora). Uma importação de imagem em IO segura `sourceImportMutex_`
  (`EngineMedia.cpp:174, 577`); um toque de 3D durante a importação espera por ela
  **[hipótese: risco de ANR com mídia grande]**.
- **[código✓] Timeline do modo Edição consulta o motor por clipe por quadro.**
  `drawClip` (`K/editor/EditTimeline.kt:904`) chama `store.thumbnails.get(...)`
  dentro do desenho. `ThumbnailCache.get` (`EditorStore.kt:10840`) não memoriza a
  falta nem tem orçamento, então cada clipe visível ainda sem miniatura faz um
  `queryThumbnail` bloqueante a cada desenho, mais uma `Key` e um `asImageBitmap()`
  por clipe por quadro. A timeline principal já tem orçamento e memória de falta
  (`ThumbStrip`).
- **[código]** Timeline principal (`K/editor/timeline/TimelinePainter.kt`): sobras
  de alocação por quadro: `Brush.verticalGradient(listOf…)` por barra visível
  (`:1573`), `Paint()` + `saveLayer` por linha com chaves travadas (`:608-613`),
  `Path()` em `drawEdgeArrow` (`:415`), texto de bloco de legenda medido por bloco
  por quadro (`:459`) com `rememberTextMeasurer(cacheSize = 16)`
  (`Timeline.kt:107`). Miniaturas ainda são buscadas na fase de desenho, na
  principal, até 6 JNI + `createBitmap` por quadro (`TimelinePainter.kt:2198`).
- **[código]** Navegador de fontes: `fontFamilyOf` (`K/editor/FontBrowser.kt:
  248-257`) faz `File.exists` + `Typeface.createFromFile` na composição, para cada
  linha que entra na tela.
- **[código]** Menores: `publish()` aloca `ProjectState`, `PreviewState` e uma
  string por vsync (`:1524-1553`); `store.project` (com `dirty`/`canUndo`) é lido no
  `BoxWithConstraints` do `EditorScreen` (`:340`) e refaz `EditorLayout.solve` a
  cada mudança de `dirty`; `refreshEffectParams` refaz a localização de todos os
  parâmetros a 10 Hz (`effects/EffectI18n.kt:41`); JNI dentro de corpo de composição
  em `panels/CurvePanel.kt:464, 647`; listas preguiçosas sem `key` em
  `AiVideoPanel.kt:422`, `CaptionsPanel.kt:208`, `ColorToolsPanels.kt:181, 275`,
  `Rig3DPanel.kt:197`, `TextMode.kt:280`, `TopBars.kt:523`.
- **[código]** Compose: Kotlin 2.4.0 sem bloco `composeCompiler {}` (sem relatório
  de estabilidade). O estado do `EditorStore` é granular (`mutableStateOf` por
  campo, `:423-574`), não um `StateFlow` único.

**iOS [código]**:

- `AureaModel` tem ~190 `@Published` e é observado em ~50 arquivos; qualquer
  publicação invalida a árvore. Tocando, `status` republica a 5 Hz
  (`AureaModel.swift:2069`); `refreshModel` (`:2183-2283`) converte todas as camadas
  e chaves por `[[String: Any]]` a cada revisão.
- **Scrub relê o inspetor a cada passo**: `TimelineView.swift:2134-2136` e `:2487`
  chamam `holdView` → `AureaModel.optimisticPlayhead` (`AureaModel.swift:2626-2634`),
  que atribui `status.playhead` e roda `refreshSelectedLayer()` inteiro (`:2286`:
  efeitos, detalhe, lente, forma, ossos, máscaras, parâmetros; cada um um
  `lock_guard(modelMutex_)` com conversão para `NSDictionary`), até 60 vezes por
  segundo. O caminho leve já existe (`timelineScrub`, `:2619`) e o comentário dele
  descreve exatamente essa trava.
- **Locks bloqueantes dentro de `body`**: `Engine::clipboard_state`
  (`Engine.cpp:9053`) em `TimelineView.keyActionBar` (~`:631`, reavaliado a cada
  vsync tocando); `Engine::copy_preview_buffer_ranges` (`Engine.cpp:14405-14412`) a
  cada 200 ms; `Engine::query_gizmo` (`Engine.cpp:6938`) em `PreviewStage.body` e no
  `Canvas` do `StageOverlay` (`EditorView.swift:~447, 782, 843`).
- Abrir projeto é síncrono na principal sem retorno visual: `AureaModel.open(_:)`
  (`:3002-3030`) → `engine.loadProject`; `openingProject` é ligado e desligado na
  mesma chamada e nenhuma view o lê. Sair do editor salva na principal
  (`saveOnLeave`, `:1848, 3043`).
- Menores: `Timer.publish` de 60 Hz sempre ligado na `TimelineView` (`:297, 614`);
  glifo de pausa arco-íris redesenha a cada quadro (`ToolbarView.swift:1196`);
  `thumbnailForLayer` (`I/bridge/AureaEngine.mm:5724`) faz um `query_layer_detail`
  completo por miniatura só pela proporção; `UIImage.fromRGBA`
  (`AureaModel.swift:7237`) faz três cópias e um laço por pixel na principal;
  registro de fonte dentro do `body` da célula (`FontBrowserView.swift:212-231`).

### 2.3 Render da prévia

Laço **[código]**: thread `aurea-render` em `Engine::render_thread_main`
(`Engine.cpp:1590`) → `render_frame` (`:12609`). `renderMutex_` o quadro todo;
`modelMutex_` na drenagem de comandos e no `Renderer::prepare`, solto antes de
`Renderer::render`. Único ponto de espera pela GPU no caminho estável: a cerca de
`begin_frame` (`VulkanCommands.cpp:866`). Nenhuma leitura de volta nem `wait_idle`
na prévia estável.

- **Pipelines dentro do quadro [código + medido]**: `ShaderLibrary::pipeline`
  (`E/render/ShaderLibrary.cpp:364-378`) compila na primeira utilização.
  `Renderer::scan_for_warmup` (`Renderer.cpp:990`) só percorre `layer.effects` →
  `Effect::pipelines()`; os sistemas que não são efeito (conjunto de fluxo,
  `time_slice`, `affine_resample`, `depth_map`, variantes 3D, partículas) ficam de
  fora. A varredura é pulada enquanto toca (`:2279`). `flush_warmup` (`:9090`)
  compila tudo de forma síncrona antes de `begin_frame`; um log mostra "99 pipelines
  do projeto" num único flush, sem tempo registrado. O cache em disco só é gravado
  em `Engine::shutdown` (`:1202`) e `suspend` (`:1284`): se o sistema matar o app,
  perde-se.
- **`default_family()` carrega todas as fontes do sistema [código✓]**:
  `E/text/Text.cpp:832-845` percorre `FontManager::instance().list()` e chama
  `FontManager::load(e.path)` para cada entrada, comparando o ponteiro com
  `default_font()`. `default_font()` (`:809`) cria a fonte por `Font::load` direto,
  fora do cache do `FontManager`, então os ponteiros nunca coincidem **[código]** e
  o laço lê, faz hash e monta uma face HarfBuzz de **cada** fonte, guardando todas
  em `cache_`. Dispara em `place` → `spanFont` (`:631-637`) para texto com trecho de
  peso e família vazia, dentro do `prepare`, com `modelMutex_`, uma vez por
  processo. O resultado hoje é sempre a string vazia.
- **Disco e CPU pesada com `modelMutex_` no `prepare` [código]**: HDRI
  (`Engine::hdri_lookup` → `read_hdri_file`, `Engine.cpp:7208-7226`); LUT
  (`cube_lookup`, ver 1.5); varredura de todas as pastas de fonte no primeiro uso
  (`FontManager::scan_locked`, `E/text/FontManager.cpp:279`); SDF por glifo novo com
  `stbtt_GetGlyphSDF`, 3× o tamanho a partir de 160 px (`Text.cpp:949-959`); cópia
  do buffer inteiro da imagem no primeiro uso (`up.data = rl.source.pixels->rgba`,
  `Renderer.cpp:5138`).
- **Governador [código✓]**: `frameMs = max(cpuMs, gpuMs)`
  (`AdaptiveResolutionController::update`, `RenderScheduler.cpp:339-345`); a espera
  pela imagem (`timings.acquireWaitMs`) só vai para `PerfPOD.acquireMs`. No GLES,
  `Impl::retire` (`engine/gpu/gles/GlesDevice.cpp:112-124`) só calcula
  `completedGpuMs` a partir dos temporizadores por passe (`f.timerCount`), e esses só
  existem com `settings.gpuTimers` (`FrameGraph.cpp:1027`), que o Android desliga
  com o painel fechado (`EditorStore.kt:1002, 4923`). Ou seja: **no GLES, com o
  painel DEV fechado, o AUTO não enxerga o tempo de GPU**, justamente nos aparelhos
  mais fracos.
- **Mudança de nível joga fora cache e texturas [código]**: a chave do cache de
  prévia inclui largura, altura e cada ajuste de qualidade
  (`Renderer::configure_preview_cache`, `:1250-1275`); `TextureDesc::compatible`
  exige tamanho exato e os tamanhos antigos ficam 120 quadros no pool; o cache de
  fluxo depende de `heavyQ_.flow` (`video_flow`, `:6245-6262`).
- **Invalidação total [código✓ + código]**: qualquer mutação sobe a revisão e apaga
  todo o cache de prévia e cancela o PreRender (`submit_commands` `:12377`,
  `bump_model_revision_locked` `:18511`); até travar e renomear camada sobem a
  revisão (`Engine.cpp:19393`). Depois, o preenchimento ocioso re-renderiza até
  `min(capacidade, 300)` quadros, começando 650 ms após a mudança, com ~2/3 de
  ocupação (`PreviewCachePolicy.hpp:77-118`): GPU, calor e bateria gastos depois de
  edições triviais.
- **Texto refeito a cada quadro [código]**: o caso de texto no `prepare`
  (`Renderer.cpp:3205-3540`) chama `font_for` e `text::layout_quads`
  (`Text.cpp:1026` → `place` → `hb_shape` `:564`) e aloca ~15 `std::vector` por
  camada de texto, sem cache. Um glifo novo reenvia o atlas inteiro de 2048²
  (`Renderer::render` `:9209-9232`).
- **3D [código]**: o mapa de sombra é transiente do grafo, refeito a cada montagem
  (`"3d-sombra"`, `E/scene3d/SceneRenderer.cpp:2124/2416`); o motion blur chama
  `scene3d_.build` por subquadro (`Renderer.cpp:6590-6596`); `mapResolution` da
  composição pode subir o mapa da prévia acima do tamanho do AUTO
  (`SceneRenderer.cpp:2003`).
- **Escala dos intermediários [código, ganho hipótese]**: `texel_scale_for`
  (`Renderer.cpp:173`) é potência de dois, mas `preview_viewport_scale` produz
  frações (ex.: 0,7). Para fator entre 0,5 e 1, cada camada e cada passe de efeito
  roda em k = 1 em RGBA16F; só o alvo da composição encolhe.
- **GLES [código]**: uma cópia extra de tela cheia por quadro (`glBlitFramebuffer`
  com inversão de Y, `GlesDevice.cpp:172-178`); `ensure_shader` compila o ESSL antes
  de tentar o binário do programa em disco, lido com `ifstream` na thread de render
  (`engine/gpu/gles/GlesResources.cpp:211, 291-306`).
- Menores **[código]**: cada camada com modo de mistura não Normal custa uma cópia
  do fundo (`composite_draws`, `:8661-8700`); o passe `"previa-cache"` roda em todo
  quadro sem acerto (`:9369-9378`); `make_unique<FrameSnapshot>` por precomposição
  por quadro (`:2753, 3838, 4401`); cinco passadas O(n) pelas camadas
  (`:2378-2440`).

### 2.4 Decodificação, scrub, proxies, miniaturas e áudio

- **Decoders se multiplicam em timeline de cortes rápidos [código]**: as entradas
  são por (camada, mídia) em `MediaManager::source_for`
  (`E/media/MediaManager.cpp:219`); dois cortes do mesmo arquivo = dois decoders.
  `MediaManager::collect` mantém a fonte 180 quadros depois de sair, e
  `Renderer::prepare` (`:5241`) abre a próxima 1,5 s antes. Só 32 bits tem limite
  (4 fontes). `maxSupportedInstances` chega até `DeviceCapabilities::
  decode_parallelism()`, que **não tem chamador**. Quando o hardware recusa,
  `MediaCodecDecoder::create_codec` (`engine/platform/android/MediaCodecSource.cpp:
  747`) abre o decoder de software na hora e fica nele até o fim da vida daquele
  decoder.
- **Um decoder fechando bloqueia toda abertura nova [código]**: a guarda
  `retiring_ || !retired_.empty()` em `source_for`; `retire_main` espera uma thread
  que pode estar em `next_raw_buffer` (3 s), `acquire_image` (1 s) ou
  `wait_producer_fence` (0,5 s). `PreviewRefill::hold` segura a imagem anterior por
  no máximo 250 ms; depois a camada é composta sem quadro.
- **Proxy chega tarde onde mais faz falta [código]**: `PreviewProxyService::generate`
  abre com `MediaPriority::Thumbnail`, que `initial_video_path` mapeia para planos
  por **software**; reamostra com `resample_plane` escalar; ocupa no máximo 25 % do
  tempo e para durante o Play (`Engine.cpp:12733`). Em LOW, `proxyHeavyPixels =
  1280×720` (`DeviceCapabilities.cpp:632`): **todo clipe 1080p espera esse proxy**.
  A troca para o proxy não é contínua (fecha o original, espera, abre o proxy).
- **4K original em aparelho com menos de 5 GB não tem folga de decodificação
  [código]**: `driver_gl_cache_budget`/`driver_gl_live_frames`
  (`engine/include/aurea/platform/AndroidVideoPath.hpp:159-181`): orçamento 48 MB
  abaixo de 5 GB → `live = 5 + 1`; `VideoSource` guarda `live − 5` = 1 quadro.
  `wrap_driver_gl` chama `external_quad(..., 4096, ...)`: cada quadro vira um alvo
  RGBA em resolução cheia mesmo com a prévia em 1/4.
- **Passo de quadro e toque na régua forçam busca por keyframe até para +1 quadro
  [código]**: `PlaybackController::step` → `seek` → `++mediaEpoch_`;
  `VideoSource::thread_main` zera `decoderValid_` na troca de época, então
  `reachable_forward` é falso.
- **Todo quadro da fonte é convertido, mesmo os que a composição não usa
  [código]**: `VideoSource::thread_main` busca na grade da fonte; 60 fps em
  composição de 30 fps paga um passe GL, uma espera de cerca e uma vaga de cache por
  quadro descartado.
- **Rebaixamentos permanentes [código; frequência em campo é hipótese]**: em
  `MediaCodecDecoder::next_frame` só `Timeout` é perdoado; outro erro no caminho GL
  passa aquele decoder para planos de CPU de vez. `ExitDiagnostics.safeVideoMode`
  grava `SAFE_VIDEO_BUILD`: um crash durante vídeo deixa **todos** os vídeos em
  software até a próxima versão.
- **Abertura do caminho GL é pesada [código]**: `GlVideoBridge::create` cria
  contexto EGL, pbuffer e compila/liga shader por decoder; `scan_keyframes` soma até
  50 ms.
- **Miniaturas disputam com o playback [código]**: `ThumbnailService::decode` abre o
  arquivo original com decoder de software (até 2 ao mesmo tempo), não pausa durante
  o Play (só ritmo térmico) e o cache é só de memória.
- **Caminho de planos pela CPU aloca por quadro [código]**: `planes_from` →
  `copy_decoded_yuv420` (`DecodedPlaneBounds.hpp:21`) faz `vector::resize` em um
  `CodecFrame` novo e separa o croma em laço escalar. **Não há NEON nem libyuv no
  código de mídia [código✓: nenhuma ocorrência de `arm_neon`/`__ARM_NEON` em
  `engine/src`, `engine/include`, `engine/platform/android`].** Afeta emulador,
  backend GLES, modo seguro, miniaturas e geração de proxy.
- **Threads [código]**: `jobs_.start(workers)` roda (`Engine.cpp:706`), mas nada é
  submetido ao pool fora de `E/jobs`; cada fonte cria uma thread por abertura e uma
  de decodificação.
- **MediaCodec [código]**: `priority` é definido; `operating-rate` e `low-latency`
  não.
- **iOS [código]**: um `AVAssetReader` novo por busca (`seek_to_keyframe` →
  `start_reader`, `I/bridge/IOSVideoDecoder.mm:439-503`); a primeira busca fora do
  zero percorre todas as amostras (`build_timing_index`, `:392-432`). Vídeo de
  10 bits/HDR sai do caminho sem cópia: o `CVMetalTextureCache` só aceita BGRA/RGBA
  (`engine/gpu/metal/MetalResources.mm:1155-1168`) e o P010 sobe pela CPU
  (`prepare_cpu_planes`, `:210-272`); a conversão para RGB em si não é feita na CPU.
- **Áudio [código]**: o callback não aloca nem trava. Menores: `mixer_main` acorda a
  cada 2 ms com o anel cheio; `AAudioOutput::stop` segura `mutex_` até 100 ms e
  `presented()` usa o mesmo mutex.

### 2.5 Memória, projeto e armazenamento

- **Salvar (e o salvamento automático) codifica o projeto inteiro com `modelMutex_`
  [código + medido]**: `Engine::save_project_impl` (`Engine.cpp:2299-2400`) codifica
  sob o lock, relê fora dele e grava de forma atômica com `.bak`. Dispara 3 s depois
  da última edição (`autosaveIfIdle`, `EditorStore.kt:1603`, em IO). No host eram
  11,6–16,8 ms de lock; em celular de 2018 é **[hipótese]** algo como 5–10× isso. O
  motor já registra `saveStats_.lastLockNs/maxLockNs`.
- **Desfazer clona a composição inteira por edição [código]**:
  `History::before_mutation` (`E/command/History.cpp:118-136`) chama `comp.clone()`.
  **[medido: PHASE_8, host]** 0,93 ms com 1000 camadas, então não é trava no PC; o
  custo com muitas chaves em celular não foi medido.
- **Abrir projeto é serial e ansioso [código]**: `Engine::load_project_checked`
  (`Engine.cpp:1871-2292`) decodifica todas as imagens referenciadas, carrega fontes
  e importa todos os modelos 3D em série; não há log de tempo decorrido. No Android,
  `loadProject` roda em `Dispatchers.Default`, mas `readMeta` e `enterEditor` →
  `refreshNow` → `refreshModel` completo rodam na principal
  (`EditorStore.kt:10071-10108`).
- **Orçamento congelado na abertura** e **Android 64 bits sem teto**: ver 1.4.
- **Bateria [código]**: três threads do PreRender acordam a cada 250 ms mesmo sem
  PreRender em uso (`PreRenderCache.cpp:443`); o laço de status volta antes do
  `checkMemoryPressure` enquanto há exportação ou PreRender preparando
  (`EditorStore.kt:1402-1404`).
- 32 bits **[código]**: não há `mmap` de arquivo no motor; teto de 256 MiB, 4 fontes
  de vídeo, 2 vagas de leitura na exportação. `driver_gl_cache_budget` e o
  `maxBytes` de 192 MB do `DecodedFrameCache` não dependem do tamanho do ponteiro;
  só o orçamento compartilhado os limita.

### 2.6 Configuração de build

| Item | Estado | Evidência |
|---|---|---|
| Otimização do motor no Android | `-O2 -g` (`CMAKE_BUILD_TYPE=RelWithDebInfo`) | **[código✓]** `android/app/build.gradle.kts:174`; `engine/CMakeLists.txt:500` |
| Otimização no iOS (Release) | `-O3` | **[código✓]** `engine/CMakeLists.txt:499` |
| LTO | nenhum, em nenhuma plataforma | **[código✓]** sem `flto`/`INTERPROCEDURAL` no CMake |
| Exceções / RTTI | `-fno-exceptions -fno-rtti`, visibilidade oculta; fronteiras de importação com `-fexceptions` | **[código✓]** `build.gradle.kts:169`; `CMakeLists.txt:496` |
| NEON escrito à mão | nenhum no motor; só a vetorização automática | **[código✓]** |
| ABIs | `arm64-v8a` + `armeabi-v7a`; AAB dividido por ABI e densidade, idioma não | **[código✓]** `build.gradle.kts:160, 316` |
| R8 / recursos | `isMinifyEnabled` e `isShrinkResources` ligados | **[código✓]** `build.gradle.kts:220-223` |
| dex depois do R8 | 11,1 + 8,1 + 2,9 = 22,1 MB | **[medido: AAB de 11/11]** |
| `libaurea.so` | 113,3 MB (arm64), 107,1 MB (v7a), sem símbolos | **[medido: AAB de 11/11]** |
| Seções da `.so` (v7a) | `.rodata` 89,9 MB, `.text` 15,9 MB, `.bss` 4,0 MB | **[medido: `llvm-readelf -S`]** |
| O que enche a `.rodata` | redes embutidas por `.incbin` (`engine/assets/ai` 33 MB, `rotobrush` 25 MB, `hyper` ~18 MB) e os shaders | **[código✓]** `engine/cmake/AureaNcnn.cmake`, `AureaHyperNeural.cmake`, `AureaShaders.cmake` |
| Empacotamento da `.so` | `useLegacyPackaging = false` (mapeada direto do APK) | **[código✓]** `build.gradle.kts:288` |
| AAB | 399 MB, dos quais 147 MB de `proguard.map` e 229 MB de símbolos nativos (metadados, não vão ao usuário) | **[medido]** |
| APK universal | 147 MB | **[medido]** |
| Perfil de base | 12,5 KB, só de bibliotecas; nenhum do app | **[medido]** |
| Modelos ML Kit | 3 `.tflite` de pose (6,4 + 3,0 + 2,8 MB); duas dependências de pose | **[medido + código✓]** |
| Compose | Kotlin 2.4.0, sem bloco `composeCompiler {}` | **[código]** |
| Páginas de 16 KB | `-Wl,-z,max-page-size=16384` | **[código✓]** `CMakeLists.txt:639` |
| iOS na CI | Release com `SWIFT_COMPILATION_MODE=singlefile`, que anula o `wholemodule` do projeto | **[código✓]** `.github/workflows/build-ipa.yml:169` |

Observação de tamanho: a `.rodata` mapeada não ocupa RAM até ser tocada, então os
~76 MB de redes embutidas pesam no download e no armazenamento, não na memória nem
na abertura. Tirá-las do pacote (entrega sob demanda) seria mudança visível, fora do
escopo desta versão.

### 2.7 PC (`desktop/`)

- **Caminho do quadro até a janela [código]**: `host_record_copy`
  (`engine/gpu/vulkan/VulkanHostPresent.cpp:288-420`) lê da GPU para um buffer
  mapeado; `copy_host_frame` (`:439-471`) faz um `memcpy` (w·h·4, 8,3 MB em 1080p,
  33 MB em 4K) chamado de `FramePump.poll` (`desktop/src/main/kotlin/pc/
  EnginePreview.kt:104-134`) **na thread do Compose**; depois `Image.makeFromPixmap`
  e um novo envio para a GPU pelo Skia. Se `makeFromPixmap` copia é **[hipótese]**
  (o comentário do código diz que não).
- **A prévia consulta o motor a cada vsync para sempre [código]**:
  `EnginePreview.kt:52-57`, `while(true){ withFrameNanos{}; pump.poll() }`. Que isso
  force o Compose a redesenhar a janela a cada vsync parado é **[hipótese]**.
- **Shim `Canvas.drawBitmap` refaz a imagem Skia a cada chamada [código]**
  (`desktop/src/main/kotlin/shim/android/graphics/Canvas.kt`), usado por
  `TimelinePainter.kt:736, 1072, 1942` para cada miniatura a cada desenho;
  `Paint.font()` aloca uma fonte por `drawText`/`measureText`.
- **Decodificação [código]**: DXVA → textura D3D11 → anel de staging → `Map` →
  `memcpy` → upload de planos na Vulkan (ida e volta GPU→CPU→GPU por quadro). A
  classe `Video` do Windows não redefine `max_live_frames` (padrão 8,
  `VideoSource.hpp:68`), então guarda só 3 quadros e antecipa 2;
  `keyframe_interval_us` fica no padrão de 2 s.
- **Orçamento [medido]**: segue a RAM livre na abertura (712–4114 MB na mesma
  máquina); o cache de prévia para em 512 MiB e as miniaturas Kotlin em 24 MB; a
  VRAM não entra na conta.
- Menores **[código]**: o shim do `Choreographer` é um temporizador fixo de
  16,666 ms, não vsync (`shim/android/View.kt:96-110`); empacotamento MSI nunca
  testado.

---

## 3. Lista priorizada

P0 = trava, ANR ou engasgo que o usuário sente. P1 = ganho grande. P2 = acabamento.
"Onde valida": **H** = testes de host neste PC, **PC** = app do PC, **E** = emulador
Android, **A** = só aparelho físico, **CI** = só na CI do iOS. Ganho sem número é
qualitativo; a regra do dono é não afirmar ganho sem medir.

### P0

| ID | O que | Arquivos e funções | Ganho esperado | Risco | Como medir | Onde valida |
|---|---|---|---|---|---|---|
| P0-1 | Pipelines fora do quadro. (a) `scan_for_warmup` declara também os sistemas que não são efeito a partir das propriedades da camada; (b) varrer em qualquer mudança de revisão, inclusive tocando; (c) na Vulkan compilar em thread auxiliar e marcar o quadro como incompleto (o caminho já existe), no GLES um orçamento em ms por quadro; (d) gravar o cache em disco com atraso depois de cada lote; (e) registrar o tempo do flush | `E/render/Renderer.cpp` (`scan_for_warmup`, `flush_warmup`, `prepare`), `E/render/ShaderLibrary.cpp` (`pipeline`), `Engine.cpp` (`shutdown`, `suspend`), `engine/gpu/vulkan/VulkanDevice.cpp` | Some o engasgo de primeiro uso; a abertura depois de o sistema matar o app deixa de recompilar tudo (1,6–2,1 s medidos no PC com cache inválido) | Médio: segurança de thread de `pipelines_` e do `VkPipelineCache` | `PerfPOD.pipelineCompilesLive`; contagem de "criado durante o playback" no log; "abertura do motor" fria e quente; `pacingP99Ms` | H, PC, E; drivers Adreno/Mali só A |
| P0-2 | `default_family()` devolve `""` sem o laço (é o que ele já devolve hoje, então a imagem não muda) | `E/text/Text.cpp:832-845` | Tira uma trava única de até vários segundos **[hipótese]** e a retenção de todas as fontes do sistema na RAM | Baixo. Decisão do dono se um dia quiser resolver a família de verdade: isso mudaria pixels exportados | Teste de host: tempo do primeiro `prepare` de um texto com trecho em negrito e família vazia; `FontManager` com N fontes | H, E |
| P0-3 | Tirar disco e CPU pesada de dentro do `prepare`: HDRI e LUT com carregamento assíncrono que devolve "pendente"; LRU na LUT em vez de apagar tudo; varredura de fontes em tarefa na abertura; SDF de glifo em tarefa; mover o buffer da imagem em vez de copiar | `Engine.cpp` (`hdri_lookup`), `E/engine/EngineMedia.cpp` (`cube_lookup`), `E/text/FontManager.cpp` (`scan_locked`), `Text.cpp` (`atlas_glyph`), `Renderer.cpp:5138` | `prepare` mais curto = menos tempo com `modelMutex_` = interface e render sem esperar disco | Médio: exportação e congelamento precisam continuar bloqueantes | `cpuPrepareMs` p99; `heartbeatMaxMs` do teste de playback; contador de leituras de LUT | H, E |
| P0-4 | Android: a interface para de esperar o render. (a) aplicar a guarda de gesto do `refreshNow` aos ~56 setters, com valor otimista local; (b) `refreshModel` como um instantâneo único do motor sob um lock (ou `try_lock` mantendo o último valor), fora da principal; durante gesto só o detalhe; (c) mover `addShape3D`/`addText3D`/`pushText3D`/`applyTextFont` para `Dispatchers.Default` atrás do `busyMessage` que já existe | `K/state/EditorStore.kt` (`publish`, `refreshModel`, `refreshDetail`, setters), `engine/platform/android/aurea_jni.cpp`, `Engine.cpp` (`query_*`) | Arrasto de controle e digitação sem engasgo; menos risco de ANR | Médio-alto: é o arquivo central da interface (11 mil linhas, com mudanças sem commit); valor velho na tela se a guarda errar | Cenário `arrasto` do `PlaybackBench` (`ui95/ui99`, `ui>33`); `heartbeatMaxMs`; `Trace.beginSection` em `refreshModel`/`refreshDetail` | E (a parte da interface); o tamanho real do ganho só A |
| P0-5 | iOS: (a) em `optimisticPlayhead`, limitar a leitura do detalhe a 100 ms e sem pilha de efeitos (igual ao `DETAIL_PLAYBACK_NS` do Android; o `finish()` já relê tudo no fim do gesto); (b) `try_lock` com último valor em `clipboard_state` e `copy_preview_buffer_ranges` (parte C++ compartilhada, compila no host e serve ao Android); (c) ler o gizmo uma vez em `refreshSelectedLayer` | `I/app/AureaModel.swift:2626-2634`, `I/app/TimelineView.swift:2134, 2487, ~631`, `I/app/EditorView.swift:~447`, `Engine.cpp:9053, 14405` | 5–8 idas ao lock do modelo a menos por passo de scrub | Baixo em (a) e (b); médio-baixo em (c), às cegas | Contar chamadas da ponte no simulador da CI; em aparelho, `PerformanceTest.swift` | (b) H; o resto CI e A |
| P0-6 | Decoders: (a) teto de fontes vivas em 64 bits por `concurrentInstances`, reaproveitando o despejo LRU dos 32 bits; (b) passar a `VideoSource` para a próxima camada da mesma mídia quando o alvo é alcançável para a frente; (c) só bloquear abertura nova quando vivos + fechando atingem o teto | `E/media/MediaManager.cpp` (`source_for`, `collect`), `Renderer.cpp:5241`, `engine/platform/android/MediaCodecSource.cpp` (`create_codec`), `DeviceCapabilities` (`decode_parallelism`) | Acaba a queda para software em timeline de cortes; menos camada sem imagem em cortes e na troca para proxy | Médio: a passagem precisa respeitar época e quadros exigidos; **mexe em código que a exportação também usa** | Contagem de decoders vivos e de aberturas; `stats_.seeks`; fechamento lento injetado | Contadores em E/PC; a recusa do hardware só A |
| P0-7 | Aparelho fraco com vídeo pesado: (a) proxy com decoder de hardware (planos de CPU, caindo para software), ocupação ~60 % parado e com calor normal, redução com NEON ou GPU; (b) alvo GL do tamanho que a prévia precisa (lado maior ≤ 1920, salvo fonte aberta para exportação ou captura exata) | `E/media/PreviewProxy.cpp` (`PreviewProxyService::generate`, `resample_plane`), `engine/include/aurea/platform/AndroidVideoPath.hpp`, `MediaCodecSource.cpp` (`wrap_driver_gl`) | Proxy 5–10× mais rápido **[hipótese]**; 4× menos preenchimento e memória por quadro 4K e 4–7 quadros de folga | Médio: defeitos de planos YUV de fabricante (foi o motivo de escolher software); a mesma entrada não pode servir à exportação | Tempo até o proxy pronto; quadros em cache; `droppedFrames` | Fila e ocupação em E; o caminho de hardware e o GL só A |

### P1

| ID | O que | Arquivos e funções | Ganho esperado | Risco | Como medir | Onde valida |
|---|---|---|---|---|---|---|
| P1-1 | Governador enxerga o custo real: no GLES, um par de marcas de tempo por quadro sempre (como `collect_timings` da Vulkan) e zerar totais velhos; somar a espera pela imagem quando a GPU não é medida; ligar `set_quality_floor` ao `minPreviewDenominator` | `engine/gpu/gles/GlesDevice.cpp` (`retire`), `E/render/RenderScheduler.cpp` (`update`, `set_quality_floor`), `Renderer.cpp:9201` | O AUTO passa a funcionar nos aparelhos mais fracos e no calor | Baixo; só prévia | Teste de host do governador (desce, sobe, não oscila); `gpuFrameMs` com o painel fechado | H (Vulkan); GLES em E; efeito real só A |
| P1-2 | Mudança de nível não joga cache fora: quadros de outra qualidade valem como acerto mostrável enquanto toca; histerese antes de realocar | `Renderer.cpp` (`configure_preview_cache`), `TransientTexturePool`, `video_flow` | Sem engasgo logo depois de um passo do AUTO | Médio | `texturesCreated`, `pacingP99Ms` em torno da troca | H, E |
| P1-3 | Invalidação por trecho: alimentar `pendingPreviewRanges_` em cortar, mover e dividir e em edições locais sem efeito de tempo, expressão ou camada de ajuste; travar e renomear não sobem a revisão do cache; teto de preenchimento ocioso por classe (LOW ~1 s; parar em WARM) | `Engine.cpp` (`submit_commands`, `bump_model_revision_locked`, `:19393`, `:13013`), `E/render/PreviewCachePolicy.hpp` | Menos GPU, calor e bateria depois de cada edição; a linha azul sobrevive a edições pequenas | Médio-alto: quadro velho se a análise de dependência errar | Testes `PreviewBuffer`; quadros re-renderizados por edição | H |
| P1-4 | Cache do layout de texto por camada (chave: fonte, conteúdo, tamanho, espaçamento, caixa, trechos); animadores aplicados por cima; enviar só o retângulo novo do atlas | `Renderer.cpp:3205-3540`, `Text.cpp` (`layout_quads`, `place`), `Renderer.cpp:9209-9232` | `prepare` mais curto em projeto com muita legenda | Médio (igualdade com as imagens douradas) | `cpuPrepareMs` no cenário `texto12` do `PlaybackBench` e em teste de host com 50 textos | H, E |
| P1-5 | Timeline do modo Edição usa o `ThumbStrip` da timeline principal (orçamento + memória de falta) | `K/editor/EditTimeline.kt:904` (`drawClip`), `EditorStore.kt:10840` (`ThumbnailCache.get`) | Sem JNI bloqueante por clipe por quadro | Baixo | `ui95/ui99` rolando o modo Edição | E |
| P1-6 | 3D: mapa de sombra persistente (chave: luz, pose e tamanho dos que projetam), reaproveitado entre quadros e entre subquadros do obturador quando parado; limitar ao tamanho do AUTO quando não é exportação | `E/scene3d/SceneRenderer.cpp:2003, 2124, 2416`, `Renderer.cpp:6590-6596` | Cena 3D parada custa bem menos por quadro | Médio | `PERF_*` de 3D (3d-sombra 0,24 ms no host), `Perf8C.BenchBetaHeavyScene` | H |
| P1-7 | Passo de +1 quadro sem busca por keyframe: na troca de época só abortar a entrega em voo | `E/playback` (`PlaybackController::step`), `E/media` (`VideoSource::thread_main`) | Uma decodificação em vez de flush + GOP | Baixo-médio | `stats_.seeks` em passo de quadro | H, E, PC |
| P1-8 | Não converter quadros que a composição não amostra (passo do consumidor no `DecodeRequest`, `render=false`), exceto mistura de quadros, fluxo e reverso | `VideoSource::thread_main`, `next_timed_frame`, `MediaCodecSource.cpp` | ~2× menos conversões e o dobro de antecipação em 60 fps dentro de 30 fps | Médio | Contadores de quadros convertidos | Contadores em E; ganho real só A |
| P1-9 | Miniaturas não disputam com o Play: decodificar do proxy pronto, segurar durante o Play (como os proxies), cache pequeno em disco | `E/media` (`ThumbnailService::decode`) | Play mais liso logo depois de importar; timeline reabre com miniaturas | Baixo | `PERF` de miniaturas (13,3 / 156,2 ms no host); `droppedFrames` tocando com miniaturas pendentes | H, E |
| P1-10 | Caminho de planos pela CPU: pool de buffers (como `pool_->take` do Windows) e separação de croma com NEON | `DecodedPlaneBounds.hpp:21` (`copy_decoded_yuv420`), `planes_from` | Menos alocação e cópia por quadro em GLES, modo seguro, miniaturas e proxy | Baixo; **NEON só se valida em aparelho ou em teste arm64** | `decode médio` no emulador; teste unitário do resultado bit a bit | Pool em E; NEON só A |
| P1-11 | Abertura do Android: adiar `AureaAds.initialize` (WebView + LevelPlay) para depois do primeiro quadro; cache em memória da chave do Keystore e primeira leitura fora da principal; pular a cópia das fontes quando o arquivo já existe com o mesmo tamanho e carimbo de instalação; pré-carregar a classe do motor em thread de fundo | `K/MainActivity.kt:99-112, 342, 397`, `K/ads/AureaAds.kt:20`, `K/ads/LevelPlayAdsBackend.kt:60-77`, `K/captions/Captions.kt:28-59`, `K/state/EditorStore.kt:931-975`, `K/engine/AureaEngine.kt:80-90` | Primeiro quadro e `engineReady` mais cedo | Baixo-médio (ordem de inicialização dos anúncios e do consentimento) | `adb shell am start -W` (TTID), `reportFullyDrawn` no `engineReady`, tag `AureaStart`, linha "abertura do motor" | E |
| P1-12 | Perfil de base do app (abertura, Home, abrir projeto, timeline) | novo módulo ou regra de geração em `android/` | Abertura fria e primeiras interações mais rápidas depois de instalar ou atualizar **[hipótese]** | Baixo no produto; custo de montar a geração | `am start -W` com e sem perfil compilado (`cmd package compile -m speed-profile`) | E (x86_64); em ARM só A |
| P1-13 | Salvar fora do lock: codificar a partir de um instantâneo; e abrir projeto com log de tempo, decodificação de imagens em tarefas e adiar o que não está no cabeçote | `Engine.cpp` (`save_project_impl` `:2299`, `load_project_checked` `:1871`), `E/command/History.cpp` | Sem pausa quando o salvamento automático dispara; projeto grande abre antes | Médio (consistência do instantâneo) | `save_stats()` (`lastLockNs/maxLockNs`); log novo de abertura | H, E |
| P1-14 | Memória: recalcular o orçamento ao voltar ao app ou abrir projeto, com histerese (a classe fica fixa); teto por classe no Android 64 bits; teto em bytes para originais de imagem na GPU e para os caches por idade | `E/platform/DeviceCapabilities.cpp:300, 355`, `Engine.cpp` (`apply_memory_budgets`), `Renderer.cpp` (`collect_resources`) | Menos morte por falta de memória em 3–4 GB; não fica preso a um orçamento ruim | Médio (vai e vem de cache) | PSS no `StressBattery` e no teste de playback | H, E; pressão real só A |
| P1-15 | Navegador de fontes: carregar a face fora da composição (`produceState` em IO, face padrão enquanto isso) e LRU; no iOS, tirar o registro do `body` da célula | `K/editor/FontBrowser.kt:248-257`, `I/app/FontBrowserView.swift:212-231` | Rolagem da lista de fontes sem engasgo | Baixo. A face padrão aparece por um instante antes da fonte real: confirmar que isso não conta como mudança visível | `ui>33` rolando a lista | E; iOS CI/A |
| P1-16 | iOS: `engine.start` e a sonda da NPU fora da principal; abrir projeto fora da principal reaproveitando a sobreposição `importingMedia`; expor os tempos de abertura na ponte | `I/app/AureaApp.swift:47`, `AureaModel.swift:1133, 1758, 3002`, `I/bridge/AureaBridge.mm:124` | Home interativa antes; sem congelar ao abrir projeto grande | Médio às cegas (muitos caminhos supõem `started`); o indicador ao abrir é uma pequena mudança visível, **decisão do dono** | Tempos de abertura no simulador da CI | CI, A |
| P1-17 | iOS: manter o `AVAssetReader` quando o alvo está à frente dentro do GOP; montar o índice de tempo na abertura | `I/bridge/IOSVideoDecoder.mm:392-503` | Scrub para a frente mais rápido | Médio, sem como validar aqui | `PerformanceTest.swift` | só A |
| P1-18 | PC: (a) invalidar a prévia só quando o motor publicar quadro novo; (b) cache da imagem Skia no shim `Bitmap` por geração; (c) `max_live_frames` do decoder do Windows para ~13 e intervalo de keyframe real; (d) orçamento do PC pela RAM total com piso, e degrau próprio de cache de prévia; (e) embrulhar o buffer mapeado sem o `memcpy` | `desktop/src/main/kotlin/pc/EnginePreview.kt`, `desktop/src/main/kotlin/shim/android/graphics/{Canvas,Bitmap}.kt`, `engine/platform/windows/WindowsMedia.cpp`, `desktop/.../shim/android/app/App.kt:73-79`, `engine/gpu/vulkan/VulkanHostPresent.cpp` | CPU e GPU ociosos perto de zero; timeline mais leve; playback com mais folga | Baixo em (a)–(d); baixo-médio em (e) | `-Dskiko.fps.enabled=true`; `aurea_pc_smoke.exe` (custo ocioso, cadência); `aurea_pc_decode_bench.exe` | PC |
| P1-19 | iOS: compilar o IPA de entrega com módulo inteiro (a CI força `singlefile`) | `.github/workflows/build-ipa.yml:169` | Ganho não medido | Tempo e memória da CI. **A 0.5.3 não gera IPA**, então fica anotado | — | CI |

### P2

| ID | O que | Onde | Onde valida |
|---|---|---|---|
| P2-1 | Escada de escala mais fina só na prévia (3/4, 3/8) para os intermediários por camada | `Renderer.cpp:173` (`texel_scale_for`) | H |
| P2-2 | GLES: desenhar a saída direto no FBO 0 com recorte invertido; só compilar ESSL quando o binário do programa faltar | `engine/gpu/gles/GlesDevice.cpp:172`, `GlesResources.cpp:211, 291` | E |
| P2-3 | Fluxo óptico com mais de 2 quadros de cache e nível de prévia no Rotobrush | `Renderer.cpp` (`video_flow`, `trim_flow_cache`), `E/ai` | H |
| P2-4 | Usar o pool de tarefas para abertura de fontes em vez de uma thread por fonte | `Engine.cpp:706`, `MediaManager::source_for` | H |
| P2-5 | Contexto EGL e programa compartilhados entre decoders; cache do intervalo de GOP por mídia | `engine/platform/android/GlVideoBridge.cpp` | A |
| P2-6 | Uma nova tentativa do mesmo caminho antes de rebaixar o decoder; tentar de novo o caminho GL depois de sessões limpas | `MediaCodecSource.cpp` (`next_frame`), `ExitDiagnostics.safeVideoMode` | A |
| P2-7 | Sobras de alocação por quadro na timeline principal (gradiente, `Paint`, `Path`, medição de texto de legenda) | `K/editor/timeline/TimelinePainter.kt:415, 459, 608, 1573` | E |
| P2-8 | `store.project` com `derivedStateOf` no layout; cache da localização de parâmetros; JNI fora do corpo de composição; `key` nas listas | ver 2.2 | E |
| P2-9 | Threads do PreRender dormindo de verdade quando não há uso; `mixer_main` sem acordar a cada 2 ms | `PreRenderCache.cpp:443`, `E/audio` | H |
| P2-10 | Heurística de calor sem a API (Android < 10): piora contínua do tempo de quadro desce o nível | `RenderScheduler.cpp` | H (lógica); efeito só A |
| P2-11 | Desfazer com camadas compartilhadas (cópia na escrita) | `E/command/History.cpp:118` | H |
| P2-12 | iOS: `Timer` de 60 Hz da timeline, glifo de pausa a 30 Hz, miniatura sem `query_layer_detail`, `UIImage.fromRGBA` sem três cópias | ver 2.2 | CI, A |
| P2-13 | Provedores de inicialização que não são usados na abertura (`MlKitInitProvider`, `MobileAdsInitProvider`) | `android/app/src/main/AndroidManifest.xml` | E |
| P2-14 | Avaliar `-O3` e ThinLTO no motor Android | `android/app/build.gradle.kts:174`, `engine/CMakeLists.txt:499-500` | Tamanho em qualquer build; velocidade só A |
| P2-15 | Pacote: uma só variante de pose do ML Kit; redes embutidas fora da `.so` (mudança visível, fora da 0.5.3) | `android/app/build.gradle.kts`, `engine/cmake/*` | Tamanho do AAB |
| P2-16 | PC: AppCDS, ProGuard, `Choreographer` por vsync, teste do MSI | `desktop/build.gradle.kts:225-236`, `shim/android/View.kt:96` | PC |
| P2-17 | iOS: P010 pelo cache de texturas (R16 + RG16) | `engine/gpu/metal/MetalResources.mm:1155` | só A |
| P2-18 | `operating-rate` e `low-latency` no MediaCodec | `MediaCodecSource.cpp` (`create_codec`) | só A |

---

## 4. Como medir antes e depois

Regra: medir antes de mexer, com o mesmo binário de base, e guardar os dois números.
O emulador varia muito entre rodadas (a mesma APK deu `video3` a 26,0 e a 18,8 fps),
então cada cenário roda pelo menos duas vezes e compara-se faixa com faixa.

**Host (este PC, motor com Vulkan real)**

- `aurea_tests.exe` (em `engine/build/host-*`), rodado pelo PowerShell com as DLLs do
  FFmpeg no `PATH`, sem Gradle em paralelo e com `AUREA_AUDIO_SILENT=1` (os testes
  não podem tocar som). Depois de mudar um header, apagar os `.obj` antes de confiar
  numa falha.
- Filtros úteis: `Perf.PERF_` (com verificador contra `docs/performance/
  baseline_host.json`, regressão = p50 +40 %), `EffectPerf.
  EveryCatalogEffectGpuCostAt1080p`, `Perf8C.BenchBetaHeavyScene`,
  `Gpu.SteadyPlaybackCreatesNoTexturesAndMeasuresGpu` (`test_gpu.cpp:1590`),
  `Heavy.QualityLadderAndExportIsAlwaysFull` (`test_heavy.cpp:915`), `PreviewBuffer`,
  `VideoSource`, `Export` e as imagens douradas (para provar que nada mudou).
- Campos do `PerfPOD` para comparar: `cpuPrepareMs`, `cpuRecordMs`, `gpuFrameMs`,
  `acquireMs`, `pacingP95Ms`/`pacingP99Ms`, `pipelineCompilesLive`,
  `texturesCreated`, `droppedFrames`.

**PC (app nativo)**

- `tools\build_aurea_pc_jni.cmd` gera em `engine\build\pc-jni\bin`:
  - `aurea_pc_decode_bench.exe <vídeo> [quadros=300] [paralelos=3]`: ms por quadro
    (média, p50, p95, máximo), buscas, decoders em paralelo.
  - `aurea_pc_smoke.exe [saída] [vídeos…]`: primeiro quadro, cadência tocando, custo
    ocioso, falhas de áudio, intervalos de apresentação.
  - `aurea_pc_video_test.exe <projeto.aurea> [saída]`: 3 s de playback (quadros
    pretos e repetidos, maior intervalo), tempo de miniatura.
- Logs `desktop/run-*.log`: linha "abertura do motor: X ms (aparelho, gpu, renderer
  com N pipelines, resto)", contagem de "criado durante o playback", `classe=` e
  `orcamento_mb=`. `-Dskiko.fps.enabled=true` para o custo da janela.

**Emulador Android (x86_64)**

- `PlaybackBench`: `--es aureaDemo bench --es aureaBench <rótulo>` (debug; precisa de
  `demo/pf.mp4` na pasta externa do app); grava `bench-<rótulo>.txt` com `fps`,
  `perdidos`, `pacing95/99`, `cpu`/`gpu`, `ui95/99`, `ui>33`, `status99`, `inicio`.
  Cenários: `video1`, `scrub`, `video1+fx`, `cache60`, `video3`, `texto12`,
  `arrasto`.
- Teste de playback: `--es aureaPlaybackTest <arquivo> --ei testFps N`; relatório
  `.jsonl` com `heartbeatMaxMs` (trava da principal) e PSS.
- `StressBattery`: Configurações → `StressSheet` (`K/home/SettingsTab.kt:136`).
- Painel DEV/HUD: `store.toggleHud()` nas configurações do projeto.
- Abertura: `adb shell am start -W` (TTID); logcat `AureaStart`, `AureaEngine`
  ("abertura do motor"), `AureaAds`, `AureaFonts`. Falta instrumentar
  `reportFullyDrawn` e `Trace.beginSection`; sugerido como primeiro passo da Fase B.
- `simpleperf` já foi usado no emulador para achar os leitores do PreRender.

**iOS**

- Só a CI: 76 casos de XCUITest no simulador, nenhum com `measure`/`XCTMetric`. Dá
  para contar chamadas da ponte e tempos de abertura depois que a ponte os expuser.
  `PerformanceTest.swift` e `ShellPerfHud` só servem em aparelho.

---

## 5. O que não dá para medir sem aparelho físico

Vale declarar isso no relatório de cada lote da Fase B.

- Qualquer número de fps, tempo de quadro ou engasgo em Snapdragon 660/845, Helio
  P60, Exynos, Unisoc ou em qualquer GPU Adreno, Mali ou PowerVR. O emulador usa a
  RTX 3050 do host.
- Decodificação por hardware: caminho sem cópia, limite real de instâncias, recusa
  do codec e queda para software, `operating-rate`, vídeo de 10 bits/HDR. O emulador
  decodifica por software e sobe planos pela CPU.
- Código ARM: `armeabi-v7a` e `arm64-v8a` compilam, mas não executam aqui; qualquer
  NEON escrito à mão, `-O3` ou LTO só tem a velocidade conferida em aparelho.
- Backend GLES em driver real e compilação de pipelines em driver Adreno/Mali
  (onde o tempo de compilação é ordens de grandeza maior que na NVIDIA).
- Calor, bateria e estrangulamento térmico; pressão de memória real e mortes pelo
  sistema em 3–4 GB.
- Ganho do perfil de base em ARM (no emulador mede-se a tendência em x86_64).
- iOS inteiro: Metal, VideoToolbox, ProMotion, jetsam; nem compilar é possível aqui.
- "Todos os processadores": não é comprovável. O que dá para entregar é a correção
  por leitura de código mais a medição no host, no PC e no emulador.

---

## 6. Já está bom (não reauditar)

- **Render**: efeitos neutros saem da cadeia (`is_identity`, `EffectGraph.cpp:475`) e
  os de cor por pixel são fundidos; camadas com opacidade 0 e fora da tela são
  puladas; camadas em modo Normal vão num passe só; o desfoque gaussiano usa
  pirâmide; partículas são analíticas no shader; vídeo tem caminho sem cópia;
  parâmetros de efeito por id inteiro; um acerto de cache pula o `prepare` inteiro;
  a thread de render dorme quando ociosa.
- **Mídia**: a thread de render nunca espera o decoder; pedidos são "o último
  vence"; o scrub mostra o keyframe primeiro; tempo por PTS com VFR; codecs são
  soltos em segundo plano; fontes abrem sob demanda, fora do render; o callback de
  áudio não aloca nem trava; formas de onda têm cache em disco.
- **Android**: o laço de status é pelo `Choreographer` e cai para 250 ms depois de
  30 quadros quietos; o palco não lê estado no corpo; a timeline principal é um
  `Canvas` único com leituras na fase de desenho e caches de layout; o navegador de
  efeitos tem listas com chave e prévias fora da principal; a Home decodifica capas
  em IO num LRU de 24 MB; o salvamento automático roda em IO; o trim chega aos
  caches e ao motor; sem `Thread.sleep`, `runBlocking` ou laço de espera no app.
- **iOS**: shaders pré-compilados e `MTLBinaryArchive`; `CAMetalLayer` bem
  configurada; o display link só roda tocando; o render para em segundo plano; capas
  e prévias com `NSCache` limitado.
- **Armazenamento**: gravação atômica com `.bak`; varreduras de órfãos e limites
  fora da thread principal.
- **PC**: a classe já sai ULTRA; a apresentação segue o vblank do DWM.

## 7. Lacunas desta auditoria

Não lidos: backend Metal; `ColorPickerKit`, corpos de `EffectsBrowser`/`EffectsView`,
`ProxyOptimizer`, `StorageView` (iOS); `AppearancePanel.kt`, `SpeedGraph.kt`,
`TextPanel.kt`, `AddLayerPanel.kt`, `LayerPanel.kt` (Android); tetos em bytes de
layout de texto, Rotobrush e fluxo; se a segmentação do Rotobrush roda dentro do
lock; a semântica de cópia do Skiko; a limitação do scrub no lado Kotlin.

## 8. Notas para a Fase B

- **Esperar os outros agentes saírem do motor.** `Engine.cpp`, `Renderer.cpp` e
  `EditorStore.kt` têm mudanças sem commit. Nunca `git checkout/reset/stash/commit`.
- **Sobreposição com a exportação**: P0-1 (pipelines), P0-6 (decoders), P1-13
  (salvar) e P1-14 (memória) tocam código que a exportação usa. Combinar com
  `EXPORT_053_AUDIT.md` antes de mexer.
- **Paridade**: toda mudança de interface vale para Kotlin e Swift no mesmo
  trabalho; regras no motor C++. O PC compila o Kotlin do Android sem alterações,
  então mudança em `K/` precisa recompilar `desktop/` (`compileKotlin`).
- **Android compila o motor com `-fno-exceptions`**: nada de `try/catch` em código
  novo (o host MSVC aceita e esconde o erro).
- **Ordem sugerida**, do menor risco para o maior:
  1. Instrumentação e linha de base (`reportFullyDrawn`, `Trace`, log do flush de
     pipelines, log de abertura de projeto) e rodar as bancadas "antes".
  2. Motor, baixo risco: P0-2, LRU da LUT (parte de P0-3), `try_lock` de P0-5(b),
     gravação do cache de pipelines (P0-1d/e), P1-1.
  3. Android: P0-4(a), P1-5, P1-11, P1-15, depois P0-4(b)/(c) e P1-12.
  4. iOS em paridade: P0-5(a)/(c) e os menores de P2-12.
  5. Mídia: P1-7, P1-9, P1-10 (pool), depois P0-6 e P0-7.
  6. Render: resto de P0-1 e P0-3, P1-2, P1-3, P1-4, P1-6.
  7. PC: P1-18.
- A cada lote: o que mudou, os números antes e depois com a origem, e o que ficou
  só conferido por leitura.

---

## 9. Phase B part 1 — done (12/11/2026)

Regras seguidas: sem `git checkout/reset/stash/commit`; nenhuma mudança visível de
interface no Android nem no iOS; sem `try/catch` no motor; cada mudança pequena e
reversível sozinha; o vídeo exportado não muda. Nada foi commitado. O coordenador
cancelou a parte 2 (a 0.5.3 sai hoje); o que eu já tinha começado dela entrou só
quando estava completo e com teste passando (9.3). O que estava pela metade saiu
(9.7).

### 9.1 O que deu e o que não deu para medir aqui

- **Host (este PC, MSVC Release, Vulkan real na RTX 3050)**: pasta própria
  `engine/build/host-perf`, reconstruída do zero antes dos testes. Não existe um
  binário "antes" limpo: durante o trabalho outros agentes mudaram headers
  (`Layer.hpp`, `PathRig.hpp`, `VectorTree.hpp`, `Renderer.hpp`, `Engine.hpp`) e as
  tentativas de linha de base pararam em erros de arquivos que não são desta tarefa.
  Os números de "antes" vêm de (a) medir no MESMO binário o trabalho que foi retirado,
  (b) logs que já existiam no repositório, ou (c) só leitura de código. Cada linha diz
  qual.
- **Emulador Android: não usado.** Nenhum estava ligado e gerar APK estava fora das
  regras; sem APK novo não há "depois". O Kotlin foi só compilado.
- **App do PC: não executado** (aberto com o dono). `aurea.dll` não foi recompilada:
  as mudanças do PC foram compiladas (Kotlin) ou conferidas por leitura (C++).
- **iOS: não compila aqui.** Edições de Swift listadas em 9.5 para a CI.

### 9.2 Motor (C++ compartilhado: Android, iOS e PC)

| # | Mudança | Arquivos | Antes → depois |
|---|---|---|---|
| M1 | **P0-2** `default_family()` devolve `""` direto, sem abrir todas as fontes do sistema dentro do preparo do quadro (com o modelo travado). O resultado já era `""`: pixels iguais (decisão do dono). | `engine/src/text/Text.cpp` | **[medido: host, mesmo binário]** o laço retirado abria as 139 fontes do sistema deste PC (345,3 MB, que ficavam no cache) em 713,2 ms, uma vez por processo, no preparo do quadro; o layout do mesmo texto agora leva 0,07 ms. Teste `Text.WeightSpanWithoutFamilyCostsALayoutNotEverySystemFont` (mesmos glifos e mesma largura de antes). Num celular com centenas de fontes em `/system/fonts` o custo era maior **[hipótese]**. |
| M2 | **P0-3 (fontes)** As fontes do sistema são lidas adiantado num worker de fundo na abertura do motor (`FontManager::prewarm`, fora da trava do gerenciador). A lista continua montada por `scan_locked`, no mesmo momento e na mesma ordem de antes (a ordem decide o desempate entre fontes de mesma família e peso); a varredura só deixa de reler os arquivos. Log novo com o tempo da varredura. | `engine/src/text/FontManager.cpp`, `engine/include/aurea/text/FontManager.hpp` (um método novo, sem mudar o layout da classe), `engine/src/engine/Engine.cpp` (`Engine::initialize`) | Não medido isoladamente (o host tem poucas fontes). Por leitura: tira da thread de render, no 1º texto com família, a leitura do cabeçalho de cada arquivo de `/system/fonts` (centenas no Android). |
| M3 | **P0-3 (LUT)** O cache de tabelas `.cube` do motor não é mais apagado inteiro ao chegar a 8 entradas: teto de 32 MiB e 256 entradas, saindo primeiro o que ninguém mais segura. Pior caso de memória igual ao de antes. | `engine/src/engine/EngineMedia.cpp` (`make_cube_lut_room`, 3 usos) | Não medido (sem teste com mais de 8 LUTs). Por leitura: com 9+ LUTs, cada volta relia e interpretava os arquivos no preparo do quadro. |
| M4 | **P0-1 (a)** A varredura de aquecimento declara o conjunto do fluxo óptico (12 pipelines de `video_flow` e do desfoque vetorial) quando alguma camada pode pedir fluxo (`frameBlend == 2`, `vectorBlur > 0`, Flow Retime, Flow Motion Blur). | `engine/src/render/Renderer.cpp` (`scan_for_warmup`) | **[medido: host]** os 12 pipelines entram na varredura do projeto; o quadro que os compila leva 15,6 ms na RTX 3050. Antes eles nasciam no meio do primeiro quadro com fluxo, tocando (10 num mesmo quadro nos logs do PC de 11/11). Teste `Startup.FlowPipelinesWarmWithTheProjectNotInsideTheFirstFlowFrame`. Em GPU de celular a compilação é bem mais cara **[hipótese]**; só aparelho mede. |
| M5 | **P0-1 (b)** Tocando, a varredura também roda quando a revisão do modelo muda (efeito ligado com o playback rodando): os pipelines dele entram juntos antes de pegar a imagem da tela, não um a um no meio dos quadros. | `Renderer.cpp` (`Renderer::prepare`, uma condição) | Não medido (precisa de playback com superfície). |
| M6 | **P0-1 (d)** O cache de pipelines em disco é gravado com o motor parado há 500 ms quando surgiram pipelines desde a última gravação. Antes só no `shutdown`/`suspend`: o sistema matando o app perdia a sessão. Nunca espera (`try_lock` no render; não roda com export, captura ou playback). | `engine/src/engine/Engine.cpp` (`Engine::render_thread_main`) | Não medido aqui (exige superfície). Referência: abertura do motor no PC com cache inválido 1587–2133 ms contra 255–372 ms com cache (logs de 11/11). |
| M7 | **P0-1 (e)** Logs: o pré-aquecimento diz quanto tempo levou; abrir projeto ganhou a linha `projeto aberto em X ms`. | `Renderer.cpp` (`flush_warmup`), `Engine.cpp` (`Engine::load_project`) | — |

### 9.3 Também entraram (começados na parte 2, completos e com teste)

| # | Mudança | Arquivos | Medição / teste |
|---|---|---|---|
| X1 | **Prévia guardada sobrevive a travar, ligar o ímã e renomear** (renomear só sem nenhuma expressão no projeto, porque expressão lê nome de camada). A revisão do MODELO sobe (a UI relê a lista), junto com o contador de edição por trecho que já existia (`previewRangeEdits_`): `preview_cache_revision()` não muda. As chaves do preenchimento ocioso e do PreRender passaram a usar a revisão da prévia, para essas edições não descartarem o que já foi guardado nem cancelarem o PreRender. | `engine/src/engine/Engine.cpp` (`project_has_expressions`, `apply_command_internal`, chaves `PreviewIdleKey`/`preRenderKey_`) | Teste `PreviewBuffer.LockAndRenameKeepTheCachedPreviewUnlessAnExpressionCanReadNames` (20 verificações): trava e renomeação mantêm a linha azul; com uma expressão no projeto, renomear descarta. Ganho: sem re-renderizar até 300 quadros depois de cada travar/renomear; não medido em tempo. |
| X2 | **Teto de decoders da prévia também em 64 bits**: entrada 4, os outros 8, nunca acima das instâncias de hardware que o aparelho declara (mínimo 2). Mesma regra do teto de 32 bits: só fontes OCIOSAS cedem o lugar. O export tem o próprio `MediaManager` (sem teto): não muda. | `engine/include/aurea/media/MediaManager.hpp` (`set_source_cap` + um campo), `engine/src/media/MediaManager.cpp`, `Engine.cpp` (`Engine::initialize`) | Teste `AddressSpace32.SixtyFourBitPreviewCapEvictsOnlyIdleSources` (53 verificações): 10 cortes seguidos nunca passam de 4 decoders; 6 layers visíveis juntas não trocam de decoder. O efeito no aparelho (sem cair no decoder de software) só aparelho mede. |
| X3 | **Proxy com o aparelho frio**: a geração usa até 50% do tempo (antes 25% sempre). Morno, quente ou crítico: 25% como antes; parada enquanto toca, como antes. | `engine/src/media/PreviewProxy.cpp` (`PreviewProxyService::generate`) | Por construção: com o trabalho dominando, o tempo de parede cai de 4× para 2× o trabalho. Não medido em aparelho. |
| X4 | **Cache do layout de texto**: `layout_quads` guarda os últimos 64 resultados (chave = `raster_key` + fonte + margem + nível de glifo grande, válida para uma geração do atlas). Texto parado não refaz o shaping do HarfBuzz a cada quadro. O resultado é o mesmo, campo por campo (os quads saem dos mesmos glifos já no atlas). | `engine/src/text/Text.cpp` | **[medido: host, mesmo binário]** 55 glifos: refeito 0,440 ms, do cache 0,0002 ms. Teste `Text.RepeatedLayoutComesFromTheCacheAndIsIdentical` (258 verificações: quads idênticos byte a byte; outro texto ou outra margem não vêm do cache). |
| X5 | **Piso térmico do AUTO**: o `minPreviewDenominator` da política (QUENTE = 1/2, CRÍTICO = 1/4) chega ao governador (`set_quality_floor` não tinha quem chamasse). Definido em `current_render_settings`, na mesma thread que roda `adapt().update()`. Atenção: o Android mapeia MODERATE com estrangulamento para QUENTE, então a prévia AUTO cai para 1/2 já no MODERATE; a exportação não muda. | `Engine.cpp` (`current_render_settings`) | Teste `Thermal.HotThermalPolicyFloorsTheAutomaticPreviewAtHalf` (8 verificações): QUENTE leva o AUTO a ≥ 1/2. O teste do controlador que diz "quente: resolução fica" continua valendo (é o controlador sozinho, sem piso). |

### 9.4 Android (Kotlin; o PC compila os mesmos arquivos)

| # | Mudança | Arquivos | Medição |
|---|---|---|---|
| A1 | **P0-4** Releitura do detalhe fora do caminho do toque: 15 setters de controle contínuo que só mandam comando (ou já guardam o valor otimista) pedem `refreshDetailSoon()`, que sai uma vez no próximo quadro do laço de status. Antes cada evento de toque fazia ~20 consultas bloqueantes ao motor na thread principal e ainda lia o valor antigo. Setters: `setTextContent`, `setTextSize`, `setTextColor`, `setTextStrokeWidth`, `setTextStrokeColor`, `setTextTextureValues`, `setTextDynamicValue`, `setTextStyleValue`, `setTextStyleValues`, `setShapeFill`, `setShapeStroke`, `setAudioVolume`, `setAudioGain`, `setAudioPan`, `setAudioFade`. | `android/app/src/main/java/com/aurea/aurea/state/EditorStore.kt` | Não mensurável aqui. Em emulador: cenário `arrasto` do `PlaybackBench` (`ui95/ui99`, `ui>33`). |
| A2 | **Abertura: fontes** Os 4 arquivos (8,8 MB) não são mais recopiados a cada abertura fria: carimbo de instalação + tamanho ao lado de cada fonte; a data é renovada para a limpeza do cache (`CacheStorage.enforceLimits`) seguir igual. No PC o carimbo é `0`: copia sempre, como antes. | `EditorStore.kt` (`installBundledFont`) | Não mensurável aqui. |
| A3 | **Abertura: anúncios** `AureaAds.initialize` (provedor do WebView por IPC, UMP, `LevelPlay.init`) sai de dentro do `onCreate` e roda depois do primeiro desenho da janela, mantendo a ordem em relação ao "app carregou" do App Open. | `android/app/src/main/java/com/aurea/aurea/MainActivity.kt` | Não mensurável aqui (`am start -W` no emulador). |
| A4 | **Abertura: Keystore** O mesmo cifrado já aberto no processo não volta ao Keystore (~9 leituras da sessão viram 2). Só em memória; `put`/`remove`/chave nova invalidam; a chave de API digitada pela pessoa (Groq) fica de fora. | `android/app/src/main/java/com/aurea/aurea/captions/Captions.kt` (`KeyVault`) | Não mensurável aqui. |
| A5 | **Medição** `reportFullyDrawn()` quando o motor fica pronto ("Fully drawn" no logcat). | `MainActivity.kt` | — |
| A6 | **P1-5** Timeline do modo Edição: a falta de miniatura é lembrada até a geração mudar (antes, consulta bloqueante por clipe a cada desenho). | `android/app/src/main/java/com/aurea/aurea/editor/EditTimeline.kt` | Não mensurável aqui. |
| A7 | **P1-15 (parte invisível)** Navegador de fontes: as faces das próximas 16 células abrem em IO antes de entrarem na tela; quem chega antes abre na hora, como antes (nenhum espaço em branco novo). | `android/app/src/main/java/com/aurea/aurea/editor/FontBrowser.kt` | Não mensurável aqui. |
| A8 | **Perfil de base**: não dá para gerar sem aparelho/emulador e sem APK. Módulo preparado e DESLIGADO (fora do `settings.gradle.kts`, nunca compilado); o cabeçalho do `build.gradle.kts` dele traz os passos para ligar e medir. | `android/baselineprofile/` | — |

Compilação: `:app:compileDebugKotlin -PaureaAbi=arm64-v8a` passou (12/11, 14:2x, `BUILD SUCCESSFUL`, sem erro).

### 9.5 iOS (Swift não compila aqui: conferir na CI)

Uma única edição de Swift:

- `engine/platform/ios/app/AureaModel.swift`: propriedade nova
  `private var lastOptimisticDetail: TimeInterval` (no corpo da classe `AureaModel`,
  logo acima de `optimisticPlayhead`) e o corpo de `optimisticPlayhead(_:)`. O arrasto
  da régua relia o inspetor inteiro a cada passo; agora no máximo a cada 100 ms e com
  `refreshSelectedLayer(refreshEffectStack: false)`, como o Android desde `608ba091`.
  O valor final vem do `refreshStatus` quando o motor confirma o cabeçote.

O iOS também recebe as mudanças do motor (9.2 e 9.3).

### 9.6 PC

| # | Mudança | Arquivos | Medição |
|---|---|---|---|
| D1 | A prévia deixa de pedir quadro ao relógio do Compose a cada vsync para sempre: depois de 30 buscas vazias passa a um temporizador de 5 ms; o primeiro quadro novo volta ao vsync. | `desktop/src/main/kotlin/pc/EnginePreview.kt` | Não medido (o app não pode ser aberto por mim): `-Dskiko.fps.enabled=true` parado e tocando. |
| D2 | `Canvas.drawBitmap` não cria mais uma imagem do Skia por chamada: o `Bitmap` guarda a imagem até os pixels mudarem. Custo: uma cópia a mais dos pixels por bitmap desenhado enquanto ele vive. | `desktop/src/main/kotlin/shim/android/graphics/Bitmap.kt`, `.../Canvas.kt` | Não medido. |
| D3 | Decoder do Windows declara 13 quadros vivos (antes o padrão 8: 3 em cache e 1 para trás; agora 8 e 6). | `engine/platform/windows/WindowsMedia.cpp` | Não compilado (vai no `aurea.dll`, fora das regras); medir com `aurea_pc_decode_bench.exe`. |

Compilação: `desktop` `compileKotlin` passou (12/11, logo depois do Android, `BUILD SUCCESSFUL`, sem erro).

### 9.7 Retirado ou não feito, e por quê

- **Retirado: tempo de GPU por quadro no GLES** (a correção do AUTO no GLES, P1-1).
  Só compila no Android e o build Android não terminaria no prazo; código de GPU sem
  nem compilar não vai para a versão. `GlesDevice.cpp`/`GlesInternal.hpp` voltaram a
  ficar idênticos ao HEAD.
- **Não aplicado: linha azul sem esperar o modelo** (`copy_preview_buffer_ranges` com
  `try_lock`). Precisava de dois campos novos em `Engine.hpp`, que ficou travado por
  outro processo por mais de 15 minutos; nenhuma metade ficou no código.
- **Não feito (parte 2 cancelada)**: reaproveitar decoder entre cortes (o pré-abrir de
  1,5 s faz os dois coexistirem: pouco ganho); recuperar o decoder rebaixado para
  software no meio do uso (reabrir seria um corte visível; precisa de aparelho);
  expirar o "modo seguro de vídeo" do Android (troca um possível crash repetido por
  velocidade: decisão do dono); proxy pelo decoder de hardware (o caminho de planos
  de fabricante é o que o modo seguro protege; precisa de aparelho); reaproveitar o
  mapa de sombra (precisa de chave sobre pose, skinning e morph; a sombra é ~21% do
  3D no host: 0,24 de 1,13 ms); gravar fora do lock (o `Project` não tem cópia;
  o autosave já só roda parado e ocioso há 3 s); abrir projeto com decodificação em
  paralelo (o orçamento de importação admite uma imagem por vez de propósito);
  iOS `engine.start`/abrir projeto fora da principal (muitos caminhos supõem
  `started` logo depois do `onAppear`; sem compilar, arriscado); `AVAssetReader`
  reaproveitado entre buscas (o `VideoSource` já não busca para alvos à frente dentro
  do intervalo de keyframe, e o leitor não pode ser reposicionado no meio da leitura:
  `resetForReadingTimeRanges` exige ler até o fim antes); `-O3`/LTO no motor Android
  (precisa de dois builds Android completos para comparar tamanho; ficou o script
  `build/agents/perf_android_build.cmd`, que monta o motor Android fora do Gradle com
  as mesmas flags e `AUREA_PERF_BUILD_TYPE=Release` para o `-O3`).
- **Tamanho do pacote (só números, nada removido)**: no AAB de 11/11, `libaurea.so`
  113,3 MB (arm64) e 107,1 MB (v7a), dos quais ~90 MB são `.rodata`. As redes
  embutidas somam ~74 MB (`midas-v21-small` 31,7; EfficientTAM 21,8; RIFE 10,1;
  real-esrgan e span ~8; u2netp 2,2), mais dex 22,1 MB e 3 modelos de pose do ML Kit
  (12,2 MB; as duas variantes são usadas: Headbop e mocap). Tirar redes do pacote
  pede entrega sob demanda (mudança visível): decisão do dono.

### 9.8 Testes do motor depois das mudanças

Binário `engine/build/host-perf` reconstruído do zero às 13:33–14:01 (12/11), rodado
da pasta `engine\`, um filtro por vez, prioridade abaixo do normal, sem som.

| Filtro | Testes | Verificações | Falhas |
|---|---|---|---|
| Os 6 testes novos (9.2/9.3) | 6 | 353 | 0 |
| `Text` | 269 | 93.180 | 0 |
| `Startup` (pré-aquecimento de pipelines, abertura) | 40 | 582 | 0 |
| `PreviewBuffer` | 15 | 344 | 0 |
| `PreviewIdle` | 9 | 182 | 0 |
| `PreRender` | 31 | 4.963 | 0 |
| `CubeLut` | 4 | 16.442 | 0 |
| `AddressSpace32` | 7 | 138 | 0 |
| `MediaManager` | 3 | 46 | 0 |
| `VideoSource` | 27 | 981 | 0 |
| `PreviewProxy` | 18 | 430 | 0 |
| `Thermal` | 9 | 221 | 0 |
| `Expr` | 60 | 5.712 | 0 |
| `Serialization` | 37 | 66.899 | 0 |
| `Golden` (imagens douradas) | 11 | 61.744 | 0 |
| `Playback` | 41 | 1.350 | 2 (anteriores) |
| `Export` | 289 | 225.058 | 1 (anterior) |

As 3 falhas já existiam antes deste trabalho, com o mesmo sintoma, nos logs do
repositório: `CaptureEpochGpu.NaturalPlaybackAdvanceDoesNotCancelBoundCapture`
(`advancedTo >= 3`; já falhava em `host-046-051-full.log` de 09/11 e
`host-alpha-fx-gpu2.log` de 10/11), `Perf8C.SteadyPlaybackOf50LayersCreatesAndAllocatesNothing`
(3,0 alocações por quadro; igual em `host-046-full.log` de 02/11 e de 09/11) e
`ExportWatchdog.LateValidationCannotPublishAnAbandonedSession` (já falhava em
`host-alpha-fx-s-Export.log` de 10/11). As imagens douradas passaram sem atualizar
referência: o export não mudou.

Um teste novo falhou na primeira rodada por erro do próprio teste: ele lia
`EngineStatus::modelRevision`, que `read_status()` não preenche. Passou a ler o que as
duas interfaces leem (`fill_status` → `EngineStatusPOD`); a mudança do motor não mudou.

### 9.9 Para quem integrar

- `MediaManager.hpp` e `FontManager.hpp` mudaram (um campo e um método): apague os
  `.obj` das pastas de host antes de confiar num teste (o MSVC em pt-BR não segue
  dependência de header).
- Nada foi commitado. Os arquivos tocados estão nas tabelas acima.
