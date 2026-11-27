# Exportação com Vulkan, OpenGL ES e Metal

26/11/2026

A auditoria acompanhou a renderização fora da tela, a sincronização GPU–CPU, a leitura de texturas, a fila de quadros e a entrega ao codificador. O Android usa Vulkan ou OpenGL ES. O iOS usa Metal e compartilha o fluxo de composição e exportação em C++.

## Evidência dos aparelhos

Os 58 registros no resumo de exportação incluem 31 crashes nativos, seis crashes sem classificação nativa, 16 encerramentos por memória, quatro ANRs e uma saída anormal. O primeiro quadro de cada pilha nativa aponta para `vulkan.adreno.so` em 20 registros, `vulkan.sp9863a.so` em nove e `libGLES_mali.so` em dois. Os dois registros Mali estão em threads distintas. Uma pilha aponta para o local da queda. A causa de cada queda exige o caminho de comandos e os recursos ativos no momento.

O Vulkan já mantém uma proteção por pipeline e identidade do driver para quedas repetidas durante compilação ou execução de compute. O teste existente dessa proteção confirmou que duas mortes com a mesma chave bloqueiam o pipeline correspondente e que outros pipelines continuam disponíveis. O desligamento por falta de memória tem tratamento separado.

## Correções aplicadas

| Problema | Mudança no Aurea |
| --- | --- |
| Visibilidade da memória no Vulkan | A leitura imediata agora registra a barreira de escrita por transferência para leitura pelo host. A espera pelo fence e a invalidação da memória não coerente continuam no fluxo. A falha de mapeamento passa a retornar ao chamador. |
| Formato, amostragem e tamanho das linhas | Vulkan e Metal verificam se a textura de cor pode ser copiada, além de rejeitar um stride menor que a linha. OpenGL ES valida dimensões e stride e explicita o estado de empacotamento de pixels. |
| Leitura assíncrona no OpenGL ES | A capacidade do PBO só é contabilizada depois de uma alocação bem-sucedida. A leitura só fica pendente após `glReadPixels` completar sem erro. Uma falha de mapeamento ou invalidação em `glUnmapBuffer` interrompe a entrega do quadro. |
| Pressão de memória no OpenGL ES | A memória temporária de upload e leitura e o espelho de buffers na CPU passam a usar alocações que retornam erro ao motor. As transferências são verificadas contra a faixa de endereçamento antes da alocação. Isso importa em especial para processos de 32 bits. |
| Contexto EGL perdido ou fence ausente | A perda de contexto fica registrada até a reinicialização. O fim de um quadro sem fence retorna falha. Quando a GPU está indisponível, o encerramento destrói o contexto antes de executar as liberações adiadas. |
| Tempo de apresentação no codificador Android por Surface | Os dois caminhos de Surface passam a verificar se o EGL aceitou o timestamp antes de publicar o quadro. O caminho direto aguarda o término do uso da textura importada, inclusive quando a apresentação falha. |
| Leitura no iOS | O Metal rejeita textura ou stride incompatível e propaga a ausência do ponteiro do buffer como erro. |

## Proteções existentes conferidas

- O export principal trabalha fora da tela. A fila limita quadros em trânsito, ordena a entrega, verifica cancelamento e exige a conclusão de todos os quadros. O export reduz o paralelismo sob pressão, mantendo as dimensões e as amostras de motion blur pedidas.
- O Vulkan consulta extensões, formatos e orçamento de memória do dispositivo. O orçamento do driver é uma estimativa. A disponibilidade de alocação também depende da memória do sistema e do espaço de endereçamento.
- A cópia assíncrona NV12 do Vulkan já tinha a barreira GPU–CPU. O ajuste desta auditoria cobre a leitura imediata usada na captura e na exportação de imagem.
- A exportação principal do Android usa o caminho portátil. O EGL da Surface do codificador é uma rota distinta, sujeita à configuração do encoder e às limitações do driver. Os testes de MP4 desta rodada usaram libopenh264.
- Falha de leitura de mídia de origem pode ocorrer antes da codificação. O acervo permite classificar a queda, mas não vincula o relato E5-14-2 a uma chamada Vulkan ou GLES específica.

## Validação

| Ambiente | Resultado |
| --- | --- |
| Android ARM64, API 35, Vulkan 1.3 no emulador | Compilação e instalação concluídas. Sete testes passaram. |
| Android ARM64, API 35, OpenGL ES 3.1 no mesmo emulador | Sete testes passaram. O log do motor confirmou a seleção de OpenGL ES em execução. |
| Android armeabi-v7a | As seis unidades C++ alteradas foram compiladas com o NDK. |
| iOS | A verificação corrente da API Swift, da ponte Objective-C e das chaves de texto apontou zero problemas. A compilação com Xcode e o teste em iPhone ainda estão pendentes. |

Os testes dos dois backends Android incluíram MP4 com vídeo importado, efeito e áudio, exportação sem prévia, cancelamento seguido de nova tentativa de MP4 e PNG, 3D com partículas e motion blur e a correção de borda do Deslocamento Turbulento. A cena com 1.200 objetos glTF entregou os 36 quadros em 960 × 540 a 30 fps após um aviso simulado de memória. A decodificação verificou os timestamps e as dimensões da saída.

A emulação confirma o fluxo portátil do aplicativo. A rejeição de timestamp em uma Surface MediaCodec e as assinaturas nativas dos aparelhos Adreno, Unisoc e Mali ainda dependem de reprodução física. Os resultados aqui são funcionais, sem medição comparativa de desempenho.

## Fontes primárias

- Barreira para leitura pela CPU, fence e invalidação de memória mapeada. [Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/synchronization_examples.html) · [Vulkan Memory Allocation](https://docs.vulkan.org/spec/latest/chapters/memory.html)
- Propagação de dispositivo perdido e espera de submissões. [Vulkan Devices and Queues](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html) · [Vulkan Synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
- Orçamento de memória, consulta de capacidades e validação no Android. [VK_EXT_memory_budget](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html) · [Android Vulkan](https://developer.android.com/games/develop/vulkan/overview) · [Camadas de validação no NDK](https://developer.android.com/ndk/guides/graphics/validation-layer)
- Formatos de leitura, framebuffer, armazenamento de pixels e descarte do conteúdo de buffer. [OpenGL ES 3.1](https://registry.khronos.org/OpenGL/specs/es/3.1/es_spec_3.1.pdf) · [EXT_color_buffer_float](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_color_buffer_float.txt)
- Perda de contexto e timestamp da Surface do encoder. [EGL 1.5](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf) · [EGL_ANDROID_presentation_time](https://registry.khronos.org/EGL/extensions/ANDROID/EGL_ANDROID_presentation_time.txt) · [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
- Cópia de pixels por command buffer no iOS. [Apple Metal](https://developer.apple.com/documentation/metal/reading-pixel-data-from-a-drawable-texture)
