# Fase 8 — Relatório de performance

Números medidos, nunca "no olho" (SPEC §4, §159). Cada tabela diz onde rodou,
em que resolução e o que o número é. Seção por marco; esta é a **8A (baseline)**.

---

## 8A — Profiling: baseline do host

### Ambiente (o que estes números são e o que NÃO são)

| | |
|---|---|
| Máquina | PC de mesa: AMD Ryzen 5 5500 (6 núcleos / 12 threads), 31,9 GB RAM |
| GPU | NVIDIA GeForce RTX 3050, driver NVIDIA 616.92, Vulkan 1.4.351 |
| SO | Windows 11 (NT 10.0 build 22631) |
| Build | Release, MSVC 19.51 (`aurea_tests.exe`: motor real, backend Vulkan real) |
| Carga do host | **77,5%** da CPU ocupada por OUTROS processos durante a baseline (builds das outras frentes da Fase 8 e o emulador com `-gpu host` usando a mesma GPU). Medido pela própria suíte (`host_other_cpu_load_pct`). Três tentativas de baseline: 39,3%, 79,5% e 77,5% — o PC não ficou quieto; gravada a última (código final). |
| Data | 2026-09-22 18:48, `docs/performance/baseline_host.json` |

Limites, sem fingir:

- **Host ≠ celular.** Uma RTX 3050 de mesa tem ordens de grandeza mais banda e
  ALU que uma GPU móvel; nada aqui vira "X fps no aparelho". Serve para
  comparar builds (regressão) e para achar o estágio caro.
- **Emulador não mede performance real** (SPEC, limites): a GPU dele é a do host.
- **Sem Mac**: iOS/Metal/Instruments não medidos.
- **Quadro offscreen é SERIAL**: `render_offscreen` prepara (CPU), grava,
  submete e ESPERA a GPU. O preview de verdade sobrepõe CPU e GPU e só
  apresenta; "parede" aqui ≈ CPU + GPU, não é o fps do preview.
- **Decode sintético**: o vídeo dos testes é NV12 gerado na CPU
  (`tests/SyntheticVideo.hpp`); o custo NÃO representa MediaCodec/HW. Sai à
  parte (`decode_ms_avg`) e os quadros medidos são desenhados depois de
  decodificados (a 1ª passada espera o decoder, a 2ª é a medida).
- **Export sem encoder**: no host não há MediaCodec; o sink é nulo. Mede-se o
  export de verdade até a entrega do NV12 (render em qualidade final +
  conversão + leitura). O export 4K é **recusado pelo motor** no host (sem
  tabela de codecs o teto fica no conservador) — recusa honesta, não se finge
  encoder; o custo de quadro 4K sai no PERF_4K_*.
- **PC compartilhado**: a GPU é estável entre execuções (PERF_1080_HEAVY 2,97 ms
  nas três baselines; 4K_HEAVY 9,66–9,82), exceto um caso isolado de +50%
  (PERF_1080_BASIC 1,08 ms com 69,9% de carga, 0,71 nas outras quatro). A CPU
  NÃO é: tempos de CPU sobem até +100% com o host cheio (tabela abaixo).

### Como rodar

```
engine/build/host/tests/Release/aurea_tests.exe Perf   # sem AUREA_BENCH: só o teste do verificador
AUREA_BENCH=1        aurea_tests.exe Perf   # mede e imprime
AUREA_BENCH=baseline aurea_tests.exe Perf   # grava docs/performance/baseline_host.json
AUREA_BENCH=compare  aurea_tests.exe Perf   # grava bench_atual.json e FALHA se piorar
AUREA_BENCH_STRICT=1                         # com compare: CPU inconclusiva também falha
```

(Os modelos glTF ficam fora do git em `engine/tests/data/gltf/`; sem eles o
PERF_3D avisa e pula.)

Regressão (§130): falha quando o **p50 de GPU ou de CPU** de um benchmark
passa de **+40%** da baseline **e** a diferença passa de um piso absoluto
(0,25 ms nos quadros e no custo por efeito; 2 ms nos tempos da campanha — abaixo
disso é ruído de medição). No modo compare, um benchmark de quadro acima do
limite é **medido de novo até 2 vezes** (mesma cena, mesmos quadros, nada
desligado) e fica o melhor p50: contenção de outro processo não é regressão do
código. Os tempos dominados pelo decoder sintético (playback/scrub serial,
decode) ficam fora do verificador.

PC compartilhado: se no `compare` o host está mais de 10 pontos mais ocupado
por outros processos do que na baseline, piora **de CPU** sai como
**INCONCLUSIVA** (impressa, não falha — rode de novo com o host quieto); piora
**de GPU** falha sempre; `AUREA_BENCH_STRICT=1` faz tudo falhar. Isto foi
medido, não suposto: o mesmo binário, contra uma baseline de 39,3% de carga,
com o host a 58,4% acusou +46% a +170% em tempos de CPU (criar legendas,
salvar/abrir 1000 camadas, gravação do PERF_TIMELINE) e nenhuma piora de GPU.
Validação final: `compare` contra a baseline gravada (77,5% de carga) com o
host a 69,6%: **0 regressões, 0 inconclusivas** (e antes, contra a de 79,5%
com o host a 74,6%: 0). O teste `Perf.RegressionCheckerReadsBaselineAndFlagsFortyPercent`
prova que +39,5% passa, +42,5% falha, o id é exato (não prefixo), o piso vale
e a carga da baseline é lida.

### O que cada quadro mede

`Engine::render_offscreen` agora cronometra (`Engine::OffscreenMeasure`):
prepare sob o lock do modelo, espera pelos quadros exatos do decoder, gravação
do FrameGraph, submissão, espera da GPU; com `set_offscreen_timers(true)` a
GPU do **quadro exato** por timestamp query (depois do `wait_idle` o backend já
leu as queries deste quadro) e o tempo **por passe**
(`last_offscreen_gpu_passes`); mais draw calls, passes, camadas, efeitos vivos,
draws/triângulos 3D, partículas, memória do alocador de GPU e dos transitórios
do FrameGraph. RSS pelo `GetProcessMemoryInfo`. O export e as capturas
continuam sem timestamp (só o benchmark liga).

### PERF_* (§128) — baseline

Orçamento: 16,67 ms a 60 fps, 33,33 ms a 30 fps. n = quadros medidos. Em ms;
CPU = prepare + gravação + submit; parede = quadro serial.

| Benchmark | Resolução | n | GPU p50 / p95 / p99 (σ) | CPU p50 / p95 (prepare) | Parede p50 / p95 | Passes · draws · camadas · efeitos | RSS · GPU usada · transit. (MB) |
|---|---|---|---|---|---|---|---|
| PERF_1080_BASIC (1 vídeo + título) | 1920×1080 | 120 | 0,72 / 0,72 / 0,72 (0,00) | 0,41 / 0,62 (0,04) | 1,27 / 2,50 | 3 · 5 · 2 · 0 | 190 · 61 · 15 |
| PERF_1080_HEAVY (5 camadas, §98) | 1920×1080 | 120 | 2,97 / 3,11 / 3,57 (0,09) | 0,93 / 1,19 (0,11) | 4,08 / 4,52 | 23 · 28 · 5 · 6 | 237 · 191 · 99 |
| PERF_4K_BASIC | 3840×2160 | 60 | 2,51 / 2,52 / 4,83 (0,32) | 1,15 / 1,59 (0,06) | 3,78 / 4,97 | 3 · 5 · 2 · 0 | 294 · 168 · 16 |
| PERF_4K_HEAVY | 3840×2160 | 60 | 9,68 / 10,30 / 11,19 (0,45) | 2,36 / 2,99 (0,12) | 12,28 / 13,41 | 23 · 28 · 5 · 6 | 538 · 467 · 251 |
| PERF_3D (3 glTF: PBR, sombra, esqueleto) | 1920×1080 | 120 | 1,24 / 1,33 / 1,64 (0,13) | 0,12 / 0,20 (0,03) | 1,47 / 1,72 | 3D: 7 draws, 517 804 triângulos | 299 · 215 · 39 |
| PERF_PARTICLES_10K | 1920×1080 | 60 | 0,07 / 0,07 / 0,07 | 0,06 / 0,07 | 0,23 / 0,26 | 10 000 partículas | 146 · 46 · 15 |
| PERF_PARTICLES_100K | 1920×1080 | 60 | 0,21 / 0,21 / 0,21 | 0,06 / 0,07 | 0,36 / 0,39 | 100 000 | 148 · 46 · 15 |
| PERF_PARTICLES_500K | 1920×1080 | 60 | 0,83 / 0,86 / 0,87 (0,01) | 0,06 / 0,07 | 1,00 / 1,06 | 500 000 | 149 · 46 · 15 |
| PERF_PARTICLES_1000K | 1920×1080 | 60 | 1,65 / 1,67 / 1,69 (0,03) | 0,08 / 0,13 | 1,87 / 1,93 | 1 000 000 | 165 · 46 · 15 |
| PERF_CAPTIONS_2000 (agrupadas, 546 camadas) | 1920×1080 | 40 | 0,04 / 0,05 / 0,05 | 0,14 / 0,18 (0,06) | 0,29 / 0,36 | 3 · 5 · 2 · 0 | 186 · 57 · 0 |
| PERF_CAPTIONS_5000 (agrupadas, 1364 camadas) | 1920×1080 | 40 | 0,04 / 0,05 / 0,05 | 0,16 / 0,19 (0,07) | 0,30 / 0,36 | 3 · 5 · 2 · 0 | 204 · 55 · 1 |
| PERF_CAPTIONS_5000_PALAVRA (4095 camadas) | 1920×1080 | 40 | 0,05 / 0,05 / 0,05 | 0,42 / 0,70 (0,20) | 18,6 / 47,0 ¹ | 2 · 3 · 1 · 0 | 198 · 44 · 0 |
| **PERF_TIMELINE** (1000 camadas vivas, 10 000 keys) | 1920×1080 | 60 | 8,86 / 8,99 / 9,02 (0,08) | **18,75 / 41,57** (1,26) | **28,39 / 56,21** | **1001 · 2001 · 1000 · 0** | 197 · **990 · 800** |
| PERF_TEXT (30 textos + animador) | 1920×1080 | 60 | 0,20 / 0,20 / 0,20 | 0,78 / 0,94 (0,45) | 1,13 / 1,40 | 31 · 61 · 30 · 0 | 212 · 36 · 4 |
| PERF_FLOW_1080 (optical flow, 20%, sem cache) | 1920×1080 | 30 | 1,69 / 1,71 / 1,71 (0,40) | 0,78 / 0,88 | 2,65 / 2,78 | fluxo: LK 0,11 · deforma 0,11 ms | 250 · 111 · 15 |

¹ Parede do quadro inclui a espera pelo quadro de vídeo exato (sleep de 5 ms)
depois de saltos de 1 min na fala; GPU e CPU do quadro são os da coluna.

Passes mais caros (GPU média por quadro, timestamp por passe):

- 1080_HEAVY: blur-h 0,41 · blur-v 0,41 · composição 0,26 · desfoque de movimento 0,25 ms.
- 4K_HEAVY: **desfoque de movimento 2,80** · composição 1,38 · cor do vídeo 0,43 · blur-v 0,23 ms.
- 3D: 3d-pbr 0,89 · 3d-sombra 0,24 ms.
- PARTICLES_1000K: partículas 1,57 ms (linear: ≈0,16 ms por 100k).

PERF_EXPORT (intervalo entre quadros entregues ao sink nulo; qualidade final;
em `cpu_ms` no JSON, `gpu_ms` = 0 porque não é separado por quadro):

| Export | Quadros | Total | Vazão | Intervalo p50 / p95 / p99 (σ) | RSS |
|---|---|---|---|---|---|
| 1080p cena básica | 90 | 1722 ms | 52,3 q/s | 13,37 / **65,63** / 104,09 (20,67) ms | 169 MB |
| 1080p cena pesada | 90 | 1651 ms | 54,5 q/s | 15,10 / **46,35** / 66,35 (11,53) ms | 247 MB |
| 4K cena pesada | — | — | — | recusado no host (sem tabela de codecs) | — |

### Campanha de medição (§2) — o que dá para medir no host

| Medida | Tempo | Nota |
|---|---|---|
| Abertura do motor, 1ª no processo | 261,4 ms | `initialize`: instância/dispositivo Vulkan + pré-aquecimento de todos os pipelines (sem cache de pipeline em disco no teste) |
| Abertura do motor, quente | 79,8 ms | mediana de 3, driver já aquecido |
| Projeto novo 1080p | 0,02 ms | `new_project` |
| Abrir projeto pesado / 1000 camadas | 0,11 / 20,2 ms | `load_project` (mediana de 5 / de 3) |
| Primeiro quadro exato depois de abrir | 57,4 ms | decoders e texturas novos |
| Salvar (= autosave) pesado / 1000 camadas | 11,6 / **16,8 ms** | `save_project` segura o **lock do modelo** o tempo todo |
| Decode sintético 1080p / 4K | 13,4 / 56,6 ms | CPU, não é HW — só referência |
| Playback serial 1080p / 4K (p50) | 18,1 / 58,4 ms | quadro a quadro esperando o decoder (p95 19,9 / 101,8) |
| Scrub até o quadro exato 1080p / 4K (p50) | 13,5 / 59,3 ms | salto aleatório, GOP 30 (p95 18,5 / 91,8) |
| Waveform 60 s pronta | 108,1 ms | 48 kHz até o último balde |
| Waveform: ler 1200 baldes prontos | 0,02 ms | zoom/scroll |
| Miniatura: primeira / 12 prontas | 13,3 / 156,2 ms | 1080p → 90 px |
| Importar glTF: Fox / DamagedHelmet / MetalRoughSpheres | 12,7 / 198,9 / 469,4 ms | parse + texturas, 1 vez |
| Legendas: criar 2000 / 5000 palavras (agrupadas) | 43,6 / 244,7 ms | 546 / 1364 camadas (mediana de 3) |
| Legendas: criar 5000 palavras, 1 camada por palavra | 481,0 ms | 4095 camadas |
| `query_layers` 1000 / 4096 linhas | 0,06 / 0,09 ms | lado do motor; a decodificação Kotlin não entra |
| Edição com desfazer (1000 camadas) | 0,93 ms | `LayerSetOpacity` + snapshot |
| Rastreio de ponto 1080p | 27,2 ms/quadro | `track_point`, 150 quadros em 4,1 s |
| Rastreio de câmera 1080p (rápido) | 56,6 ms/quadro | 300 quadros em 17,0 s (17,7 q/s), erro 0,25 px |

Sensibilidade à carga (mesmo código do motor; a CPU sofre, a GPU não):

| Medida | Host 39,3% ocupado | Host 77,5% ocupado |
|---|---|---|
| PERF_TIMELINE CPU p50 | 10,05 ms | 18,75 ms |
| PERF_1080_HEAVY GPU p50 | 2,97 ms | 2,97 ms |
| PERF_4K_HEAVY GPU p50 | 9,82 ms | 9,68 ms |
| Salvar 1000 camadas | 7,85 ms | 16,81 ms |
| Criar 5000 legendas por palavra | 301,5 ms | 481,0 ms |
| Rastreio de câmera | 39,0 ms/quadro | 56,6 ms/quadro |
| Abertura do motor, quente | 57,4 ms | 79,8 ms |

### Custo de GPU por efeito (§86)

Placa 1080p RGBA8, cada efeito com os **valores de demonstração** (os da
prévia do catálogo). Custo = GPU p50 com o efeito − GPU p50 da mesma placa sem
efeito, medida logo antes (15 quadros cada; placa sozinha: 0,178 ms, 2 passes).
"Mudou" = fração de pixels alterados numa captura de 192 px — efeito que some
sem erro apareceria como custo zero enganoso.

| Efeito | GPU (ms) | Passes | Mudou | | Efeito | GPU (ms) | Passes | Mudou |
|---|---|---|---|---|---|---|---|---|
| Pixel sort | +1,921 | 1 | 33% | | Níveis | +0,130 | 1 | 100% |
| Minimax | +1,323 | 1 | 87% | | Dissolver em ondas | +0,118 | 1 | 98% |
| Raios | +1,239 | 1 | 75% | | Varredura de luz | +0,110 | 1 | 30% |
| Deep Glow | +0,837 | 8 | 54% | | Curvas | +0,107 | 1 | 99% |
| Desfoque gaussiano | +0,759 | 2 | 58% | | Saturação / Exposição | +0,105 | 1 | 100% |
| Dano JPEG | +0,449 | 1 | 62% | | Brilho/contraste / Tingir / Matriz | +0,104 | 1 | 100% |
| Lens Blur | +0,428 | 1 | 64% | | **Colorama** | +0,102 | 1 | 100% |
| VHS | +0,424 | 1 | 100% | | Glitch cruzado | +0,101 | 1 | 51% |
| Eco e rastro | +0,377 | 1 | 100% | | **Grão** | +0,100 | 1 | 96% |
| Uni VHS | +0,290 | 1 | 99% | | Motion Tile | +0,091 | 1 | 96% |
| Turbulência | +0,283 | 1 | 37% | | Chave de croma | +0,089 | 1 | 31% |
| Meio-tom | +0,261 | 1 | 100% | | Wave Warp | +0,082 | 1 | 13% |
| Unsharp | +0,253 | 3 | 11% | | Glitchify | +0,081 | 1 | 38% |
| Sinal | +0,249 | 1 | 76% | | **Varredura** / Tremor | +0,081 | 1 | 100% / 19% |
| Glow | +0,241 | 5 | 73% | | Chave de luma | +0,080 | 1 | 41% |
| Nitidez | +0,188 | 1 | 11% | | Inverter | +0,079 | 1 | 100% |
| Deformar | +0,185 | 1 | 7% | | Transform | −0,036 | 0 | 91% (entra na matriz da composição) |
| Holomatrix | +0,183 | 1 | 100% | | Controles de expressão (5), Posterizar tempo, RGB no tempo | ≈0 | 0 | 0% (controle não desenha; temporal numa imagem parada não tem outro instante) |
| Dano de filme | +0,133 | 1 | 81% | | | | | |

Todos os 47 do registro medidos (lista completa em `baseline_host.json`,
`effects_gpu_1080`). Os efeitos por pixel NÃO fundidos custam ~0,1 ms cada em
1080p nesta GPU (um passe de tela cheia); os caros são os de vizinhança larga
(pixel sort, minimax, raios) e os de muitos passes (Deep Glow 8, Glow 5).

### Achados (gargalos reais, medidos) — entrada das próximas frentes

1. **P1, corrigido nesta frente — Grão, Varredura e Colorama não desenhavam no
   projeto.** Eram `EffectClass::PerPixel` sem `color_op()`; o EffectGraph tira
   do plano, como identidade, o efeito por pixel que não produz ColorOp. A
   prévia do catálogo (que chama o `build` direto) mostrava o efeito; aplicado
   na camada (preview e export), **0% dos pixels mudavam** (medido na primeira
   execução do custo por efeito). Classe corrigida para `Neighborhood` (passe
   próprio) e novo teste `Gpu.EveryCatalogEffectChangesTheProjectFrame`: todo
   efeito do catálogo com os valores de demonstração tem de mudar o quadro do
   projeto. Depois: +1 passe, 96–100% dos pixels mudam, ~0,1 ms em 1080p.
2. **Timeline grande (8C/8B): 1000 camadas = 1001 passes e 800 MB de
   transitórios.** Cada forma vira um passe e uma textura própria; a gravação
   do FrameGraph custa **10,1 ms de CPU com o host a 39%** (≈10 µs por passe;
   18,8 ms com o host cheio) e o quadro serial estoura o orçamento de 60 fps.
   A GPU usada vai a **990 MB** (800 MB de transitórios físicos) — num celular
   é OOM. Candidatos: camada simples desenhada direto na composição (sem
   textura própria), lote de formas, aliasing de transitórios entre camadas.
3. **Export com picos (8F):** p50 13–15 ms, mas **p95 46–66 ms**. O laço é
   serial (espera o decoder com `sleep(5 ms)` em volta do prepare → render →
   leitura → sink), sem decode N+1 / render N / encode N−1 sobrepostos (§92).
4. **Desfoque de movimento em 4K (8C):** o passe mais caro da cena pesada em
   4K (2,80 de 9,68 ms de GPU), com 8 amostras no preview.
5. **Autosave segura o lock do modelo (8G):** o autosave do app é
   `save_project` na thread de IO com o `modelMutex_` preso durante
   serialização E escrita: 7,9–16,8 ms com 1000 camadas — o render thread
   espera esse tempo. §53 pede autosave sem micro-freeze.
6. **Espera por decode em passos de 5 ms (8C/8F):** `render_offscreen` (e o
   export) repetem o prepare com `sleep(5 ms)` até ter o quadro exato; o
   playback serial e o scrub exato ficam quantizados por isso.
7. **Legendas grandes (8D):** criar 5000 palavras custa 0,24 s agrupadas (1364
   camadas) e 0,48 s por palavra (4095 camadas). Desenhar é barato (só as
   visíveis entram: 0,16–0,42 ms de CPU). A timeline Kotlin com 4096 linhas é
   o que falta medir no aparelho.
8. **Timestamps por passe limitados a 64 por quadro** (`kMaxTimers` do backend
   Vulkan): acima disso o total do quadro continua certo, mas o detalhamento
   por passe só cobre os 64 primeiros (visto no PERF_TIMELINE).
9. Partículas escalam linear e barato na GPU do host (1M = 1,57 ms); 3D com
   meio milhão de triângulos e sombra = 1,24 ms. No host não são gargalo —
   precisam do aparelho para dizer algo (8E).

### HUD de desenvolvimento (§3)

"Diagnóstico na tela" (Editor › menu) mostra, só o que foi medido:

- FPS do preview e da UI; **quadros lentos da UI** (> 1,5 vsync no
  Choreographer) e o pior intervalo do último segundo;
- **ritmo** (§145): intervalo entre quadros apresentados TOCANDO, p50/p95/p99
  e desvio padrão do último segundo (anel fixo de 128 na thread de render, sem
  alocação; só aparece depois de tocar);
- CPU do quadro com **prepare e gravação** separados; GPU total e por etapa
  (cor, efeitos, blur, glow, composição, saída) — "—" quando o aparelho não
  tem timestamp; present e acquire; orçamento;
- quadros perdidos, seek, escala de render (AUTO), resolução do preview,
  **estado térmico por nome** e a fração das operações caras sob calor;
- cache de decode (quadros, MB) e **taxa de acerto** dos caches de optical
  flow e de máscara (só quando houve uso);
- RAM do motor **sobre o orçamento**, heap nativo e Java do processo, memória
  livre do sistema e o aviso de memória baixa (`ActivityManager.MemoryInfo`,
  1×/s);
- GPU usada, **reservada**, nº de alocações, transitórios;
- passes, **draw calls**, camadas e **efeitos vivos**; 3D (draws, triângulos,
  primitivas fora do frustum, MB residentes) só com cena 3D; **partículas**
  só com emissor no quadro;
- **áudio**: fila do mixer, buffer da saída, underruns e trechos sem bloco —
  só com a saída aberta.

Não aparece (o motor não mede, e a HUD não inventa): taxa de acerto do cache de
quadros decodificados, cache de render/textura, estado do proxy.
Ponte: `bridge::PerfPOD` passou de 256 para 384 bytes (campos novos a partir
do byte 256; espelho em `EnginePods.kt`). Tudo sai de contadores que o motor
já tinha; nada alocado por quadro. Validação: `:app:assembleDebug` (x86_64)
compilou; o teste no aparelho/emulador fica com o coordenador.
