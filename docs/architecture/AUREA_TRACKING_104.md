# Rastreamento do Aurea 1.0.4

## Diagnóstico

O Motion Tracker e a Câmera 3D já combinavam decodificação própria, luma em resolução de análise, resultados por camada e aplicação em keyframes no motor. O ponto usa correlação normalizada e a câmera usa Shi–Tomasi, Lucas–Kanade em pirâmide, validação de ida e volta, estimação robusta, triangulação e refinamento. O ponto duplo reduzia e copiava a mesma luma para cada ponto. Para trás, o decoder relia quadros do mesmo GOP. Um alvo VFR ligeiramente anterior à janela conseguia selecionar o quadro futuro.

O motor existente cobre os primitivos que justificariam começar com OpenCV. Ceres é uma opção futura para medir um refinamento de grande porte contra o atual. Esta mudança aproveita o código já compilado para Android e iOS, sem adicionar dependências, pesos de modelo ou licenças. Os modos de rosto e corpo reutilizam ML Kit e Vision, já usados pelo app. [Optical flow no OpenCV](https://docs.opencv.org/4.x/d4/dee/tutorial_optical_flow.html) · [Ceres Solver](https://github.com/ceres-solver/ceres-solver)

## Fluxo e persistência

- `AnalysisDecode.hpp` seleciona quadros pelos PTS e pela duração e agrupa trechos descendentes em ordem de decodificação crescente. A janela retém luma de análise, com teto de 12 MiB e 24 quadros. Uma falha de pré-leitura tenta o caminho normal para o quadro solicitado.
- `PointTracker.cpp` compartilha a pirâmide e a luma entre os pontos. A aceitação avança a medição e o estado do tracker juntos. A verificação de ida e volta pode ser ativada com um limiar configurável para ponto e dois pontos.
- `CameraTracker.cpp` calcula os integrais de detecção local na seleção com borda suficiente para o operador. O Smart Tracker acompanha essa vizinhança com o deslocamento mediano dos pontos e continua estimando o movimento final no modelo escolhido. Objeto usa similaridade e superfície usa perspectiva.
- `EngineMotion.cpp` publica a confiança por quadro, a trajetória recente e as medidas CSV no espaço da fonte. O CSV está em ordem cronológica da composição, com tempo da fonte em microssegundos e campos de posição vazios para observações perdidas. A matriz é do quadro de referência ao quadro medido, em pixels da fonte. Os resultados do rastreio óptico continuam no cache existente por camada. Máscaras e nulos são gravados como keyframes padrão do projeto e usam o histórico para desfazer.
- `Engine.cpp` fornece a reconstrução por IDs de pontos, trajetória, erro de reprojeção, câmera no quadro atual e plano dominante quando o solve tem suporte para ele. A vista normaliza apenas o desenho interativo. Origem, chão e criação de nulos usam o solve calibrado do motor.
- `TrackingPanel.kt`, `MediaPanels.swift`, a viewport de cada plataforma e as pontes JNI e Objective-C expõem medição, reconstrução, seleção de pontos e exportação. O detector semântico fica assíncrono e envia observações normalizadas ao C++ para convertê-las em nulos editáveis. O rosto produz pontos de olhos, nariz e boca e um nulo de pose em 2D quando os dois olhos foram medidos. O corpo produz nulos por articulação confiável.

## Medição

| Teste controlado no Windows | Antes | Depois | Verificação |
| --- | ---: | ---: | --- |
| Decodificação inversa, 84 alvos VFR | 81,6 ms | 34,7 ms | Luma idêntica comparada ao seletor sem cache com PTS corrigido |
| Buscas no decoder, mesmo teste | 77 | 4 | 1.482 chamadas de leitura antes, 123 depois |
| Dois pontos, 180 quadros em 1280×720 | 594,8 ms | 258,5 ms | Posições e scores produziram o mesmo checksum |

Os números isolam etapas em imagens de teste. Tempo de inferência, uso de GPU e memória total do app não foram extrapolados desses testes.

## Arquivos e validação

- Motor compartilhado em `engine/src/tracking`, `engine/include/aurea/tracking`, `engine/include/aurea/Engine.hpp` e `engine/src/engine/EngineMotion.cpp` e `Engine.cpp`.
- Android em `engine/platform/android/aurea_jni.cpp`, `AureaEngine.kt`, `EditorStore.kt`, `TrackingPanel.kt`, `Stage.kt`, `MocapDetector.kt` e nos recursos de idioma.
- iOS em `engine/platform/ios/bridge/AureaEngine.h` e `AureaEngine.mm`, `AureaModel.swift`, `MediaPanels.swift`, `EditorView.swift`, `MocapPanel.swift` e `AureaStrings.swift`.
- Os testes de algoritmo selecionados de `test_tracking.cpp` e `test_motion_geometry.cpp` passaram no Windows, com 15 casos, 2.030 verificações e zero falhas. O teste VFR comparativo usou o decoder de teste de `test_tracking_flow.cpp`. A construção Android produziu o APK x86_64 e o pacote de instrumentação. Os testes de dispositivo usam `Aurea104DeviceTest.kt`.
- No emulador Android, passaram quatro cenários isolados. O ponto automático rastreou para trás, ordenou a saída CSV e retomou após a pausa. A superfície deslocou 70,0 px, gerou máscara editável com Undo e aplicou Corner Pin. A reconstrução de câmera usou um clipe H.264 de cena controlada e reportou 0,149 px de erro médio de reprojeção, além de exibir a nuvem e a câmera no painel. O clipe de rosto e corpo passou pelos detectores do Android e criou nulos com keyframes editáveis.

## Pendências

Ainda faltam edição incremental de um intervalo dentro de um resultado salvo, suavização editável de trilhas fora da estabilização, controles individuais de níveis de pirâmide e contagem de features, associação automática de identidade entre várias pessoas, deformação não rígida de múltiplas regiões, exportação de cena 3D para ferramentas externas e medições de GPU e memória em aparelhos físicos. O cache de sessão permite pausa e retomada durante o job óptico. A persistência continua feita quando um resultado completo é aceito. O iOS precisa de compilação Xcode e teste nativo.
