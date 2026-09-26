# Aurea 2115

## Implementação

- Preview preenche a área disponível (crop somente na tela; exportação mantém a composição). Altura maior e menu + flutuante com oito categorias e diálogo central. Botão de marcador no transporte.
- Legendas abrem mesmo sem seleção compatível e permitem selecionar uma fonte de vídeo/áudio. Texto comum não é tratado como fonte de fala.
- Nomes visíveis dos presets de texto convertidos para nomes genéricos, preservando IDs.
- Deep Glow: composição de luz premultiplicada, mapeamento correto da imagem expandida, aproximação óptica por seis Gaussianas em escalas logarítmicas, exposição e suavidade do limite. Não se afirma equivalência com o algoritmo proprietário.
- Lens Blur: raio em unidades da textura de entrada e integração premultiplicada.
- Pixel Sort: ordenação real em intervalos delimitados por limiares, com blocos de até 64 amostras; não é uma implementação de todos os modos do AE Pixel Sorter.
- Turbulência: evolução em graus controlada por keyframes, deslocamento do domínio do ruído, mistura e fixação de bordas. A distribuição de ruído não foi comparada com AE.
- Hotspots: seleção por limite e cor, brilho, saturação e desfoque de entrada. Implementação independente inspirada nos controles documentados de S_Hotspots.
- Reverb, Flanger e Echo: envios paralelos com resposta finita, leitura fracionária e cache de blocos sem alocação no callback. Resultado independente da divisão em callbacks e do seek. Os controles não são animáveis nesta versão; caudas respeitam o fim da camada.
- Luzes flutuantes: emissor 3D em caixa, discos suaves, cores durante a vida e partículas secundárias distribuídas pela trajetória. Preset 13, sem renumerar os anteriores.

## Referências

- https://www.plugineverything.com/deep-glow
- https://aescripts.com/pixel-sorter/
- https://borisfx.com/documentation/optics-2026/Optics%202026.5/Filters-S_Hotspots.html
- https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/distort-effects.html
- https://youtu.be/CFLJmhXjkOQ (análise automatizada: Particular, emissor Box e Scattered Trail)

## Validação local

- Android `compileReleaseKotlin`: passou.
- Contratos Swift/ponte, projeto Xcode, tipos de parâmetros e recursos compartilhados: passaram.
- GPU: halo óptico, exposição, ordenação de pixels, Hotspots, determinismo/seek/reabertura dos quatro presets Particle World, limites das camadas: passaram.
- Áudio: 17 testes passaram, incluindo comparação entre callbacks completos e fragmentados nos três efeitos; teste adicional de integração adicionar/desfazer no snapshot passou.
- Texto: 60 testes passaram. Presets: 14 testes passaram. EffectGraph: 13 testes passaram.
- Lens Blur: preservação de energia em bordas transparentes passou.
- Varredura GPU de 184 testes: 183 passaram; o teste de catálogo classificava áudio como efeito visual. Corrigido para exigir pixels inalterados nos três efeitos de áudio e reexecutado com sucesso.
- Primeiro workflow iOS detectou acesso direto ao setter privado de seleção; corrigido para usar `model.select` e manter o painel de legendas aberto. A validação do IPA e do simulador será registrada após a nova execução.

## Limites de aceitação

Não há comparação pixel a pixel com Deep Glow, S_Hotspots, Turbulent Displace ou Pixel Sorter no AE. Fluidez, áudio e estabilidade no iPhone/Samsung físicos continuam pendentes de reteste. As implementações e testes locais não comprovam identidade com os plugins proprietários.
