# Controles reais de remapeamento de tempo — 13/10/2026

O painel de velocidade agora permite escolher Normal ou Remapear e, quando o
remapeamento está ativo, apresenta os controles existentes de tempo de origem,
keyframes, curva da propriedade, áudio e mistura de quadros. Android e iOS usam
a mesma avaliação do motor C++ para mostrar o quadro de origem no cabeçote.

A captura fornecida pelo usuário mostrava um gráfico inline VELOCIDADE
8x–0,25x. A fonte atual já não possuía esse gráfico dentro de SpeedPanel. Por
isso esta alteração não afirma ter removido um gráfico de um APK específico:
ela corrige a rota do painel atual, que continuava oferecendo velocidade normal
mesmo com remapeamento ativo, e reutiliza os controles reais de remapeamento.
O editor normal de curvas de keyframes continua disponível.

## Implementação

- `engine/src/engine/Engine.cpp`: `query_time_remap_current_source(layerId)`
  retorna `Layer::source_frame_f(playhead)` sob o mutex do modelo, incluindo
  início da camada, offset de mídia e a curva avaliada no motor. Camada ausente
  retorna NaN. A interface não reimplementa essa curva.
- `android/app/src/main/java/com/aurea/aurea/editor/panels/SpeedAudioPanels.kt`:
  SpeedPanel abre TimeRemapEffectEditor quando `timeRemap` está ativo. O valor
  mostrado lê `EditorStore.timeRemapCurrentSource`, observando as alterações
  do detalhe da camada/cabeçote. A opção Normal desativa o remapeamento.
- `engine/platform/ios/app/LayerPanels.swift` e `AnimationTools.swift`: rota e
  leitura equivalentes em Swift; o getter é exposto pela ponte Objective-C++.
- As duas interfaces reutilizam textos existentes e indicam a opção
  selecionada à acessibilidade. Os botões de escolha têm área de toque de 44.

## Evidência e limites

Os dois testes `CurrentSourceGetter` executaram 31 verificações no motor host:
remapeamento manual sem efeito, offset/início da camada, trecho congelado,
trecho reverso, curva do efeito avaliada no motor e camada inexistente. O
registro inicial está em
`build/validation/astra-export-20261013/host/prerender-getter-core-CurrentSourceGetter.log`.

A compilação Kotlin/Compose passou após corrigir a leitura direta do membro
privado `EditorStore.engine` para o wrapper público. O conjunto JVM associado
executou 25 testes, sem falhas: 23 de EffectPickerLogic e 2 de decodificação do
progresso PreRender. Esses testes não validam o gesto nem os controles visuais
em aparelho. O registro de identidade é
`build/validation/prerender-20261013/ui-validation.json`.

A compilação Apple do conjunto cumulativo e a execução nativa das duas
interfaces ainda precisam de registros próprios. Testes C++ e compilação JVM
não comprovam a execução desses controles no iPhone ou no Android. Não foi
inspecionado um APK da captura para identificar qual versão exibia o gráfico.
