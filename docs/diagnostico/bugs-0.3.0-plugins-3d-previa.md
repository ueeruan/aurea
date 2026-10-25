# Bugs da 0.3.0: plugins, prévia preta, câmera 3D, grupos, PNG, motion blur

Data: 25/10/2026. Branch `claude/release-2194`. Sem APK/IPA gerados.

Legenda: **[PROVADO]** pelo código ou por teste; **[INFERIDO]** sem execução
que confirme; **[NÃO COMPROVADO]** precisa de dado do aparelho.

| # | Relato | Causa | Correção | Onde |
|---|---|---|---|---|
| Crítico | Android fecha ao instalar plugin | Navegador de efeitos: a linha de ladrilhos que começava no grupo "plugins" tinha a chave `"plugins"`, a mesma da porta "Gerenciar plugins"; o Compose derruba o app com chave repetida. iOS não usa essas chaves. [PROVADO pelo código] | Chave da linha com prefixo (`groupRowKey`), chave da porta fixa (`PLUGIN_DOOR_KEY`) — já estava no diretório de trabalho, sem commit | `EffectPicker.kt`, `EffectPickerLogic.kt`, teste `groupRowKeysNeverCollideWithTheFixedHomeKeys` |
| 1 | Prévia/clipes pretos, play para | (a) espera da cerca do decoder (0.2.7) com o resto do prazo da imagem (às vezes ~0 ms) e, ao estourar, o quadro apagado ficava "pendente" e o Timeout fazia o decoder descer de caminho para sempre (lookahead → CPU → software) [PROVADO pelo código]; (b) Filtros/Ajustar/scopes capturavam quadros exatos a cada meio segundo **tocando**, com a GPU presa [PROVADO pelo código] | (a) cerca com prazo próprio de 500 ms; estourou, o quadro é descartado com estado coerente e o decode segue; (b) captura só com o play parado (Android e iOS) | `MediaCodecSource.cpp`, `ColorToolsPanels.kt`, `EditModeView.swift` |
| 1 | P30 lite: a prévia nunca aparece | Vulkan "subia" mesmo sem compilar os pipelines básicos e a queda para OpenGL ES só acontece se a GPU falhar ao iniciar. Não há regra para Huawei/Mali-G51. [mecanismo PROVADO; o passo que falha no aparelho NÃO COMPROVADO] | `Renderer::initialize` falha se o pipeline de composição ou o de saída não compilar: o Android cai no OpenGL ES | `Renderer.cpp` |
| 2 | Fecha quando um objeto passa pela câmera 3D | No quadro do cruzamento (canto atrás da câmera): texto/forma/vetor eram desenhados 4× mais densos (16× a memória, até 8192² RGBA16F) e o motion blur pedia o teto de amostras (até 128 cenas 3D inteiras). [mecanismos PROVADOS; o crash NÃO COMPROVADO no aparelho] | densidade normal no cruzamento; trajeto não mensurável usa as amostras escolhidas, não o teto; ponto atrás da câmera (w ≤ 0) não é mais projetado | `Renderer.cpp`, `ShutterPlan.hpp` |
| 5 | Motion blur com fantasma no export (projetos antigos) | Teto adaptativo do export 3D só descia (até 2 cenas por quadro), voltava só com 30 quadros < 0,25 s e media o quadro seguinte e a espera do encoder junto. Projeto antigo (64 amostras) ficava pesado e caía nessa descida; a prévia não tem esse teto. [PROVADO pelo código] | piso de 8 amostras no teto adaptativo (abaixo disso entra o degrau de sombra/MSAA); mede só CPU do quadro + espera de GPU dele; volta com 8 quadros < 0,5 s | `Engine.cpp`, `Renderer.hpp` |
| 6 | Imagem "pula" para a de outro grupo | Miniaturas da timeline (Android e iOS) guardadas pelo id da camada, que se repete entre grupos (cada composição tem a sua tabela) [PROVADO pelo código] | limpar as miniaturas ao entrar/sair de grupo, nas duas plataformas | `EditorStore.kt`, `AureaModel.swift` |
| 7 | Só uma câmera 3D funciona | Criar câmera (e o rastreio e a importação do AE) desligava `active` de todas as outras e nada religava; a escolha pegava a de baixo [PROVADO] | nenhuma é desligada; vale a câmera visível mais de **cima** no instante (como no AE); ao abrir projeto todas voltam ativas | `Engine.cpp`, `EngineAeImport.cpp`, `Renderer.cpp`, `Serialization.cpp` |
| 7 | Botão 3D não responde | Câmera nova nasce com "Afetar camadas abaixo": toda camada abaixo já é 3D e o botão só muda o próprio estado [PROVADO] | **Pendente de decisão** (ver abaixo) | — |
| 8 | Grupo colado ligado ao original | Colar no mesmo projeto reaproveitava a composição do grupo; o Duplicar faz cópia [PROVADO] | colar cria a própria composição (cópia profunda), como o Duplicar | `Engine.cpp`, teste `Clipboard.PastedGroupGetsItsOwnComposition` |
| 9 | iOS não abre no dia seguinte | Certificado do sideload revogado | Sem correção no app | — |
| 10 | PNG transparente esticado e com linhas | Miniatura ignorava o alfa e amostrava ponto a ponto; na prévia a imagem passava por bilinear de alfa reto (a cor dos pixels transparentes vazava para as bordas) e um toque só ao reduzir [PROVADO pelo código] | miniatura: média de área ponderada pelo alfa sobre o xadrez; prévia: bilinear pré-multiplicado e 4 amostras ao reduzir 2× ou mais | `ThumbnailService.cpp`, `rgba_to_linear.frag`, `Renderer.cpp`, teste `Engine.ImageThumbnailHonoursTransparency` |
| 11 | Eixo Z errado em modelo 3D com efeito | Modelo com efeito vira um grupo de cena próprio, desenhado chapado na ordem das camadas, sem teste de profundidade com o resto da cena [PROVADO pelo código] | **Corrigido na 0.3.1**: cada par (grupo do modelo com efeitos, outro grupo 3D) recebe os objetos do outro como recorte (`SceneInstance::holdout`, só profundidade, cor 0 em `pbr.frag`) | `Gpu.ModelWithEffectsKeepsItsDepthAgainstTheOtherModels` (falha sem o recorte) |

## Pendências e decisões

- **Botão 3D**: "Afetar camadas abaixo" ligado por padrão em câmera nova é uma
  escolha deliberada (timeline v47, testes `Scene3DLens`). Opções: (a) voltar ao
  padrão do AE (desligado) — o botão volta a mandar; (b) manter e criar um campo
  por camada "ficar 2D" que o botão liga (muda o formato do projeto).
- **Modelo 3D com efeito (Z)**: corrigido na 0.3.1 pelo recorte entre grupos 3D. Limite: planos 2D em 3D do outro grupo ainda não recortam o modelo com efeito.
- Nada foi validado em aparelho físico. iOS: só checagens estáticas (sem Mac).
