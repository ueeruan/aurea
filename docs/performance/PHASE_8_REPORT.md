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
