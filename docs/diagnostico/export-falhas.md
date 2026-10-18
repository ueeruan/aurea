# Diagnóstico: falhas de exportação (0.2.3, build 2191)

Data: 18/10/2026. Branch `claude/release-2191`.

Método: leitura do código-fonte (motor C++, Android/Kotlin, iOS/Swift). Nada foi
compilado, executado ou testado em aparelho. Nenhum arquivo de código foi alterado.

Legenda:
- **[PROVADO]**: o comportamento está no código citado.
- **[INFERIDO]**: dedução plausível, sem execução que a confirme.
- **[NÃO COMPROVADO]**: hipótese que precisa de dados do usuário (projeto ou log).

---

## Resumo

1. **"Frame 0 of 522520"**
   - A tela estava na fase Running, não em "preparando". Logo, `start_export`
     retornou 0: o encoder abriu, a GPU alocou e o worker começou. [PROVADO]
   - 522520 é `ctx->frames`. Com fps de saída = fps da composição e "aparar ao
     conteúdo" ligado (padrão), isso significa que alguma camada de conteúdo
     termina no quadro 522520 e que `duration_` ≥ 522520 (4,84 h a 30 fps). [PROVADO]
   - Não há troca de unidade (ms/µs/s→quadros) em nenhum caminho de importação,
     JNI, seek ou fps. Todos foram conferidos. [PROVADO]
   - Causa mais provável: a duração da composição infla sem teto e não volta
     sozinha. Camadas novas herdam essa duração. O principal suspeito é a
     auto-rolagem da timeline ao arrastar um clipe ou a borda dele, que funciona
     como catraca. [código PROVADO, execução INFERIDA]
2. **Parado no quadro 0**
   - Nada do que roda antes do 1º quadro cresce com o total de quadros. O 1º
     quadro depende de decoder, shaders e partida do encoder. [PROVADO]
   - O motor limita a espera do quadro a 30 s sem progresso ou 120 s no total.
     A tela do Android cancela após 180 s sem mudança e desiste 15 s depois.
   - "Para sempre" só acontece se uma chamada nativa não voltar e ignorar o
     cancelamento. [INFERIDO]
3. **Falha "em quase todos os aparelhos"** — causa nº 1, comum a todos:
   - O snapshot do export resolve **todos** os assets do projeto, inclusive os
     não usados, os de camadas apagadas e os substituídos.
   - No Android, cada `content://` é copiado inteiro a cada export. Uma URI morta
     em qualquer asset derruba o export com **erro 18**.
   - Assets nunca são removidos do projeto, e "Substituir mídia" cria um asset
     novo sem tirar o antigo. Reimportar não conserta o projeto. [PROVADO]

---

## A. Como uma composição chega a 522520 quadros

### A.1 De onde vem o número exibido [PROVADO]

- A linha "Frame %1$s of %2$s · %3$s fps · %4$s left" (`sh_export_progress`) é
  montada em `android/.../editor/ExportScreen.kt:656-657`.
  - Ela recebe `st.framesTotal`, vindo de `Exporter.kt:414-420`.
  - Esse valor vem de `ExportProgressPOD.framesTotal`: `EnginePods.kt:509`,
    offset 12, igual a `BridgePods.hpp:313`.
  - O POD é preenchido em `Engine.cpp:17085`, a partir de `ctx->progress.framesTotal`.
- `ctx->progress.framesTotal = ctx->frames` é atribuído em `Engine.cpp:13723`, no
  fim de `start_export`, logo antes de o worker começar (`:13741`).
- O cálculo está em `engine/src/engine/Engine.cpp:13378-13383`:
  `seconds = export_duration(trim) / comp->fps()` e
  `frames = ceil(seconds * ctx->fps)`.
  - O único teto é `u32`.
- `Composition::export_duration` (`engine/src/timeline/Composition.cpp:52-68`):
  - Com trim (padrão Android, `Exporter.kt:133`; iOS, `AureaModel.swift:217`),
    retorna o maior `end` entre as camadas que não são Null, Camera, Light,
    Adjustment ou Unknown.
  - Esse valor é limitado a `duration_`.
- O fps de saída vem de `ExportScreen.kt:91,318-322` → JNI `aurea_jni.cpp:3787`
  → `Engine.cpp:13369-13372`.
  - Valores possíveis: 0 (usa o da composição) ou 1–240.
  - Acima de 240 é recusado. Não existe caminho que gere fps lixo.
- A frase "Lining up the pixels…" (`exp2_fun_1`) é só a 1ª de 10 frases que
  giram a cada 3 s (`ExportScreen.kt:603-606`). Ela não indica a fase nem o tempo
  decorrido.

### A.2 Unidades conferidas (todas corretas) [PROVADO]

| Caminho | Unidade | Onde |
|---|---|---|
| Duração da mídia no Android (`AMEDIAFORMAT_KEY_DURATION`) | µs | `engine/platform/android/MediaCodecSource.cpp:227,1342` |
| Importar vídeo/áudio → quadros | `ceil(us*fps/1e6)` | `Engine.cpp:2209,2273` |
| Seek (Kotlin → motor) | ns (`frameTimeNs`) | `EditorStore.kt:3558,3633`; `Engine.cpp:16873` |
| `LayerSetTimeRange` | quadros absolutos | `CommandBatch.kt:117-127`, `TimelineController.kt:971-990` |
| Duração em Ajustes do projeto | s × fps, teclado até 86400 s | `ProjectSettingsSheet.kt:198-199` |
| Gravador de voz | µs, via `import_audio` | `VoiceRecorder.kt:253-290` |
| `retime` (troca de fps) | proporcional, preserva segundos | `Composition.cpp:299-339` |
| Carregar projeto | quadros i64 crus, sem migração de unidade | `Serialization.cpp:1990,1005-1007` |

No iOS, a duração do vídeo vem do `timeRange` da trilha. A do **áudio** usa
`asset.duration` (`engine/platform/ios/bridge/IOSVideoDecoder.mm:674,823`) e pode
superestimar quando o vídeo é mais longo. Isso é um risco, não a causa do print.

### A.3 Caminhos que inflam a duração (sem teto em lugar nenhum)

- `Composition::set_duration` só exige ≥ 1 (`Composition.hpp:193-194`).
- Os comandos aceitam até 2^40 quadros (`Engine.cpp:15349`).
- O export só barra acima de `u32`.

1. **Catraca da auto-rolagem da timeline** — principal suspeito. [código PROVADO, execução INFERIDA]
   - Ao arrastar um clipe ou a borda dele perto da margem direita, o laço de
     auto-rolagem chama `holdView(view + passo)` e depois `autoApply`
     (`TimelineController.kt:1392-1402`).
   - `holdView` limita a vista a `store.navigationEnd`, o maior `end` entre as
     camadas (`TimelineController.kt:204`; `Engine.cpp:13167-13173`).
   - Durante o arraste, esse maior `end` é o próprio clipe arrastado. Cada passo
     empurra o clipe, que empurra o teto. O `maxDelta` vai até `Int.MAX_VALUE`
     (`TimelineController.kt:974`).
   - O motor aceita o novo `end` e estende `duration_` (`Engine.cpp:15697-15741`,
     `:8186-8188`).
   - Na zoom mínima, isso passa de 1000 quadros por segundo de dedo parado. Não foi
     medido em aparelho.
   - O iOS tem a mesma lógica (`engine/platform/ios/app/TimelineView.swift:1946`).
2. **Puxar a borda além da mídia não tem limite** [PROVADO]
   - O trecho extra repete o último quadro (`Engine.cpp:8056`, `:8148-8154`,
     beta 0.1.6).
3. **A duração gruda e camadas novas a herdam** [PROVADO]
   - `duration_` só diminui por "Aparar projeto", por Ajustes ou por troca de fps.
   - Texto, forma, nulo, imagem, modelo 3D, partículas e luz nascem com
     `end = max(playhead+1, duration)` (`Engine.cpp:2433,2683,2849,4717,8708,8772,9132,10050`;
     `EngineVector.cpp:88,536,575`).
   - Basta uma inflação, mesmo desfeita pelo usuário arrastando de volta. Qualquer
     texto ou imagem criado depois passa a ir até o quadro 522520, e o "aparar ao
     conteúdo" deixa de ajudar.
4. **Contêiner com duração falsa** [INFERIDO]
   - Nenhuma checagem compara `KEY_DURATION` com o último sample (Android) ou
     com a outra trilha (iOS). O clipe entra com o comprimento que o arquivo declara.
5. **Ajustes do projeto aceitam até 24 h** [PROVADO]
   - Teclado de duração até 86400 s (`ProjectSettingsSheet.kt:198`). Sem aviso.
6. **Descartados**
   - Velocidade: a UI permite no mínimo 0,1x, ou seja, 10x de comprimento, e isso
     não se acumula (`Engine.cpp:16577-16583`).
   - Composições aninhadas: só herdam (`Engine.cpp:6612`, `EngineLayerAnim.cpp:692`).
   - Loop: só afeta a reprodução.
   - `retime`.
   - Migração de projeto.
   - Ao abrir um projeto não se validam ends nem duração, mas também nada é
     convertido.

### A.4 O número em si [INFERIDO]

- 522520 = 40 × 13063 (primo). Não é múltiplo de 24, 25, 30 ou 60.
- Logo, não é um valor redondo digitado (como "4 h"). Parece o resultado de um
  arraste ou de um arredondamento para cima.
- 522520 ms = 8,7 min é coincidência: nenhum caminho trata ms como quadros.

### A.5 Para comprovar [NÃO COMPROVADO]

Pedir ao usuário um destes:
- o `.aurea` do projeto, para ler `duration` e os `end` das camadas;
- o "Relatar um problema";
- linhas do logcat com `video importado:` (que registra o µs da mídia),
  `export-ffmpeg frame=0` e `export start failed`.

---

## B. Por que fica parado no quadro 0

### B.1 Antes de `start_export` retornar (tela "Preparando" com spinner)

Nada nesta fase é proporcional ao total de quadros. [PROVADO]

| Etapa | Cresce com | Prazo | Onde |
|---|---|---|---|
| Limpar temporários e esperar a GPU da prévia | — | 45 s (`kExportWorkerHangNs`) ou `exportGpuTimeoutMs` | `Engine.cpp:13842-13913` |
| Fechar decoders da prévia | nº de decoders | `workerHangNs` | `Engine.cpp:13912` |
| Serializar o projeto (com `modelMutex_`) | tamanho do projeto | nenhum | `Engine.cpp:13311-13335` |
| `capture_snapshot` + `exportAssetPathResolver` | **soma dos bytes de todos os `content://` do projeto** | 30 s sem progresso ou 120 s no total, **por arquivo** | `ExportRecovery.cpp:227-283`, `ExportAssetSource.hpp:20-97`, `aurea_jni.cpp:468` |
| `scheduler.configure` e `queue.prepare` | O(1); só guardam `frames` | — | `RenderScheduler.cpp:11-34`, `OfflineFrameQueue.cpp:7-15` |
| `OfflineAudioSnapshot::create` | nº de camadas/aninhamento | cancelável | `OfflineAudioSnapshot.cpp:216-235` |
| Conferir espaço em disco | **usa `frames`** | — | `Engine.cpp:13468-13484` |
| Abrir o encoder (FFmpeg) | — | 45 s sem batida ou 120 s no total | `Engine.cpp:13528`; `ExportSinkStartup.cpp:310-388` |
| Texturas e buffers de leitura da GPU | resolução | — | `Engine.cpp:13581-13715` |

- Com 522520 quadros em 1080p30 a cerca de 8 Mbps, a estimativa é ~18 GB.
- O motor só recusa se houver menos de ~9 GB livres. Nesse caso sai
  `StorageFull` (28), "espaço livre insuficiente".
- Com mais espaço, segue.

### B.2 Depois que o worker começa (o caso do print)

`framesDone` só sobe depois que o encoder grava o quadro 0 **e** o áudio até o
quadro 1 (`Engine.cpp:14560-14590`).

1. **Produtor** — `render_export_frame` → `OfflineFrameRenderer::render`
   (`engine/src/export/OfflineFrameRenderer.cpp:69-121`).
   - Repete até ter o quadro decodificado exato.
   - Falha com `Timeout` ou `DecodeFailed` após 30 s sem progresso ou 120 s no
     total (`fenceTimeoutNs_`, `:16`, `:115`).
   - O "progresso" inclui compilação de shader e upload. O renderer offline é
     novo, então o 1º quadro recompila pipelines; em Mali/PowerVR isso pode levar
     dezenas de segundos. [INFERIDO]
2. **Não há watchdog do produtor no motor.**
   - `export_worker_hung` só vigia o worker do encoder enquanto ele está
     ocupado. A regra diz que esperar quadro na fila "não é travar"
     (`engine/include/aurea/export/ExportWatchdog.hpp:157-162`). [PROVADO]
3. **Encoder**
   - `write_video` do quadro 0 entra em período de teste.
   - O 1º pacote tem 5 s por degrau da escada (Surface → ByteBuffer → legado →
     software). Fila cheia: 5 s na partida e 30 s depois.
     (`engine/src/export/FFmpegExportSink.cpp:74-79,1205-1218`).
4. **Áudio do quadro 0**
   - `write_export_audio` → mixar → decodificar o bloco 0 de cada clipe.
   - A fase AudioMix tem limite de 45 s (`ExportWatchdog.hpp:144`).
5. **Tela**
   - Android: 180 s sem mudar `framesDone` nem `message` → cancela; 15 s depois →
     falha "travou" (`ExportStallWatchdog.kt:51-53`, `Exporter.kt:386-406`).
   - A `message` do motor fica fixa em "exportando" durante o quadro 0, então a
     tela não distingue lentidão de travamento.
   - iOS: mesma regra (`AureaModel.swift:4131-4213`), mas **não roda durante
     "preparando"** e o Cancelar fica desabilitado (`ExportView.swift:426`).
6. **Chamadas nativas sem prazo**
   - `MediaCodec` e `AMediaExtractor` dentro do decoder, no lado do produtor.
   - No iOS: `avcodec_open2` → VideoToolbox, e `copyNextSampleBuffer`.
   - Se uma delas não voltar e ignorar o cancelamento, a tela desiste, mas o
     contexto continua "running". O próximo export recebe `InvalidState` (5),
     "já existe um export em andamento" (`Engine.cpp:13245`). [INFERIDO]

**Conclusão B:**
- Ficar no quadro 0 é um problema do **primeiro quadro** (decoder, shader ou
  partida do encoder), não do tamanho.
- O desenho do código limita a espera a cerca de 2–3,25 min, seguida de falha.
  [PROVADO, para os prazos]

O total gigante pesa depois do quadro 0:
- horas de render;
- cerca de 18 GB de saída;
- `+faststart` regrava o arquivo inteiro no fim (`FFmpegExportSink.cpp:669`);
- o "0%" fica parado por minutos: 1% equivale a 5225 quadros (`ExportScreen.kt:630`).

---

## C. Causas comuns a todos os aparelhos (em ordem) e plano de correção

### C1. O snapshot exige todos os assets do projeto; `content://` morto vira erro 18 [PROVADO]

**Evidência:**
- `capture_snapshot` percorre `for_each_asset` (todos), não só os alcançáveis a
  partir da composição exportada (`ExportRecovery.cpp:240-268`).
- O resolvedor do Android abre cada `content://`. Se falhar, devolve
  `MediaSourceMissing`, o erro 18 (`ExportAssetSource.hpp:26-27`).
- O motor retorna `sourceStatus` antes de qualquer outra coisa (`Engine.cpp:13353`).
- Assets nunca saem do projeto: `Project::remove_asset` só é usado no rollback de
  PSD (`EnginePsd.cpp:140`).
- "Substituir mídia" faz `add_asset` novo (`EngineMedia.cpp:343-360`, `:418-425`) e
  o antigo continua lá.
- Imagens têm os pixels na memória (`frozenImages`), mas mesmo assim o caminho
  delas passa pelo resolvedor.
- Juntando tudo: um projeto com uma URI morta (permissão só da sessão, antes da
  0.2.3), mesmo num clipe apagado ou já substituído, **nunca mais exporta**.
- A nota da 0.2.3 ("precisam reimportar a mídia") não resolve: o asset antigo
  continua no projeto.

**Correção:**
1. Motor (`ExportRecovery.cpp`, `Engine.cpp:13341`): calcular o conjunto de
   assets alcançáveis a partir da composição exportada.
   - Incluir camadas, aninhadas recursivamente, áudio, fontes de texto,
     dependências 3D/HDRI/LUT e referências de efeito (`LayerReference`).
   - Passar só esses por `add_path`/resolvedor.
   - Assets não usados ficam fora da cópia congelada, ou com o caminho intacto e
     sem resolução.
2. Imagens que já estão em `frozenImages` não precisam do resolvedor. Pular quando
   os pixels estão retidos.
3. Coletar assets órfãos ao salvar, respeitando o histórico de desfazer. Uma opção
   é contar referências também nos snapshots do `history_`.
4. "Substituir mídia" deve oferecer **revincular**: trocar o `sourcePath` do asset
   existente para todas as camadas que o usam. Ao abrir o projeto, listar as
   mídias ausentes (`lastLoadMissing_`) com ação de revincular, nas duas UIs.
5. **Testes** (motor, `engine/tests/test_export.cpp`):
   - Projeto com um asset não usado de caminho inexistente e resolvedor que
     falha nele → o export inicia.
   - O resolvedor é chamado só para os assets usados.
   - Depois de substituir a mídia, o export não pede a fonte antiga.
6. **Paridade:** a mudança no motor vale para os dois.
   - O iOS não tem resolvedor, então asset não usado ausente não bloqueia hoje.
     Asset usado ausente falha no render.
   - A UI de revincular precisa ser feita no Android (`EditorStore.kt`) e no iOS
     (`AureaModel.swift`, `EditorView.swift`).

### C2. Duração sem teto, que gruda e é herdada → export de horas (o print) [código PROVADO, execução INFERIDA]

**Evidência:** seção A.3.

**Correção:**
1. Timeline (Android `TimelineController.kt:204,1392-1402`; iOS
   `TimelineView.swift:1946`): na auto-rolagem durante arraste ou aparo, limitar a
   vista a `max(navigationEnd no início do gesto, fim da duração) + margem fixa`
   (por exemplo 10 s). Não usar o `navigationEnd` vivo.
2. Motor: camadas criadas no playhead terminam em `max(t+1, navigation_end_locked)`
   em vez de `comp->duration()`. Pontos: `Engine.cpp:2433`, `2683`, `2849`, `4717`,
   `8708`, `8772`, `9132`, `10050` e `EngineVector.cpp:88`, `536`, `575`.
3. Motor: um teto de sanidade no export, por exemplo
   `kMaxExportSeconds = 3 h`, configurável.
   - Em `start_export_checked` (`Engine.cpp:13378`) e no export de imagem/PreRender.
   - Erro claro: `NotSupported` com "duração acima de N h".
4. Tela Exportar (`ExportScreen.kt:115`, `ExportView.swift`): mostrar a duração e
   avisar quando ela passar muito do conteúdo de mídia ou de 30 min. Oferecer
   "Aparar projeto ao conteúdo" (`Engine.cpp:8382`).
5. Opcional: duração "automática" que acompanha o conteúdo até o usuário fixá-la em
   Ajustes.
6. **Testes:**
   - Motor: inflar a duração com `LayerSetTimeRange` e voltar; adicionar texto →
     o `export_duration(true)` não inclui a cauda.
   - Motor: `start_export` com 4 h → recusa com a mensagem.
   - JVM (Android): auto-rolagem num arraste segurado não passa da margem.
   - XCTest equivalente no iOS.

### C3. Todo export copia por inteiro todos os `content://`; `export-sources` nunca é limpo [PROVADO]

**Evidência:**
- `ExportAssetSource.hpp:41-96`: copia para um temporário, compara byte a byte com
  a cópia anterior e só então reaproveita. É I/O total a cada export, com limite de
  120 s por arquivo e exigência de espaço livre ≥ tamanho + 8 MB (`:34-38`).
- Mídia na nuvem (Google Fotos) baixa pelo provedor → `Timeout` (26).
- A pasta `export-sources` não é apagada em lugar nenhum (único uso em `:31`), então
  cresce até dar `StorageFull` (28).
- A recuperação de export está desligada (`Engine.hpp:226`; nenhuma plataforma
  liga), então a cópia **não é necessária**.
- O decoder já abre `content://` por fd (`MediaCodecSource.cpp`, `open_source`
  com `opener`).

**Correção:**
1. Com `enableExportRecovery == false`, o resolvedor do Android deve só validar que
   a URI abre (`opener`) e devolver a própria URI, sem copiar.
2. Se a cópia continuar, reaproveitar pela chave (URI, tamanho, mtime), sem reler, e
   limpar `export-sources` por LRU ou depois de um export concluído.
3. **Teste:** host com `opener` falso contando leituras; e um instrumentado com mídia
   do seletor.

### C4. PreRender e export compartilham `exportCtx_` [PROVADO]

**Evidência:**
- O PreRender usa `start_image_export_internal` com `trimToContent = false`
  (`Engine.cpp:14800-14817`) e publica o mesmo `exportCtx_` (`:15104-15123`).
- Com o PreRender rodando, `start_export` devolve `InvalidState` (5)
  (`Engine.cpp:13238-13246`).
- Nenhuma UI cancela o PreRender antes de exportar (`Exporter.kt:225-246`,
  `AureaModel.swift:3948-3960`).
- O PreRender ignora o trim: com duração inflada, ele tenta 522520 quadros.

**Correção:**
1. Em `start_export_checked`, se `exportCtx_->preRenderMode` estiver ativo:
   `invalidate_prerender()` + `await_cancelled_prerender_worker()` (já existem,
   `Engine.cpp:14732-14790`) antes de recusar.
2. O PreRender passa a usar `navigation_end`, não a duração.
3. **Teste no motor:** PreRender ativo → `start_export` inicia.

### C5. Quadro 0 sem estágio visível nem watchdog do produtor [PROVADO]

**Evidência:** seção B.2. A `message` fica fixa em "exportando". A UI mostra
"0 fps · 0:00 left" sem dizer o que está esperando.

**Correção:**
1. O produtor publica a fase em `progress.message` ou nas flags: "decodificando
   fonte X", "compilando shaders", "aguardando encoder (degrau N)". Isso alimenta
   o watchdog da UI e o diagnóstico.
2. A UI troca a linha de quadros por "Preparando o primeiro quadro…" enquanto
   `framesDone == 0`, nas duas plataformas.
3. Opcional: um watchdog do produtor no motor, que registra a fase em que travou.
4. iOS: o watchdog também deve rodar durante "preparando", e o Cancelar precisa
   ficar ativo.
5. **Teste no motor:** decoder falso que nunca entrega → falha `DecodeFailed`
   dentro do prazo, com a fase na mensagem.

### C6. Duração de contêiner sem checagem [INFERIDO]

**Correção:**
1. Na importação, comparar `KEY_DURATION` com o último sample (seek ao fim) no
   Android.
2. No iOS, usar o `timeRange` da trilha de áudio (`IOSVideoDecoder.mm:674,823`).
3. **Teste:** fixtures com duração falsa.

### Ordem sugerida para a próxima sessão

C1 → C4 → C2 (teto no motor + UI) → C5 → C3 → C6.

C1 e C4 são mudanças pequenas no motor, com testes de host, e cobrem os relatos de
erro 18 e de "não começa".

### Paridade

- Correções no motor (C1, C2 motor, C4, C5 motor) valem para Android e iOS.
- Mudanças de UI precisam ir nas duas, com textos e acessibilidade:
  - auto-rolagem;
  - aviso de duração;
  - revincular mídia;
  - estágio do 1º quadro;
  - Cancelar no iOS.
- C3 é só Android, porque o iOS copia a mídia para `Documents/Media` na importação.

### Estado da validação

Só análise estática:
- nada foi compilado;
- nenhum teste novo;
- nenhuma reprodução em aparelho.

A causa exata do print depende do projeto do usuário (A.5).
