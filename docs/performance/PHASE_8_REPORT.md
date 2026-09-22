# Fase 8 — Relatório de performance

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
