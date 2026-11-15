# AureaHYPER Adaptive Rendering

## Bancada de 15/11/2026

A investigação usa o motor C++ do workspace, em Release, com MSVC e Vulkan na NVIDIA GeForce RTX 3050. A compilação inicial confirmou que o executável estava atualizado. Os resultados brutos ficam em `build/validation/hyper-adaptive-20261115/`. O manifesto `baseline-source.json` identifica o HEAD e os hashes dos arquivos centrais antes das mudanças.

Os tempos de GPU vêm de timestamp queries. Os testes de módulos usam o menor tempo válido depois do aquecimento. O benchmark de composição informa suas médias e percentis separadamente. Os tempos por passe incluem a instrumentação detalhada. Os resultados desta bancada descrevem o host.

Nenhum aparelho Android estava conectado na consulta inicial do ADB. A compilação Apple e a execução em aparelhos são verificações separadas.

## Arquitetura encontrada

O AureaHYPER existente reúne restauração, upscale, limpeza temporal, interpolação e estabilização. O preview de seus efeitos usa os passes Turbo. O job da Home também possui caminhos neurais Lite e Max. O compositor e o renderer 3D são compartilhados com o restante do editor.

| Pedido | Componentes encontrados na análise |
|---|---|
| Render graph | `FrameGraph`, dependências de recursos, poda, vidas, aliasing, pool transitório, barreiras e recuperação por segmentos |
| Resolução reduzida | Pirâmide de bloom e pré-passe de profundidade, GTAO, SSR e neblina a meia resolução |
| Reconstrução temporal | Fluxo bidirecional do Hyper e vetores de motion blur 3D. Reconstrução de preview com profundidade e histórico ainda exige integração própria |
| Shader fusion | `EffectGraph`, até 12 operações de cor por passe e transformações incorporadas ao compositor |
| Cache | Quadros compostos, fontes, máscaras, fluxo, modelos, ambientes, pré-render lossless em disco e pré-render por grupo |
| GPUs tile-based | Load ops, descarte de profundidade, anexos MSAA transitórios e resolve no fim do passe |
| Qualidade adaptativa | `AdaptiveResolutionController`, política térmica, pressão de memória, `PreviewQuality` e `HeavyQuality` |
| Pipelines | Aquecimento por `ShaderLibrary`, fingerprint dos shaders e caches dos backends |
| PBR | Materiais metal/roughness, IBL, PCF/PCSS, GTAO, SSR, chão reflexivo e neblina volumétrica |
| Exportação | `finalQuality`, renderer privado de export, obturador e agendamento por índice absoluto |

## Gargalos medidos

O teste `FullHdCostPerModulePrints` encontrou o maior custo no estimador de movimento por blocos do Hyper. Os cinco preditores vizinhos repetiam candidatos em áreas sobrepostas. Cada avaliação repetida relia a janela de 25 amostras. O fluxo bidirecional também reconstruía a mesma pirâmide de luma para cada sentido.

| Cenário a 1080p | Baseline |
|---|---|
| Hyper Clean com 3 quadros | +17,886 ms de GPU |
| Hyper Clean com 5 quadros | +35,184 ms de GPU |
| Busca de movimento no Clean com 5 quadros | 27,481 ms |
| Hyper Motion com cálculo do par | +17,385 ms de GPU |
| Hyper Motion com par em cache | +2,054 ms de GPU |
| Composição com 50 camadas e efeitos | 300 passes e 23,29 ms de GPU |

Os valores adicionais dos módulos são a diferença para a cena de controle sem o módulo.

## Etapa 1

O shader de busca reconhece os candidatos já avaliados pelos preditores anteriores. A primeira avaliação define a ordem de desempate por comparação estrita.

| Métrica | Antes | Etapa 1 |
|---|---|---|
| Busca no Clean com 5 quadros | 27,481 ms | 19,450 ms |
| Busca no Motion | 13,874 ms | 9,544 ms |
| Custo adicional do Clean com 5 quadros | 35,184 ms | 27,949 ms |
| Custo adicional do Motion | 17,385 ms | 13,295 ms |

São reduções de aproximadamente 29% e 31% nos passes de busca desta rodada. A suíte `HyperGpu` aprovou sete testes e 176 verificações. Doze quadros de exportação RGBA32F, incluindo bordas temporais, movimento, ruído e corte de cena, foram comparados com as capturas anteriores. Todos ficaram idênticos bit a bit. Os resultados estão em `stage1-HyperGpu.log` e `stage1-frame-comparison.json`.

## Etapa 2

O fluxo bidirecional passou a compartilhar a pirâmide de luma entre os dois sentidos. O FrameGraph recebeu medição opcional da gravação de CPU por passe e consulta de tráfego estimado e conjunto de recursos usados. A consulta distingue recursos repetidos, profundidade descartada e anexos MSAA resolvidos.

A estimativa conta o tamanho integral de cada recurso declarado uma vez por direção. O tráfego físico de DRAM também depende da cobertura dos desenhos, da quantidade de amostras do shader, da compressão e dos caches do hardware. A estimativa e as timestamp queries são dados distintos.

O custo da pirâmide no Motion passou de 0,193 ms na baseline para 0,097 ms nesta rodada. O custo adicional total do Motion foi 12,371 ms, frente a 17,385 ms na baseline. A suíte FrameGraph aprovou 33 testes e 56255 verificações. A suíte HyperGpu aprovou sete testes e 176 verificações. Os doze quadros de exportação voltaram a coincidir bit a bit com a baseline.

## Etapa 3

A análise de movimento do preview usa blocos de 16, 32 ou 64 pixels conforme a escala de fluxo. A pirâmide de luminância e o warp final preservam sua densidade. As chaves de cache incluem a configuração de análise. SSR e neblina usam limites graduais de passos. O caminho de exportação mantém a análise integral e a configuração solicitada.

Android fornece memória disponível e pressão pelo ActivityManager. iOS fornece o headroom de memória pelo sistema. A política compartilhada atualiza a admissão mesmo quando já existe um benchmark em cache. Pressão crítica pode antecipar o cooldown. Uma espera de decode isolada preserva a resolução de renderização.

Na rodada da etapa 4, Motion com análise a meia densidade mediu 6,664 ms e a um quarto, 4,210 ms. Clean de cinco quadros mediu 14,136 ms e 8,718 ms, respectivamente. Motion integral mediu 12,497 ms, frente aos 17,385 ms iniciais. São cenas e configurações específicas desta bancada.

A composição de 50 camadas oscilou de 23,29 ms iniciais para 28,46 ms em outra rodada. Essa cena não recebeu uma alteração algorítmica específica. A variação impede atribuir um ganho global ao editor com estes resultados.

## Etapa 4

O preview 3D ganhou reconstrução temporal com oito posições Halton, reprojeção da câmera, teste de profundidade reversa, validação de histórico, clamp de vizinhança e resposta à mudança de luminância e alpha. O alpha corrente é preservado. Seek, saltos de quadro, revisão de conteúdo e mudança de qualidade invalidam o histórico.

A implementação atende um único grupo 3D raiz com geometria estática opaca durante reprodução em resolução reduzida. Skinning, partículas, planos com textura de camada, motion blur, DOF e cenas compostas incompatíveis usam o caminho espacial. Vetores temporais de objetos móveis e deformações permanecem pendentes.

O histórico usa RGBA16F e profundidade R32F em ping-pong, com 24 bytes por pixel. A admissão considera os formatos reais e limita o orçamento a uma fração da memória disponível. A publicação depende da conclusão do grafo. Recursos antigos são liberados depois do uso pela GPU.

A suíte HyperAdaptive aprovou nove testes e 2314721 verificações. Os doze quadros RGBA32F de exportação coincidiram bit a bit com a baseline nas etapas 1 a 4. Os testes exercitam rejeição de profundidade, transparência, invalidação, limites de memória e o caminho de exportação.

## Precisão e caches

As tentativas de exportação preservam tamanho, FPS, codec solicitado, upscale e quantidade de amostras. Os caminhos de recuperação cobrem backend compatível e codificação por software. Uma configuração impossível retorna erro explícito. O checkpoint inclui uma revisão para impedir reuso de tentativas antigas com qualidade reduzida.

O lote de amostras limita memória em cenas 3D raiz compatíveis. Cenas mistas podem retornar BudgetExceeded quando as amostras exatas ultrapassam o orçamento. O batching geral para essas cenas fica pendente.

O cache Vulkan marca pipelines criados, grava por substituição atômica e conserva o estado dirty se a publicação falhar. O teste de falha injetada aprovou a repetição da gravação. O teste de cache incremental aprovou oito verificações.

## Instrumentação

Cada passe pode registrar CPU de gravação, GPU por timestamps, conjunto de recursos e bytes estimados de leitura e escrita. Os bytes representam o footprint integral declarado dos recursos. A largura de banda física requer contadores específicos de hardware. As capturas também registram memória GPU usada e reservada.

Os resultados brutos, comparações de frames e TSVs permanecem em `build/validation/hyper-adaptive-20261115/`. A validação em GPU mobile física ainda precisa ser realizada.

## Referências de comportamento

A seleção de load/store e a redução de acessos externos seguem as recomendações para renderização em tiles. [Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html)

A reconstrução temporal precisa registrar as coordenadas de movimento, profundidade, invalidação do histórico e regiões reativas. A documentação de FSR 2 e MetalFX fornece os contratos de entrada usados como referência. [AMD GPUOpen](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/), [Apple MetalFX](https://developer.apple.com/documentation/metalfx/mtlfxtemporalscalerbase)

Os caches de pipelines dependem da compatibilidade dos dados com o dispositivo e o driver. [Especificação Vulkan](https://docs.vulkan.org/spec/latest/chapters/pipelines.html)
