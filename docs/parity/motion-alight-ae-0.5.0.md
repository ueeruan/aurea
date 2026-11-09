# Motion automático (Onda): Alight Motion 5.0.280 + After Effects → AUREA 0.5.0

Data: 07/11/2026. Pedido: engenharia reversa de como o Alight Motion faz o motion, pesquisa do After Effects e adaptação ao AUREA (motor + Android + iOS). Este documento descreve comportamento; nenhum código, shader ou asset da referência entrou no produto.

## Identidade e método

- APK: `build/reference/alight-motion-5.0.280/reference.apk`, SHA-256 `4caeb6dea44e8f123b88833aef7b28dd54f4a48fb0cc6a9e5085ba4c1bbf4159` (o mesmo de `alight-motion-5.0.280.md`).
- REA (JADX headless 0.7.1, JAR `6e5eacf5…ff75`), APK nunca executado:
  - inventário `…scene.animators.*`: `ev_cb1f5c0bb5ce98d49ee78e3d72f75284b1100191df3d01df5a0b0e4590742da7` (registro completo `ev_5b1d5404…ac4c`). Cobertura completa, 26 classes.
  - `WaveAnimators`: `ev_206818f7f90adc79b83e5ae34944177e303c1bd5d3682a925910f48a18a2a314` (5 classes internas).
  - `WaveAnimatorsKt` (`triangle`): `ev_bd8032637787744f7c982777157aecb75fdf6c0189699b84d3ac85749751ee84`.
- Corpo dos métodos lido da decompilação JADX já guardada em `build/reference/am-ui-20261014/jadx-out/` (mesma versão do APK): `WaveAnimators.java`, `WaveAnimatorsKt.java`, `ColorAnimators.java`, `AnimatorEnvironment.java`, `AnimatorKt.java`, `AnimatorSerializerKt.java`, `KeyableKt.java`. Decompilação é evidência estática derivada; não comprova execução.

## O que o Alight Motion faz (observado na decompilação)

**Modelo.** Cada propriedade animável (`Keyable<T>`) tem uma LISTA de `animators`. `KeyableKt.valueAtTime` calcula o valor dos keyframes no instante e aplica os animadores em ordem (`value = animator.animate(value, env)`). `copyAddingAnimator` e `copyRemovingAnimator` acrescentam ou tiram um animador da lista. Os animadores se acumulam e não substituem os keyframes.

**Registro.** `AnimatorKt.ALL_ANIMATORS` = classes internas de `WaveAnimators` + `ColorAnimators`, 7 no total. As anotações dizem a que propriedade cada um serve (`AnimatorOf`: Location, Rotate, Orientation, Scale, Skew, Color, Gain, Thickness, Opacity, Float, Pivot).

| Id (atributo `type` no XML) | Propriedade | Parâmetros (faixa do controle) | Conta |
| --- | --- | --- | --- |
| `float.sine` | rotação | frequency, magnitude | `v + mag · sin(t · f · 2 · D)` |
| `float.sine.mult` | opacidade | frequency (0,05–5), low (0–1), high (0–1) | `v · (low + (high − low) · (sin(t · f · 2 · D) + 1) / 2)` |
| `v2d.sine` | posição | frequencyX/Y (0,05–5), magnitudeX/Y (0–300) | `v + (magX · sin(t · fX · 2 · D), magY · sin(t · fY · 2 · D))` |
| `v2d.tri` | posição | iguais ao seno | o mesmo, com `triangle(x) = (|((x + 0,75) mod 1) − 0,5| − 0,25) · 4` |
| `v2d.rnd` | posição | frequency, magnitudeX/Y (0–300) | `v + mag · simplexNoise(v.x, v.y, t · f · D, semente 123/456)` |
| `color.sine` | cor sólida | frequência R/G/B (0,05–10), magnitude R/G/B (0–1) | seno por canal, alfa intacto |
| `color.rnd` | cor sólida | frequency, magnitude R/G/B | simplex por canal |

`AnimatorEnvironment` dá `getTime()` (normalizado na duração do elemento, como os keyframes do Alight) e `getDurationInSeconds()`. Logo `t · D` = segundos desde o começo do elemento (inferido do uso conjunto, não de execução).

**Unidade da frequência (inferência).** O argumento do seno é `2 · f · segundos`, **sem π**. Então um ciclo dura `π / f` segundos, e "frequência 1" no Alight equivale a 1/π ≈ 0,318 Hz. O triângulo recebe o mesmo argumento, mas tem período 1: o mesmo número dá velocidades diferentes no seno e no triângulo.

**Arquivo.** `AnimatorSerializerKt` grava `<animator type="v2d.sine" frequencyX=… frequencyY=… magnitudeX=… magnitudeY=…/>`, um atributo por propriedade do construtor, dentro da propriedade animada. Se faltar parâmetro, o projeto é recusado (`MalformedSceneException`).

## O que o After Effects faz (referência de unidades)

Pesquisa de referência pública, sem execução nesta sessão: no AE, a oscilação periódica é escrita como expressão `Math.sin(time * freq * Math.PI * 2) * amp`, com frequência em Hz (ciclos por segundo), e `wiggle(freq, amp)` é o ruído (o painel Wiggler gera keyframes aleatórios). A fase é um deslocamento em graus ou em ciclos. O AUREA já tinha `wiggle` e `loopOut` nas expressões (`engine/src/expr/Expression.cpp`) e o wiggle no animador de camada.

## Decisão de adaptação para o AUREA

O comportamento vem do Alight: animador automático somado em cima dos keyframes, opacidade em pulso multiplicativo low/high, posição X/Y independentes, seno ou triângulo. As unidades vêm do AE: **Hz de verdade** e **fase em graus**, porque a frequência sem π do Alight não é intuitiva (o mesmo número dá velocidades diferentes no seno e no triângulo). Um projeto do Alight convertido usaria `Hz = f / π` no seno e no triângulo.

O que o AUREA acrescenta em relação aos dois:
- rotação e escala também oscilam;
- todo valor da onda aceita keyframe e expressão (por exemplo, a amplitude cresce no tempo);
- no texto, cada letra, palavra ou linha usa o **atraso** do animador, então a onda corre pelas letras;
- a mesma onda vale em camada inteira, texto 2D, texto 3D e forma 3D.

O equivalente do `v2d.rnd` (ruído) é o **wiggle**, que o AUREA já tinha. A cor (`color.sine`/`color.rnd`) **não entrou**; veja Pendências.

Mudança de decisão: `build/reference/am-omega-20261026/DECISOES.md` §06 dizia que "os 7 animadores da referência não entram". O pedido de 07/11 ("pegar tudo… adaptar tudo") reverte isso para o motion. A Onda entra no animador de camada existente, sem tela nova.

## Implementação

| Camada | O que mudou |
| --- | --- |
| Motor | `LayerAnimator` ganha `waveShape` + 12 valores (`Layer.hpp`). `LayerAnimParam` 18..29 (`kWavePosX … kWavePhase`) com faixa própria (`clamp_param`). `wave_value()` = seno `sin(2π·ciclos)` ou triângulo em fase com o seno. O pulso de opacidade é multiplicativo (`Accum::opMul`). A onda usa o relógio por unidade (com atraso), fora do vai e volta do loop. A margem do texto (`glyph_padding`) conta a amplitude da onda. |
| API | `Engine::kLayerAnimFloats` 32 → 48 e `Engine::layer_anim_slot()`: valores da onda em 32..43, forma em 31 (lida no `set_layer_animator` só com a marca 2146 em 30), bits de keyframe da onda em 44/45. Os bits antigos 24/25 ficam iguais. `set_layer_anim_param` e `toggle_layer_anim_key` servem aos ids novos sem mudança. Copiar e colar e desfazer funcionam como para os outros valores. |
| Arquivo | Seção Timeline v57: bloco no fim da camada, um registro por animador. Projetos ≤ v56 abrem sem onda (padrões neutros). |
| Android | `LayerAnimSection.kt`: grupo **Onda**, com dica, chips Seno/Triângulo e 12 réguas com losango de keyframe, expressão e teclado numérico. `AureaEngine.LAYER_ANIM_FLOATS`. A timeline (`animatorRailTrack`) aceita os ids 0..29. |
| iOS | `LayerPanels.swift`: o mesmo grupo, com os mesmos slots, bits e identificadores de acessibilidade (`layer.anim.N.wave.shape`, `layer.anim.N.param.18…29`). `AureaModel.layerAnimFloats`, e `animatorRailTrack` aceita 0..29. |
| Textos | 13 chaves `la_wave*` nos 7 catálogos Android (`tools/i18n_add.py`) e nas 7 tabelas de `AureaStrings.swift`. |

## Validação

Implementação, compilação e validação em aparelho estão registradas separadamente; veja o fim do arquivo.

## Pendências e limites

- **Cor oscilante** (`color.sine`/`color.rnd`): o AUREA não tem uma "cor sólida" única por camada (texto, forma e sólido guardam cor em lugares diferentes). Fica para quando houver um alvo de cor comum.
- **Importar `<animator>` do Alight**: o importador atual (`engine/src/project/AlightMotion.cpp`) só gera presets de efeitos e não lê transform. Converter exige outro tipo de preset (animador de camada) com a regra `Hz = f / π`.
- **Ruído simplex** do `v2d.rnd`: não reproduzido. O wiggle do AUREA (ruído suave por passo, com semente e pausa) cumpre o papel, mas não dá os mesmos números.
- **Curvas** ainda ausentes da lista de 02/11 (elastic-step, steps-random, double bezier, cyclic): não reanalisadas nesta passada.
- Nada aqui comprova a execução do Alight; a referência foi lida estaticamente.

## Registro de validação

| Plataforma | Implementação | Compilação | Validação em execução |
| --- | --- | --- | --- |
| Motor C++ | Feita | Host MSVC Release (`engine/build/host-046`, objetos recompilados do zero) aprovado | `LayerAnim`: 23 testes, 706 verificações, 0 falhas (4 novos da Onda). `Animation`: 42 testes, 0 falhas. `Serializ`: 36 testes, 5 falhas no mesmo teste anterior a esta mudança (ver abaixo). Log: `build/reference/motion-050-host-build.log`. |
| Android | Feita | `:app:compileUiTestKotlin` e `assembleDebug` x86_64 aprovados | JVM `TransformKeyPropertiesTest` 6/6. **Emulador** `Aurea_API35` (1080×2340), APK debug: medidas na tabela abaixo. |
| iOS | Feita (mesmos slots, bits, textos e ids) | Não compilado: depende de Mac/Xcode | Não validado em simulador ou aparelho. |

**Medidas no emulador.** Estrela com animador de camada, Onda Posição X = 200 px, 1 Hz. Escala do palco: 1080 px de composição em 440 px de tela.

| Instante | Forma | Esperado | Medido |
| --- | --- | --- | --- |
| 1,1667 s | Seno | +173,2 px | ≈ +174 px |
| 1,6667 s | Seno | −173,2 px | ≈ −174 px |
| 1,6667 s | Triângulo | −133,3 px | ≈ −132,5 px |
| Desfazer | volta a Seno | −173,2 px | ≈ −174 px |

Capturas em `build/validation/motion-050/` (`02-onda-panel.png`, `03-t125.png`, `04-t175.png`, `05-triangle.png`, `06-undo.png`).

**Falha anterior, fora desta mudança.** `Serialization.InvalidMaterialOverridesAreRejectedWithoutPartialLoad` (`test_serialization.cpp:1594`, de 14/10) espera que um material inválido recuse o projeto. O commit `862ba763` (06/11) passou a trazer o valor de volta à faixa em vez de recusar. O teste ficou desatualizado; não tem relação com a v57.

## Morph de forma e de máscara: as duas lacunas P0 (0.5.0)

Pesquisa de formas, morph e máscaras do Alight Motion 5.0.280, com REA e só leitura. As notas e os IDs de evidência estão em `build/reference/am-shapes-050/` (`NOTES.md`, `evidence.txt`). Principais IDs:
- `ev_39b7f112…`: `KeyableCBKnot`, cada ponto tem os próprios keyframes;
- `ev_e0e82901…`: `valueAtTime`;
- `ev_3f1af3ab…`: efeito experimental Morph Path;
- `ev_b8b09ae0…`: `BlendingMode`.

**Observado no Alight.** O caminho tem a mesma topologia em todos os tempos:
- cada ponto e as duas alças têm keyframes;
- inserir um ponto divide o trecho por De Casteljau, e o ponto novo nasce parado;
- acrescentar na ponta também cria um ponto parado.

Morph entre contagens diferentes não acontece no fluxo normal.

**No AUREA havia duas diferenças:**
1. A máscara com keys de contagens diferentes segurava a forma até o próximo key (`MaskRaster.cpp`). O vetor já reamostrava.
2. Inserir ou apagar um ponto com a forma animada mudava só o key do cabeçote. Isso criava as contagens diferentes do item 1.

**O que mudou (só no motor; Android e iOS editam por `edit_vector_path`/`edit_mask_path`):**
- `mask::evaluate_path`: com contagens diferentes, usa o mesmo `vector::lerp_path`/`resample` do vetor. O key de segurar continua segurando.
- `propagate_path_topology` (`EngineOmegaContent.cpp`): Inserir, Apagar e Acrescentar valem em todos os keys com a mesma contagem.
  - Inserir divide o mesmo trecho no mesmo t, e a forma de cada key não muda.
  - Acrescentar põe o ponto no mesmo lugar em todos os keys.
  - Apagar tira o mesmo índice.
  - Mover ponto, mover alça e canto/curva continuam valendo só no key do cabeçote.
  - Vale no vetor (`edit_vector_path`) e na máscara (`edit_mask_path`). Um passo de desfazer por operação.
- O desenho da máscara com a caneta continua trocando o caminho inteiro (`set_mask_path`), igual nas duas plataformas. Quase sempre é usado sem keys; se houver keys, o morph reamostrado cobre a diferença.

| Plataforma | Implementação | Compilação | Validação |
| --- | --- | --- | --- |
| Motor | Feita | Host MSVC (`host-046`) aprovado | `OmegaContent`: 16 testes, 3 novos aprovados (`MaskMorphsBetweenKeysWithDifferentPointCounts`, `VectorPointEditsKeepTheSameTopologyInEveryKey`, `MaskPointEditsKeepTheSameTopologyInEveryKey`). A falha restante, `DocumentV3KeepsNewFieldsAndV2StillOpens`, espera a versão 3 do documento vetorial, que já está na 4 desde antes desta mudança. `EditorReference` 13/13, `ClipTime` 37/37. Log: `build/reference/p0-host-build.log`. |
| Android | Nenhuma mudança necessária (a edição passa pelo motor) | APK com esta mudança: não gerado | Não validado no emulador. |
| iOS | Nenhuma mudança necessária (a edição passa pelo motor) | Depende de Mac | Não validado. |

Falhas vistas nas suítes de apoio, fora desta mudança:
- `Gpu.MaskBlendModesSurviveSaveAndReopen` grava em caminho relativo (`tests/build/prompt03/…`) que não existe na pasta de onde rodei.
- `Edit`: `TrackText3DParticlesTextSurvivePlayScrubEditExport` (`test_beta_heavy.cpp:218`) exportou 0 quadros. Texto 3D, partículas e export; não investigado.

## Formas novas: Lua e Gota (P1, 0.5.0)

**Referência.** O Alight Motion 5.0.280 tem `assets/shapes/moon.xml` (raio 100, deslocamento 0..1000, padrão 250) e `teardrop.xml` (raio, cauda, aperto, largura da cauda). Os arquivos estão extraídos só para inspeção em `build/reference/am-shapes-050/shapes/`. Nada deles entrou no produto; a geometria é própria.

**No AUREA** as duas são formas de distância com sinal (SDF), escritas igual na CPU (`ShapeGeometry.cpp`) e no shader (`shape.frag`):
- **Lua, tipo 25:** círculo menos o mesmo círculo deslocado. O deslocamento é o parâmetro Profundidade (`kParamDepth`): padrão 0,25, a mesma lua fina do padrão 250/1000 do Alight.
- **Gota, tipo 26:** base redonda embaixo e ponta em cima, unidas pelas tangentes. O Canto (`kParamCorner`, px) arredonda a ponta. Numa caixa mais larga que alta, a Gota vira círculo.
- Os parâmetros já existiam, então o formato do arquivo não muda. Um app antigo desenha os tipos 25 e 26 como retângulo.
- Ladrilhos 33 e 34 do catálogo (`Engine::add_shape`). A Gota nasce em pé (0,72 : 1).
- "Converter em contorno" já funciona para as duas, porque amostra o mesmo `signed_distance`.

**Ficam para a próxima rodada do P1:**
- quadrilátero com 4 cantos livres e cauda do balão como ponto livre (pedem parâmetros de ponto e alças novas no palco);
- ângulo inicial da fatia e do arco (por ora, girar a camada);
- cauda, aperto e largura da cauda da Gota como parâmetros separados.

| Plataforma | Implementação | Compilação | Validação |
| --- | --- | --- | --- |
| Motor + shader | Feita | Host MSVC (`host-046`, objetos que incluem `ShapeGeometry.hpp` recompilados) | `Shape2D`: 5 testes, 378 verificações, 0 falhas, com o novo `MoonAndTeardropHaveTheExpectedGeometry`. GPU do host (Vulkan): `ParametricShapesRenderMatchTheCpu`, ladrilhos 23 a 34, com 0 pixels fora da conta da CPU (Lua: 0 de 5.750; Gota: 0 de 4.126). `OmegaOutline` 4/4. Imagens: `build/validation/p1-shapes/`. |
| Android | Grade de Adicionar › Forma, troca ‹ ›, nome, linha (Deslocamento / Arredondar a ponta), miniaturas | `compileUiTestKotlin` aprovado. JVM `ShapeParamsTest` 4/4 (o seletor tem os tipos 0..26) e `TransformKeyPropertiesTest` 6/6 | Não validado no emulador. GLES vem do mesmo `shape.frag`, compilado no build. |
| iOS | O mesmo (`AddPanelView`, `EditorView`, `EditorLayout`, `LayerPanels`) | Depende de Mac | Não validado. Metal vem do mesmo `shape.frag` na CI. |
| Textos | `sh_shape_moon`, `sh_shape_teardrop`, `shp_offset`, `shp_tip_radius` nos 7 idiomas (Android e `AureaStrings.swift`) | — | `i18n_check`: só os 6 avisos que já existiam (`ai_suggest_3`, `fxt_white_…`). |

As outras lacunas da pesquisa seguem abertas: P1 (parâmetros de ponto, alças por forma) e P2/P3 (keyframe por ponto, morph para outra camada, abrir no ponto). A lista completa está em `build/reference/am-shapes-050/NOTES.md`.

**Observado no aparelho, também fora desta mudança.** Na linha "Semente do wiggle", o rótulo aparece espremido, com letras quebradas em linhas.
