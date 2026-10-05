# Estresse no Android isolado

Os dois filtros pertencem a `com.aurea.aurea.editor.HeavyEditingStressTest` e
exigem o argumento de instrumentação `aureaStress=true`. A bateria sustentada
recusa qualquer pacote diferente de `com.aurea.aurea.uitest`. Não limpe os dados
do aplicativo principal para executar os testes.

## Filtros

- `denseEditSurvivesPreviewTrimReloadAndExport`: baseline existente, preservado:
  96 camadas, vídeo VFR, FBX animado, 176 efeitos, motion blur, pré-comp, trim,
  reabertura e exports de 90 quadros em 720p e 1080p. Os exports podem demorar.
- `sustainedFullHdEditingRecoversAfterReloadAndSurfaceRecreation`: nova bateria
  de quatro minutos úteis, além de preparação e aquecimento. O argumento opcional
  `aureaStressSeconds` aceita de 180 a 1800 segundos; o padrão é 240.

Para selecionar somente a bateria sustentada, use o argumento `class`:

```
com.aurea.aurea.editor.HeavyEditingStressTest#sustainedFullHdEditingRecoversAfterReloadAndSurfaceRecreation
```

## Carga e critérios

A preparação transforma a mídia sintética VFR de 160×90 em um MP4 H.264 real de
1920×1080, com 90 quadros, pelo export do motor. O teste verifica resolução,
conteúdo, sucesso e ausência de quadros aproximados. Quatro cópias com caminhos
distintos são importadas para exercitar fontes independentes.

O projeto tem 48 camadas de origem: quatro vídeos Full HD, quatro objetos 3D,
oito textos e 32 formas. São 92 efeitos e 384 keyframes. Os vídeos recebem
Motion Tile, desfoque e glow, nessa ordem; os demais elementos 2D recebem
desfoque e glow. Dois grupos de oito camadas viram pré-comps, deixando 34
camadas na raiz. Um texto tem motion blur de transformação.

Os ciclos alternam resolução da prévia entre inteira, metade e quarto;
editam contorno; enviam rajadas de seeks; tocam o projeto em loop; e repetem
trim de memória, salvar/reabrir e descarte/recriação da SurfaceView real.

O teste exige avanço do playhead nativo **e** mudança dos pixels apresentados
na superfície. Capturas síncronas são feitas depois de observar a prévia, para
não esconder uma falha de apresentação. Também verifica conteúdo não vazio,
mudança entre tempos animados, quantidade de camadas após reabrir e comparação
dos pixels antes/depois de salvar. Não exige uma taxa arbitrária de FPS.

Um watchdog independente publica heartbeat na main e registra sua pilha se
ficar sem resposta por cinco segundos. Ele não chama Compose nem o motor.
Importações, consultas nativas, captura, export, salvar/reabrir e amostragem de
memória acontecem fora da main. Uma morte pelo sistema requer consultar também
o motivo de encerramento do Android; ausência de exceção Java não significa
sucesso.

## Evidência

Cada execução cria `files/stress-sustained-<timestamp>/` no pacote de teste:

- `progress.txt`, gravado incrementalmente, com cada fase antes das operações
  pesadas, PSS, heap Java/nativo, RAM disponível, pressão, memória GPU usada e
  reservada, alocações, texturas, cache de decode, CPU/GPU/decode/present,
  percentis de pacing, quadros antigos/perdidos, temperatura e decoder;
- MP4 Full HD, suas quatro cópias, projeto `.aurea` e capturas reais da tela;
- linha `ALL PASSED` somente depois de todos os critérios.

Compare memória retida após aquecimento e ao final sob a mesma resolução,
quadro, reabertura, histórico vazio e política de trim. Os limites de regressão
consideram o orçamento reportado pelo motor e retenção dos alocadores; não
constituem prova de ausência de vazamentos. GPU não reportada deve ser tratada
como não medida. Preserve logs brutos, informações de encerramento e artefatos
extraídos no workspace em `build/reference/heavy-stress-20261005/`.

Este harness é instrumentação Android. Implementá-lo ou compilar Kotlin não
comprova execução no aparelho, e não valida o aplicativo nativo iOS.
