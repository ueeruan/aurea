# Fase 8 — Relatório de performance e estabilidade

(Cada frente escreve só a própria seção. Números reais, medidos; o que não foi
medido está dito.)

## 8B — Memória, caches, pressão do sistema, jobs e vazamentos

**Onde foi medido.** Host Windows 11 Pro, AMD Ryzen 5 5500 (6 núcleos / 12
threads), 31,9 GB, RTX 3050 (Vulkan), build Release do motor. Sem aparelho
Android físico: estes números são do host e servem para comparar ANTES ×
DEPOIS com o mesmo código de medida — não são números de celular. Nada aqui
depende de resolução de preview, FPS de tela ou tempo de GPU de celular; onde
há resolução, ela é dita.

**Como medir de novo.** `engine/tests/test_memory.cpp`, suíte `Perf8B`
(`aurea_tests.exe Perf8B`). O mesmo arquivo compila contra o motor de antes da
fase (as verificações novas ficam atrás de `AUREA_MEMORY_API_8B`): o "antes"
abaixo é o commit `be24f9c` com este arquivo de teste, o "depois" é esta
frente. Contratos: suítes `Memory8B` e `Jobs8B`.

### 1. Antes × depois (medido)

| Medida | Antes | Depois |
|---|---|---|
| Pool de tarefas PARADO (4 workers), CPU em 1 s | **4156 ms (402% de um núcleo)** | 31 ms (3%) |
| Motor inteiro parado (headless, workers do aparelho = 1 no host), CPU em 1 s | 969 ms (94%) | 31 ms (3%) |
| Espera de uma tarefa HIGH com 12 LOW de 30 ms ocupando o pool — p50 / p95 / máx | 10,08 / 26,13 / 29,18 ms | 0,01 / 0,02 / 0,02 ms |
| Tarefa LOW sob rajada HIGH contínua de 400 ms — começou após | 401,6 ms (só quando a rajada acabou) | 0,5 ms |
| Reverso tocando (30 fps, GOP 30, 4 ms/quadro) — quadro exato no prazo / seeks / decodes por quadro | 27/60 (45,0%) / 32 / 6,92 | 41/60 (68,3%) / 13 / 4,43 |
| Scrub para trás — no prazo / seeks / decodes por quadro | 25/60 (41,7%) / 32 / 5,75 | 40/60 (66,7%) / 12 / 2,92 |
| Scrub vai-e-volta numa região — no prazo / seeks | 43/60 (71,7%) / 10 | 50/60 (83,3%) / 7 |
| Playback para a frente — no prazo / seeks | 59/60 (98,3%) / 1 | 59/60 (98,3%) / 1 (sem regressão) |
| Ciclo longo (12 × abrir → editar → render → export → salvar → reabrir → fechar), crescimento do ciclo 4 ao 12 | privada +0,3 MB; threads, handles, texturas e buffers de GPU +0 | privada −0,1 MB; threads, handles, texturas e buffers de GPU +0 |

**O achado grande (§38 bateria, §31–34):** todo worker do JobSystem girava em
`std::this_thread::yield()` quando não tinha trabalho. Com o app parado, cada
worker queimava um núcleo inteiro — no host, 4 workers = 402% de CPU sem fazer
nada; no motor com os workers que o aparelho recomenda, 94% de um núcleo o
tempo todo. Num celular isso é bateria e temperatura jogadas fora desde a
abertura do app. Agora o worker gira 64 vezes e dorme numa variável de
condição (sem janela de despertar perdido: a contagem de pendentes é feita
antes de ler quem dorme, dos dois lados em seq_cst).

### 2. Orçamentos (§12)

O teto total é `DeviceCapabilities::memory_budget_bytes()` (¼ da memória
disponível medida; 384 MB sem medição). A divisão é UMA tabela
(`kBudgetShare`, `engine/include/aurea/memory/MemoryManager.hpp`), aplicada em
`Engine::apply_memory_budgets` na subida e de novo quando a GPU real é medida.

| Categoria (`MemoryClass`) | ‰ | Em 384 MB | Em 1 GB | Quem consome | Aplicado? |
|---|---|---|---|---|---|
| Thumbnails | 15 | 5,8 MB | 15 MB | `ThumbnailService` (bytes, LRU) | **sim** — despeja acima do teto |
| Waveforms | 10 | 3,8 MB | 10 MB | `audio::WaveformCache` (bytes, LRU pela consulta) | **sim** |
| Proxies | 80 | 31 MB | 80 MB | — | reservado: o app não gera proxy hoje |
| DecodedFrames | 265 | 102 MB | 265 MB | `DecodedFrameCache` de TODAS as fontes, orçamento compartilhado | **sim** — quem insere acima do teto despeja os próprios piores (nunca o último quadro) |
| RenderedFrames | 140 | 54 MB | 140 MB | cache de render do `Renderer` (flow, máscara, planos, LUT, malha vetorial, pool transitório) | trim sim; teto não (dono: 8C/8E) |
| GpuTextures | 200 | 77 MB | 200 MB | imagens e texturas 3D | declarado; medido no backend (`GpuMemoryStats`) |
| GpuGeometry | 100 | 38 MB | 100 MB | malhas 3D, partículas, vetor | declarado |
| Audio | 40 | 15 MB | 40 MB | `AudioBlockCache` (já era LRU por esse teto) | **sim** |
| Assets | 60 | 23 MB | 60 MB | fontes, atlas de glifos, shaders | declarado |
| Export | 70 | 27 MB | 70 MB | readback/fila do export | declarado (dono: 8F) |
| Persistent | 20 | 7,7 MB | 20 MB | projeto aberto | nunca recusado, nunca despejado |

"Declarado" = o número existe e é lido por quem quiser (`memory().budget()`),
mas o consumidor ainda não recusa acima dele; esses consumidores são das
frentes 8C/8E/8F. A telemetria soma o que é contado (`total_used`,
`pressure`).

**Cache que só ocupava RAM (reavaliado com número):** as miniaturas têm dois
caches em série — os bitmaps da UI (Kotlin) na frente e o do motor atrás.
Rolando a timeline de 60 s ida e volta e trocando o zoom (2784 pedidos da UI),
o cache da UI acertou 90,9% e o do motor **0%**: ele só faz a ponte entre a
thread de decode e a UI buscar. O teto dele caiu de "900 miniaturas por
contagem" (sem limite em bytes) para 1,5% do orçamento em bytes, e os 2,5% que
sobraram foram para os quadros decodificados.

### 3. Todo cache com orçamento, LRU, versão, invalidação e métricas (§14–15)

| Cache | Orçamento | Despejo | Versão / invalidação | Métricas |
|---|---|---|---|---|
| `DecodedFrameCache` | bytes da categoria (compartilhado) + nº de buffers do decoder | temporal (distância ao playhead × direção) | `version` muda no `clear` (seek em decoder novo, suspensão) | bytes, quadros, acertos, erros, despejos |
| `ThumbnailService` | bytes (era contagem) | LRU (a imagem também sobe no LRU agora; antes só o vídeo) | chave leva hash do caminho da mídia (relink invalida sozinho); miniatura decodificada para um projeto já fechado não entra no cache novo | idem |
| `WaveformCache` | bytes (era ilimitado) | LRU pela última consulta; nunca a que está sendo calculada nem a consultada nos últimos 2 s | relink recalcula; troca de projeto limpa (antes o mapa só crescia entre projetos) | idem |
| Cache de miniaturas da UI (Kotlin) | bytes: 1/16 do heap, 8–24 MB (era 400 bitmaps) | LRU | — | `bytes()` na tela Armazenamento |

`MemoryManager::collect_metrics` junta tudo; `EngineTelemetry.frameCacheHitRate`
e `frameCacheEntries` (que ficavam em 0) agora são a soma real das fontes.

Correção junto: estourar o orçamento de UMA categoria pedia despejo a TODAS
(`try_reserve` → `request_reclaim` geral): soltava miniatura para "abrir
espaço" num contador de quadros que não mudava. Agora pede só à própria
categoria (teste `Memory8B.ReserveReclaimsOnlyItsOwnClass`).

### 4. Cache de quadros por modo (§16)

- **Para a frente (playback):** já existia (atual + 3–4 adiante). Sem mudança.
- **Para trás (reverso tocando e scrub descendo):** antes, o prefetch do
  playback olhava sempre para a FRENTE, mesmo tocando ao contrário — cada
  quadro custava um seek + GOP inteiro e ainda decodificava 3 quadros que já
  tinham passado. Agora o seek até o alvo, que já decodifica os anteriores de
  qualquer jeito, entrega os últimos N (até 6; 5 com os 12 buffers do
  MediaCodec) ao cache; no reverso tocando, a janela é reabastecida quando
  sobra menos de um quadro atrás. Seeks caíram 32 → 13 (reverso) e 32 → 12
  (scrub para trás). Limite honesto: com decode zero-copy o cache não passa de
  7 quadros (buffers do ImageReader), então reverso a 30 fps com GOP de 1 s
  ainda perde quadro (68% no prazo no teste).
- **Região do scrub:** a política temporal já mantinha os dois lados; com a
  janela para trás o vai-e-volta subiu de 71,7% para 83,3% no prazo.
- **Time remap:** a camada com remap mandava direção 0 e o decoder pré-carregava
  3 quadros PARA A FRENTE mesmo com a curva descendo ou parada. Agora a
  direção do decoder é a da curva no próximo quadro (subindo: prefetch à
  frente; descendo: janela para trás; parada: só o alvo).

### 5. Pressão de memória do sistema (§13)

`MainActivity.onTrimMemory(level)` → `EditorStore.onTrimMemory` → JNI
`nativeTrimMemory` → `Engine::trim_memory(level)`. O nível do Android vira um
estágio (`trim_stage_for_os_level`) e o motor roda os estágios em ordem:
miniaturas fora da tela → waveform antiga → quadros sem uso (fica o do
playhead) → cache de render que não entrou no último quadro (+ pool
transitório) → mips altos → assets 3D sem uso → temporários (fontes de vídeo
ociosas: decoder, thread e buffers). A UI solta os bitmaps dela (miniaturas e
prévias de efeito) a partir de RUNNING_LOW/UI_HIDDEN. Projeto, alterações não
salvas, histórico e timeline nunca entram (`Persistent` não é registrável).

Simulação de cada nível no host (`Perf8B.OsTrimLevelsReleaseMemoryAndKeepTheProject`):
projeto 1280×720, 2 camadas de vídeo sintético + texto, aquecido antes de cada
nível (render de 15 quadros, miniaturas e waveform consultadas e depois 2,1 s
fora da tela). KB liberados:

| Nível | Estágio até | Miniaturas | Waveform | Quadros | Cache de render (GPU, medido no backend) | Total | Motor antes → depois |
|---|---|---|---|---|---|---|---|
| 5 RUNNING_MODERATE | waveform antiga | 136,6 | 2,0 | 0 | 0 | 138,5 | 18,59 → 18,46 MB |
| 10 RUNNING_LOW | quadros sem uso | 136,6 | 2,0 | 16 200,0 | 0 | 16 338,5 | 18,59 → 2,64 MB |
| 15 RUNNING_CRITICAL | cache de render | 136,6 | 2,0 | 16 200,0 | 18 464,0 | 34 802,5 | 18,59 → 2,64 MB |
| 20 UI_HIDDEN | quadros sem uso | 136,6 | 2,0 | 16 200,0 | 0 | 16 338,5 | 18,59 → 2,64 MB |
| 40 BACKGROUND | cache de render | 136,6 | 2,0 | 16 200,0 | 18 464,0 | 34 802,5 | 18,59 → 2,64 MB |
| 60 MODERATE | assets 3D sem uso | 136,6 | 2,0 | 16 200,0 | 18 464,0 | 34 802,5 | 18,59 → 2,64 MB |
| 80 COMPLETE | temporários | 136,6 | 2,0 | 16 200,0 | 18 464,0 | 34 802,5 | 18,59 → 2,64 MB |

Em todos: número de camadas e profundidade do histórico iguais antes e
depois, e o quadro seguinte renderiza. Neste projeto não há 3D (estágio 6 = 0)
e as duas fontes de vídeo estavam no último quadro (estágio 7 = 0). **Mips
altos (estágio 5) não liberam nada hoje:** o motor não tem streaming de mips
(é item da 8E); o estágio existe na ordem e fica vazio até lá. O trim do
cache de render espera a GPU (`wait_idle`) — é um aviso do sistema, fora do
caminho quente; durante um export ele pula a GPU (o export é dono dela).

### 6. Jobs (§31–34)

- Cinco prioridades: REALTIME (áudio), HIGH (preview, decode atual, scrub),
  NORMAL (miniaturas/waveform visíveis), LOW (proxy, análise), BACKGROUND.
  `Critical` continua como nome de REALTIME.
- Sem starvation: cada fila de baixo ganha a vez depois de 8–32 tarefas de cima
  passarem na frente dela com trabalho esperando (LOW começou em 0,5 ms sob
  rajada contínua; antes, 401,6 ms).
- LOW/BACKGROUND nunca ocupam todos os workers (automático: ativos − 1), e a
  thread roda a tarefa de fundo em prioridade de fundo do SO: HIGH p95 de
  26,13 ms → 0,02 ms com o pool cheio de trabalho de fundo.
- Workers adaptados: o número inicial continua vindo dos núcleos de
  performance (`recommended_worker_count`, big.LITTLE); a temperatura agora
  governa o pool (`Engine::set_thermal` → `JobSystem::apply_thermal`): morno =
  metade dos workers para fundo; quente = um só para fundo; crítico = metade
  do pool ativo.
- `stop()` junta (join) as threads em vez de destacá-las: a contagem de threads
  do processo volta ao que era (conferido em `Jobs8B`).

Limite honesto: hoje quase nada do motor submete ao JobSystem (miniaturas,
waveform, decode e áudio têm thread própria com prioridade de SO própria). O
pool está pronto e medido; mover esses trabalhos para ele não foi feito nesta
frente.

### 7. Armazenamento (§49–52)

Tela **Ajustes › Armazenamento** (`home/SettingsTab.kt`, `state/CacheStorage.kt`):
tamanho REAL de cada tipo lido do disco quando a seção aparece e depois de
cada limpeza; tocar num tipo limpa só ele; "Limpar tudo" limpa todos; a linha
"Na memória agora" mostra miniaturas / waveform / quadros de vídeo do motor e
solta pelo mesmo caminho do trim do sistema. Tipos: cache de gráficos
(pipeline do Vulkan), prévias de efeitos, exportação temporária, áudio de
legendas, capas de projetos apagados, outros temporários. Projetos, presets,
fontes e modelos importados não aparecem e não são apagáveis por ali.

Limpeza automática ao abrir o app (fora da main thread): tipo acima do teto
perde os arquivos mais antigos (pipeline 64 MB, prévias 64 MB, outros 32 MB;
export e legendas 0 fora de uso = sobra de crash apagada).

Temporários com dono e ciclo de vida (§52):
- export (`cacheDir/export`): sucesso copia para a galeria e apaga; **falha e
  cancelamento agora apagam na hora** (antes o arquivo parcial ficava até o
  próximo export); crash/force kill = apagado na próxima abertura;
- legendas (`cacheDir/legendas`): apagado no `finally` da transcrição; sobra de
  crash, na próxima abertura.

Não existe cache de proxy, de render nem de waveform em DISCO no app hoje —
por isso não há linha para eles (a tela não mostra tipo que não existe).

Não medido: a tela em aparelho/emulador (esta frente não usa emulador); o APK
x86_64 compila.

### 8. Vazamentos (§102–104, §155–157)

`Perf8B.LongSessionMemoryStabilizes`: 12 ciclos de abrir projeto → importar
vídeo (com áudio) → efeito, texto, forma, imagem → 20 quadros renderizados →
scrub → miniaturas e waveform → export de 12 quadros → salvar → reabrir →
renderizar → projeto vazio. Por ciclo: memória privada, RSS, threads, handles,
texturas/buffers vivos e bytes usados no backend Vulkan. Do 4º ao 12º ciclo
(depois do aquecimento de pipelines): privada −0,1 MB (ruído do alocador),
threads 19 → 19, handles 340 → 340, texturas 13 → 13, buffers 5 → 5, GPU
24,5 → 24,5 MB. O "antes" também estabilizava neste roteiro (+0,3 MB).

Corrigidos, por serem crescimento sem teto ou recurso preso (não aparecem no
roteiro acima porque ele reabre o mesmo arquivo):
- `WaveformCache` nunca soltava nada: o mapa crescia a cada asset novo de
  cada projeto aberto na sessão. Agora limpa na troca de projeto e tem teto;
- `ThumbnailService` mantinha até 2 decoders abertos para sempre (o do projeto
  anterior inclusive — decoder de hardware é recurso do sistema). Agora
  devolve depois de 3 s ocioso;
- JobSystem destacava as threads no `stop()`; agora junta.

### 9. Arquivos

Motor: `engine/include/aurea/memory/MemoryManager.hpp`,
`engine/src/memory/MemoryManager.cpp`, `engine/include/aurea/jobs/JobSystem.hpp`,
`engine/src/jobs/JobSystem.cpp`, `engine/include/aurea/media/DecodedFrameCache.hpp`,
`engine/src/media/DecodedFrameCache.cpp`, `engine/include/aurea/media/ThumbnailService.hpp`,
`engine/src/media/ThumbnailService.cpp`, `engine/include/aurea/audio/Audio.hpp`
(WaveformCache), `engine/src/audio/Waveform.cpp`, `engine/tests/test_memory.cpp`.
Acréscimos localizados fora da posse: `VideoSource.hpp/.cpp` (janela para
trás, §16), `MediaManager.hpp/.cpp` (`set_memory`), `Renderer.hpp/.cpp`
(`trim_memory`; direção do time remap), `Engine.hpp/.cpp` (`trim_memory`,
tabela de orçamento, ligar caches, `apply_thermal`, `balance` a cada 120
quadros, telemetria do cache de quadros), `aurea_jni.cpp`
(`nativeTrimMemory`, `nativeMemoryReport`).
App: `MainActivity.kt` (`onTrimMemory`/`onLowMemory`), `state/EditorStore.kt`
(`onTrimMemory`, `clearStorage`, `clearCache`, `ThumbnailCache` por bytes),
`state/CacheStorage.kt` (novo), `home/SettingsTab.kt` (seção Armazenamento),
`state/Exporter.kt` (temporário apagado na falha/cancelamento),
`effects/EffectPreview.kt` (`trimMemory`), `engine/AureaEngine.kt`.
