# Investigação: estabilidade 3D e criação manual de motion

Data: 6 de setembro de 2026. Implementação local; não publicada.

## O que a pesquisa permite afirmar

- [Node Video: início rápido](https://nodevideo.com/guide/quick-start): prévia em 50% por padrão, ajuste de resolução/FPS e cache de reprodução. A exportação usa qualidade integral. O documento também descreve edição de parâmetros com keyframes e curvas.
- [Node Video: importação 3D](https://nodevideo.com/guide/custommodel.html): importa OBJ/FBX e animações FBX; alerta explicitamente que modelos de qualidade excessiva podem não ser importáveis em celulares. Não documenta um mecanismo de capacidade ilimitada nem disponibiliza seu renderizador interno.
- [Node Video: prefetch](https://www.nodevideo.com/guide/use-prefetch-to-edit-fast.html): pré-decodifica vídeo para armazenamento. A própria documentação distingue melhora da decodificação de melhora da renderização. Prefetch não resolve, por si só, sombras, skinning e efeitos caros.
- [CapCut no web.dev](https://web.dev/case-studies/capcut): confirma que a maior parte do motor é C++ e descreve Emscripten, WebAssembly/SIMD e WebCodecs. Não comprova a lista de backends gráficos de cada versão móvel citada no pedido.
- [Flutter: Impeller](https://docs.flutter.dev/perf/impeller): o Flutter já possui um motor nativo, com Metal/Vulkan e suporte dependente da plataforma. A aplicação usa `flutter_scene 0.20.0`, cuja API local fornece buffers atualizáveis e escala interna de renderização.
- [Apple: memória em Metal](https://developer.apple.com/documentation/metal/reducing-the-memory-footprint-of-metal-apps): reduzir e inspecionar os recursos gráficos é essencial. Usar C++ não elimina limites de memória nem o custo de alocar novos recursos por quadro.

## Gargalos encontrados no código e alterações

| Área | Antes | Agora |
| --- | --- | --- |
| Timeline | `Column` instanciava todas as linhas e miniaturas; miniaturas não acompanhavam a rolagem vertical | Listas virtuais com altura fixa e rolagem sincronizada |
| Consultas de projeto | Busca linear de camada/vínculo e repetição da busca por solo/duração | Índices e resultados calculados uma vez por projeto imutável |
| Modelos animados | Tempo fazia parte da assinatura; nós, materiais e buffers eram reconstruídos a cada quadro | Geometria atualizável; fluxos de vértices/normais/UV reutilizam buffers; índices só são substituídos quando necessário |
| Materiais importados | Material recriado a cada avaliação de pose | Material reutilizado por asset e índice |
| Instâncias | Alterar posição/tamanho com a mesma quantidade não atualizava os dados | Invalidação por lista de instâncias e tamanho |
| Luzes | Componentes reconstruídos mesmo sem mudanças | Reuso enquanto luzes e intensidades avaliadas não mudam |
| Texturas decodificadas | Cache sem orçamento e várias decodificações simultâneas | LRU de 64 MiB, uma decodificação por vez, cancelamento por geração e limpeza sob pressão de memória |
| Uploads | Readbacks RGBA e mipmaps concorrentes | Fila de uploads compartilhada entre views; clone de imagem mantém o upload válido durante uma expulsão do cache |
| Ciclo de vida GPU | Retenção de texturas e callbacks após sair da cena | Referências removidas; cargas antigas de panorama/textura não repovoam cenas descartadas |
| Prévia 3D GPU | Renderizava no tamanho lógico da composição | Lado maior limitado a 720 px durante reprodução/gesto e 1080 px em pausa; exportação preserva escala 1 |
| Efeitos com captura | Até 2160 px mesmo durante playback | Teto de 1080 px em playback; restaura 2160 px na pausa, respeitando o cálculo existente da tela |
| Estúdio 3D | Renderização, navegação e seleção fixadas no instante zero; arrasto alterava valores-base | Relógio, régua, playback, auto-key, keyframes, curvas e edição de mover/girar/escalar no instante escolhido |
| Criação 2D | Era necessário armar a primeira chave separadamente | Controle Auto no painel de transformação grava posição, escala, rotação, inclinação, pivô e opacidade |

A primeira edição com auto-key em um instante posterior preserva a pose inicial em zero. As alterações usam as trilhas e a serialização já existentes, com undo/redo. O Estúdio seleciona a câmera ativa da tomada; o arrasto de objetos converte deslocamentos de tela para o espaço do pai. Vistas ortográficas utilizam o renderizador compatível, em vez de serem tratadas como perspectiva pelo adaptador GPU.

## Como usar

1. Abra uma camada Cena 3D. Arraste a régua inferior até o instante desejado.
2. Toque no objeto, ou no nome ao lado dos controles para selecionar um objeto/câmera na lista.
3. Escolha Mover, Girar ou Escalar e arraste o objeto. Auto-key vem ligado no Estúdio.
4. O diamante adiciona/remove uma pose; os botões vizinhos navegam entre chaves. O botão de curva escolhe a interpolação do segmento atual.
5. Em camadas 2D, abra Mover e transformação e ative Auto antes de alterar a pose em outro instante.

## Validação e limites

- Resultado final local: `flutter analyze --no-pub` sem ocorrências; `flutter test --no-pub` com **1.207 testes aprovados**. Log em `tmp/motion-performance-tests.log`.
- Testes específicos verificam orçamento e invalidação do cache, interpolação, preservação de poses, tempos locais, undo, movimento sob parentesco e resolução integral de exportação.
- Teste de widget monta **3.000 camadas** e verifica que apenas as linhas próximas são construídas, inclusive após rolar até a camada 1.000. Verifica também sincronização com as miniaturas.
- Teste do Estúdio navega até 2 segundos e arrasta uma rotação real, comprovando que zero permanece intacto. Testes existentes cobrem importação, poses, serialização, câmeras e composição.
- Windows não permite executar o renderizador Metal em um iPhone. Testes Flutter usam o fallback de renderização; não medem reutilização de buffers no driver nem demonstram ausência de jetsam/crash nativo.
- A avaliação de skinning/morph e a montagem dos atributos continuam em Dart. A mudança reduz alocação GPU, mas não equivale a migrar o skinning para shaders. Não há aumento dos limites de importação nem suporte ilimitado a geometria.
- O orçamento de 64 MiB é do cache de imagens decodificadas, **não de toda a memória do app/GPU**. Geometria, texturas GPU referenciadas por materiais, sombras e render targets também consomem memória.
- Virtualizar 3.000 camadas na timeline não promete renderizar 3.000 camadas ativas com efeitos pesados a 60 FPS.

Para concluir a investigação do fechamento relatado: reproduzir o mesmo projeto no iPhone em profile/release; coletar relatório de crash/jetsam, pico de memória e captura Metal; comparar primeira importação, loop de 60 s, navegação repetida entre editor/Estúdio e exportação. Repetir no Android físico com Vulkan e no fallback suportado. A [documentação de profiling do Flutter](https://docs.flutter.dev/perf/ui-performance) recomenda aparelhos físicos em profile para conclusões de desempenho.
