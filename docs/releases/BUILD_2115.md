# Aurea 2115

## Implementação

- Preview preenche a área disponível (crop somente na tela; exportação mantém a composição). Altura maior e menu + flutuante com oito categorias e diálogo central. Botão de marcador no transporte.
- Legendas abrem mesmo sem seleção compatível e permitem selecionar uma fonte de vídeo/áudio. Texto comum não é tratado como fonte de fala.
- Nomes visíveis dos presets de texto convertidos para nomes genéricos, preservando IDs.
- Deep Glow: composição de luz premultiplicada, mapeamento correto da imagem expandida, aproximação óptica por seis Gaussianas em escalas logarítmicas, exposição e suavidade do limite. Não se afirma equivalência com o algoritmo proprietário.
- Halo óptico reduzido e somas intermediárias dimensionadas pelos detalhes necessários, evitando seis somas em resolução cheia. Revalidados transparência/exposição, limites e parâmetros extremos na GPU.
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
- Primeiro workflow iOS detectou acesso direto ao setter privado de seleção; corrigido para usar `model.select` e manter o painel de legendas aberto.
- Execução nativa `36244222048`: compilação Release e Debug passou, cinco cenas capturadas com renderização iniciada e sem erro do motor. Sete de nove testes de UI passaram, incluindo aplicar/remover Deep Glow e gestos com desfazer.
- As duas falhas foram identificadas nos dados do teste: a cena de vídeo não recebia os arquivos de decoder após reinstalar o app; o teste de adicionar/desfazer exigia geometria da camada já removida. O probe confirmou uma camada restante, seleção vazia e refazer disponível. Corrigidos preparo da cena e leitura do probe, preservando a verificação de desfazer com um toque.
- Atualizada a contagem esperada de shaders Metal de 70 para 71, incluindo Hotspots. Adicionado teste nativo de abertura de legendas com uma forma selecionada.

## Entrega validada

- Workflow [36245685197](https://github.com/ueeruan/aurea/actions/runs/36245685197), revisão `0475d30bf4c7e0439ef45878e1677563c5100927`: ambos os jobs concluíram com sucesso.
- Dez testes nativos de UI passaram, zero falhas (185,299 s). Incluem abertura de legendas com seleção incompatível, aplicar/remover Deep Glow, menu flutuante com preview estável e desfazer, movimento de vídeo pela seta mantendo duração, gestos, texto 3D e timeline.
- Cinco cenas do simulador capturadas com motor iniciado, zero falhas de shaders e renderização `ok`. Os 71 shaders foram verificados pelo compilador Metal da Apple.
- IPA ARM64 sem assinatura: `build/releases/2115/aurea-2115-unsigned.ipa`, 15.684.749 bytes. Verificador do pacote passou no workflow e após download local.
- SHA-256: `38089b4ad28331a1bdfa6087b35c46109c75ee2bda5c8879b820cd1fa71b9927`.

## Limites de aceitação

Não há comparação pixel a pixel com Deep Glow, S_Hotspots, Turbulent Displace ou Pixel Sorter no AE. Fluidez, áudio e estabilidade no iPhone/Samsung físicos continuam pendentes de reteste. As implementações e testes locais não comprovam identidade com os plugins proprietários.
