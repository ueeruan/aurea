# Renderização 3D e preview em tempo real — 10/10/2026

Pedido: melhorar a renderização 3D e otimizar o preview. O workspace já continha alterações anteriores, inclusive as descritas em `OPTIMIZACOES_TEXTO_3D_2026-10-10.md`; esta rodada preserva esse trabalho e descreve somente suas novas mudanças.

## Implementação

- **Motor C++ compartilhado:** cada `GpuModel` prepara, durante o upload, listas compactas dos nós com geometria e dos nós com morph targets. A preparação de morphs, sombras e desenhos passa a percorrer essas listas, em vez de varrer toda a hierarquia em cada instância e quadro. Nós de juntas e pais continuam disponíveis na pose; nós fora da cena selecionada, índices inválidos e malhas vazias ficam fora das listas. A liberação e o próximo upload reconstroem o estado.
- **Recursos por quadro:** cada asset de GPU é consultado uma vez por construção de cena; instâncias repetidas, morphs, sombras e cor reutilizam o resultado. O array de referências usa a arena do quadro. Os parâmetros, transformações, materiais e poses de cada instância continuam independentes.
- **Normais:** a matriz normal é calculada uma vez por nó que efetivamente desenha e copiada para suas primitivas. O deslocamento das juntas continua atribuído separadamente a cada desenho. Nós espelhados conservam a orientação das faces.
- **Amostras de câmera:** a consulta dos blocos de câmera usa as oito entradas locais já disponíveis no motor antes de recorrer a hashing, reduzindo trabalho de pequenas cenas com motion blur.
- **Android/Kotlin:** corrigido o agendamento duplicado de `RenderLoop`. Quando `updateIdle()` chamava `wake()` dentro do callback, ele postava um sucessor e o final do callback postava outro. Agora há somente o post do final do callback. Acordar externamente continua substituindo o callback atrasado pelo próximo vsync. Dois testes cobrem a retomada interna, a retomada externa e a parada durante um callback.
- **iOS/Swift:** a view pausa seu `CADisplayLink` auxiliar quando a composição está pausada. Comandos, conclusão de recursos, preparação ociosa e refinamento continuam dirigidos pela thread do motor compartilhado, com seus próprios pedidos e prazos. Durante playback o link volta a operar.

Não foram alterados os shaders, a resolução escolhida, a quantidade de amostras de exportação, o LOD ou os filtros de sombra. Esta rodada reduz trabalho repetido na CPU. As listas acrescentam armazenamento de índices por asset, compartilhado pelas suas instâncias; não são cópias de geometria nem um cache por quadro.

## Medição antes/depois

Mesma máquina Windows, compilação Release, backend simulado e mesma cena: 32.768 nós, uma malha, oito instâncias e uma luz direcional com sombra; composição 1280×720. O benchmark executa 160 quadros e mede a mediana da segunda metade. O caso exercita uma hierarquia grande com pouca geometria, portanto favorece a eliminação das varreduras; não representa todas as cenas.

| Etapa medida na CPU | Antes | Depois |
| --- | ---: | ---: |
| Gravação do quadro pelo renderer, incluindo construção dos passes 3D | 1,4665 ms | 0,0106 ms |
| Preparação da composição/pose | 0,0078 ms | 0,0058 ms |
| Alocações por quadro na thread medida | 71 | 73 |

A gravação ficou aproximadamente 138 vezes mais rápida **neste cenário e nesta etapa**. Não é aceleração da GPU, FPS medido no celular ou melhora equivalente do app inteiro. A diferença pequena de preparação não é apresentada como ganho independente; o benchmark também não demonstrou redução de alocações totais. A ausência de varreduras é o ganho principal.

Logs: `build/validation/realtime3d-20261010/baseline-benchmark.log` e `final-benchmark.log`. O teste também verifica primitivas visíveis e existência do mapa de sombra em preview e qualidade final.

## Compilação

- **C++ host Release:** compilado com sucesso, incluindo os testes finais.
- **Android:** Kotlin e app nativo `uiTest` x86_64 compilados; APK de instrumentação compilado. 393 testes unitários passaram, sem falhas, erros ou testes ignorados. Não houve compilação ARM nesta rodada.
- **iOS:** implementado em Swift e no motor compartilhado. Auditorias de API Swift/ponte ObjC e projeto Xcode sem problemas. A regra de layout foi conferida estaticamente. **Não compilado:** este host Windows não possui Xcode/SDK iOS; a auditoria não comprova compilação nativa.

## Validação em execução

- **Motor host:** sete testes `Optimization`, 72 `Scene3D`, 51 `Text3D`, 35 `MotionBlur` e 21 `Perf8C`, todos sem falhas. Os filtros podem selecionar testes em comum e não devem ser somados como um total de casos únicos. Os grupos cobrem poses, sombras, morphs, skin, materiais, reflexos, orientação espelhada, motion blur e preview adaptativo.
- **Emulador Android API 35:** seis testes finais aprovados no app isolado `com.aurea.aurea.uitest`: preview pausado/preparação futura, câmera com imagem e vídeo, importação no fim da composição/cena glTF selecionada/iluminação direcional, textura e bevel de texto 3D após reabertura, e cadeias de efeitos em imagem/texto/texto extrudado. As verificações de modelo e texto usam pixels capturados pelo motor; o teste de preview pausado inspeciona a imagem apresentada.
- **Ocorrência inicial preservada:** a primeira rodada dos seis testes teve uma falha de captura no cenário de importação 3D. O log registrou timeout de GPU e recriação dos recursos. O cenário passou isoladamente e o conjunto completo passou na repetição (`android-model-isolated.log` e `android-device-confirm.log`). A causa do timeout não foi estabelecida; não se presume ausência de falhas transitórias em aparelhos físicos.
- **Celulares físicos e iOS:** não executados. O emulador usa SwiftShader e não mede a GPU nem o comportamento térmico de um celular. A implementação iOS ainda precisa de compilação e execução em Mac/iPhone/iPad.

Os logs, snapshots dos arquivos antes da rodada e resultados estão em `build/validation/realtime3d-20261010/`. A suíte geral do projeto não foi reexecutada nesta rodada; as pendências anteriores documentadas no relatório de otimizações de texto não foram declaradas resolvidas.

## Referências de comportamento

Reutilização de recursos e redução de trabalho repetido seguem os princípios de [Apple — Persistent Objects](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/PersistentObjects.html), [Apple — Triple Buffering](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/TripleBuffering.html) e [Android — Rendering](https://developer.android.com/topic/performance/rendering/). Nenhum código, shader ou asset extraído de outro aplicativo foi incorporado.
