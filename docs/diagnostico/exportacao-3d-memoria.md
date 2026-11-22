# Exportação 3D com memória limitada

## Onde está o custo

A contagem de objetos não indica sozinha se a cena cabe. Objetos que compartilham malha e material podem usar a mesma geometria. Uma cena com poucas malhas exclusivas, texturas 4K, sombras, pós-processamento e motion blur pode ocupar muito mais memória. Por exemplo, uma cor RGBA16F e uma profundidade de 32 bits somam cerca de 24 MiB em 1080p e 95 MiB em 4K por par de buffers. Esses números ainda excluem MSAA, sombras, texturas, buffers de leitura para o encoder e intermediários de efeitos.

| Técnica pesquisada | Aplicação no Aurea |
| --- | --- |
| Limite dinâmico de memória e ocupação real | A admissão de quadros já considera a ocupação da GPU. O exportador agora usa a sua fatia de memória também para dimensionar os lotes de exposição 3D. O orçamento de driver muda durante a execução e uma alocação pode falhar mesmo abaixo da estimativa. [Khronos Vulkan memory budget](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html) |
| Dividir o trabalho por dependências e liberar intermediários | O exportador reaproveita o caminho de cenas em etapas. Uma cena 3D elegível termina mantendo o resultado RGBA16F para a composição e liberando os intermediários. Aplicar recortes fixos a toda a imagem exigiria validar sombras, blur, reflexos, bloom e efeitos que amostram regiões distantes para evitar emendas. [Khronos sobre GPUs tile-based](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html) · [Apple sobre load e store de attachments](https://developer.apple.com/documentation/metal/setting-load-and-store-actions/) |
| Instancing com recuperação | Objetos opacos e sombras equivalentes continuam sendo agrupados na ordem original. A engine escreve matrizes diretamente no buffer da GPU e compacta as listas em uso. Se o buffer opcional falhar, desenha os objetos individualmente com seus próprios transforms. [Vulkan vkCmdDrawIndexed](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexed.html) |
| Controlar fila e tempo de GPU | O aviso de memória retorna sem esperar pelo driver. O produtor drena os quadros e consulta a conclusão dos envios normais e auxiliares com prazo e cancelamento antes de liberar caches. O encoder conserva a posse dos quadros que já recebeu. [MediaCodec](https://developer.android.com/reference/android/media/MediaCodec) · [AVAssetWriterInput](https://developer.apple.com/documentation/avfoundation/avassetwriterinput/requestmediadatawhenready%28on%3Ausing%3A%29) |
| Respeitar os limites do processo e do encoder | O sistema pode encerrar um processo por pressão de memória. O encoder também tem limites próprios de dimensão e taxa. A recuperação de memória usa o mesmo alvo e tenta de novo o quadro antes de declarar a falha. [Android LMK](https://developer.android.com/games/optimize/vitals/lmk) · [Apple jetsam](https://developer.apple.com/documentation/xcode/identifying-high-memory-use-with-jetsam-event-reports) · [MediaCodec VideoCapabilities](https://developer.android.com/reference/android/media/MediaCodecInfo.VideoCapabilities) |

## Comportamento automático

1. O orçamento determina quantas amostras do obturador ficam em cada lote elegível. A composição mantém os instantes, a ordem e os pesos originais.
2. Se faltarem recursos, a engine espera os envios que usaram os buffers, libera temporários e tenta o mesmo quadro. A próxima tentativa reduz a exposição 3D elegível a um instante por lote e libera caches e quadros de mídia pré-carregados que podem ser refeitos.
3. Se a pressão persistir, a engine pode processar separadamente as cenas 3D sem motion blur que não dependem de planos, partículas ou texturas de outras camadas. A composição reutiliza os resultados completos das etapas.
4. Se ainda não couber ou a GPU não responder no prazo, o exportador devolve o erro em vez de entregar o quadro incompleto por falta de memória. Projetos que cabem somente com menos conteúdo ou um formato diferente ainda estão limitados pelo aparelho.

O caminho em lotes preserva as amostras dentro da elegibilidade atual do renderizador. Composições aninhadas com motion blur, cenas que leem outras camadas e muitos assets exclusivos exigem cobertura própria. Um teste com 1.200 nós em uma única malha compartilhada mede um caso geométrico concreto e deve ser interpretado dentro desse escopo.

## Validação Android

- Emulador Android x86_64 com orçamento de 128 MiB configurado no Aurea. Esse valor é um cenário de política de memória do motor, sem equivalência direta com a RAM ou o driver de um aparelho de entrada.
- A cena glTF com 1.200 nós e motion blur exportou 36 quadros em H.264 a 960 × 540 e 30 fps. Um decodificador Android independente confirmou 36 quadros visualmente diferentes, com os timestamps esperados. A sessão não sinalizou omissão de elementos, redução de blur, resolução limitada ou substituição de modelos.
- No Vulkan, o aviso de memória chegou após o primeiro quadro. No segundo, o motor registrou 24 texturas liberadas e uma queda de 157.778 KiB no uso de GPU rastreado. A exportação final levou 7,27 s do início do acompanhamento até a confirmação do último quadro. A compilação preliminar levou 6,68 s no mesmo cenário. Duas medições isoladas não demonstram aceleração.
- Com OpenGL ES forçado no mesmo emulador, os 36 quadros também passaram na verificação independente. Foram 39,93 s com o encoder de software e 111.136 KiB liberados na GPU após o aviso.
- A comparação dos MP4 decodificados, alinhada por quadro, mediu PSNR médio de 49,79 dB entre as duas compilações. Os MP4 têm compressão com perdas. O teste de regressão de exportação também passou com áudio, áudio em pré-composições com remapeamento de tempo, partículas e texto 3D, GLB, FBX e OBJ.
- O teste longo exportou dois minutos com áudio e confirmou os 3.600 quadros e seus timestamps. A fixture de projeto legado abriu e exportou sem modificar o arquivo de origem.
- No teste controlado de alocação, separar a cena e os efeitos reduziu o pico estimado de 365,8 MiB para 295,5 MiB, cerca de 19%. Com teto de 330 MiB, a renderização em etapas concluiu e a execução do grafo inteiro excedeu o orçamento. A sombra permaneceu em 4096 px.
- A comparação de pixels antes da compressão no Vulkan mostrou erro RGBA máximo zero em quatro casos de exposição em lotes. A falha injetada no buffer de instâncias manteve os desenhos originais. Uma alocação de texto que falhou uma vez foi recuperada e o quadro chegou ao encoder. A falha persistente resultou em erro de orçamento sem codificar o quadro. Um envio auxiliar bloqueado retornou timeout dentro do teste, sem chamar a espera global da GPU.
- O iOS usa o mesmo código C++ nesta mudança. Compilação com Xcode e validação em hardware Apple continuam pendentes.
