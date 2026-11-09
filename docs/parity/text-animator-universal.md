# Animador de texto universal (0.5.0)

Referências: After Effects (comportamento dos Text Animators) e Alight Motion 5.0.280 (organização e fluxo do Text Transform, analisados com REA). Nenhum código, shader ou asset de terceiros entra no produto.

## Auditoria: o que existia

Seis caminhos independentes animavam texto, cada um com avaliação, trilhas e tela próprias:

| Caminho | Onde ficava | Problema |
| --- | --- | --- |
| Pilha `TextData::animators` | `TextAnimator.cpp`, trilha 33 | Um seletor por animador; rampas e facilidade diferentes do AE |
| Efeito `aurea.text.animator` | `TextAnimator.cpp` | Cópia da pilha em forma de efeito |
| Efeito `aurea.text.animator_pro` | `TextAnimatorPro.cpp` | Modelo do AE, mas isolado e com um seletor de cada tipo |
| Efeito `aurea.text.transform` | `TextTransform.cpp` | Modelo do Motion, com matrizes próprias |
| Efeitos de conteúdo (progresso, aleatório, espaçamento) | `TextTransform.cpp` | Etapa separada, antes do layout |
| Presets (Juan, Pack, Motion) | `*.inc`, `EngineLayerAnim.cpp` | Três formatos de receita |

Também havia animadores de camada por letra, que continuam: não são animação de texto, valem para qualquer camada.

## Evidência do Motion (REA, `reference.apk`, SHA-256 4caeb6de…bf4159)

- Observado em `fx__text-transform.xml`: 5 seções, nesta ordem: Faixa (início, fim, fase), Transformar (componente, âncora, deslocamento, ângulo, escala, esticar, opacidade), Atributos visuais (cor), Interpolação e easing (ease in, ease out, sobreposição, forma), Ordem aleatória (interruptor no título, semente).
- Observado em `WHy.lG$mY0` (`oo`, `pQ`): seção fechada esconde as linhas; a seta gira 0↔90° em 300 ms; um interruptor logo depois de uma seção com o mesmo rótulo vira o interruptor do título.
- Observado em `WHy.lG` (linhas 6720–6762): o conjunto de seções abertas começa vazio. Só uma seção marcada `EDITFIRST` nasce aberta, e o Text Transform não tem nenhuma.
- Observado em `effect_setting_slider`: rótulo de 75 dp, régua e valor de 56 dp numa linha, usando a largura inteira do painel.

## Decisão de arquitetura

Um modelo só, `TextData::animators`, e um avaliador só, `text::evaluate_text_animators`:

- Cada animador tem uma LISTA de seletores combináveis (faixa, oscilação, expressão) e qualquer combinação de propriedades.
- O seletor "varredura" e o `rig` Motion preservam, sem mudança visual, o que vinha do Transformar texto.
- Os efeitos antigos viram animadores ao abrir o projeto (`text::migrate_text_effects`), levando keyframes e expressões.
- O formato salvo sobe para v59. Projetos de v14 a v58 continuam abrindo.

## O que mudou

- **Motor:** `TextAnimator` ganhou lista de seletores (faixa, oscilação, expressão), modo de combinação, unidades, propriedades novas e etapa de conteúdo. A avaliação antiga dos efeitos saiu de `Renderer`, `Text3D`, `TextAnimatorPro` e `TextTransform`.
- **`selectorValue`:** antes devolvia sempre 100. Agora é o resultado real dos seletores de cima.
- **Migração:** `text::migrate_text_effects` roda ao abrir projeto, colar, aplicar preset e importar do After Effects. Formato v60; projetos de v14 a v59 continuam abrindo.
- **Copiar e colar, presets:** levam os animadores e todas as propriedades novas.
- **UI:** uma lista só (`TextAnimSection.kt`, `TextAnimationSection` em `LayerPanels.swift`). O editor abre na largura toda, com as seções do Motion recolhidas, e "Seletores" guarda o nível do After Effects. Os seis efeitos saíram do navegador de efeitos.

## Estado por plataforma

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita | Host aprovado | `Text` 220 testes, `Expr` 33, `Serializ` 36, `Presets` 31, `AeImport` 25: 0 falhas |
| Android | Feita | `assembleDebug` x86_64 aprovado | Emulador: adicionar animador, abrir seção, arrastar Posição X (0 → 36 px) e o texto andou |
| iOS | Feita | Não compilado: depende de Mac/Xcode | Não validado |

## Suíte completa (08/11/2026)

2656 testes, 72 falhas. Conferidas contra um binário de 03/11, de antes do animador novo:

- **Causadas por este trabalho:** 2 testes. `EveryCatalogEffect…` acusava `aurea.text.progress`, `randomizer`, `spacing` e `animator_pro` como inertes. Esses efeitos só existem para abrir projetos antigos, e os testes passaram a ignorá-los.
- **Do ambiente:** `MaskBlendModes…` e `PuppetDeforms…` gravam em caminho relativo; passam rodando da pasta certa.
- **Já falhavam em 03/11:** `V2140Regression` (9), `MaterialOverrides` (6), `SceneCuts` (1), `CaptureResources` (6), `VulkanDispatchOwners` (4), `SceneEnvironmentJobs` (2), `Stability` (30, memória de GPU), `Fuzz` (1).
- **Mudaram por commits de depois de 03/11, sem relação com texto:** `MotionTile` (margem, 5e549ddf) e `ProjectPackage` (modo de resgate, 862ba763).
- **Ainda inerte:** `aurea.generate.raster_extrude` em camada 2D, efeito de 3D que não foi mexido.

## Limitações conhecidas

- Texto a partir de 160 px na tela usa um nível do atlas com 3× os pixels, no mesmo atlas e sem memória extra. Numa letra de 700 px, o erro de contorno caiu 23% (3507 para 2699 px); a borda continua vindo de SDF de um canal.
- Coreano: fonte embarcada `NotoSansKR-Hangul.otf` (OFL, 1,8 MB), porque o NotoSansCJK do Android 15 é CFF2 e o stb não lê.
- Motion blur por glifo usa o caminho existente do renderizador e não entrou na comparação com o AE.
- O ruído da oscilação é próprio: tem a mesma forma de uso do Wiggly Selector, mas os valores não são os do AE.
- A ordem aleatória migrada do Pro usa outro embaralhamento; a seed mantém determinismo, não a mesma ordem de antes.
- Animar a semente do Transformar texto não migra: fica o valor parado.
- Os efeitos antigos continuam registrados no motor apenas para ler projetos e importar `.ffx`; não aparecem na interface.

## Matemática medida no After Effects

Medido no After Effects 26.5, com folhas de texto renderizadas e leitura do peso de cada letra (`build/reference/ae-medida/`).

| Item | Regra |
| --- | --- |
| Valor da unidade | Amostrado no centro da letra, `(i + 0,5) / n` |
| Rampas | Saturam fora do intervalo |
| Quadrada com Suavidade | `clamp((cobertura − 0,5) / s + 0,5)`; s ≈ 0 corta em 0,5 |
| Redonda | `sqrt(sin(π t))` |
| Suave | Triângulo passado na cúbica (0,5; 0) (0,5; 1) |
| Ease High e Low | Cúbica com `P1 = L ≥ 0 ? (L, 0) : (0, −L)` e `P2 = H ≥ 0 ? (1 − H, 1) : (1, 1 + H)`, aplicada antes de Amount |
| Subtract | `acima × (sinal(v) − v)`; v negativo inverte o que vem de cima |
| Add, Intersect, Min, Max, Difference | Confirmados, inclusive com valores negativos |
| Soma entre seletores | Limitada a −1..1 a cada seletor |
| Primeiro seletor | Parte de 1 em Subtract, Intersect e Min; de 0 nos outros |
| Tracking do animador | Em pixels |
| Pivô | Letra: centro do avanço, na linha de base. Palavra e linha: centro da caixa, na linha de base. Tudo: centro da caixa nos dois eixos |
| Grouping Alignment | X: `alinhamento × meia largura`. Y: `alinhamento × 1,2 × altura do H` (aproximado, Arial e Times em ±1,5% do em) |
| 3D por letra sem câmera | Câmera padrão: 39,6° na largura, no centro da composição |
| Opacidade por letra | Em gama no AE, mesmo com blending linear ligado no projeto |

Projetos gravados antes da v60 mantêm a matemática antiga (`TextAnimator::aeModel = 0`). Efeitos antigos migrados e presets salvos pela pessoa antes desta versão também.

## Comparação quadro a quadro

`AeTextCompareDeviceTest` monta no emulador a mesma folha da comp `CMP` do AE: mesmos valores, Arial Bold, 30 qps, 2 s. A comparação usa 8 quadros (0, 8, 15, 23, 30, 38, 45 e 59), com o deslocamento fixo de layout (15 px) removido.

Erro = soma das diferenças absolutas ÷ tinta. O piso de 0,039 é a diferença de antisserrilhado com o texto parado.

| Animação | Erro no quadro 0 | Pior erro | Em repouso | Observação |
| --- | --- | --- | --- | --- |
| Typewriter | sem tinta nos dois | 0,039 | 0,039 | Igual |
| Wave | 0,039 | 0,039 | 0,039 | Igual |
| Fade Up | 0,046 | 0,046 | 0,039 | Igual, centroide até 3,9 px |
| Bounce por expressão | 0,132 | 0,132 | 0,123 | Centroide em 0,6 px; o piso desta linha é maior e a causa não foi investigada |
| Smooth Reveal | 0,137 | 0,137 | 0,045 | Desfoque em luz linear, um pouco mais largo que o do AE |
| Tracking Reveal | 0,042 | 0,338 | 0,039 | Até cerca de 6 px no fim da linha com tracking pequeno (quadro 38) |
| 3D por caractere | 0,581 | 0,581 | 0,050 | Centroide até 10 px em X e 4 px em Y nos primeiros quadros |

O teste remove uma chave que apareceu no quadro 30 em uma camada. A causa não foi investigada.

### Não comparado

Line Spacing, Skew, Rotation Z, Anchor Point animado, seletor por palavra e por linha em movimento, motion blur, Wiggly, `wiggle()`, `seedRandom()`, `ease()`, `linear()` e `selectorValue`. O motor de expressões tem essas funções; só `time`, `textIndex` e `Math` foram comparados com o AE.

### Limitações desta correção

- A opacidade em gama supõe contraste entre texto e fundo: exata em branco sobre preto e em preto sobre branco, aproximada nos outros casos.
- Velocidade e influência valem por trecho e guardam alças normalizadas. Trecho sem variação de valor não aceita velocidade.
- Velocidade de saída e de entrada ficam no menu da curva, em diálogo; o gráfico continua com as alças.
- `Line Spacing` no painel mostra só Y. O X existe no motor.
- A importação de script do AE aproxima: nesta folha, 7 camadas entraram com 2 keyframes e nenhuma expressão.

### Registro desta correção

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita | Host e Android x86_64 compilados | Host: suíte completa em 09/11/2026, abaixo. Emulador API 35, alvo `aurea_text_device_tests`: 92 de 93 testes |
| Android x86_64 | Feita | `assembleDebug` e `assembleUiTest` aprovados | Emulador API 35: folha de 7 animações e `Beta050PanelsDeviceTest` (agrupamento e diálogo de velocidade e influência tocados na tela) |
| Android arm64 e armv7 | Feita | `assembleDebug` aprovado nas duas | Não executado (sem aparelho ARM) |
| iOS | Feita | Não compilado (depende de Mac) | Não validado |

Os testes novos passaram no emulador: `ShapesAndEaseMatchAfterEffects`, `SpeedAndInfluenceRoundTrip` e `OldProjectTextAnimatorsKeepTheirMath`, que regrava um arquivo como a v59 e confere a matemática antiga. A única falha é `RichTextSpansColorBoldAndSize` (`test_text.cpp`), que espera 11 glifos brancos e encontra 10: a Roboto do sistema Android tem a ligadura "fi", e o texto do teste tem "ficou". Esse alvo não cobre o resto da suíte do motor nem o backend GLES, que foi desligado porque o gerador de shaders do host também é bloqueado.

`Beta050PanelsDeviceTest` encontrou e cobre um defeito: trocar o agrupamento gravava no motor, mas a tela não redesenhava. Corrigido no Android; o iOS já recarrega depois de gravar.

### Suíte completa do motor (09/11/2026, host)

2673 testes, 66 falhas em 18 testes. Grupos ligados a esta correção, 0 falhas: `Text` 227, `Serializ` 37, `Presets` 31, `AeImport` 25, `Expr` 33, `Curve` 38.

Nenhuma falha foi atribuída a esta correção:

| Situação | Testes |
| --- | --- |
| Já listadas em 08/11 | `V2140Regression` (9), `MaterialOverrides` (6), `SceneCuts` (1), `CaptureResources` (6), `VulkanDispatchOwners` (4), `SceneEnvironmentJobs` (2), `Stability` (30), `Fuzz` (1), `MotionTile` (1) |
| Efeito inerte já conhecido (`aurea.generate.raster_extrude`) | `EveryCatalogEffectChangesTheProjectFrame`, `…OnAnAdjustmentLayer` |
| Falham igual nos binários de 02/11 e 03/11 | `NaturalPlaybackAdvanceDoesNotCancelBoundCapture`, `ReadyProxyCannotReplaceOriginalWhileFinalCaptureWaits`, `SteadyPlaybackOf50LayersCreatesAndAllocatesNothing` (3 alocações por quadro) |
| Intermitente | `ExportWatchdog.LateValidationCannotPublishAnAbandonedSession`: falhou uma vez na suíte e passou em 3 de 3 repetições isoladas |
