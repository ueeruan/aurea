# Auditoria e migração do renderer 3D — 06/09/2026

## Estado e critério de conclusão

Esta migração está em validação, não concluída nem habilitada na distribuição de produção. Filament é o backend escolhido para o protótipo integrado. A chave de build `--dart-define=AUREA_FILAMENT=true` habilita o caminho em avaliação; o restante continua no caminho de compatibilidade. Não há afirmação de 60 FPS em Android/iOS, nem benchmark comparativo entre todos os motores.

## Auditoria do app

O app é Flutter/Dart. `Scene3DGpu` traduz o domínio para `flutter_scene 0.20.0`, sobre Flutter GPU/Impeller. O renderer alternativo `Scene3DPainter` chama `renderScene` na pintura, fazendo transformação, iluminação, clipping e ordenação de triângulos em Dart. O custo ocorre no caminho da interface. A disponibilidade do primeiro pode encaminhar o aparelho para o segundo. A identificação de plataforma não prova qual caminho um testador executou; precisamos do diagnóstico do aparelho.

O adaptador GPU atual também avalia modelos e monta buffers na sincronização feita durante build. Texturas passam por imagem decodificada, readback RGBA e upload. Há cache e instancing, mas o teto de preview é fixo, sem feedback temporal. A interface do widget depende do motor concreto. A exportação captura a composição Flutter; trocar para uma textura nativa sem um contrato de captura não garante os mesmos pixels no arquivo final.

Baseline executado: `flutter test test/ferramenta_custo_cena3d_test.dart`. Windows, teste Flutter, compositor CPU, 1280 × 720, cinco amostras após aquecimento. São tempos de trabalho CPU/gravação de comandos, não FPS apresentados nem tempo de GPU.

| Cena | Triângulos visíveis | Geometria média | Pintura completa média |
| --- | ---: | ---: | ---: |
| DERIVA sem modelo | 715 | 23 ms | 19 ms |
| DERIVA com modelo | 5.938 | 14 ms | 40 ms |
| MONOLITO sem modelos | 3.306 | 22 ms | 27 ms |
| MONOLITO com modelos | 10.449 | 37 ms | 71 ms |
| DERIVA em rascunho | 5.938 | 13 ms | 15 ms |
| MONOLITO em rascunho | 10.449 | 34 ms | 71 ms |

Isso identifica um gargalo estrutural do fallback. Não identifica sozinho a causa do encerramento no iPhone: memória, driver e histórico do crash ainda precisam ser medidos. Não é válido multiplicar esses resultados por um fator para prever um celular.

## Comparação técnica

| Alternativa | Android / iOS | Adequação ao Aurea e custos |
| --- | --- | --- |
| Filament | Vulkan e GLES / Metal | Renderer PBR embutível, glTF, culling e instancing; controle nativo de resolução dinâmica. Exige adaptar domínio, superfícies, efeitos e exportação. Escolhido para a migração em avaliação. |
| bgfx | Vulkan/GLES / Metal | Boa abstração gráfica e render thread; o app ainda precisaria construir materiais, importação, scene manager e grande parte do renderer de composição. Maior superfície de manutenção própria. |
| Godot Mobile | Vulkan/Metal; Compatibility OpenGL | Oferece mais sistemas prontos, mas inserir outro scene graph, ciclo de vida e pipeline de recursos duplica responsabilidades do editor. Não foi demonstrado que esse custo compensa aqui. |
| Unity as a Library | Android e iOS | Runtime completo e ferramentas maduras. A documentação registra restrições de tela cheia para o uso padrão, uma instância do runtime e memória retida após unload. Integração desfavorável a múltiplas camadas de uma composição Flutter. |
| Unreal Mobile | Vulkan / Metal | Renderers mobile, perfis de dispositivo e ferramentas de profiling. Escopo de runtime e integração muito maior que uma viewport embutida; não há benchmark local que justifique adotá-lo. |
| WebGPU | Depende do runtime/backend | API gráfica, não scene manager ou renderer pronto. Introduzir uma WebView adicionaria sincronização entre runtimes; usar nativamente ainda exige construir o subsistema. |
| C++ próprio sobre Vulkan/Metal | Implementação separada por backend | Controle máximo, mas exige manter alocação, shaders, sincronização, PBR, formatos, drivers e recuperação. Os dados disponíveis não justificam esse custo frente a Filament. |

Fontes primárias consultadas: [Filament](https://github.com/google/filament), [Thermion](https://thermion.dev/), [bgfx](https://bkaradzic.github.io/bgfx/overview.html), [threading bgfx](https://bkaradzic.github.io/bgfx/internals.html), [Godot renderers](https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html), [Unity as a Library](https://docs.unity3d.com/Manual/UnityasaLibrary.html), [Unreal Mobile](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-creating-mobile-games), [WebGPU](https://developer.chrome.com/docs/web-platform/webgpu/overview).

Essa é uma comparação de adequação e custo de integração, não uma tabela inventada de FPS. Driver, cena, transparência e termal alteram os resultados. A versão efetivamente integrada é Thermion 0.5.0, cujo hook seleciona os binários Filament v1.69.1; a lista de recursos do repositório atual não é prova de que todos estão expostos pelo adaptador.

## Arquitetura implementada para avaliação

`Editor → Renderer3D → FilamentRenderer → Thermion → Filament → API da GPU`.

- O domínio e a persistência dos projetos permanecem independentes do motor.
- `LatestFrameQueue` mantém um trabalho em andamento e substitui pedidos pendentes durante scrubbing; fechamento aguarda o trabalho antes de descartar recursos.
- A conversão das malhas existentes para GLB portátil ocorre em isolate. Materiais PBR básicos, UVs e imagens PNG/JPEG seguem no pacote.
- O GLB gerado usa índices de 16/32 bits, preserva descontinuidades de normais e compartilha a mesma imagem entre materiais do asset. Isso evita expandir todos os triângulos e repetir texturas no arquivo intermediário.
- Geometria/material equivalentes compartilham um asset com instâncias nativas. Transformações continuam editáveis; alterações apenas de câmera não reimportam a malha.
- Recursos são admitidos sequencialmente por grupo e descartados quando removidos ou substituídos. O glTF de origem é liberado após criar as instâncias. Isso não equivale ainda a streaming completo com orçamento global de memória.
- Frustum culling e automatic instancing são ativados no Filament.
- Cada quadro de preview executa begin/render/end numa única tarefa nativa, apenas na superfície vinculada à sua view. O retorno de backpressure é respeitado e não há `flushAndWait` no caminho de preview. Esse caminho substitui a chamada do adaptador que desenhava a view em todas as swapchains e esperava a GPU por quadro.
- A preferência explícita por CPU e a recuperação após encerramento anterior continuam sendo respeitadas. O novo backend participa da marca de sessão GPU ativa.
- No estúdio, guias e seleção são desenhadas por uma sobreposição leve, sem chamar a renderização CPU dos objetos novamente. Essa sobreposição é de edição e não tem teste de profundidade nativo. Vistas ortográficas continuam no caminho de compatibilidade.
- Fundo e opções de pós-processamento só são recriados/reconfigurados quando mudam; movimentar a câmera não substitui a skybox a cada quadro.
- A superfície Flutter tem tamanho limitado para preview. O controlador nativo ajusta a resolução interna com histórico e velocidade limitada, mantendo a superfície de apresentação. O Quality Manager tem histerese para reduzir e recuperar efeitos e observa a escala efetiva do renderer; os tempos Flutter são identificados como pressão do compositor, não como GPU nativa.
- A interface de produção continua no motor atual. Animações de malha, texturas vindas de layers, panorama e reflexos não são silenciosamente descartados: a compatibilidade da camada é verificada antes de selecionar o backend experimental.
- Exportação permanece no caminho existente até implementar captura nativa determinística com paridade de composição. Reduções de preview não alteram o documento ou a resolução final.

## Validação exigida antes de mudar o padrão

### Resultado local do protótipo (Windows, Vulkan)

Teste `filament_native_benchmark_test.dart`, habilitado com `AUREA_FILAMENT_BENCH=1`, executado em RTX 3050. Superfície headless 640 × 360, 40 pedidos por cena, 10 descartados do cálculo p50/p95 por aquecimento. Intervalo de 16 ms entre pedidos apenas no harness; não é medição de FPS apresentados. Cada cena teve captura de pixels verificada e um único asset de geometria residente compartilhado pelas instâncias.

| Objetos iguais | Carga inicial | Envio CPU p50 / p95 | Último timestamp GPU válido | Pedidos aceitos / recusados |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 108 ms | 0,83 / 1,01 ms | 0,14 ms | 39 / 1 |
| 100 | 16 ms | 0,79 / 1,12 ms | 0,16 ms | 40 / 0 |
| 500 | 54 ms | 1,03 / 1,20 ms | 1,43 ms | 40 / 0 |
| 1.000 | 135 ms | 1,49 / 1,66 ms | 2,23 ms | 40 / 0 |
| 2.000 | 2.924 ms | 3,04 / 3,29 ms | 3,24 ms | 40 / 0 |

Dados brutos: `output/renderer-audit/filament-desktop.json`. O envio CPU inclui atualização da cena e passagem pela fila nativa; não é tempo de GPU. O timestamp GPU é a última amostra válida disponível, não percentil. Pedidos recusados pelo renderer são registrados, não tratados como frames apresentados. O RSS do processo cresceu de aproximadamente 282 para 331 MiB durante as cenas; inclui Flutter, driver, caches e harness e não mede VRAM ou prova ausência de vazamento. Carga de 2.000 instâncias é um gargalo pendente. As cenas e a resolução diferem do baseline CPU; não calcular ganho percentual entre essas tabelas.

Onze testes passaram nessa execução (arquitetura/conversão/fila, passes do pintor e o teste nativo com cinco cenas). A compilação Android ARM64 com `AUREA_FILAMENT=true` passou. O APK inclui `libthermion_dart.so` ARM64 com aproximadamente 6,4 MiB; isso não mede o acréscimo total em um APK de produção. Esses resultados não validam os dez cenários exigidos, composição final, celulares, termal ou iOS.

Validação final do código integrado:

- `flutter test --no-pub`: **1.228 passaram**, um teste nativo ignorado por ser opt-in (ele foi executado separadamente acima).
- `flutter analyze --no-pub lib test`: **sem problemas**.
- `flutter build apk --debug --no-pub --target-platform android-arm64 --dart-define=AUREA_FILAMENT=true`: **compilou**, incluindo as guias do estúdio. Artefato: `build/app/outputs/flutter-apk/app-debug.apk`.
- `git diff --check` nos arquivos do app/testes: passou.
- Android em dispositivo: **não executado**. iOS: **não compilado nem executado neste Windows**; workflow manual preparado, ainda não disparado. Não houve publicação, envio à loja ou alteração remota.

### Checklist de liberação

1. Compilar Android ARM64 e iOS Metal. O workflow manual `renderer-validation.yml` gera builds de avaliação sem publicar em loja/TestFlight.
2. Executar os mesmos projetos no backend atual e no novo: um objeto, 100, 500, 1.000+, texturas grandes, PBR, partículas, animações, vídeo/2D/3D e stress.
3. Medir p50/p95/p99 de frame time, tempo CPU/GPU, memória antes/depois, quantidade de recursos, tempo de carga e resposta a scrubbing. Draw calls, GPU memory e shader time precisam de contadores reais ou captura por Android GPU Inspector/Perfetto e Xcode Instruments/Metal; ausência de contador é `null`.
4. Rodar em pelo menos Adreno, Mali e iPhone, com repetição abrir/fechar, troca de projeto, pause/resume, aviso de memória e aquecimento prolongado. Meta 60 FPS ou 30 FPS estáveis em stress, sem inferir isso a partir de uma RTX de desktop.
5. Comparar imagens de preview/exportação, alpha, máscaras, blends, efeitos, animações de malha, seleções, gizmos, undo/redo e vídeo como textura.

Pendências de paridade: exportação nativa e sincronização com o compositor, vídeo como textura, rigs/morph/skinning nativos, ambiente/IBL, LOD integrado, orçamento global/eviction/streaming de texturas e meshes, oclusão e partículas. Não foram removidas do motor de compatibilidade. Não declarar a missão concluída até esses itens e as validações físicas passarem.
