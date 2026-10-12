# 0.1.7 BETA 3 — Máscaras, efeitos 3D, Ambiente, Real Camera e Camera Tracker (12/10/2026)

Pedido do dono no beta: aba de Máscaras "simples e visível", objetos 3D com o
Ambiente só no básico, trocar imagem / tirar parte / animar parte como EFEITO
(igual ao Text 3D Layout), 3D Stroke e 3D Shatter, Real Camera fora da prévia
e conferir o Camera Tracker 3D. Implementação própria; referências de
comportamento são os efeitos de mesmo nome dos editores de movimento.

## O que mudou

| Área | Motor (C++) | Android | iOS |
|---|---|---|---|
| Máscaras numa tela só | — | `editor/panels/MaskPanel.kt` | `app/LayerPanels.swift` (`MaskPanel`, `MaskShapeTool`) |
| Animar parte 3D / Esconder partes 3D / Trocar textura 3D | `scene3d/PartEffects.*`, `effects/builtin/Object3DEffects.cpp`, `Renderer.cpp` (place_model, texturas trocadas `sceneImages_`), `SceneRenderer.cpp` (`textured`), `Engine::add_part_effect/set_effect_image/effect_image_name/model_part_names` | `Element3DPanel.kt` (aba Partes), `EffectsPanel.kt` (`PartTextureImageRow`), `EditorStore` | `Panel3DView.swift` (aba Partes), `EffectsView.swift` (`PartTextureImageRow`), `AureaModel` |
| 3D Shatter | `Object3DEffects.cpp` + `shaders/effects/shatter3d.{vert,frag}` | catálogo (`EffectsHuman.kt`, `EffectCatalogMeta.kt`) | catálogo (`EffectsHuman.swift`) |
| 3D Stroke | `Object3DEffects.cpp` + `shaders/effects/stroke3d.{vert,frag}`, `stroke3d_compose.frag` | catálogo | catálogo |
| Ambiente só luz/fundo/chão | — | `Element3DPanel.kt` (`LightSceneTab`), `Shape3DSection.kt` | `Panel3DView.swift`, `Shape3DViews.swift` |
| Real Camera fora da prévia | — | `RealCamera.kt`, `EditorScreen.kt` | `RealCameraStage.swift`, `EditorView.swift` |
| Camera Tracker 3D | `Engine.cpp` (restore/refine/apply/start) | `EffectsPanel.kt` (chip "analisada" só no estado 2) | `AureaEngine.mm` (`setCameraTrack` devolve BOOL), `MediaPanels.swift` |

Detalhes que importam:

- **Parte N** = o N-ésimo nó com malha da cena, na ordem dos nós (a mesma
  unidade do Text/Shape 3D Layout). Vale em forma 3D, texto 3D (as letras
  são separadas ao pôr o efeito) e modelo importado (o nó e os filhos andam
  juntos). Os índices de parâmetro ficam fixos nos projetos salvos.
- **Trocar textura 3D**: a imagem é um asset de imagem do projeto (o projeto
  guarda o caminho; volta ao reabrir pelo `imageLoader`, como camada de
  imagem). Na GPU vira uma textura sRGB com mips do Renderer
  (`scene_image_texture`), trocada no material só daquele nó.
- **3D Shatter**: 36 vértices por peça (frente, verso, laterais), peça parada
  desenha só a frente no lugar exato da camada; explosão 0% = identidade.
- **3D Stroke**: o caminho (máscaras da camada ou forma pronta) vira uma
  textura de dado N×1; um quadrado por trecho vira cápsula no fragmento;
  profundidade + mistura "sobre" para as junções não abrirem buracos.
- **Camera Tracker** (auditoria): análise terminada não é passo de desfazer —
  desfazer outra edição tirava a análise da camada; agora ela volta da última
  análise ao restaurar/aplicar. "Resolver de novo" cancelado/falho mantém a
  solução (estado 2). Código morto que recusava vídeo com escala 0% removido.
  Cancelar depois da última conferência não grava mais no projeto. `start` e
  `restore` aplicam os comandos pendentes antes de ler o tempo do vídeo.

## Compilação (12/10/2026)

- Motor no PC (MSVC, `engine/build/host`): compila.
- Android: `compileUiTestKotlin`, `compileUiTestAndroidTestKotlin` e JVM
  `testUiTestUnitTest` 398/0.
- iOS: só as checagens estáticas (check_api_swift, check_pbxproj,
  check_shared_assets, check_effect_contract, check_symbols, `_ios_check.py
  tipos`): 0 problemas. A compilação nativa é a da CI (build-ipa.yml).
- i18n: `tools/i18n_check.py` 0 problemas; rótulos dos parâmetros gerados por
  `tools/i18n_effect_catalog.py` (39 textos novos em `tools/effect_i18n_texts.json`).

## Testes do motor (GPU do PC, Vulkan)

- `Part3DEffects.MoveOnePartHideARangeAndPickTextures`,
  `Part3DEffects.EngineAcceptsThemOnlyOn3DLayersAndKeepsTheImageAcrossSave`,
  `Gpu.Part3DEffectsTextureHideAndMoveChangeTheRenderedCube` (textura trocada
  aparece no cubo, esconder some com ele, mover muda o quadro).
- `Shatter3DGpu.*` (0% = identidade, peças fora da caixa, determinismo, peças
  paradas idênticas à camada) e `Stroke3DGpu.*` (anel no lugar, só o traço,
  rotação, fim 50%).
- `Tracking.UndoKeepsTheAnalysisAndCancelledResolveKeepsTheSolve` + os 23
  testes de rastreio que já existiam.
- `Gpu.EveryCatalogEffectChangesTheProjectFrame`: Shatter e Stroke mudam o
  quadro; a única falha é `aurea.move.path`, que já falhava antes (log
  `engine/build/full-suite-1.log`).

## Validação em aparelho

Nada disto foi visto num celular ainda (Android nem iPhone): a aba de
máscaras, a aba 3D nova, os efeitos 3D na prévia e no export, a Real Camera
na área de painéis e o fluxo do Camera Tracker precisam de teste no aparelho.
