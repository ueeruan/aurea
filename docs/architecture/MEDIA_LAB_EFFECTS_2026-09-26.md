# Cinco efeitos nativos — revisão local

Pedido: JPEG Glitch, Signal 1.2.3, Tracery 1.1, Deep Glow 2 e Shadow Studio 3 no Aurea Android/iOS. Os cinco arquivos fornecidos foram abertos com a senha informada. A inspeção ficou em `build/effects-packages`, fora dos assets do produto. Nenhum `.aex`, instalador, biblioteca ou preset proprietário foi incorporado ao aplicativo ou executado.

## Implementação

- **JPEG Glitch** (`aurea.glitch.jpeg_codec`): transformação DCT separável em blocos de 8×8, quantização, alteração de coeficientes, edição/randomização da tabela, subamostragem de cor e reconstrução. Quatro passes de GPU. Mantém o alfa original e permite misturar com a imagem inicial. Não modifica bytes de um fluxo JPEG/Huffman; a corrupção é no domínio dos coeficientes. O tamanho dos blocos usa a resolução de processamento atual.
- **Signal Analog** (`aurea.glitch.analog_signal`): modulação e demodulação Y/I/Q em raster intermediário estável, alternância de croma PAL, ruído, banda de cor, fase, crosstalk, head switching, roll, scanlines e dropouts. Tempo em segundos e semente explícita; Freeze independe do frame. Dois passes. O efeito Signal antigo mantém seu ID e seus parâmetros.
- **Tracery** (`aurea.generate.tracery`): detecção por cor/alfa, propagação de rótulos conectados e compactação por prefix scan na GPU. Caixas, preenchimento, cantos, marcadores, índices/coordenadas, setas e conexões sequential/star/full/MST. Sem leitura síncrona de pixels pela CPU. Limite de 64 regiões e de 16 no modo Full Mesh; grade de detecção 64/96/128. São regiões reavaliadas por frame, sem identidade temporal persistente. Componentes muito finos/pequenos podem se perder na redução; propagação limitada a 32 iterações.
- **Deep Glow 2** (`aurea.light.deep_glow_2`): limiar com transição suave, viés de saturação, halos normalizados em escalas logarítmicas, distribuição/cor por canal, aspecto, tintas interna/externa, exposição, Add/Screen, seis opções de tone mapping e visualização da entrada/halo. Ainda não há Iris por imagem nem Lens Dirt por textura. O Deep Glow anterior permanece disponível e inalterado.
- **Shadow Studio 3** (`aurea.light.shadow_studio_3`): sombra Drop/Long/Radial, luz pontual, direção inversa, sombra interna, penumbra, cores inicial/final, cor da origem, falloff, spread, ruído e saída isolada. Amostras limitadas por qualidade. É uma projeção 2D do alfa; não é um traçador físico de uma cena 3D. Não importa os rigs, expressions e todos os presets `.ffx` do pacote.

Os novos IDs são independentes. Parâmetros, keyframes e persistência seguem o motor comum; as duas interfaces oferecem os mesmos controles principais e avançados. Não há promessa de igualdade pixel a pixel com os plugins desktop.

## Referências primárias

- [JPEG Glitch — autor/distribuidor](https://aescripts.com/jpeg-glitch/): compressão e corrupção em estágios do JPEG.
- [Signal — autor/distribuidor](https://aescripts.com/signal/): transmissão analógica modulada e decodificada.
- [Tracery — autor/distribuidor](https://aescripts.com/tracery/): detecção de regiões por cor, caixas, marcadores e conexões. A página atual também apresenta v2; o pacote fornecido é v1.1.
- [Deep Glow 2 — notas do desenvolvedor](https://www.plugineverything.com/releases): halos exponenciais, Iris, tone mapping, tintas e texturas.
- [Shadow Studio 3 — desenvolvedor](https://www.plugineverything.com/shadow-studio-3): sombras, luz, gradientes e pipeline RGBA.

## Verificação

GLSL compilado; os seis shaders novos foram traduzidos para Metal 2.1/iOS e OpenGL ES 3.1. Essa tradução não equivale a compilação Apple nem execução em iPhone/Samsung. `build/effects-packages/tests-final.log`: **7 testes, 100.187 verificações, zero falhas**. A bateria cobre render Vulkan, transparência, compressão, cor, regiões, salvamento, keyframes e desfazer. `performance.csv` mede 720p no computador, com leitura da GPU: JPEG Glitch 37,62 ms, Signal Analog 24,35 ms, Deep Glow 2 29,83 ms, Shadow Studio 3 32,67 ms e Tracery 27,93 ms por quadro (médias). Esses valores não são FPS de aparelho móvel.

Os samplers de passes com estágios dinâmicos são ligados explicitamente em todos os estágios. O teste iOS `testMediaLabEffectsReachNativeRendererAndUndo` foi adicionado para catálogo, aplicação nativa, captura e desfazer. A execução Apple e os pacotes finais serão registrados em `docs/releases/BUILD_2123.md`.

Vídeo real de teste já importado no emulador: `Tracking-Teste-20s.mp4` (TUM xyz, 20 s, convertido para H.264). A importação foi feita no projeto que estava aberto. O emulador foi devolvido ao usuário; nenhuma bateria conectada deve usá-lo enquanto estiver testando.
