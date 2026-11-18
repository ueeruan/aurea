# Flow Retime em filmagem real (1.0.3)

Pedido: o Flow Retime / Flow Motion Blur / rampas de velocidade tinham de ficar
lisos em gente, jogo e cena de filme, não só em anime. Este arquivo registra o
que mudou, o que foi medido e o que falta comprovar em aparelho.

## O que mudou no motor (C++ + GLSL compartilhados)

| Item | Arquivos | Estado padrão |
|---|---|---|
| P0-1 mistura pela confiança, sem cair na dissolução; controle "Fantasma ↔ Deformação" (parâmetro 6 do Flow Retime) | `shaders/video/flow_warp.frag`, `FlowTimeEffects.cpp`, `Renderer.cpp` (`flow_interpolate`) | ligado (50%) |
| P0-2 guia de cor (Y'CbCr) na suavização, no preenchimento de oclusão, na subida para a grade fina (joint bilateral + escolha suave) e regularização do nível base | `flow_common.glsl`, `flow_luma.frag` (modo crominância), `flow_smooth.frag`, `flow_fill.frag`, `flow_refine.frag` | ligado |
| P1-3 DIS (busca inversa por bloco 8×8 sobreposto, densificação, refinamento variacional brilho + gradiente + suavidade guiada pela cor), implementação própria a partir de Kroeger et al. ECCV 2016 | `flow_dis_patch.frag`, `flow_dis_dense.frag`, `flow_var.frag` | ligado nas 3 qualidades |
| P1-4 duas hipóteses por lado ordenadas por oclusão (forma "gather" do softmax splatting: o ponto fixo do próprio vetor e o do maior vetor da vizinhança, peso exp(Z) com Z = erro fotométrico e de ida e volta) + teste de pixel parado (HUD) | `flow_warp.frag`, `flow_tile.frag` (lê BA) | ligado |
| P1-5 contexto no tempo (previsão do par anterior como partida do DIS e filtro recursivo) | `flow_temporal.frag`, `FlowCache::ptsA/ptsB` | **desligado** (ver medida) |
| P2-6 "Neural (IA)" (RIFE v4.6, MIT, já embutido) no Flow Retime: export síncrono, prévia parada assíncrona, quadro montado na GPU | `hyper/neural/NeuralRetime.*`, `NeuralMotion.cpp` (`fields_rgb`), `flow_neural_warp.frag`, `Renderer::neural_interpolate` | opção 4 da qualidade |
| P2-7 cenas não rígidas em cor com verdade analítica | `tests/SyntheticVideo.hpp` (`LiveKind`), `tests/test_flow_live_action.cpp` | — |

`AUREA_FLOW_ABLATE` (só medida) desliga partes; `375` reproduz o algoritmo de
antes da 1.0.3 (conferido: mesmos números do executável antigo, ±0,02 dB).

## Medida (host, Vulkan, 1080p, 4 instantes por cena: 4,25 / 4,5 / 4,75 / 5,25)

PSNR (dB) só na região que muda entre os dois quadros da fonte ("PSNR-mov").
Referências fixas: mistura 21,6 dB de média, mais próximo ~19 dB.

| Cena | Rápida antes → 1.0.3 | Boa antes → 1.0.3 | Melhor antes → 1.0.3 | Neural |
|---|---|---|---|---|
| membro articulado | 23,82 → 26,45 | 23,11 → 25,70 | 22,95 → 25,23 | 27,00 |
| rosto girando | 35,92 → 35,28 | 35,70 → 36,38 | 35,10 → 36,34 | 33,23 |
| borrão na fonte | 29,45 → 30,28 | 29,00 → 29,45 | 28,58 → 30,32 | 31,93 |
| braço ocluindo | 24,04 → 25,54 | 23,72 → 25,77 | 23,05 → 25,48 | 28,17 |
| jogo + HUD | 37,16 → 38,38 | 37,99 → 38,14 | 36,90 → 38,16 | 35,16 |
| **média** | **30,08 → 31,19** | **29,91 → 31,09** | **29,31 → 31,11** | 31,10 |

Passo a passo (média PSNR-mov; Rápida / Boa / Melhor; configuração
intermediária, antes do ajuste final de Boa e da busca):
antes 30,08 / 29,91 / 29,31 → P0-1 30,49 / 30,27 / 29,85 → +P0-2 30,77 /
30,25 / 29,72 → +P1-3 31,09 / 30,53 / 30,79 → +P1-4 31,19 / 30,63 / 30,94 →
+P1-5 30,81 / 30,93 / 30,92.

Decisões tomadas pela medida (contrárias ao plano):
* Base do Melhor em 768 px: perdeu para 384 em todas as cenas (768: 24,6 /
  35,3 / 29,5 / 23,1 / 28,0*; 384: 25,2 / 36,5 / 30,2 / 24,7 / 38,0). O
  Melhor ficou em 384 com blocos mais densos e mais iterações. (*antes do
  teste de pixel parado.)
* Busca em dois níveis: perdeu para um nível (troca o vetor certo por outro
  de bloco parecido em textura que se repete). Desligada; a busca de um nível
  continua sempre ligada no export.
* Contexto no tempo: não melhorou a média nem a cintilação medida; ajuda o HUD
  e atrapalha braço com oclusão, e deixa o quadro depender da ordem dos pares
  (export em segmentos paralelos). Desligado.
* Neural: melhor em membro, borrão e oclusão; pior em rotação e no HUD parado.
  Fica como opção, não como padrão.

Custo no host (tempo de parede do quadro 1080p com o fluxo recalculado, menos
o da mistura, mediana das 5 cenas; GPU do host compartilhada, medida ruidosa):
Rápida 2 → 9 ms, Boa 1,5 → 10 ms, Melhor 5 → 14 ms. Neural: ~200 ms a mais por
quadro (RIFE a 1/2 resolução no ncnn Vulkan + NV12→RGB na CPU).

## Plataformas

* Android/Kotlin e iOS/Swift: a interface é a genérica de parâmetros; mudaram
  textos (7 idiomas), tabelas de tradução por identidade, dicas e a lista de
  parâmetros principais (`EffectsHuman`).
* Shaders: SPIR-V (glslc), ESSL 3.10 e MSL (tradutor `engine/tools/metal-shaders`)
  gerados sem erro para os 16 shaders de fluxo.
* Neural no iOS: ncnn na CPU. Próximo passo: Core ML (converter o flownet do
  RIFE v4.6 para `.mlpackage` fp16 e trocar o `NetRunner` no Apple), porque a
  CPU do iPhone leva segundos por quadro em 1080p.

## Falta comprovar em aparelho

* Custo real por quadro no celular (DIS + 2 hipóteses), principalmente na
  prévia com Rápida em GLES.
* Neural: memória e tempo no Android (Vulkan) e no iOS (CPU); a prévia parada
  pede um quadro novo quando a rede termina (`set_neural_ready_callback`).
* Vídeo real (as cenas são sintéticas, com ruído de sensor e 4:2:0).
