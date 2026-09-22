# Fase 8 — Relatório de performance

Números medidos, nunca "no olho" (SPEC §4, §159). Cada tabela diz onde rodou,
em que resolução e o que o número é. Seção por marco; esta é a **8A (baseline)**.

---

## 8A — Profiling: baseline do host

## 8E — Sistemas pesados (texto, vetor, máscara, 3D, partículas, optical flow, tracking, efeitos)

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

## 8F — Export

### Ambiente da medição (declarado, não é celular)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500 (6 núcleos / 12 threads), 32 GB, NVIDIA GeForce RTX 3050 (driver 32.0.16.1692), Vulkan. Motor em Release (MSVC).
- **Encoder:** stub do host (`BenchSink` em `engine/tests/test_export.cpp`). Faz o mesmo trabalho que o `MediaCodecExport` faz na CPU: copia os planos Y/CbCr para um "buffer de entrada". Além disso calcula um hash (FNV em palavras de 64 bits) para o golden. O custo dele aparece em coluna própria (`encoder`). **Não há codificação H.264/HEVC real no host.**
- **Decoder:** vídeo sintético (`SyntheticVideo.hpp`, padrão cinza). O preenchimento por pixel foi trocado por `assign`, com os mesmos bytes de saída. Em 4K o laço antigo levava cerca de 40 ms por quadro e virava o gargalo de qualquer medida, o que não representa um decoder de hardware.
- **Áudio:** 48 kHz estéreo sintético, passando pelo mixer e pelo PCM16 reais.
- **Não medido aqui:** aparelho Android real (sem aparelho na bancada) e iOS (sem Mac). O caminho do MediaCodec foi **compilado** (`assembleDebug`, x86_64) mas **não executado**: pelas regras desta frente, sem adb/emulador.
- **Como reproduzir:**
  - comando: `AUREA_BENCH_EXPORT=1 AUREA_BENCH_GLTF=<pasta com DamagedHelmet.glb e Fox.glb> aurea_tests.exe Resolutions` (ou `EffectsAnd3D`, `LongExports`);
  - variante serial: `AUREA_BENCH_DEPTH=1`.

### Caminho antes (medido)
O export rodava numa thread só, com tudo em série: `prepare` (esperando o decoder) → `render` → **2× `read_texture`** → `write_video` → áudio. Em cada quadro, essa thread:
- criava e destruía dois buffers de leitura, dois command pools e dois fences;
- submetia e **esperava a GPU inteira duas vezes**;
- copiava os planos para vetores intermediários;
- quando faltava quadro do decoder, dormia 5 ms "às cegas" antes de tentar de novo.

Além disso, o `Renderer::render` com alvo offscreen chamava `begin_frame`. No Android, com swapchain, isso **adquiria e apresentava uma imagem vazia da tela a cada quadro exportado** e disputava a superfície com o ciclo de vida do app. É um risco de crash (P1) e foi corrigido: o render passou a usar `begin_offscreen_frame`.

### Caminho depois
O export virou um pipeline sobreposto de três estágios (§92–95):

| Estágio | Onde roda | O que faz |
|---|---|---|
| Decode N+1 | thread de decode (já andava adiantada no modo Playback) | Avisa o export por `on_frame_ready` → `exportWakeCv_`, sem o sono cego de 5 ms. |
| Render N | produtor `aurea-export` + GPU | Composição → NV12 → **cópia para o buffer de leitura do slot no mesmo frame**. Não há submissão extra nem espera logo depois do render. Depois de submeter N, espera **só o fence do quadro N−1** (`GPUBackend::wait_frame`). |
| Encode N−1 | thread `aurea-export-enc` | `write_video` direto do buffer mapeado, sem cópia intermediária, e o áudio até o fim do quadro. O sink continua sendo chamado de uma thread só. |

Limites e garantias:
- **Memória:** 3 slots circulam entre produtor e encoder, e isso limita a memória a 3 × 1,5 × L × A bytes (9,3 MB em 1080p, 37 MB em 4K). Quando o encoder não devolve slot, o produtor espera; é isso que segura o ritmo.
- **Ordem:** a ordem dos quadros e dos pts é garantida pela fila FIFO. O áudio continua amostra-exato.
- **Calor (§37):** em `set_thermal` Serious/Critical, ou com throttling, o export passa a 1 quadro em voo (serial). **Os bytes de saída são idênticos.** A UI recebe a flag `kExportThermalReduced`.

### Golden: saída idêntica antes × depois
Hash combinado (Y+CbCr de todos os quadros, com dither ligado, o padrão do app). O mesmo nas 2 execuções de antes e nas 3 de depois:

| Cenário | Hash antes | Hash depois |
|---|---|---|
| básico 1080p30 | cc85c3dc371f1641 | cc85c3dc371f1641 |
| básico 1080p60 | de4d470d4983892a | de4d470d4983892a |
| básico 4K30 | 91f26b38c5be5cc7 | 91f26b38c5be5cc7 |
| básico 4K60 | 8bc571984614c733 | 8bc571984614c733 |
| efeitos 1080p30 | f2483fefaf9e93d1 | f2483fefaf9e93d1 |
| efeitos 4K30 | 9b097721f0206073 | 9b097721f0206073 |
| 3D 1080p30 | 9e3ed556275721b5 | 9e3ed556275721b5 |
| 3D 4K30 | 49cf520ca3d89c04 | 49cf520ca3d89c04 |

**Diferença: 0.** Testes permanentes (`Export.*`):
- `PipelinedOutputIsByteIdenticalToSerial`: pipeline × serial, byte a byte (diferença máxima 0), com 64000 amostras de áudio exatas nos dois;
- `HeatReducesParallelismNeverQuality`: quente × frio, hash igual e flag ligada.

### Vazão do pipeline (quadros/s, host)
Antes: 2 execuções. Depois: 3 execuções, mediana entre parênteses. O ganho é calculado contra o **melhor** valor de antes.

| Cenário | Antes (q/s) | Depois (q/s) | Ganho |
|---|---|---|---|
| básico 1080p30 (150 q) | 277–306 | 704–735 (707) | 2,3× |
| básico 1080p60 (240 q) | 301–346 | 718–770 (748) | 2,2× |
| básico 4K30 (90 q) | 68–92 | 184–194 (188) | 2,0× |
| básico 4K60 (120 q) | 87–93 | 191–199 (192) | 2,1× |
| efeitos 1080p30 (120 q) | 197–222 | 392–405 (405) | 1,8× |
| efeitos 4K30 (60 q) | 58–64 | 97–106 (101) | 1,6× |
| 3D 1080p30 (120 q) | 124–138 | 177–194 (193) | 1,4× |
| 3D 4K30 (60 q) | 44–45 | 69–75 (72) | 1,6× |

Composição dos cenários:
- **efeitos:** 5 camadas (vídeo, 2 formas animadas, imagem e texto) com desfoque gaussiano 12 px, exposição, curvas, 3× brilho (glow), saturação, texto, e desfoque de movimento em 2 camadas que se movem.
- **3D:** DamagedHelmet (PBR), Fox (animação esquelética), sombras da luz-chave (padrão) e 2 emissores de partículas.

**Mesmo pipeline em serial** (`AUREA_BENCH_DEPTH=1`, que é o modo sob calor): 311/334/93/95 q/s no básico e 206–210/63–64/126–128/48 nos pesados. Isso é praticamente o antes. Ou seja, o ganho vem da **sobreposição**; tirar só a leitura síncrona quase não muda.

### Tempo por estágio (ms/quadro, básico 4K30)
| Estágio | Antes (serial, somam) | Depois (em paralelo) |
|---|---|---|
| decode (espera do quadro exato) | 0,09 | 0,03 |
| render (CPU: preparar + gravar + submeter) | 1,23 | 2,0–2,3 |
| readback (antes: 2 leituras síncronas; depois: espera do fence N−1) | 6,46 | 2,7–3,1 |
| encoder (stub: cópia de 12 MB + hash) | 2,80 | 3,5–4,0 |
| áudio | 0,27 | 0,28–0,31 |
| **tempo de parede por quadro** | **10,9** | **5,2** |

- **O que estoura depois:** o produtor (render + espera do fence, cerca de 5 ms), com o encoder logo atrás (cerca de 4 ms). No celular, a cópia para o buffer do MediaCodec entra na coluna "encoder".
- **Orçamento:** um export 4K30 no host anda a 5,2 ms/quadro, contra 33,33 ms do tempo real (6,3× o tempo real).

### Export longo (§98–101: modo acelerado, 160×90 a 30 fps com som)
| Duração | Antes | Depois | Memória privada a cada 10% (depois, MB) | Handles | Deriva A/V |
|---|---|---|---|---|---|
| 10 min (18 000 q) | 10,5 s, 1718 q/s | 4,8 s, 3767 q/s | 285 286 286 287 299 265 265 265 265 | 339 → 343 | 0 µs |
| 30 min (54 000 q) | 31,5 s, 1712 q/s | 17,2 s, 3141 q/s | 341 276 276 276 260 252 252 257 257 | 344 → 348 | 0 µs |
| 60 min (108 000 q) | 62,1 s, 1740 q/s | 36,9 s, 2924 q/s | 263 247 247 248 248 248 248 248 249 | 349 → 349 | 0 µs |

- **Memória:** não cresce por quadro. No de 60 min, de 10% a 90% ficou entre 247 e 249 MB; antes do 10% aparece a sobra do cenário anterior sendo devolvida.
- **Travamento:** nenhum (o vigia acusa travamento com 10 s sem quadro novo).
- **Áudio:** termina exatamente no fim do último quadro, em amostras inteiras, com pts contíguos. A deriva foi medida, não estimada.
- **Temperatura durante o longo:** não medida (o host não expõe estado térmico ao motor). A política de calor está coberta pelo teste de equivalência.

### Encoder de hardware (§96–97)
- O `MediaCodecExport` registra o encoder que **de fato** abriu, com nome (`AMediaCodec_getName`, API 28, via dlsym) e classificação hardware/software.
- **Fallback:** se o encoder de hardware recusa a receita (resolução ou fps acima do bloco), o sink tenta o encoder de software do sistema (`c2.android.*`/`OMX.google.*`) com a **mesma** resolução, taxa e fps. Não há queda silenciosa: aparece `AUREA_LOG_WARN` com os dois nomes e a UI recebe a flag `kExportSoftwareEncoder`.
  - O `ExportProgressPOD` leva as flags no campo +28 (antes `reserved`); o ABI não mudou.
  - O Exporter/ExportScreen mostra o aviso durante o export: "exportando por software (mais lento, mesma qualidade)".
- Quando o sink não sabe dizer (API < 28, ou o host), vale a tabela do MediaCodecList.
- **Sondagem de codecs:** antes do Android 10 todo codec contava como hardware. Agora o software do AOSP é reconhecido pelo nome. A versão da sondagem subiu para ela ser refeita uma vez.
- **Não verificado em aparelho:** a escolha real de codec, o fallback em execução e os nomes. Pendente para o teste em Android real.

### Temporários e cancelamento (§52, §153)
- **Arquivo parcial:** o sink apaga o arquivo parcial no cancelamento, no erro de encoder/muxer e na falha ao finalizar (código já existente, conferido).
- **Galeria:** o Exporter apaga o temporário depois de publicar, com sucesso ou falha. Na abertura, apaga o que sobrou de um export morto pelo sistema (arquivos da pasta `cache/export` mais velhos que a sessão, fora da main thread).
- **Cancelamento:** `cancel_export` acorda todas as esperas do pipeline (slot livre, fila do encoder, decoder). Medido em **15,9–18,5 ms** (antes 19,1 ms) com um encoder simulado de 20 ms por quadro (`CancelIsResponsiveAndReleasesTheSink`, limite de 250 ms). O sink é abortado, nunca finalizado, e a GPU volta ao preview.
- **Shutdown:** com export em andamento, usa o mesmo cancelamento. Antes só marcava a flag, e com o pipeline isso deixaria uma espera sem acordar.

### Testes
- `aurea_tests`: **450 testes, 0 falhas.** Eram 443; entraram 7 em `test_export.cpp`, dos quais 3 são benchmarks que só rodam com `AUREA_BENCH_EXPORT=1`.
- Os 9 testes de export que já existiam passam sem mudança.
- `:app:assembleDebug -PaureaAbi=x86_64` compila.

# AUREA V2 — Fase 8: relatório de performance

<!-- Cada frente escreve SÓ a sua seção (8A, 8B, …) para não conflitar. -->

## 8D — UI / Timeline

### Onde foi medido (sem fingir)
- **Motor:** host Windows 11, AMD Ryzen 5 5500, MSVC Release, `aurea_tests.exe TimelineScale` (`engine/tests/test_timeline_scale.cpp`).
- **Kotlin:** JVM do host (JDK 21, HotSpot com JIT aquecido: 100 rodadas de aquecimento, mediana de 200), `./gradlew :app:testDebugUnitTest` (`TimelineScaleBench`, `TimelineWaveTest`, `TimelineKeyframesScaleTest`). A alocação sai de `ThreadMXBean.getThreadAllocatedBytes`.
- **Não medido:** celular (a bancada não tem aparelho Android; emulador fora por instrução). O ART do aparelho é mais lento que a JVM do host e o custo de cada travessia JNI e da espera pelo lock do modelo do motor não aparece no host. Os números abaixo servem para o **antes/depois** e a ordem de grandeza. A latência gesto → quadro apresentado não foi medida: não há telemetria disso nem aparelho; o que se mede é o trabalho feito dentro do evento de toque.
- Cenário de escala: 1000 camadas com 10.000 keyframes (999 × 9 + 1 × 1009), 1 camada com 10.000 keyframes, e legendas de 5000 palavras sobre um vídeo de 30 min (1390 camadas de texto).

### 1. Releitura do modelo a cada `modelRevision` (§42–45, §147)
Cada mudança no motor (inclusive cada passo de um arrasto na timeline) relê as camadas e os keyframes.

| Medida (1000 camadas, 10.000 kf) | Antes | Depois |
|---|---|---|
| Consultas JNI de keyframes por releitura | 1000 (uma por camada) | 1 (`query_all_keyframes`) |
| Motor, host: keyframes de todas as camadas | 0,046–0,071 ms (1000 consultas) | 0,047–0,057 ms (1 consulta) |
| Kotlin, JVM: releitura num passo de arrasto de clipe | 0,77–0,87 ms, **1609 KB** alocados | 0,20–0,22 ms, **212 KB** |
| Kotlin, JVM: mover 1 keyframe | igual ao de cima (tudo refeito) | 0,17–0,20 ms, 370 KB |
| Kotlin, JVM: projeto recém-aberto (frio) | 0,77–0,87 ms, 1609 KB | 0,45–0,52 ms, 2076–2123 KB (guarda os bytes crus para comparar depois) |
| Revisão sem mudança nos keyframes | 10.000 `KeyframeRow` novos | `KeyframeSnapshot` 0,022 ms, 0 KB; `RowCache` 0,032 ms, 23 KB |
| Releituras dentro de um gesto | 1 releitura completa síncrona **por comando** (no losango, uma por trilha) + 1 no laço | 0 no evento de toque; no máximo 1 por quadro, no laço |
| Teto de camadas / keyframes | 512 camadas, 4096 kf por camada (cortava em silêncio: 5000 palavras = 1391 camadas e a timeline mostrava 512) | sem teto (os buffers dobram quando enchem) |

- O motor **não** era o gargalo (0,02–0,07 ms no host). O custo estava no Kotlin (objetos novos para tudo a cada revisão), nas 1000 travessias JNI e na releitura síncrona dentro do gesto — que ainda esperava o motor largar o modelo (o quadro do preview segura o lock enquanto prepara) e lia o estado velho, porque o comando só é aplicado no quadro do motor.
- Arrasto de clipe passa a enviar posições **absolutas** calculadas no começo do gesto (`setLayerRanges`): idempotente, não depende de a releitura ter chegado (antes o delta era medido contra a linha relida e podia repetir ou perder um passo).

### 2. Desenho da timeline com 10.000 keyframes (§42–45)
Lista de desenho dos losangos por linha e por quadro (`Keyframes.visibleGroups`, função pura testada contra o algoritmo antigo), vista andando 1 quadro por desenho:

| Zoom | Antes | Depois | Grupos na tela |
|---|---|---|---|
| 2 dp/s (tudo vira pílula) | 10,07 µs | 5,73 µs | 1 |
| 80 dp/s (padrão) | 0,87 µs | 1,14 µs | 35 |
| 800 dp/s | 0,12 µs | 0,22 µs | 4 |

Hit-test de losango com 10.000 instantes: 0,25–0,62 µs. Linhas fora da tela já não custavam nada (laço só de `first..last`), régua e marcas em lote (2 Paths), nenhuma alocação por quadro no desenho. Com 10.000 keyframes o desenho já era O(visível); o ganho aqui é o pior caso (zoom aberto) limitado a O(largura/4 dp · log n).

### 3. Waveform (§46)
| Medida | Antes | Depois |
|---|---|---|
| Consultas ao motor por linha de som, tocando 60 s a 30 fps | 1800 (uma por quadro) | 14 |
| Consultas na pinça 4× (140 desenhos) | 140 | 5 |
| Reabrir projeto com 30 min de áudio (host, áudio sintético) | 2,0–3,8 s recalculando | 4,2–6,7 ms (cache em disco) |
| Consulta de 900 baldes no motor | 0,15–0,21 ms calculando / 0,008 ms pronta | igual (agora quase nunca chamada) |

- `WaveStrip` guarda por linha uma janela em grade **fixa** do tempo (`WaveGrid`, degraus de √2 no zoom): a forma não treme mais com a vista (os baldes andavam com ela) e cada consulta — que trava o modelo do motor — vale por várias telas.
- `WaveformCache` grava o nível 0 em `<cache>/waveform/<hash>.awv` (escrita atômica temp → rename; arquivo ruim é apagado e recalculado — testado). Limite de disco/limpeza desse diretório é da 8B (Ajustes › Armazenamento).

### 4. Miniaturas (§47)
Já eram em segundo plano (thread própria, decoder próprio em CPU, fila com os mais recentes primeiro, cache LRU no motor e na UI). Gargalo corrigido: numa rolagem rápida cada balde novo na tela perguntava ao motor (lock do modelo) e criava um Bitmap na thread da UI, sem limite por quadro. Agora **no máximo 6 perguntas por quadro**; o resto vem no quadro seguinte. O orçamento de memória das miniaturas (hoje 400 imagens) fica com a 8B.

### 5. Legendas grandes (§84–85)
| Medida (5000 palavras, vídeo de 30 min) | Antes | Depois |
|---|---|---|
| `create_captions` no host (1390 camadas) | 104,5 ms (1 execução) | 17,2–25,4 ms (3 execuções) |
| Onde roda | thread da UI | `Dispatchers.Default`, com aviso "Criando legendas…" |
| Palavras compostas ao abrir o painel | 5000 (FlowRow dentro de Column rolável) | só os itens na tela (`LazyColumn`, 24 palavras por item) |
| Tocar numa palavra | recompõe as 5000 | recompõe os itens visíveis |
| Marcar vícios | 5000 consultas JNI na thread da UI | em `Dispatchers.Default` |
| Corrigir 1 palavra | 5000 consultas JNI + JSON gravado na thread da UI | 1 consulta; JSON em IO |
| Abrir o painel com transcrição guardada | JSON + content resolver na thread da UI | em IO |

- A causa dos 104 ms era a busca do quadro de cada palavra: linear na camada inteira (54.000 quadros) por palavra; virou busca binária quando o tempo da fonte é crescente (o caso normal), com a varredura antiga como reserva.

### 6. Ociosidade e recomposição (§40–41)
- **Laço de status** (`EditorStore.startStatusLoop`): antes um callback do Choreographer a cada vsync (60–120/s) **sempre**, até na Home. Agora, depois de 30 quadros sem nada mudar (sem tocar, sem scrub, sem gesto, HUD fechado, status igual), lê o status 4×/s; qualquer comando enviado acorda o vsync na hora. Trabalho do motor que termina sozinho (miniatura, waveform, import) aparece em até 250 ms. (Número de configuração, não medição de aparelho.)
- O laço não recompõe quando nada muda (já comparava `ProjectState`/`PreviewState`); faltavam os arrays: `detail`, `gizmo`, `timeRemap`, `textStyle`, `textAnimators`, `shapeParams` e `textPath` eram objetos novos a cada releitura (a cada quadro tocando) e invalidavam quem os lê mesmo iguais. Agora o valor velho volta quando o conteúdo é o mesmo (o detalhe é comparado pelos 256 bytes crus).
- Auditoria por tela (leitura de código): **Home** sem timers (só o laço de status, agora ocioso); **Editor** recompõe com o cabeçote só tocando/scrub; **Efeitos** `EffectParam` compara conteúdo, prévias sob demanda; **Vetor** e **Curva** só laços de gesto; **Legendas** virtualizada; **Timeline** tempo lido só na fase de desenho (nada recompõe com o relógio). Animações infinitas existentes: indicador de atividade (só enquanto ocupado) e prévias de preset (só com o painel de presets aberto e na tela).

### 7. Latência de toque (§147)
Dentro do evento de toque, antes, cada passo de mover/aparar/arrastar losango fazia: comando + releitura completa do modelo (1000 JNI de keyframes, detalhe, efeitos) esperando o lock do motor. Depois: comando e nada mais (a releitura vai para o próximo quadro do laço, uma vez). Scrub já era coalescido no motor e sem debounce; não havia debounce indevido na timeline.

### Testes
- Motor: 447 testes, 0 falhas (4 novos em `TimelineScale`).
- JVM: 34 testes, 0 falhas. Os testes JVM da timeline **nem compilavam** antes desta fase (`Friction` removido em 4ba17d4; linha 46/barra 36 antigas) — corrigidos.
- `assembleDebug` x86_64 ok.

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

# Aurea V2 — Fase 8: relatório de performance

(Cada frente escreve só a sua seção.)

## 8C — Render / preview

### Ambiente da medição (declarado, não é celular)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500, 32 GB, **NVIDIA GeForce RTX 3050** (Vulkan 1.4.351), build Release MSVC.
- **Projeto:** 1920×1080 e 3840×2160, 30 fps. **Preview:** FULL (alvo fora da tela do tamanho da composição). Orçamento: 16,67 ms (60) / 33,33 ms (30).
- **FPS/quadros perdidos de aparelho:** não medidos — sem aparelho Android físico na bancada e sem Mac (§ limites da SPEC). Os números abaixo são do motor no host; não são "de celular".
- **Cena:** N imagens 480×270 espalhadas (escala ~0,45 da largura, rotação, opacidade 0,85); "fx" = exposição + brilho/contraste + saturação + tingir + blur 6 px em todas, glow em 1 de cada 5.
- **CPU** = backend falso (só o custo do motor, sem driver). **GPU** = timestamps Vulkan, mediana da 2ª metade de 60 quadros em sequência (2 quadros em voo).
- Reproduzir: `AUREA_BENCH=1 engine/build/host/tests/Release/aurea_tests.exe Perf8C`.

### Qual estágio estoura o orçamento (§5)
No host, **nenhum** cenário passa de 16,67 ms. Pior caso, 4K + 50 camadas com efeitos: GPU 5,65 ms (composição 3,65 + efeitos 1,99), CPU prepare 0,05 ms + gravação 0,16 ms. O estágio dominante é **GPU**: composição (sobreposição de camadas grandes em RGBA16F) no 4K, efeitos no 1080p. A CPU só pesava com muitas camadas — pela compilação quadrática do FrameGraph (corrigida abaixo). Em celular a GPU é a primeira a estourar; é ela que a escada de resolução do AUTO ataca primeiro.

### Antes → depois (medido, mesmo host, mesma cena)

| Cenário | Antes | Depois |
|---|---|---|
| CPU gravação, 1080p 50 camadas fx | 0,646 ms | 0,165 ms |
| CPU gravação, 1080p 200 camadas fx (§127) | 7,931 ms | 0,847 ms |
| CPU prepare, 1080p 200 camadas fx | 0,428 ms | 0,348 ms |
| Alocações por quadro no caminho quente (prepare+render) | 6 (7 com 200 camadas) | **0** |
| Passes, 50 imagens sem efeito | 51 | 1 |
| Passes, 50 imagens fx / 200 fx | 241 / 961 | 191 / 761 |
| GPU 1080p 50 camadas / fx | 1,39 / 3,57 ms | 0,87 / 3,09 ms |
| GPU 4K 50 camadas / fx | 3,39 / 6,14 ms | 2,89 / 5,65 ms |
| GPU 1080p 10 camadas / 4K 10 | 0,28 / 0,61 ms | 0,18 / 0,51 ms |
| Intervalo entre quadros, desvio / p99 — 1080p 50 fx | 0,59 / 4,80 ms | 0,16 / 3,54 ms |
| Intervalo entre quadros, desvio / p99 — 4K 50 fx | 0,72 / 7,49 ms | 0,25 / 6,23 ms |
| Preview AUTO, série FULL=25 ms a 60 fps (3000 quadros) | 34 trocas (FULL↔1/2) | **1** troca |
| Preview AUTO, 6000 quadros, subidas que falham (ruído 10%) | 78 trocas | 13 trocas, 6 subidas, espera 32× |

Frame pacing (§145): o intervalo é a cadência de submissão fora da tela, sem vsync (o host não tem swapchain nos testes) — mede a regularidade do motor, não a do display.

### O que mudou
1. **FrameGraph linear (§24–25):** a busca do produtor varria todos os acessos por acesso (O(n²), com trecho O(n³)). Agora: acessos por recurso em CSR, busca binária nos escritores, fila de Kahn em heap, nascimento/morte de transitórios por posição, ordenação por contagem (o `stable_sort` alocava a cada quadro). Mesma ordem, mesmos aliasings (todos os testes do grafo passam).
2. **Imagem convertida uma vez:** a conversão sRGB→linear rodava a cada quadro para cada camada de imagem (o comentário do shader dizia o contrário). Agora a versão linear na densidade da camada fica em cache (dois tamanhos por imagem, despejo após 240 quadros sem uso, destruição na troca de projeto/dispositivo). Teste na GPU real: **0 texels diferentes** entre o quadro que converte e o que reaproveita; 2 camadas da mesma imagem = 1 conversão.
3. **Zero alocação por quadro (§26–28):** medido com `operator new` instrumentado (só a thread do render conta). Os 6 vinham de listas locais nos uploads de glifos/máscaras/vetores (trocadas por listas do renderer — duas linhas em cada, sem mudar a lógica da 8E).
4. **Preview AUTO 2.0 (§6–11):** decisão por métrica — GPU, CPU, decode, quadros perdidos, pressão de memória, calor e complexidade (troca grande de passes/camadas recomeça a média). Duas escadas: resolução FULL→1/2→1/4→1/8 e reduções (desfoque de movimento, flow, SSAO, sombra, partículas, blur, LOD: 1 → ½ → ¼). CPU no gargalo corta reduções antes da resolução; decode no gargalo **não** derruba resolução (não adiantaria) e fica registrado. Descer: 3 quadros acima de 85% do orçamento na média móvel. Subir: a **previsão** do degrau de cima (razão de custo medida na última descida; 3× se não medida) abaixo de 65% por 60 quadros × espera; subida desfeita em menos de 180 quadros dobra a espera (até 32×), estabilidade por uma janela a reduz. Pisos térmico (quente: reduções ½; crítico: 1/4 + ¼) e de aparelho (`set_quality_floor`, para a 8H) valem na hora.
   - Botões expostos em `RenderSettings::quality` / `Renderer::preview_quality()`. Desfoque de movimento, partículas e flow já respondem (pelo `heavyScale`, que agora é o menor entre calor e AUTO). SSAO, sombra, amostras de blur e LOD estão expostos; **quem os aplica é a 8E/8H**.
   - Telemetria (§164): escala em uso segue no `PerfPOD` (renderScaleNum/Den); `EngineTelemetry` ganhou escala, degrau de reduções, gargalo, espera de subida e camadas podadas. O `PerfPOD` (ABI de 256 bytes, HUD da 8A) não foi mexido.
5. **Pular trabalho (§19–22):** o plano de efeitos passou para antes do decode; camada 2D inteira fora da composição, só com efeitos de cor (sem desfoque/eco/RGB no tempo/perspectiva), não abre decoder, não sobe textura e não gera passe — vale para matte (matte fora da tela recorta igual a matte ausente). Oculta e opacidade 0 já pulavam; efeito desligado e identidade (blur raio 0, exposição 0) já saíam do grafo — agora com teste que conta.
6. **Fusão (§23):** já existia (`color_stack`, até 12 operações, 1 LUT por passe). Verificado: exposição + brilho/contraste + saturação + tingir + matriz de cor + níveis = **1 passe** (sem fusão seriam 6).
7. **Tipos consultados por camada** (posterizar tempo, eco, RGB no tempo) resolvidos no `initialize` (eram 3 buscas lineares por camada por quadro).
8. **Timers de GPU:** 64 → 512 por quadro; acima de 64 passes o detalhe por etapa saía errado (composição 0 ms com 191 passes).

### Verificações (testes, sempre rodam — `Perf8C`)
- AUTO determinístico: mesma série → mesma sequência de escalas; FULL caro assenta em 1/2 com 1 troca; carga no limite com ±25% de ruído: ≤ 1 troca; intervalo entre tentativas de subida nunca encolhe; CPU no gargalo mantém FULL e reduz; decode no gargalo mantém FULL; cena mais leve volta a FULL; memória ≥ 0,9 desce; pisos térmico/aparelho.
- Relógio (§9): quadros de 100 ms a 30 fps → após 3 s a timeline está no quadro 90 (2 derrubados por quadro), nunca atrasada; com relógio de áudio mestre o vídeo segue o áudio.
- Export (§8): com AUTO no degrau mínimo e `heavyScale` 0,25, o export tem 1000 slots de partícula (o preview 250) e `effective_quality` volta tudo a 1.
- Decode: 5 vídeos (visível, oculto, opacidade 0, fora da tela com cor, fora com blur) → **2 decoders abertos**, 1 camada podada. (Antes desta frente a camada fora da tela abria decoder — por construção, não havia poda; não medido separadamente.)
- Regime com 50 camadas fx: 0 texturas criadas por quadro, 190 transitórias em 92 físicas (98 reaproveitadas), 0 alocações.
- Render sob demanda (§39): parado, 20 chamadas + 1,2 s de thread de render ociosa = **0** apresentações; um seek = exatamente 1.
- Suíte: 443 → 460 testes, 0 falhas. Android `assembleDebug` x86_64 compila.

### Lock do modelo (§29–30)
50 camadas com efeitos, render em laço + UI lendo `read_status` a cada 4 ms: espera **p50 0,002 ms**; máximo 5,2 ms, sempre no 1º quadro — o `prepare` copia os pixels de cada imagem nova para o upload sob o lock (50 × 480×270). Em regime o lock só cobre o prepare (0,03–0,35 ms). `submit_commands` é lock-free. O `renderMutex_` fica preso durante a apresentação (espera do vsync): só a prévia de efeito e a captura disputam com ele. **Pendente:** tirar a cópia de pixels do lock exige mudar a posse de `Engine::images_`.

### Vulkan (§140–142)
- **Camada de validação ausente neste host** (sem Vulkan SDK; sem downloads): não verificável aqui. O backend agora conta erros/avisos da validação (`vk::Backend::validation_errors()`) e o teste `VulkanCachedImageIsPixelExactAndValidationClean` exige zero quando a camada existir.
- `vkDeviceWaitIdle` só em `wait_idle` (desligar, recuperar dispositivo, captura fora da tela), `detach_surface` e recriação do swapchain. Nenhum no playback: `begin_frame` espera só a cerca do quadro N−2 (2 em voo) → CPU e GPU sobrepostas; uploads no quadro vão pelo anel de staging.

### Android
- `RenderLoop.kt` **não** dispara render: é o laço de estado da UI (Choreographer). O motor tem thread própria, acordada por comando/decoder, que dorme parado (acorda no máximo a cada 500 ms e não desenha). Achado para a 8D/8H: esse laço de estado chama `readStatus` (JNI + lock do modelo) a cada vsync mesmo com o editor parado.

### Pendências honestas
- Números em aparelho real (FPS, quadros perdidos, térmico) — sem aparelho.
- SSAO, resolução de sombra, amostras de blur e LOD: botões prontos, sistemas (8E) ainda não leem.
- Cópia de pixels de imagem nova sob o lock do modelo (1º quadro).

## 8H — Aparelhos fracos, temperatura e bateria

**Onde foi medido.** Host: Windows 11 Pro 10.0.22631, AMD Ryzen 5 5500 (12 threads), 32 GB, NVIDIA GeForce RTX 3050 (Vulkan 1.4). Build Release, `aurea_tests.exe`. **Não há aparelho Android na bancada:** nenhum número abaixo é de celular. Classe e política em celular foram exercitadas com perfis sintéticos (os números de um perfil são os que o `MediaCodecList`/Vulkan de um aparelho daquele tipo responderiam, não uma medição dele).

### Bateria (§38): o que acordava sem trabalho

| Medida (host, 1 s parado) | Antes | Depois |
|---|---|---|
| `JobSystem` com 4 workers, sem tarefa | **384,3 %** de um núcleo | 0,0 % |
| Motor inteiro sem superfície (4 workers + render + mixer + miniaturas + waveform) | **356,1 %** de um núcleo | 0,0 % |
| Acordadas/s da thread de render sem superfície / suspensa | 2 (timeout de 500 ms) | 0 (medido) |
| Acordadas/s dos workers parados | contínuo (`yield` em laço) | 0 (medido) |
| Laço de estado do Kotlin (`startStatusLoop`) parado | 1 por vsync (60–120/s) | 4/s depois de 30 quadros sem mudança (por construção; **não medido em aparelho**) |
| Decoders de miniatura abertos com a fila vazia | até 2, até fechar o projeto | 0 depois de 2 s (medido) |

- **Causa raiz dos 384 %:** `JobSystem::worker_main` fazia `yield` e voltava à fila para sempre; `yield` sem outra thread pronta volta na hora, então cada worker ocioso ocupava um núcleo inteiro. Ninguém submete tarefa ao pool hoje — era só calor e bateria. Agora gira 0,2 ms e dorme numa `condition_variable` (Dekker `pending_`/`sleepers_`, sem aviso perdido: `Battery.ParkedWorkersWakeForWork`, 20 rajadas de 50 tarefas).
- No build de `00eb0c8` o celular tinha 1 worker (ver bug dos núcleos abaixo), ou seja, **1 núcleo a 100 % o tempo todo com o app aberto**; antes de `00eb0c8`, `perf − 2` workers girando.
- A thread de render **com** superfície e parada continua acordando a cada 500 ms: é a rede das 75 mudanças de `modelRevision_` que não chamam `request_render`, e a `render_frame` pula barato. Fica para 8C (render sob demanda, §39).
- **Medido e mantido:** o mixer de áudio acorda a cada 2 ms com o anel cheio **durante o play** (~500/s). Parado, ele dorme sem prazo. Espaçar isso muda a latência do áudio depois de um seek, e áudio vem antes de bateria (§166). Não mexi sem aparelho para medir.

### Classe do aparelho (§110–111)

`classify_device` (em `DevicePolicy.hpp`) dá uma faixa por eixo medido, e a classe é a **menor** delas. O eixo que segurou fica registrado e a UI mostra o porquê:

| Eixo | LOW | MID | HIGH | ULTRA |
|---|---|---|---|---|
| RAM total | < 4,5 GB (3–4 GB nominais) | < 7 GB | < 10,5 GB | ≥ 10,5 GB |
| RAM livre agora | < 512 MB ou < 1/8 da total: desce uma faixa | | | |
| GPU (limites Vulkan) | sem compute, API 1.0, < 256 invocações ou textura < 4096 | < 512 invocações, sem fp16 ou textura < 8192 | o resto | ≥ 1024 invocações, API ≥ 1.3, textura ≥ 16384, fp16 storage |
| CPU (freq. máx.) | < 2,2 GHz ou < 4 núcleos | < 2,4 GHz | < 2,95 GHz | ≥ 2,95 GHz |
| Codecs (hardware) | sem H.264 ou < 1080p | sem HEVC ou sem 4K | — | HEVC 4K |

Eixo não medido é neutro, exceto a GPU: sem GPU medida, a classe fica em no máximo **MID** (nem o plano de entrada nem o de topo no escuro).

| Perfil (teste) | Resultado |
|---|---|
| Entrada: 3 GB, 8 × 1,8 GHz, Mali-T830 (Vulkan 1.0, 256 invocações, sem fp16), H.264 1080p, sem HEVC | **LOW**: memória LOW, GPU LOW, CPU LOW, codec MID. Motivo: memória |
| Médio: 6 GB, 2,3 GHz, Adreno 618 (1.1, 1024 invocações, fp16), HEVC 4K | **MID**: memória MID, CPU MID |
| Alto: 8 GB, 2,84 GHz, Adreno 660 (1.1), HEVC 4K | **HIGH** |
| Topo: 12 GB, 3,2 GHz, Adreno 740 (1.3), HEVC e AV1 4K | **ULTRA**, nada segurou |
| Host real (RTX 3050, Vulkan 1.4, 1024 invocações, textura 32768, fp16 1/1, 32 GB) | **ULTRA** |

### Política (§107–108) e onde cada valor se liga

`device_policy(classe, térmico)` gera os valores. Os que o motor **já aplica hoje**:

| Valor | LOW | MID/HIGH/ULTRA | Onde entra |
|---|---|---|---|
| Escala inicial do preview | 1/4 (1/2 abaixo de 720 linhas) | AUTO, como antes | `recommended_initial_scale` → `AdaptiveResolutionController::configure` |
| `heavyScale` (amostras de motion blur, partículas, resolução do flow; ≤ 0,25 troca o flow pela mistura) | 0,5 | 1 | `Engine::preview_heavy_scale` → `RenderSettings::heavyScale` |
| Fatia do cache de decode | 16 % | 24 % (como antes) | `Engine::apply_memory_budgets` |
| Lado das prévias de efeito | 160 px | 320 (MID) / 512 (HIGH, ULTRA) | `Engine::render_effect_preview` (o pedido da UI é 320 × 200) |
| Pausa entre miniaturas (térmico) | por temperatura | por temperatura | `ThumbnailService::set_pacing_ms` |

Os **ganchos** (valor decidido e testado; o knob é de outra frente):

- **8C:** `minPreviewDenominator` (HOT 2, CRITICAL 4). Substitui `thermal.severe()` e `should_degrade()` no `AdaptiveResolutionController`. Também `preferProxyAboveShortSide` (LOW 720, MID 1440, HIGH 2160, ULTRA nunca): ainda não existe proxy de preview. Também `backgroundPauseMs` para as prioridades LOW/BACKGROUND.
- **8E:** `shadowMapSize` (LOW 1024, os outros 2048; HOT metade, CRITICAL 512), que substitui o `SceneRenderer::shadowSize_` fixo em 2048, e `particleScale`.
- **8F:** `exportPipelineDepth` (LOW 2, MID/HIGH 3, ULTRA 4; HOT −1, CRITICAL 1). O export de hoje é serial.

O plano MID é idêntico ao de antes da Fase 8 (teste `LowProfileAppliesTheLowPlan`): quem já funcionava não muda.

### Temperatura (§35–37)

`PowerManager.THERMAL_STATUS_*` → faixa, numa tabela só (`thermal_state_from_android`, testada no host; o JNI só repassa o número):

| Android | Faixa | Ação |
|---|---|---|
| 0 NONE | NORMAL | — |
| 1 LIGHT | WARM | trabalho de fundo com 150 ms de pausa entre miniaturas; preview igual |
| 2 MODERATE, 3 SEVERE | HOT | `heavyScale` × 0,5, partículas × 0,5, sombra pela metade, piso de preview 1/2, fundo com 500 ms, export com −1 de profundidade |
| 4 CRITICAL, 5 EMERGENCY, 6 SHUTDOWN | CRITICAL | `heavyScale` ≤ 0,25 (flow vira mistura), sombra 512, piso 1/4, fundo com 1000 ms, export serial. Nada é desligado: o editor continua respondendo |

- **Export (§37):** `exportRenderScale` e `exportHeavyScale` valem 1 em todas as 16 combinações de classe e temperatura (`Thermal.PolicyActionsPerTier`). O calor só tira paralelismo.
- `Gpu.ThermalReducesPreviewButNotExport` continua passando: no crítico, o preview fica com 1 px nítido (mistura) e o export com 21 px (movimento de pixels).
- Pausa real medida: 3 miniaturas em HOT (500 ms) levaram 1002 ms (`Battery.ThumbnailPacingSlowsBackgroundWork`).

### Não esconder (§109)

- **Ajustes › Este aparelho** mostra a classe e o que a segura, o plano em números, a temperatura de agora (o relatório é relido ao abrir a seção) e uma frase para cada limitação. Exemplos: "Este aparelho exporta até 1080p: o codificador de vídeo dele não passa de 1920 × 1080." e "HEVC indisponível neste aparelho…".
- **Exportar:** a resolução acima do teto aparece desligada, com o motivo embaixo (antes era filtrada em silêncio). O HEVC aparece desligado, com o motivo, quando não há codificador.
- **Novo projeto:** uma resolução acima do que o aparelho exporta continua escolhível, e a folha diz o teto e o porquê.

### Bugs achados e corrigidos (com teste)

| ID | Sev. | O quê | Correção / teste |
|---|---|---|---|
| 8H-1 | P2 (bateria/calor) | Workers do `JobSystem` girando parados (384 %) | parking; `Battery.IdleJobSystemDoesNotSpin`, `IdleEngineDoesNotSpin` |
| 8H-2 | P2 | `detect_cpu` tomava os padrões da struct (1/1/1) por medição: **todo aparelho e o host com 1 núcleo**, 1 worker e "1 núcleos (1+1)" nos Ajustes (regressão de `00eb0c8`) | a medição sai de `platform_`; `DeviceClass.HostIsClassifiedFromMeasurement` |
| 8H-3 | P2 | Mapa tag→codec nascia com zeros: aparelho **sem HEVC** ficava com "HEVC de hardware" = o decoder H.264 | padrão `kInvalidIndex`; `DeviceClass.MissingCodecIsAbsentNotCodecZero` |
| 8H-4 | P2 | Teto de export saía do **decoder**: o aparelho que decodifica 4K mas só codifica 1080p oferecia 4K, que falhava | teto pelo codificador H.264 + `ExportLimit`; `ExportCeilingFollowsTheEncoderNotTheDecoder` |
| 8H-5 | P2 | `DeviceProfile` mandava 0 instâncias por codec: `decode_parallelism` = 1 em todo aparelho | `maxSupportedInstances`, formato da sondagem v2 (refaz a sondagem guardada) |

### Microbenchmark de primeira execução (§112): não feito

Sem aparelho para calibrar, os cortes de um benchmark (ms por passe → faixa) seriam números inventados, e §163 proíbe isso. Os limites Vulkan medidos já separam os perfis testados. O gancho existe: `GpuCapabilities::estimatedFillRatePerSec` e o `DeviceProfile`, que guarda uma vez por aparelho.

### Testes

- Motor: **459 testes, 0 falhas** (eram 443). `test_device.cpp` novo, com 16 testes nas famílias Battery, DeviceClass e Thermal.
- Android: `assembleDebug` x86_64 ok. `DeviceReportTest` (JUnit): 3 de 3.
- O `TimelineMathTest.kt` já existente **não compila** (`Friction` não existe mais em `main`). Para rodar o `DeviceReportTest`, ele foi tirado do lugar e devolvido; não é da 8H.

(Cada frente escreve só a sua seção.)

## 8I — Release e abertura

### Onde foi medido (§4, §159)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500, 32 GB, NVIDIA RTX 3050 (Vulkan real, os MESMOS shaders SPIR-V e o MESMO backend do Android). Teste `engine/tests/test_startup.cpp` (suíte `Startup`), mediana de 5 rodadas, 3 execuções.
- **APK:** `assembleRelease` (arm64-v8a + armeabi-v7a, assinado com a chave de debug — não há `key.properties` na árvore de trabalho), auditado com `dexdump`/`llvm-readelf` do SDK/NDK.
- **Não medido aqui (declarado, não fingido):** abertura a frio/quente no celular, tamanho instalado real, tempo de compilação de pipeline em Mali/Adreno. Não há aparelho físico na bancada e a regra desta frente era não usar adb/emulador. No host o driver da NVIDIA tem cache de shader próprio em disco, então "a frio" = sem o cache do Aurea; no celular cada pipeline compilado a frio custa tipicamente dezenas de ms, e é aí que o corte de pipelines na abertura aparece de verdade.

### Abertura do motor (§59–62)

| Host, mediana de 5 | Antes | Depois |
|---|---|---|
| Pipelines compilados antes do 1º quadro | **40** (todos os efeitos + todo o 3D + export) | **16** (composição, vídeo, forma, vetor, texto, máscara, pilha de cor, saída) |
| Renderer na abertura fria (shaders + pipelines) | 13,8 ms (13,0–18,3 em 3 execuções) | 9,0 ms (7,7–9,1) |
| Renderer na abertura quente (cache do Aurea no disco) | 3,4 ms (2,8–4,2) | 2,5 ms (2,2–2,6) |
| `Engine::initialize` fria / quente | 64,9 / 56,7 ms | 57,0 / 50,8 ms |
| Backend (instância + dispositivo Vulkan) | ~51 ms | ~47 ms (não é código do Aurea; varia com a carga da máquina) |
| 1º quadro de projeto novo (forma + texto) | 8,6 ms | 7,1–11,4 ms (sem pipeline novo: os dois estão no conjunto quente) |

- O que saiu da abertura, medido no MESMO processo: 12 pipelines de efeito = 2,1–2,6 ms a frio no host; os 11 do 3D entram no 1º quadro parado depois da primeira camada 3D (1º quadro com 3D: 6,6–10,1 ms no host, compilando os 11).
- `Engine::startup_timings()` + uma linha de log por abertura: `abertura do motor: X ms (aparelho, gpu, renderer com N pipelines, resto)` — a abertura no celular passa a ser medida pelo logcat, sem estimar.

**Preguiçoso, fora do playback (§60):** fora do playback contínuo (parado, scrub, export) o renderer varre o projeto inteiro (efeitos ligados e camadas 3D de TODAS as composições, não só o instante) e compila o que falta antes de pegar a imagem da swapchain. No playback não varre (nada muda no modelo sem pausar). `compiles_since_mark()` continua 0 no playback (teste `ProjectPipelinesWarmWhenUsedNotAtOpen`: desfoque + 3D aquecidos no 1º quadro parado, 5 quadros depois sem compilar nada). 3D, optical flow, partículas, rastreio e legendas não criam nada na abertura: o 3D sobe só texturas neutras de 1 px; os outros nascem no primeiro uso (conferido no `Engine::initialize` e no `EditorStore.init`).

**Cache de pipeline persistente (§62, §176):** o arquivo agora é cabeçalho nosso + blob do driver: versão = impressão digital FNV-1a de todo o SPIR-V embutido (~330 KB, calculada uma vez), fornecedor, dispositivo, versão do driver, UUID, tamanho e soma. Antes de entregar ao driver também confere o cabeçalho Vulkan dentro do blob (drivers de celular com defeito caem com blob ruim). Truncado, corrompido, de outra GPU/driver, formato antigo ou app atualizado → apagado e recompilado. Marca `.carregando` gravada antes de entregar o blob: se sobreviveu, a carga anterior derrubou o processo e o cache é apagado sem ser lido (sem loop de crash na abertura). Gravação atômica (tmp → fflush/fsync → rename) e só quando entrou pipeline novo (ir para segundo plano deixou de regravar ~600 KB toda vez). Teste `CorruptPipelineCacheIsDiscardedAndRebuilt`: byte trocado, truncado, formato antigo, lixo, versão trocada e marca de crash — em todos o motor sobe, o arquivo é refeito e a abertura seguinte o aceita.

### Abertura do app (inspeção do código, §59–63)
O que roda até a Home, na ordem:
1. `MainActivity.onCreate`: edge-to-edge, `setContent`. `EditorStore` (ViewModel) no construtor: `AureaEngine.create` (carrega a .so), `EffectPrefs`, `ThumbnailCache`; `refreshProjects()` em `Dispatchers.IO`.
2. Thread `aurea-ciclo`: `DeviceProfile.probe` (tabela de codecs só na 1ª abertura ou SO novo; depois prefs), `engine.initialize` (acima), superfície pendente.
3. Main, com o motor pronto: `deviceReport`, listener térmico, `readCatalog` (1 JNI, 47 efeitos), `EffectPreviewStore` (agora só um objeto), laço de status.

Tirado da abertura: **decodificação da foto das prévias** (JPEG 640² + cópia de 1,6 MB para o motor, no main thread, em toda abertura) → carregada na 1ª prévia, na fila de render das prévias; **`CaptionsState`** (cofre da chave Groq) e **`PresetLibrary`** (listagem das 5 pastas de presets + 2 SharedPreferences, no main thread) → `by lazy`, no primeiro uso.

### Navegador de efeitos (§63)
- Já era sob demanda (cada cartão pede a sua ao entrar na tela). Concorrência NÃO era limitada: cada prévia ia para `Dispatchers.Default` e até N núcleos ficavam presos esperando o mesmo mutex de render do motor. Agora: fila de UMA (`limitedParallelism(1)`); cartão que sai da tela antes da vez é cancelado sem custo.
- O comentário prometia cache em disco e ele não existia: toda abertura do navegador refazia tudo. Agora memória → disco (`cache/motor/previas/<instalação>/…webp`, WebP 90: ~20 KB por prévia contra ~127 KB em PNG) → GPU. App atualizado = pasta nova, a antiga apagada. "Limpar cache" renomeia na hora e apaga em segundo plano.
- Custo que o disco evita (host): 44 prévias de 47 efeitos = 425–559 ms de GPU na 1ª vez (31 pipelines compilados), 396–529 ms nas seguintes sem o disco. Com o disco, da 2ª abertura em diante: zero prévias na GPU.
- A foto: 640 → 320 px (o cartão é 320 × 200 e o corte usa a largura toda, 1:1), 117 → 42 KB; textura e bitmap 4× menores.

### Build release e tamanho (§180–183, §187–190)

| APK release (arm64 + armv7) | Antes | Depois |
|---|---|---|
| **APK** | **24.208.966 B (23,09 MB)** | **11.548.193 B (11,01 MB)** |
| dex (no APK / cru) | 11,5 MB / 43,0 MB (3 dex) | 1,55 MB / 2,98 MB (1 dex) |
| libaurea.so arm64 / armv7 | 4,75 / 3,78 MB | 3,81 / 3,05 MB |
| entradas na `.dynsym` da .so (arm64) | 4.348 (o motor inteiro exportado) | 555, das quais 252 definidas: 179 `Java_*`, `JNI_OnLoad` e a API pública do zstd |
| res / resources.arsc | 66 itens 409 KB / 415 KB | 24 itens 324 KB / 119 KB |
| assets | foto 117 KB + presets 1,2 KB | foto 42 KB + presets 1,2 KB |

- **R8 ligado** (`isMinifyEnabled`, `isShrinkResources`). O dex era 44 MB cru por causa do `material-icons-extended` inteiro e do Compose sem encolher. `proguard-rules.pro` prende o que o C++ chama por JNI (`AureaEngine.openContentFd`/`decodeImage` estáticos + nativos). Conferido no dex do APK: 179/179 nativos com o nome exato que o C++ exporta, os dois callbacks `PUBLIC STATIC` com a assinatura que o `GetStaticMethodID` pede. `mapping.txt` sai em `build/android/app/outputs/mapping/release/` (guardar junto com o APK publicado).
- **`-fvisibility=hidden`** no build nativo do Android: a .so exportava o motor inteiro (~400 KB de `.dynsym`/`.dynstr` por ABI) e toda chamada entre funções do motor passava pela PLT. `--gc-sections` e `-ffunction-sections` já vinham do NDK.
- Sem assets de teste na release (assets = foto + 4 JSON de presets; res = ícone, splash, fonte de ícones). Os 4 `modelo_*.jpg` (73 KB) não tinham referência e saíram.
- **Instalado (não medido — estimativa):** a .so não é extraída (`useLegacyPackaging = false`), então o instalado ≈ APK (11,0 MB) + código do ART (perfil de base presente: tipicamente 1–3 MB) ≈ 12–14 MB, mais dados: cache de pipeline ~0,6 MB e prévias ~0,9 MB (44 × ~20 KB). Um aparelho arm64 com APK por ABI (loja/AAB) baixaria ~7,2 MB.
- Build de release é gerável e medível: `./gradlew :app:assembleRelease` (sem `key.properties` usa a chave de debug, que é a mesma do Aurea oficial — ver `_identity/signing`).

### Log e rede (§180–184)
- **Log no motor:** `AUREA_LOG_TRACE/DEBUG` compilam para nada com `NDEBUG` (o Android é `RelWithDebInfo`, que define `NDEBUG`); `log_write` confere o nível ANTES de formatar; buffer na pilha, sem alocação. Auditados os 70 `AUREA_LOG_INFO/WARN`: nenhum por quadro no caminho normal — o do export é a cada 300 quadros; os demais são de falha ou de evento único. **Kotlin:** zero `Log.*`/`println` no app.
- **Rede:** a única chamada é `GroqWhisperProvider.transcribe` (HTTPS para a Groq), feita só pelo botão "Gerar legendas"/"Transcrever de novo", em `Dispatchers.IO`, com timeouts de 20 s/180 s. Nada de rede na abertura, no playback, no export ou no autosave.

### Código morto e botões falsos (§185–186, §195)
- **Inalcançáveis:** `ParentPanel`, `TransitionsPanel`, `EchoPanel` (nenhum botão abria `EditorPanel.Parent/Transitions/Echo`; "Seguir camada" vive na barra do topo, e o eco virou o efeito "Eco e rastro" com migração do projeto antigo). Junto saiu o `queryEcho` que rodava (JNI + alocação) em todo refresh do detalhe da camada.
- **"Em breve" zerado:** "Qualidade 3D" (Ajustes), o "olho" de opções de visualização (barra de transporte), 16 curvas sem motor (famílias Quique e Outras, 3 "Degraus"; ficam Bézier e Manter) e as ações "Gráfico de velocidade" e "Loop" da curva. `comingSoon()` não existe mais.
- **Restos de Comunidade/Perfil e outras sobras:** glifos (At, CloudFill, LockShield, PlusApp, Person*), `rememberResourceThumbnail`/`decodeResource` (miniatura dos modelos da A.01), `HomeLinkRow`, `SwitchRow`, `TileChevron`, `MaterialChevron`, `GlyphCircle`, `EffectBrowserTile`, `ShellSwitch`, `casasAutomaticas`, `compactBar*`, `effectHuman`, `effectMeta`, `paramTypeLabel`, `selectOnly`, `cancelPointPick`, `parentSelectionToLast`, `showError`, `Engine3D`/`LayerSeconds`/`Quality3D`; 4 drawables `modelo_*`.
- **Permissões sem recurso:** CAMERA, RECORD_AUDIO, REQUEST_INSTALL_PACKAGES, WRITE_EXTERNAL_STORAGE (no Android 8–9 o export já ia para a pasta do app). As de leitura de mídia ficaram (projetos migrados da A.01 podem ter caminho de arquivo).

### Testes
- Motor: **447 testes, 0 falhas** (eram 443; +4 `Startup`).
- Android: `assembleDebug -PaureaAbi=x86_64` e `assembleRelease` (arm64 + armv7) compilam; auditoria JNI do APK acima.

### Para as outras frentes (achado aqui, fora da posse da 8I)
- **8H/8D:** o laço de status (`RenderLoop`, Choreographer) roda a cada vsync desde a abertura, também na Home — `readStatus` por JNI 60–120×/s sem editor aberto.
- **Áudio:** `AudioEngine::initialize` abre o stream AAudio na abertura do motor; poderia abrir no primeiro play.
- **8B/8G:** `MemoryManager` ("orçamento recusou") e `EffectGraph` ("efeito não montou os passes") logam a cada quadro enquanto a falha persistir — sem limite de taxa.
- **8H:** `DeviceProfile.surveyed/hardwareDecoderCount` e `DeviceReport.exportCeiling` sem chamador.
- Espelho JNI: `AureaEngine`/`CommandBatch` têm ~15 wrappers sem chamador (echo, rgb, transição, recuperação de sessão, telemetria…). O R8 já os tira do dex; o C++ correspondente continua na .so.

| Máquina | PC de mesa: AMD Ryzen 5 5500, NVIDIA GeForce RTX 3050 (Vulkan), Windows 11 Pro 22631 |
| Build | Release, MSVC (`aurea_tests.exe`: motor real, backend Vulkan real, validação desligada na medição) |
| Projeto / alvo | 1080p (1920×1080) fora da tela, RGBA16F; preview = mesma resolução com a escala pesada indicada (0,5 / 0,25) — a resolução do preview (1/2, 1/4) é da 8C |
| FPS / quadros perdidos | não se aplica: render fora da tela, sem present (GPU e CPU por quadro abaixo) |
| GPU | timestamp por passe, **mediana** de 8 quadros em regime (o anel de 2 em voo devolve o quadro de 2 atrás, com o mesmo conteúdo) |
| CPU | `prepare` + gravação do FrameGraph, mediana. O host estava carregado pelos builds das outras frentes: CPU tem ruído de ±30% |
| A/B | o **mesmo** `tests/test_heavy.cpp` compilado sobre a base (`be24f9c`) e sobre esta frente, rodado em sequência (`AUREA_HEAVY_V2` por `__has_include`) |
| Aparelho | **nenhum celular medido** (sem aparelho na bancada; emulador não vale para performance — SPEC "Limites"). iOS/Metal não testável aqui |

Reproduzir: `AUREA_BENCH=1 aurea_tests.exe Heavy`. Golden dos efeitos:
`AUREA_HEAVY_DUMP=<pasta>` grava a saída de EXPORT de cada efeito;
`AUREA_HEAVY_REF=<pasta>` compara (`AUREA_HEAVY_FULLRES=1` em 1080p,
`AUREA_HEAVY_ONLY=<chave>` filtra).

### O botão de qualidade dos sistemas pesados (para o AUTO da 8C)

`render/HeavyQuality.hpp`: um bloco por sistema em vez de um número só.
`RenderSettings::heavy` (+ `heavyExplicit`) define cada botão; sem ele,
`HeavyQuality::from_scale(heavyScale, previewDenominator)` desce a escada.
**Export (`finalQuality`) ignora os dois e usa `full()`** (teste
`Heavy.QualityLadderAndExportIsAlwaysFull`; e no 3D o export pedido durante o
calor crítico continua com mapa 2048).

| Botão | Cheio (export) | Escala 0,5 | Escala 0,25 | Onde |
|---|---|---|---|---|
| `particles` (fração dos slots) | 1 | 0,5 | 0,25 | partículas |
| `flow` (base da pirâmide ×384 px) | 1 | 0,5 | 0,25 | optical flow |
| `flowBlurSamples` | 16 | 8 | 4 | desfoque vetorial |
| `shadowMapSize` (também ÷ preview 1/2, 1/4) | 2048 | 1024 | 512 | 3D |
| `shadowFilter` | PCF 6×6 | PCF 2×2 bilinear | PCF 2×2 bilinear | 3D |
| `lodBias` (tamanho na tela ×) | 1 | 0,75 | 0,5 | 3D |
| `effects` (amostras dos caros) | 1 | 0,5 | 0,25 | Desfoque de lente, Raios |

Contadores para o HUD (8A): `Renderer::heavy_stats()` (`HeavyStats`) —
uploads/bytes do atlas, glifos rasterizados, atlas refeitos, retriangulações e
acertos do vetor, rasterizações e acertos de máscara, flow calculado/acerto/
despejo/bytes, e do último quadro: slots de partícula, desenhos 3D, desenhos
de sombra, desenhos instanciados, primitivas visíveis/recortadas, triângulos e
tamanho do mapa de sombra.

### 1. Texto, vetor e máscara (§64–67)

| Medição (motor inteiro, `Heavy.*`) | Antes | Depois |
|---|---|---|
| Texto com animador (máquina de escrever), 45 quadros em regime: uploads do atlas / glifos rasterizados / atlas refeitos | sem contador (o código só subia com glifo novo) | **0 / 0 / 0** |
| Vetor parado com a camada andando 20 quadros: retriangulações / acertos | sem contador | **0 / 20** |
| Vetor com morph FORA do intervalo dos keys (antes do 1º, depois do último) | retriangulava todo quadro (o quadro entrava no hash) | hash estável → 0 (`Heavy.VectorMorphHashIsStableOutsideTheKeyRange`) |
| Máscara parada com a camada andando 20 quadros: rasterizações / acertos | sem contador | **0 / 20** |
| Fontes de reserva no 1º caractere ausente | TODAS as 12 candidatas de uma vez (no Android inclui a CJK, a maior da lista) | uma a uma, pela escrita do caractere; para na 1ª que tem o glifo |

- O atlas SDF é por glifo na base de 64 px (escala não re-rasteriza); o
  buffer de glifos do quadro é um anel mapeado (sem textura). Glifo NOVO
  (digitar) ainda sobe o atlas inteiro (4 MB, R8 2048²) — upload parcial
  precisa de API de sub-região no backend (fora da posse da 8E).
- No host a fonte padrão já cobre árabe/hebraico, então o teste de reservas
  carrega 0; o ganho de RAM das reservas só aparece no Android (não medido
  aqui). `FontManager` guarda só as fontes realmente usadas (a lista de fontes
  lê só o cabeçalho de cada arquivo).

### 2. 3D (§68–75)

Capacete (DamagedHelmet: 15.452 triângulos, 5 texturas 2048²), 1080p.
"Crítico" = escala pesada 0,25 (calor crítico/AUTO).

| Cena | Modo | Desenhos cor | Desenhos sombra | Triângulos | Mapa | GPU sombra | GPU cor 3D | GPU total |
|---|---|---|---|---|---|---|---|---|
| 1 capacete | export — antes | 1 | 1 | 15.452 | 2048 | 0,047 | 0,263 | 0,367 |
| 1 capacete | export — depois | 1 | 1 | 15.452 | 2048 | 0,047 | 0,264 | 0,367 |
| 1 capacete | crítico — antes | 1 | 1 | 15.452 | 2048 | 0,047 | 0,222 | 0,325 |
| 1 capacete | crítico — depois | 1 | 1 | 15.452 | **512** | **0,011** | **0,161** | **0,227 (−30%)** |
| 25 capacetes | export — antes | 25 | 25 | 386.300 | 2048 | 0,135 | 0,906 | 1,095 |
| 25 capacetes | export — depois | **1** (instanciado) | **1** | 386.300 | 2048 | 0,134 | 0,868 | 1,058 |
| 25 capacetes | crítico — antes | 25 | 25 | 386.300 | 2048 | 0,135 | 0,902 | 1,092 |
| 25 capacetes | crítico — depois | **1** | **1** | **193.125** | **512** | 0,152 | **0,614** | **0,823 (−25%)** |

(ms de GPU; CPU do quadro 0,03–0,08 ms nos dois.)

- **Instancing**: malha + primitiva + LOD + material + pipeline iguais (sem
  skin/morph) viram UM desenho com N instâncias; matrizes num SSBO do quadro
  (duas mat4 por instância na cor, uma na sombra). Transparentes ficam fora
  (ordem por profundidade). Quadro instanciado × modelos separados:
  **diferença máxima 0/255** (`Heavy.Scene3DInstancingCullingAndShadowKnob`).
  Na RTX 3050 o ganho de GPU é pequeno (−3%); o que cai é o nº de chamadas de
  desenho (25 → 1), que é custo de driver/CPU no celular — não medido aqui.
- **Frustum culling**: contado (`visiblePrimitives`/`culledPrimitives`) e
  testado: caixa movida para fora da tela = 1 recortada, visíveis 4 → 3. As
  contas do 3D agora são do QUADRO inteiro (antes o `stats_` zerava a cada
  cena/subquadro de desfoque e mostrava só o último).
- **LOD sem popping**: histerese de ±15% em volta dos limiares (60/160 px) no
  preview — um tamanho oscilando em cima do limiar não troca a malha a cada
  quadro. O export escolhe sem histórico (o mesmo quadro sai igual sozinho ou
  em série). `lodBias` do preview crítico: 386.300 → 193.125 triângulos.
- **Sombra reduzível**: mapa 2048 → 512 e PCF 36 → 4 amostras no crítico
  (sombra 0,047 → 0,011 ms num capacete). Com 25 capacetes a sombra é limitada
  por vértice (0,13–0,15 ms nos dois tamanhos).
- **Deduplicação**: um `GpuModel` por asset — 1 e 25 capacetes ocupam os
  mesmos **107,7 MB** de GPU (malha, texturas, materiais; a animação é do
  asset compartilhado, só as matrizes avaliadas são por instância).
- **Mips / 4K**: textura acima de 2048 é reduzida NO IMPORT
  (`kModelTextureCap`, já existia) e a GPU gera os mips. Streaming de mips
  **não foi feito**. Achado para a 8B: o `SceneAsset` mantém as texturas
  decodificadas na RAM depois do upload — **80 MB** só no capacete — porque o
  modelo é re-subido após 240 quadros sem uso; soltar exige reler do arquivo
  (Engine, fora da posse da 8E).

### 3. Partículas (§76–79)

Emissor de 1600×900, tamanho 4→1 px, vida 4 s, instante 6 s (regime), 1080p.

| Partículas | Blend | GPU export antes | GPU export depois | GPU preview 0,5 antes | depois | CPU |
|---|---|---|---|---|---|---|
| 10k | aditivo | 0,086 | 0,090 | 0,080 | 0,085 | 0,02 |
| 100k | aditivo | 0,289 | 0,287 | 0,223 | 0,232 | 0,02 |
| 500k | aditivo | 1,132 | 1,105 | 0,823 | 0,867 | 0,03 |
| 1M | aditivo | 2,043 | **1,979** | 1,445 | 1,533 | 0,03 |
| 1M | normal | 2,057 | **1,974** | 1,439 | 1,522 | 0,02 |

- Partículas são ANALÍTICAS (shader resolve pela semente, slot, geração e
  tempo): **não há buffer de simulação** — nada a redimensionar; o slot morto
  é reaproveitado por construção (nasce de novo a cada `slots/taxa` s);
  **nenhuma ordenação** (2D sem profundidade; aditivo é comutativo e o
  "normal" desenha na ordem estável dos slots). CPU 0,02–0,03 ms em qualquer
  contagem (só o bloco de parâmetros).
- Quad indexado (4 execuções do vertex shader por partícula em vez de 6):
  −3% a −4% em 1M no export. O preview 0,5 metade dos slots já existia; a
  diferença de ±6% entre as colunas de preview está dentro do ruído do host.
- 1M partículas = ~2 ms na RTX 3050: não é gargalo no host; no celular (10–20×
  mais lento) o botão `particles` do AUTO é o que segura.

### 4. Optical flow (§80–82)

Vídeo sintético 1080p, velocidade 20% (quadro intermediário por movimento de
pixels) + desfoque vetorial, cache desligado (todo quadro calcula). ms de GPU:

| Modo | luma | pirâmide | LK (estimativa) | deformação (interpolação) | desfoque vetorial antes → depois | GPU total antes → depois |
|---|---|---|---|---|---|---|
| export | 0,081 | 0,023 | 0,116 | 0,117 | 0,281 → 0,281 | 1,863 → 1,868 |
| preview 1,0 | 0,082 | 0,023 | 0,118 | 0,116 | 0,268 → 0,269 | 1,838 → 1,833 |
| preview 0,5 | 0,065 | 0,017 | 0,052 | 0,114 | 0,266 → **0,152** | 1,744 → **1,630** |
| preview 0,25 | 0,031 | 0,013 | 0,026 | mistura (0,15) | 0,264 → **0,094** | 1,715 → **1,549** |

- Estimativa em pirâmide com base ≤ 384 px (export) e 192/96 px no preview 0,5
  / 0,25 (já existia; agora pelo botão `flow`). A deformação é na resolução do
  vídeo; no 0,25 o preview troca movimento por mistura (já existia). **Não há
  estágio de oclusão separado** no algoritmo (a deformação usa ponto fixo de 3
  passos) — não se inventou um.
- Novo: amostras do desfoque vetorial pelo botão (16/8/4): −43% e −65%.
- Cache: duas texturas por camada + **orçamento** (48 MB padrão,
  `set_flow_cache_budget`), LRU por camada, despejo contando a textura que vai
  entrar. Teste: 3 camadas com orçamento de 1,90 MB → residente 1,32 MB, 7
  despejos, flow continua calculando.

### 5. Tracking (§83)

Textura sintética movida (2,3; −1,7) px/quadro em 1080p, 12 pontos, 30
quadros; erro em px da resolução CHEIA.

| Análise | Erro médio | Erro máx | Passo NCC (12 pontos) | RGBA → cinza |
|---|---|---|---|---|
| 1080p | 0,66 | 2,21 | 23,1 ms | 3,56 ms |
| 720p | 1,45 | 7,17 | 23,2 ms | 1,63 ms |
| 360p | 1,76 | 7,46 | 22,3 ms | 0,36 ms |
| 240p | 2,93 | 8,14 | 22,1 ms | 0,16 ms |

- O rastreio de pontos do motor já analisa em miniatura de ≤ 360 px (e o
  rastreador de câmera numa altura reduzida própria): a conversão cai 10× e o
  decode/escala também; o erro sobe de 0,66 para 1,76 px (em 1080p) —
  suficiente para estabilizar/fixar camada. **Nenhuma mudança de código**.
- Achado: o passo do NCC custa o mesmo em qualquer resolução (~1,9 ms por
  ponto no host: janela e raio fixos em px, média da janela recalculada por
  candidato). É o próximo alvo se o rastreio de muitos pontos pesar.

### 6. Efeitos (§86–91)

Custo = mediana da soma dos passes do próprio efeito, 1080p, valores de
demonstração (os da prévia). Export = qualidade cheia; preview 0,5 = escala
pesada 0,5 na mesma resolução (a redução de resolução do preview é da 8C).

| Efeito | Chave | Passes antes→depois | GPU export antes | depois | GPU preview 0,5 antes | depois |
|---|---|---|---|---|---|---|
| Ordenar pixels | `aurea.stylize.pixel_sort` | 1→1 | 1,932 | 1,930 | 1,937 | 1,989 |
| Minimax | `aurea.stylize.minimax` | 1→1 | 1,358 | 1,361 | 1,358 | 1,360 |
| Raios de luz | `aurea.light.rays` | 1→1 | 1,230 | 1,235 | 1,247 | **0,649** |
| Desfoque gaussiano | `aurea.blur.gaussian` | 2→2 | 0,765 | 0,759 | 0,759 | 0,758 |
| Dano de JPEG | `aurea.stylize.jpeg_damage` | 1→1 | 0,446 | 0,441 | 0,442 | 0,441 |
| Desfoque de lente | `aurea.blur.lens` | 1→1 | 0,428 | 0,420 | 0,419 | **0,201** |
| Eco e rastro | `aurea.time.echo` | 1→1 | 0,380 | 0,380 | 0,380 | 0,380 |
| VHS | `aurea.glitch.vhs` | 1→1 | 0,355 | 0,357 | 0,358 | 0,357 |
| Brilho profundo | `aurea.light.deep_glow` | 6→5 | 0,858 | **0,345** | 0,840 | **0,345** |
| Máscara de nitidez | `aurea.blur.unsharp` | 3→3 | 0,285 | 0,290 | 0,284 | 0,289 |
| Turbulência | `aurea.distort.turbulence` | 1→1 | 0,286 | 0,287 | 0,288 | 0,287 |
| Brilho | `aurea.light.glow` | 5→5 | 0,256 | 0,255 | 0,256 | 0,253 |
| Meio-tom | `aurea.stylize.halftone` | 1→1 | 0,207 | 0,203 | 0,203 | 0,202 |
| VHS Fita | `aurea.glitch.uni_vhs` | 1→1 | 0,199 | 0,200 | 0,204 | 0,201 |
| Nitidez | `aurea.blur.sharpen` | 1→1 | 0,189 | 0,189 | 0,188 | 0,188 |
| Lente | `aurea.distort.warp` | 1→1 | 0,189 | 0,188 | 0,189 | 0,189 |
| HoloMatrix | `aurea.stylize.holomatrix` | 1→1 | 0,166 | 0,167 | 0,168 | 0,167 |
| Sinal | `aurea.glitch.signal` | 1→1 | 0,139 | 0,140 | 0,143 | 0,146 |
| Ondulação que dissolve | `aurea.distort.ripple_dissolve` | 1→1 | 0,135 | 0,139 | 0,135 | 0,136 |
| Dano de filme | `aurea.stylize.film_damage` | 1→1 | 0,132 | 0,133 | 0,136 | 0,132 |
| Níveis | `aurea.color.levels` | 1→1 | 0,132 | 0,130 | 0,131 | 0,131 |
| Motion Tile | `aurea.stylize.motion_tile` | 1→1 | 0,123 | 0,123 | 0,114 | 0,123 |
| Curvas | `aurea.color.curves` | 1→1 | 0,111 | 0,110 | 0,111 | 0,110 |
| Exposição / Brilho e contraste / Saturação / Tingir / Matriz de cor | `aurea.color.*` | 1→1 | 0,103–0,104 | 0,103–0,104 | 0,103–0,104 | 0,103–0,104 |
| Faixa de luz | `aurea.light.sweep` | 1→1 | 0,106 | 0,103 | 0,112 | 0,105 |
| Glitchify | `aurea.glitch.glitchify` | 1→1 | 0,099 | 0,100 | 0,103 | 0,105 |
| Glitch em cruz | `aurea.glitch.cross` | 1→1 | 0,099 | 0,100 | 0,103 | 0,105 |
| Grão | `aurea.stylize.grain` | **0→1** | 0 (não desenhava) | 0,098 | 0 | 0,101 |
| Colorama | `aurea.color.colorama` | **0→1** | 0 (não desenhava) | 0,098 | 0 | 0,101 |
| Varredura | `aurea.stylize.scanlines` | **0→1** | 0 (não desenhava) | 0,079 | 0 | 0,079 |
| Chave de croma | `aurea.key.chroma` | 1→1 | 0,098 | 0,094 | 0,096 | 0,100 |
| Chave de luma | `aurea.key.luma` | 1→1 | 0,086 | 0,103 | 0,090 | 0,091 |
| Tremor | `aurea.distort.shake` | 1→1 | 0,078 | 0,081 | 0,078 | 0,078 |
| Onda | `aurea.distort.wave_warp` | 1→1 | 0,080 | 0,080 | 0,079 | 0,080 |
| Inverter | `aurea.color.invert` | 1→1 | 0,079 | 0,079 | 0,079 | 0,079 |
| Transformar | `aurea.transform` | 0→0 | 0 | 0 | 0 | 0 (entra na matriz da composição) |
| Posterizar tempo / RGB no tempo | `aurea.time.*` | 0→0 | 0 | 0 | 0 | 0 (temporal: quadro parado não tem outro instante) |
| Controles de expressão (5) | `aurea.control.*` | 0→0 | 0 | 0 | 0 | 0 (não desenham) |

CPU por efeito: 0,02–0,06 ms (planejamento e gravação).

- **Brilho profundo com pirâmide**: o halo já descia a pirâmide; o que pesava
  era o NÚCLEO borrado em resolução cheia (blur H+V = 0,55 de 0,86 ms). Agora
  núcleo com raio ≥ 12 texels vive em 1/2 e, quando núcleo e halo caem na
  mesma redução, o limiar (imagem clara) é um só: 6 → 5 passes, **0,858 →
  0,345 ms (−60%)**, export incluído.
- **Desfoque de lente** (anéis do disco) e **Raios** (amostras no raio) com
  variante de preview pelo botão `effects`: −52% e −48% no preview 0,5; export
  intocado.
- **VHS, VHS Fita, Glitchify, Glitch em cruz, Sinal, Dano de filme/JPEG e
  Grão**: procedurais na GPU, um passe cada, hash no shader; CPU só monta os
  uniforms (0,02–0,03 ms). Nada de ruído gerado na CPU nem textura de ruído
  subida por quadro.
- **P1 corrigido (mesmo achado da 8A)**: Grão, Varredura e Colorama eram
  `PerPixel` sem `color_op` e o EffectGraph os descartava como identidade —
  **não desenhavam na timeline nem no export** (só na prévia do catálogo). Agora
  `Neighborhood` (passe próprio). O teste de regressão ficou na 8A
  (`Gpu.EveryCatalogEffectChangesTheProjectFrame`); o benchmark A/B desta
  frente também acusa na base (3 efeitos sem mudar o quadro).
- Os caros que sobram (Ordenar pixels 1,9 ms, Minimax 1,4 ms, Raios 1,2 ms)
  não têm variante de preview: reduzir amostras mudaria o desenho de forma
  visível; o que os barateia no preview é a resolução 1/2–1/4 da 8C (custo
  proporcional à área).

**Golden/diff do export** (saída de EXPORT de cada um dos 47 efeitos, base ×
depois, 8 bits sRGB), 480×270: **43 efeitos idênticos** (diferença máxima 0);
Grão, Varredura e Colorama mudam porque agora desenham (a base não aplicava
nada); Desfoque de lente, Raios, Brilho e Faixa de luz também idênticos em
1080p. **Brilho profundo**: diferença média **0,084/255** em 1080p (0,047 em
480×270); **0,45% dos canais** passam de 3/255 — todos nas curvas de nível
duras do "estouro" (o limiar recorta o brilho e a borda anda alguns px com o
núcleo em 1/2); imagem visualmente igual (conferida lado a lado). Critério do
benchmark: o do golden (máx. 3, média 0,35) ou média ≤ 0,35 com ≤ 2% dos
canais acima de 3.

### Testes (0 falhas, 455 testes no host)

`tests/test_heavy.cpp` (novo): `AnimatedTextPlaybackNeverTouchesTheAtlasInSteadyState`,
`FallbackFontsLoadOneAtATime`, `StaticVectorAndMaskAreBuiltOnceWhileTheLayerMoves`,
`VectorMorphHashIsStableOutsideTheKeyRange`, `Scene3DInstancingCullingAndShadowKnob`,
`FlowCacheStaysWithinItsBudget`, `QualityLadderAndExportIsAlwaysFull` e os
benchmarks `Bench*` (só com `AUREA_BENCH=1`). `TestFramework.hpp`: registro
de 512 → 1024 testes (acima do limite os testes eram descartados em silêncio;
com as 9 frentes somando testes, 512 estourava).

### Não feito / limites declarados

- Nenhum número de celular (sem aparelho). Os ganhos de chamadas de desenho
  (instancing) e de partículas precisam do Android real para virar ms.
- Streaming de mips 3D; soltar as texturas decodificadas do `SceneAsset`
  depois do upload (80 MB no capacete) — recomendação para a 8B.
- Upload parcial do atlas de glifos ao digitar (4 MB por glifo novo) — precisa
  de API de sub-região no backend.
- Deduplicar o MESMO arquivo importado duas vezes (hoje vira dois assets) —
  é do import no Engine.
- HUD: os contadores estão em `Renderer::heavy_stats()`; mostrar é da 8A.
