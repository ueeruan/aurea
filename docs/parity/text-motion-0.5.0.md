# Animação de texto: Alight Motion 5.0.280 → AUREA 0.5.0

Data: 08/11/2026. Pedido: trocar a animação de texto do AUREA pela do Motion, nas duas plataformas, mantendo a interface do AUREA. Este documento descreve comportamento. O código é próprio; nenhum código, shader ou asset da referência entrou no produto.

## Evidência

- APK `build/reference/alight-motion-5.0.280/reference.apk`, SHA-256 `4caeb6de…bf4159`. REA (JADX headless 0.7.1) na classe `SceneElementType`: `ev_156bcbefbcad4e7fa8a22acd66fc92f2db952bf8c842cd2ed1f146e308ca90c6`.
- Corpo do render em `build/reference/am-ui-20261014/jadx-out/.../scene/SceneElementType.java`, l. 876-1239.
- Definições e presets extraídos para `build/reference/am-text-050/`: `fx__text-transform.xml`, `fx__textprogress.xml`, `fx__textrand.xml`, `fx__text-spacing.xml`, `effects__presets__text-presets.xml`.

Tudo isso é leitura estática; não houve execução do Motion.

## Como o Motion anima texto (observado)

Não existe animador separado. A animação de texto são **efeitos** na camada: Text Transform (`texttransform`, nativo) e os três com script JS (`textprogress`, `textrand`, mais `textspacing`). Presets são receitas de parâmetros com keyframes em cima deles.

**Unidades.** Os contornos do texto são agrupados em letra, palavra ou linha. Só o que tem tinta conta como unidade; espaço não ocupa janela.

**Janelas.** Com N unidades e O = 1 + overlap: D = N + 2·O − 1, largura w = O/D, unidade em `t = (ordem + O)/D + fase`.

| Forma | Peso p |
| --- | --- |
| Quadrado | `min(clamp((t−início)/w), clamp((fim−t)/w))`; com início ≥ fim, `1 − max(...)` |
| Suave | gaussiana no meio da faixa: `exp(−((u−½)·2π)²/2)`, com `u` pela posição `t + w/2` |
| Triângulo | `u < ½ ? 2u : 2(1−u)` |

**Ease.** `CubicBezier(max(in/2,0), max(−in/2,0), 1−max(out/2,0), 1−max(−out/2,0))` sobre p.

**Aplicação.** Posição + deslocamento·p; escala X = 1 + s·p; escala Y = 1 + s·p + esticar·p; rotação + ângulo·p. Âncora na camada ou no centro da unidade. Opacidade: alpha < 0 soma `alpha·p`; alpha ≥ 0 subtrai `(1−p)·alpha`, ou seja, apaga o que está **fora** da faixa. Cor: mistura com p.

**Progresso.** Fatia o texto em `round(n·início)..round(n·fim)` e acrescenta um cursor. O cursor pisca só com o corte parado em relação ao quadro anterior.

**Aleatório.** Em `start..end`, troca cada caractere cujo ruído fica abaixo de `amount`; o caractere novo vem de um conjunto, escolhido por `evo`. Pode manter espaços.

**Espaçamento.** Letter spacing em em; line spacing multiplica.

## O que estava errado no AUREA

O `aurea.text.transform` tinha os mesmos parâmetros, com outra matemática: células iguais de 100/N, fase movendo a faixa em vez das unidades, suave por cosseno multiplicado, ease por potência, alpha positivo clareando o que está dentro, esticar multiplicando a escala e espaços contando como unidade. A conta foi trocada pela de cima (`transform_influence`, `engine/src/text/TextTransform.cpp`).

**Projetos já salvos** com esse efeito passam a usar a conta nova: com faixa parcial, o movimento muda. Com a faixa inteira (0..100 %), o peso continua 1.

## O que entrou

| Peça | Mudança |
| --- | --- |
| Motor | Conta do Motion no Transformar texto. Efeitos novos `aurea.text.progress`, `aurea.text.randomizer` e `aurea.text.spacing`, avaliados antes do layout. `Engine::apply_motion_text_preset` com 10 receitas em unidades do AUREA; substitui a animação de texto anterior em um desfazer. |
| Android | `TextAnimSection.kt`: presets do Motion, linha "Efeitos de texto", Explodir mantido. A lista de animadores antigos só aparece se a camada já tiver algum. Recursos no modelo do AE (Animador, Pro, importar `.ffx`) ficam no fim. Catálogo e seletor de efeitos com os três novos. |
| iOS | `LayerPanels.swift`: o mesmo painel, com ponte `applyMotionTextPreset`, os mesmos ids de acessibilidade, catálogo e seletor. |
| Textos | 14 chaves + 27 rótulos de efeito nos 7 idiomas. |

Diferenças deliberadas em relação ao Motion:
- A rotação com âncora na unidade tem `+ 1` grau fixo no código do Motion. Parece defeito; não foi reproduzido.
- A ordem aleatória usa sorteio próprio; a sequência de um mesmo `seed` não é igual à do Motion.
- O ruído do Aleatório é próprio; o caractere sorteado não é o mesmo do Motion.

## Validação

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita | Host MSVC Release (`engine/build/host-046`, objetos refeitos) aprovado | `TextTransform` 11 testes, `TextContent` 3, `SharedText3D` 5, `I18n` 8, `LayerAnim` 23: 0 falhas. Log: `build/reference/motion-050-host-text2.log`. |
| Android | Feita | `assembleDebug` x86_64 aprovado | Emulador `Aurea_API35`: painel novo; "Digitar" em 00:00:05 mostra `AURE_` (esperado: 4 de 12 letras + cursor); "Subir por letra" sobe letra a letra. Capturas em `build/validation/motion-050/text/`. |
| iOS | Feita | Não compilado: depende de Mac/Xcode | Não validado em simulador ou aparelho. |

Falhas fora desta mudança, sem investigar a causa: `Scene3DImageBlurGpu`, `ExportSwapTexture` (falham em `write_png` e `set_effect_image`), `OriginalModelExport`, `test_beta_heavy:218`.

## Pendências

- A grade de presets em Adicionar › Texto (`TextPresetPicker`/`TextPresetStripView`) ainda usa os presets antigos.
- O componente **Contorno** do Text Transform não existe no AUREA: o texto 2D é desenhado por glifo e não separa os contornos de uma letra.
- Contador e Timecode do Motion não viraram presets; o AUREA os tem como texto dinâmico.
- O importador de projetos do Motion ainda não lê os efeitos de texto.
- Só "Digitar" e "Subir por letra" foram vistos no emulador; os outros 8 presets têm só teste de motor.

# Alças das formas no palco (0.5.0)

Referência: `assets/shapes/*.xml` e `ShapeElementKt` no APK do Motion, registrados em `build/reference/am-shapes-050/evidence.md`. No Motion, cada forma viva declara alças de raio, ângulo, ponto e eixo.

O motor passa a dizer onde cada alça fica e como o arrasto vira valor (`shape::param_handles`, `shape::value_from_handle`, `Engine::query_shape_param_handles`, `Engine::drag_shape_param_handle`). As telas só desenham e mandam o dedo. A gravação usa o caminho de `set_shape_param`: respeita keyframes, e um arrasto é um desfazer.

| Formas | Alças |
| --- | --- |
| Retângulo | raio |
| Estrela, cruz, anel, trapézio, paralelogramo | raio interno ou espessura |
| Fatia | ângulo |
| Arco | ângulo e espessura |
| Flor, coração, selo, lua | profundidade |
| Seta | ponta e haste |
| Seta dupla | ponta e espessura |
| Engrenagem | dente e cubo |
| Linha | espessura |
| Losango, raio | ponta |
| Balão | ponta e canto |
| Onda | amplitude e espessura |
| Gota | ponta |
| Círculo, polígono, triângulo, blob | só a caixa, como antes |

Como o AUREA usa os parâmetros que já tinha, as alças seguem esses parâmetros, e não os do Motion: a estrela não tem alça de ângulo, por exemplo. Polígono e estrela no Motion têm também rotação própria; aqui é a rotação da camada.

Quando uma alça cairia sobre uma de tamanho, a tela desenha a alça afastada; o arrasto continua partindo da posição real.

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita | Host aprovado | 3 testes novos: todas as alças de 21 formas em 3 proporções devolvem o próprio valor; arrasto, keyframe e desfazer. |
| Android | Feita | `assembleDebug` x86_64 aprovado | Emulador: estrela 45 % → 84 % e Desfazer; fatia 270° → 180° e Desfazer. Capturas em `build/validation/motion-050/handles/`. |
| iOS | Feita | Não compilado: depende de Mac/Xcode | Não validado. |

Falha fora desta mudança: `OmegaContent` em `test_omega_content.cpp:241` espera a versão 3 do documento vetorial, que já é a 4 (`VectorDocument.cpp:37`).

# Converter em contorno exato (0.5.0)

No Motion, a forma viva gera o caminho bézier a partir dos parâmetros, com número de pontos fixo por forma (observado em `assets/shapes/*.xml`). No AUREA, só retângulo e elipse saíam exatos; o resto era traçado numa grade, e o número de pontos mudava com o tamanho, o que estraga o morph ponto a ponto.

`exact_outline` (`engine/src/engine/EngineOmegaOutline.cpp`) agora gera a curva de cada forma com retas, arcos ou curvas analíticas: o número de pontos depende só da forma e dos parâmetros. Por exemplo, estrela = 2N, polígono = N, cruz = 12, seta = 7, raio = 7.

Continuam no traçado antigo os casos em que a topologia muda:
- flor com profundidade acima de 0,5 (pétalas soltas);
- número de pontas fracionário no meio de uma animação;
- seta dupla com pontas cruzadas;
- balão com a cauda fora do trecho reto;
- onda com a borda dobrando sobre si (o padrão da onda, com 6 ondas e fita grossa, cai aqui).

A gota vira círculo quando a ponta cabe dentro da base (caixa baixa), então seu número de pontos muda com a proporção.

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita (a regra é só do motor) | Host aprovado | `OmegaOutline`: 6 testes, 0 falhas. 23 formas em 4 caixas: mesma silhueta da forma viva e o mesmo número de pontos em qualquer tamanho. |
| Android | Sem mudança de tela | `assembleDebug` x86_64 aprovado | Emulador: estrela convertida com 10 pontos. Captura em `build/validation/motion-050/outline/`. |
| iOS | Sem mudança de tela | Não compilado: depende de Mac/Xcode | Não validado. |

Falhas antigas, fora desta mudança: `test_omega_content.cpp:241` e `test_editing_tools.cpp:218` esperam a versão 3 do documento vetorial, que já é a 4.

Visto no emulador e ainda não corrigido: no Editar pontos, os marcadores dos pontos têm cerca de 12 dp e cobrem formas pequenas. No Motion, `knotRadius` é 4,5 dp e `curveHandleRadius` 3,75 dp.

# Keyframe por ponto e correções (0.5.0)

**Keyframe por ponto.** No Motion, cada ponto tem a própria trilha. No AUREA, `PathKey::points` diz quais pontos o key anima (vazio = todos); os outros passam pelo instante como se o key não existisse (`path_with_point_keys`, `engine/src/vector/Vector.cpp`). `Engine::toggle_vector_path_points_key` liga e desliga. Editar um ponto num key com máscara marca só esse ponto. Inserir e apagar ponto levam a máscara junto. Documento vetorial v5 (Android lê e regrava; iOS edita pelo motor).

Na tela: com pontos escolhidos no laço, o ◆ do modo keyframes age só neles; sem laço, na forma inteira. Máscaras de camada (`MaskPathKey`) ainda não têm keyframe por ponto.

**Grade de presets.** Adicionar › Presets e o painel Presets mostram os 10 presets do modelo Motion no texto 2D. Os antigos ficam só para texto 3D, porque os novos dependem de efeitos de texto 2D.

**Correções.**
- Marcadores do Editar pontos: 4,5 dp (ponto) e 3,75 dp (alça), como o Motion.
- Rótulo "Semente do wiggle" em linha própria, nas duas plataformas.
- Testes que estavam vermelhos: versão do documento vetorial; material inválido (o leitor conserta, não recusa); PNG em pasta inexistente; export com modelo Original (desde a 0.1.8 sai como a prévia e avisa); `BetaHeavy` (captura cancelada por edição concorrente não é falha; parado, a captura sai).

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor | Feita | Host aprovado | `OmegaContent` 20, `Vector` 45, `OmegaOutline` 6, `Serializ` 36, `TextContent` 3, `LayerAnim` 23, `OriginalModel` 6, `BetaHeavy` 2: 0 falhas nas execuções feitas. Sem rodada completa da suíte depois da última mudança. |
| Android | Feita | `assembleDebug` x86_64 aprovado | Emulador: grade com os presets novos e aplicar; marcadores novos; laço + ◆ cria o key. Capturas em `build/validation/motion-050/final/` e `outline/`. |
| iOS | Feita | Não compilado: depende de Mac/Xcode | Não validado. |

Observação do `BetaHeavy`: com edição contínua em outra thread, 0 das 240 capturas completam, todas canceladas. O teste agora aceita isso, mas é um limite real de captura durante edição.

## Pendências da edição de formas

- Formas de pontos livres do Motion: triângulo, quadrilátero e pentágono livres; selo perfurado; balão com cauda 2D; arco e fatia com início e fim independentes; seta e linha por 2 pontos.
- Biblioteca de cerca de 35 silhuetas vetoriais (com desenhos próprios).
- Indicação visual de quais pontos têm key.
- Componente Contorno do Transformar texto.

# Abrir no trecho, primeiro ponto e keyframe por ponto na máscara (0.5.0)

**Abrir no trecho (Motion).** Com um trecho escolhido, "Abrir caminho" corta nele. A ordem dos pontos gira para o corte virar as pontas, na forma parada e em todos os keys (`PathOp::OpenAt`, vetor e máscara).

**Primeiro ponto (After Effects, Set First Vertex).** Em caminho fechado, a célula "1" gira a ordem só no key do cabeçote, o que muda com quem cada ponto casa no morph (`PathOp::SetFirst`). O primeiro ponto aparece maior no palco.

**Keyframe por ponto na máscara.** `MaskPathKey::pointMask`, gravado na seção v58 da timeline. Com pontos escolhidos no laço, o ◆ marca só eles (`toggle_mask_path_points_key`).

# Organização dos painéis no modelo do Motion (0.5.0)

**Evidência (REA/layouts do APK 5.0.280):** `animator_list_item`, `animator_list_add`, `animator_settings_panel`, `effect_setting_static_section`, `list_panel_item_height` 37 dp, `list_panel_padding` 6,5 dp. O que veio do Motion é a organização: item de 37 dp com seta, ícone do tipo e nome; seção que abre e fecha com interruptor no título; último item "+ Adicionar". O desenho continua o do AUREA, sem asset do Motion.

**O que mudou (kit em `PanelList.kt` e `LayerPanels.swift`):**
- Animador do texto: animadores, animações prontas, efeitos de texto e explodir em seções; um único "+ Adicionar" com todos os tipos; cada tipo com ícone.
- Estilo e Layout do texto: Contorno, Fundo, Sombra, Espaçamento, Caixa e Texto no caminho em seções; alinhamento com ícones; nenhuma opção cortada na borda.
- Máscara: abas Máscaras, Pontos e Recorte (sem nome truncado); lista de máscaras com o modo à direita; a aberta se divide em Editar pontos, Modo, Borda e Seguir objeto.
- Animadores da camada: um por linha; aberto, em Geral, Força e atraso, Começa de, Tremor e Onda.
- Réguas dentro das seções: rótulo em cima e régua com a largura toda. Lado a lado com rótulo e valor sobravam cerca de 60 dp e não dava para arrastar.

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor (abrir, primeiro ponto, máscara por ponto) | Feita | Host aprovado | `OmegaContent` 23, `Vector` 45, `Serializ` 36, `Tools` 5: 0 falhas. Suíte completa não rodada. |
| Android (painéis) | Feita | `assembleDebug` x86_64 aprovado | Emulador: Animador, Estilo, Layout, Máscara e Animadores da camada conferidos por captura; primeiro ponto e abrir no trecho exercitados por toque em máscara; arrasto da régua alterou o valor. |
| iOS (painéis e as três funções) | Feita | Não compilado: depende de Mac/Xcode | Não validado. |
