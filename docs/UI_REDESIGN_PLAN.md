# UI_REDESIGN_PLAN — Plano de reestruturação da UI do editor

Data: 2026-09-07 · Complementa `UI_AUDIT.md` · Aguarda aprovação antes de qualquer código (Fase 0, regra 4 do prompt).

---

## 0. A decisão de fundo: reestruturar, não recomeçar

O prompt permite "do zero se necessário". A auditoria mostra que **não é necessário nem prudente**: o app já implementa a metade do modelo-alvo (cinco zonas, "tudo é layer", grid de categorias por tipo com teto de sete, playhead central, losangos, filmstrip/waveform, cor por tipo, um "+" só, folhas que não cobrem o preview, um contrato de seções testado). O que falta é estrutural mas localizado:

1. **Uma superfície contextual só** (Zona E) no lugar de dock + menu modal + barra de ações que aparecem e somem.
2. **Fixar as zonas**: a barra de comandos estruturais e o exportar sempre no mesmo lugar, o preview redimensionável, o cabeçalho sem quatro estados.
3. **Preset antes de parâmetro** onde ainda não há (galeria de efeitos com miniatura, blend com miniatura, as três profundidades em todos os 47 efeitos).
4. **Um componente de parâmetro** (`ParameterRow`) e as categorias E3 padronizadas.
5. **Simples / Pro**, onboarding, estado vazio, timecode tocável, losango na transporte, grupos com breadcrumb.
6. **Dar destino aos 60 métodos sem UI** — a maior parte em Pro, alguns como campos que faltavam mesmo (tamanho da fonte).

O motor (`domain/`, `application/`, os pintores em `widgets/`) não muda. A UI nova continua a montar o mesmo `CompositionView`; a exportação continua idêntica ao pixel. Onde a UI precisa de conta (encaixe, eixos da timeline, dono do keyframe, métricas de layout), ela vai para adaptadores em `application/` que só leem o modelo.

---

## 1. Arquitetura de tela alvo, zona a zona (hoje → depois)

### Zona A — Top bar (fixa, 56 pt) · `TopBar`
Hoje: 4 estados que trocam ícones. Depois: **um estado só**.
`‹ Projetos` · nome do projeto (toque = renomear, `renameProject` ganha UI) · `↶ ↷` · ⚙ Projeto (proporção/resolução/fps/fundo/guias/motion blur; o overlay de diagnóstico vai para dentro de ⚙, aba "Diagnóstico") · **Exportar** (pílula lima, sempre visível) · chip `Simples | Pro`.
Com camada selecionada o nome da camada aparece **no cabeçalho da Zona E**, não na top bar. Com painel aberto, o "‹ voltar" também é o da Zona E. Os quatro estados viram um. Seleção múltipla: o cabeçalho de E mostra "N camadas" + ações (Agrupar, Vincular, Cascata, Alinhar, Excluir) — mesma posição.

### Zona B — Preview · `PreviewCanvas`
Mantém `CompositionView` e todos os overlays (guias, editor de nós, desenho livre). Ganha: **alça de altura** entre preview e transporte (arrasta entre 30% e 60% do espaço útil; lembrada por projeto), bounding box com alças de canto/rotação desenhadas para a camada selecionada (o encaixe e os gestos são os de hoje, extraídos para `StageGestures`), zoom/pan do canvas com botão "Ajustar" (Pro), badge de qualidade ("Rascunho" já existe; vira "Prévia · toque para qualidade cheia"), botão "?" no canto (Onboarding/guia).

### Zona C — Transporte (48 pt) · `TransportBar`
`|◀ ▶ ▶|` · **timecode atual / total** (toque = digitar) · loop · **◆ keyframe** (crava/retira nas propriedades da camada selecionada; com painel aberto, na propriedade aberta) · marca (toque = pôr/tirar; o menu das marcas vira botão visível "Marcas" na aba Pro do ⚙ e no toque longo). Undo/redo saem daqui (vão para A); duplicar e casca de cebola saem daqui (duplicar vai para a linha de ações rápidas de E2; casca de cebola vai para ⚙ › Preview). Cada ícone com rótulo curto embaixo em telas ≥ 390 pt.

### Zona D — Timeline · `Timeline`, `TimelineRuler`, `LayerBar`, `KeyframeDiamond`, `GroupBreadcrumb`
Mantém `AmTimeline` (playhead central, régua, pílulas, barras com waveform/filmstrip, trim, transições, encaixe). Muda:
- **Coluna esquerda fixa** por camada: olho · cadeado · indicador de keyframe (hoje: olho + miniatura). Miniatura vai para dentro da barra (já há filmstrip).
- **Cor por tipo** consistente (já existe em `layer_look.dart`; passa a valer também no `+`, no cabeçalho de E e nos ícones).
- **Reordenar**: toque longo + arrastar (já existe arrasto vertical na barra selecionada) **e** botões visíveis na linha de ações rápidas.
- **Grupos**: barra do grupo colapsada com contagem; **toque duplo entra** (a timeline mostra os filhos com tempo local; breadcrumb `Projeto › Grupo 1` no cabeçalho de D). Isto é UI nova sobre `GroupLayer.children`, sem mudar o motor.
- **Expandir timeline** (botão no canto direito da régua): timeline ocupa até 80% e o preview vira uma janela pequena.
- **Cortes na mesma faixa** (decisão em aberto, §8-Q8): pedaços de um mesmo vídeo dividido, sem sobreposição no tempo, desenham na **mesma linha** (empacotamento visual); o modelo continua uma camada por pedaço.
- Marcadores de batida na régua já existem.

### Zona E — Painel contextual · `ContextSheet` (E1–E5)
Um único widget na metade de baixo, com **alça de altura** (níveis: espiada 22% · metade 40% · cheia = até o piso do preview, nunca cobrindo-o). Substitui `_ActionBar` + `LayerToolsDock` + os 8 painéis de `_Mode` + `AddLayerPanel` + as folhas de utilidades. **A posição nunca muda; só o conteúdo.**

| Estado | Conteúdo | O que absorve de hoje |
|---|---|---|
| **E1 · nada selecionado** | `AddToolbar`: ícones grandes com rótulo, roláveis: **Mídia · Áudio · Texto · Forma · Efeito · Ícone/Sticker · Grupo · Objeto (Pro: Nulo, Grid, Ajuste, Partículas, Cena 3D, Câmera, Luz, Elemento 3D)**. Abaixo, uma linha de projeto: `Marcas` · `Batidas` · `AutoEdit` · `Legendas`. Empty state com CTA quando não há camadas. | FAB (fica, mesmo destino), `AddLayerPanel` (vira `AddMenu` com abas), a aba Modelo (some: modelos abrem na tela Início; a aba inerte é removida) |
| **E2 · camada selecionada** | Cabeçalho: ícone do tipo com cor · nome (toque = renomear) · **Duplicar · Excluir · Organizar** (rótulo/solo/tímida/bloquear, visível, sem ⋯). `QuickActionsRow` (só as que fazem sentido para o tipo): **Dividir · Congelar · Velocidade · Volume · Alinhar · Subir · Descer · Vincular · Precomp/Tempo**. `CategoryGrid`: **Transformar · Cor & Preenchimento · Borda & Sombra · Mesclagem & Opacidade · Propriedades do elemento · Efeitos** (+ Pro: **Máscara & Recorte · Parenting · Câmera/Cena · Time remap**). | dock + menu modal + barra de ações + as 20 utilidades (viram ações rápidas ou entram em "Propriedades do elemento") |
| **E3 · categoria aberta** | Cabeçalho `‹ Transformar` + reset da categoria. Lista de `ParameterRow`; sub-abas quando a categoria tem grupos (Posição/Escala/Rotação/Skew/Pivô). Para Transformar, o pad "arraste aqui" e o dial continuam como variantes de linha (`ParameterRow.pad`, `.dial`). | `TransformPanel`, `BlendingPanel`, `ColorFillPanel`, `ShapePanel`, folhas de Som/Estilos/Legendas/Partículas/Elemento 3D/Precomp/Grid |
| **E4 · efeito selecionado** | Igual a E3 com cabeçalho do efeito: liga/desliga · ↑↓ · duplicar · remover · favoritar · **salvar preset**; chips **Pronto | Ajustar | Avançado** (profundidade); parâmetros com `isAdvanced` escondidos em Simples. | `EffectsPanel` (cartões) |
| **E5 · keyframe selecionado** | `CurvePresetGrid` (miniaturas: Linear, Ease In/Out/Both, Apple ×3, Mola ×2, Overshoot, Quicar, Elástico, Degraus, Cíclico, Aleatório, Deg. elástico) + botão **Editor de curva** (Pro, tela cheia = `CurvePanel` atual). | `CurvePanel` (presets já têm miniatura) |

**Estúdio 3D e Edit Points** continuam como **espaços de edição dedicados** (rota/modo em tela cheia) abertos por "Propriedades do elemento" — como o Scene View do Node e o Edit Points do Alight. Não são "tela por tipo": são ferramentas de manipulação direta que precisam do palco inteiro.

**FAB `+`**: fica (canto inferior direito sobre D, só sem seleção), mesmo destino de E1.

---

## 2. Simples × Pro

Toggle na Zona A, lembrado em `SharedPreferences` (`editor.pro`, padrão **Simples** para instalação nova; **Pro** para quem já tem projeto salvo — para o beta atual não perder nada de vista de repente). Regra: **Pro só acrescenta; nunca muda o lugar de nada.**

| Área | Simples | Pro acrescenta |
|---|---|---|
| E1 Adicionar | Mídia, Áudio, Texto, Forma, Efeito, Ícone, Grupo | Objeto (Nulo, Grid, Ajuste, Partículas, Cena 3D, Câmera, Luz, Elemento 3D), Desenho vetorial |
| E2 Grid | Transformar, Cor, Borda & Sombra, Mesclagem, Propriedades do elemento, Efeitos | Máscara & Recorte, Parenting, Câmera/Cena 3D, Time remap |
| E2 Ações rápidas | Dividir, Duplicar, Excluir, Velocidade, Volume, Subir/Descer | Congelar, Alinhar, Vincular, Precomp, Cascata, Estabilizar, Reenquadrar, Decupagem, Extrude, Pulsar na batida, Loop de keyframes |
| Transformar | Posição, Escala, Rotação, Opacidade | Skew, Pivô, Z/3D, Ligar 3D, Motion blur, pickwhip por propriedade |
| Mesclagem | Opacidade + **6 modos** com miniatura (Normal, Multiplicar, Tela, Sobrepor, Adicionar, Luz suave) | os outros 21 (17 nativos + 10 Aurea), todos com miniatura |
| Efeitos | Galeria com miniatura por categoria; no efeito só **Pronto** e **Ajustar** | Avançado (ficha inteira), favoritos, salvar preset, assar em keyframes, keyframe por parâmetro |
| Keyframes | Auto ao mover no palco; ◆ na transporte; easing por preset | Editor de curva, loop de keyframes, expressões (campo por propriedade), keyframe por parâmetro de efeito |
| Texto | Conteúdo, fonte, **tamanho**, **negrito**, cor, animações de catálogo | Texto em caminho, animadores AE com propriedades e seletores, presets de texto |
| Áudio | Volume, fades, mudo, normalizar | Ducking com ataque/release/threshold, remover silêncio, batidas, limpeza de voz/EQ |
| Projeto (⚙) | Nome, proporção, resolução, fps, fundo | Guias, motion blur da comp, paleta e estilos nomeados, propriedades expostas do template, dados (CSV/JSON), diagnóstico |
| Exportar | MP4 com presets (1080p/720p/4K, fps do projeto), salvar na galeria | Codec, taxa, PNG seq., Lottie, SVG, template, SRT |

Itens Pro em Simples: **não aparecem** nas listas e grids (regra do prompt, escolha "não aparece" por categoria, para manter o teto de sete e o painel cabendo sem rolar); a única exceção visível é um rodapé discreto "Mais no modo Pro" no fim de cada grid, que leva ao toggle. Documentado por categoria na tabela do §3.

---

## 3. Mapeamento feature → destino (nenhuma sem destino)

Formato: **feature** → zona/estado · Simples (S) ou Pro (P) · nota. Toques-alvo entre parênteses quando mudam.

### Projeto
- Novo projeto → tela de novo projeto com `AspectRatioChips` (16:9, 9:16, 1:1, 4:5, 4:3, custom), resolução, fps, fundo · S (3 toques: `+ Novo`, chip, `Criar`)
- Alterar proporção/resolução/fps/fundo → **A › ⚙ Projeto** · S · UI nova sobre `VideoProject.copyWith` (o campo de fundo **não existe**: precisa de `backgroundColor` no modelo → Q9)
- Renomear projeto → A › toque no nome · S · `renameProject`
- Guias → ⚙ › Guias · P · `setGuides`, `addGuide`
- Motion blur do projeto → ⚙ › Motion blur · P · `setMotionBlur`
- Paleta do projeto / estilos de texto nomeados → ⚙ › Paleta & Estilos · P · `setPaletteColor`, `removePaletteColor`, `linkLayerColor`, `upsertTextStyle`, `linkTextStyle`
- Propriedades expostas (template) → ⚙ › Template · P · `exposeProperty`, `unexposeProperty`, `setExposedValue` (+ botão "Expor" no cabeçalho de cada `ParameterRow` em Pro)
- Dados CSV/JSON, repetir por linha, caixa de texto, contêiner, pilha, contador → ⚙ › Dados & Responsivo · P · os 12 métodos de ofício (Fase 8, ver §5)
- Diagnóstico (marcha, RSS) → ⚙ › Diagnóstico · P
- Marcas (pôr/tirar, próxima, cortar em todas, distribuir, limpar, nome, cor) → C › marca (toque) + **E1 › Marcas** (folha visível) · S
- Batidas → E1 › Batidas · S (P: sensibilidade/subdivisão)
- Undo/redo → A · S · cancelar gesto = gesto de escape no palco (dois dedos batem) · P
- Salvar na galeria, padrões, vibração, cache, motor/qualidade 3D → Ajustes (fica)

### Adicionar (E1)
- Mídia (galeria/álbuns/arquivo) · Áudio · Texto · Forma (30) · Efeito (galeria; aplica na camada de cima ou cria Ajuste) · Ícone (Iconify) · Grupo (cria grupo vazio ou agrupa seleção) → E1 · S
- Desenho livre → E1 › Forma › "Desenhar" · S · Desenho vetorial → P
- Legendas (Whisper/SRT) → E1 › linha de projeto · S
- Nulo, Grid, Ajuste, Partículas, Cena 3D, Câmera, Luz, Elemento 3D (**14** sólidos) → E1 › Objeto · P
- Modelos prontos → Início (a aba inerte some)
- Importar fonte / modelo 3D / panorama / SVG via Iconify → dentro da categoria que usa (Texto › Fonte; Cena › Objetos; Cena › Ambiente)
- Preset Alight (XML) / template → Início (fica)
- `addAudioLayer`, `importVideoFromGallery`, `importImageFromGallery` → passam a ser os chamadores do E1 (deixam de ser mortos)

### Camadas e estrutura (E2 cabeçalho e ações rápidas)
- Renomear camada → E2 cabeçalho · S · UI nova (`renameLayers` com uma)
- Duplicar · Excluir · Organizar (rótulo, solo, tímida, bloquear) → E2 cabeçalho · S (Organizar sem ⋯: três ícones com estado)
- Subir/Descer · Dividir · Velocidade · Volume · Alinhar → ações rápidas · S
- Congelar · Vincular · Precomp/Tempo · Cascata · Estabilizar · Reenquadrar · Decupagem · Extrude 3D · Pulsar na batida · Loop de keyframes · Excluir e fechar · Fechar buracos · Desagrupar · Câmeras → ações rápidas · P (roláveis; cada uma com rótulo)
- Agrupar / Desagrupar / entrar no grupo → E2 (múltipla) e D (toque duplo) · S · `groupLayers`; filhos editáveis dentro do grupo (UI nova sobre `children`)
- Buscar camadas → D › lupa na régua · P · `searchLayers`
- Pasta da camada → E2 › Organizar · P · `setLayerFolder`
- Seleção múltipla → toque longo na barra (fica) + "Selecionar várias" na régua · S
- Magnético → D › ímã na régua (estado visível) · S · sobe para prefs do projeto
- Casca de cebola → ⚙ › Preview · P · Expandir preview → alça de B · S

### Timeline (D)
- Mover, aparar, encaixe, zoom, transições na junção, congelar → D · S (transição: Simples mostra os 7 tipos com miniatura; P mostra "Com efeito" e curva com 14 easings; `setTransitionEffect`, `updateTransition`, `transitionHandleReport` viram os chamadores da folha)
- Juntar pedaços → chip visível na junção "Juntar" (além do toque longo) · S
- Edição de 3 pontos (inserir, sobrescrever, lift, extract) → D › régua › "Entrada/Saída" (marcas de I/O) + ações rápidas Pro "Inserir aqui", "Sobrescrever", "Levantar", "Extrair" · P · `insertLayerAt`, `overwriteLayerAt`, `liftTimeRange`, `extractTimeRange`
- Velocidade, rampas, reverso, time remap, blur por velocidade → E3 "Velocidade" (ação rápida) · S básico / P remap
- Estabilizar / Reenquadrar / limpar estabilização → ações rápidas P (limpar aparece quando há estabilização) · `clearStabilization`

### Transformar (E3)
- Posição X/Y (pad), Escala L/A (réguas vinculadas), Rotação (dial), Opacidade → S
- Skew, Pivô, Z, rotação X/Y, Ligar 3D, motion blur da camada (um só: `toggleLayerMotionBlurReal`; o outro sai), pickwhip por propriedade → P
- Auto-key → chip com estado no cabeçalho E3 · S · Keyframe anterior/próximo → C (‹◆›) · S · Resetar → E3 cabeçalho e toque longo no nome · S
- Manipulação no palco com alças → B · S (auto-keyframe ao mover)

### Keyframes e curvas (C, E5)
- Cravar/tirar → C ◆ · S · Losangos na barra e navegação → D · S
- Presets de easing (16, com miniatura) → E5 · S · Editor de curva (alças, velocidade, copiar/colar, todos os segmentos, overshoot, parâmetros finos de mola/degraus/elástico) → E5 › "Editor de curva" · P
- Loop de keyframes (5 modos, `LoopWhen`, `count`) e inverter no tempo → E3 › cabeçalho › Loop · P
- Expressões → `ParameterRow` › toque longo no valor › "Expressão" (campo + erro do motor) · P · `withExpression`; campo numérico aceita "1080/3" e "50%" via `evalExpression` · P

### Mesclagem, máscara, recorte (E3)
- Opacidade (uma vez só; sai de Transformar) + modos com miniatura (6 S / 27 P) → E3 Mesclagem
- Máscara pronta (7), montar, pilha, editar nós, recorte por camada → categoria **Máscara & Recorte** (grid Pro; em S aparece "Revelar" como preset dentro de Efeitos › Galeria › Transições)

### Cor, traço, estilos (E3)
- Cor principal (seletor), gradiente vetorial, traço → **Cor & Preenchimento** · S (gradiente e traço com "Pronto" primeiro)
- Sombras, brilho, contorno → **Borda & Sombra** (Pronto/Montar/Avançado já existem) · S

### Efeitos (E4)
- Galeria (`EffectGallery`): busca, categorias, favoritos, **miniatura de prévia por efeito** (renderizada uma vez com uma cartela padrão pelo motor real e cacheada) → S
- Pronto / Ajustar / Avançado em **todos os 47** (os 36 sem `montar`/`presets` ganham o preenchimento no `EffectSpec` — dado, não código de motor) → S/S/P
- Ordem, ligar/desligar, duplicar, remover, resetar, cor(es) → E4 cabeçalho · S
- Favoritar → E4 e galeria · S · novo `Set<String>` em prefs (não é do projeto)
- Salvar preset → E4 · P · passa a usar `saveEffectPresetFrom` (o método morto vira o chamador)
- Assar em keyframes → E4 · P · `bakeEffectToKeyframes`
- Keyframe por parâmetro → E4 › `ParameterRow` ◆ · P · `toggleEffectParamKeyframe`
- Blob Tracker analisar → E4 · P
- Presets de efeito (6 + salvos) → galeria › aba Presets · S

### Formas (E3 "Propriedades do elemento" para Shape)
- Parâmetros, cantos, pontas, geometria, converter, composto, Merge, 7 operadores, Trim, Repeater, Morph → S (Pronto/Ajustar) / P (operadores, Merge, composto)
- Edit Points → botão "Editar pontos" → modo em tela cheia (fica) · P

### Texto (E3 "Propriedades do elemento" para Text)
- Conteúdo (campo com título), fonte (com "Abc" de prévia), **tamanho (régua)**, **negrito**, alinhamento, cor → S · `editTextLayer(fontSize:, bold:)` ganha UI
- Animações de catálogo (35) com miniatura animada → S · Texto em caminho → P
- Animadores AE: propriedades (`toggleAnimatorPropType`, `editAnimatorPropValue`, `toggleAnimatorPropKeyframe`) e seletores (`addTextSelector`, `removeTextSelector`, `cycleSelectorMode`, `editSelectorParam`, `setRangeSelectorShape`) → sub-aba "Animadores (AE)" · P · presets de texto (`applyTextPreset`) → galeria de animações · S

### Legendas (E3 para Caption)
- Cues, estilo (Pronto primeiro) → S · Exportar SRT → A › Exportar › "Legendas (.srt)" · P · `exportCaptionsSrt`

### Áudio (E3 "Volume")
- Volume, fades, mudo, normalizar → S · Ducking completo (ataque/release/threshold), remover silêncio, limpeza de voz, de-esser, EQ (`AudioProcessing`) → P (UI nova; `voice_ops.dart` deixa de ser órfão através do `AudioProcessing` já serializado) · `editVideoVolume` some da lista (redundante; permanece no controlador)

### Partículas, Elemento 3D, Cena 3D, Nulo/Grid, Precomp
- Todas as fichas atuais → "Propriedades do elemento" do tipo, reorganizadas em `ParameterRow` com Pronto primeiro · S (parâmetros principais) / P (resto)
- Estúdio 3D → botão "Abrir Estúdio" na categoria · P (a camada Cena 3D é P)
- `addGlbNode` (caminho paralelo morto) → removido do inventário como duplicata de `addModel3D` (permanece no código) · `alignCameraToRender`, `setScene3DView` → chamadores do estúdio (vistas) · P

### Exportação
- Presets 1080p/720p/4K + fps do projeto, galeria, estimativa de tamanho → A › Exportar · S (2 toques)
- Codec, taxa, PNG seq., Lottie, SVG, template (com campos expostos), SRT → aba "Avançado" da mesma folha · P

### Utilidades restantes do controlador
- `removeLayer`, `groupLayer` (singulares) → chamadores de E2 com uma camada · `cycleMaskMode`, `firstShapeBezier`, `propKeyframeTimes`, `clipHasTimeRemap`, `reverseNeedsProxy` → usados pelos adaptadores (estado dos chips) · `cancelGesture` → escape do gesto no palco · `Bone`/`TubeLimb` → **ficam sem destino de UI** (não são serializados nem alcançáveis; §8-Q7)

---

## 4. Componentes a criar (seção 9 do prompt) e arquivo de destino

Convenção: novos widgets em `lib/src/features/editor/presentation/shell/` e `…/context/`; adaptadores em `lib/src/features/editor/application/ui/`. Os arquivos atuais de `am/` são consumidos, fatiados e apagados quando o substituto passa nos testes.

| Componente | Arquivo de destino | Nasce de |
|---|---|---|
| `EditorShell` (5 zonas + FAB + métricas) | `presentation/shell/editor_shell.dart` | `editor_screen.dart` |
| `TopBar` | `presentation/shell/top_bar.dart` | `_TopBar` |
| `PreviewCanvas` (+ `ResizeHandle`, `SelectionHandles`, `QualityBadge`) | `presentation/shell/preview_canvas.dart` | `PreviewStage` (só gestos/escala; `CompositionView` fica onde está) |
| `TransportBar` (+ `TimecodeField`) | `presentation/shell/transport_bar.dart` | `_TransportBar` |
| `Timeline`, `TimelineRuler`, `LayerBar`, `KeyframeDiamond`, `GroupBreadcrumb` | `presentation/timeline/*.dart` | `am_timeline.dart` (fatiado) |
| `ContextSheet` (E1–E5, alça, níveis) | `presentation/context/context_sheet.dart` | `showParamSheet` + `ParamSheetShell` |
| `AddToolbar` (E1) · `AddMenu` | `presentation/context/add_toolbar.dart`, `add_menu.dart` | `add_layer_sheet.dart` |
| `CategoryGrid` (E2) · `QuickActionsRow` · `LayerHeader` | `presentation/context/category_grid.dart`, `quick_actions_row.dart`, `layer_header.dart` | `LayerToolsDock`, `_fileiras`, `_ActionBar`, `_UtilIcon` |
| `ParameterRow` (+ variantes `.number` (régua + valor tocável), `.pad`, `.dial`, `.color`, `.toggle`, `.choice`, `.angle`, `.point`, `.seed`) | `presentation/context/parameter_row.dart` | `AmTickRuler`, `AmValueChip`, `_RotationControl`, pads |
| `ParameterList` (E3) por categoria | `presentation/context/categories/*.dart` (transform, color_fill, border_shadow, blending, element_*, effects) | `TransformPanel`, `BlendingPanel`, `ColorFillPanel`, `ShapePanel`, folhas |
| `EffectList` (E4) · `EffectGallery` · `EffectThumbnailCache` | `presentation/context/effects/*.dart` · `application/ui/effect_thumbnails.dart` | `EffectsPanel`, `_addEffect` |
| `CurvePresetGrid` (E5) · `CurveEditor` (Pro) | `presentation/context/curve_preset_grid.dart` · `presentation/curve/curve_editor.dart` | `CurvePanel` |
| `ExportSheet` | `presentation/shell/export_sheet.dart` | `am/export_sheet.dart` |
| `ProjectSettingsSheet` (⚙) | `presentation/shell/project_settings_sheet.dart` | novo (`new_project_sheet` + overlay de diagnóstico) |
| `AspectRatioChips` | `features/projects/presentation/aspect_ratio_chips.dart` | pílulas do `projects_tab` |
| `OnboardingCoach` · `EmptyState` · `HelpButton` | `presentation/shell/onboarding.dart` | dicas do Estúdio 3D (`estudio_ux.dart`) |
| `ProModeToggle` · `proModeProvider` | `presentation/shell/pro_toggle.dart` · `application/ui/pro_mode.dart` | novo (prefs) |
| **Adaptadores** `EditorSession` (modo E1–E5, seleção, categoria aberta, retorno), `EditorLayoutMetrics`, `StageGestures`, `TimelineAxis` + `SnapTargets`, `KeyframeOwner`, `ParameterSpecs` (LayerProp/efeito → rótulo, faixa, unidade, isAdvanced), `LayerTypeStyle` (cor/ícone/rótulo) | `application/ui/*.dart` | `editor_screen.dart`, `preview_stage.dart:112-256`, `am_timeline.dart` (conta), `property_keyframe_context.dart`, `layer_look.dart` |

Tokens de design (Fase 1): `core/theme/tokens.dart` — cor por tipo (12, partindo de `layer_look.dart`), acento (lima `#B8FF3D` da logo, com teal como cor de keyframe/curva — Q5), superfícies, tipografia, espaçamento em grade de 8 pt, tamanhos mínimos (alvo 44 pt, régua 52 pt, tile 56–68 pt), tema claro (Q6).

---

## 5. Fases, estimativa e riscos

Cada fase termina com `flutter analyze` limpo, a suíte inteira verde (1.466 + os novos), um commit, e um relato (feito · falta · decisões · dúvidas). Estimativas em sessões de trabalho de ~4 h.

| Fase | Escopo | Testes que precisam continuar verdes | Estimativa | Riscos |
|---|---|---|---|---|
| **1 · Design system + shell** | Tokens; `EditorShell` com as 5 zonas fixas (top bar de um estado, preview real com alça, transporte com timecode/◆, timeline atual embutida, `ContextSheet` vazio com E1 e FAB); `EditorSession` e `EditorLayoutMetrics`; `ProModeToggle`. O editor antigo continua acessível até a Fase 3 por um flag de build de teste (`--dart-define=AUREA_UI_NOVA`). | `painel_cabe_na_tela`, `nivel1_shapes` (altura), `layer_menu_navigation`, `editor_hierarchy`, `ui_1011` | 3 | Recriar a cadeia do `PlaybackController` (7 consumidores). Mitigação: `playbackProvider` primeiro, sem mudar comportamento. |
| **2 · Timeline** | Coluna esquerda (olho, cadeado, ◆), reordenar por toque longo + botões, cor por tipo em tudo, grupos com breadcrumb (entrar/sair), expandir timeline, empacotamento de cortes na mesma linha (se aprovado), ímã e busca na régua. | `timeline_segue_o_relogio`, `sair_da_aba_e_ordem_na_timeline`, `ordem_das_camadas`, `motor-3d-performance` (listas virtuais) | 4 | Grupos com tempo local (precomp) na timeline: usar `GroupLayer.contentTimeAt`; risco de regressão de desempenho com 3.000 camadas (teste existente). |
| **3 · Painel contextual** | E1 `AddToolbar`/`AddMenu`; E2 `LayerHeader` + `QuickActionsRow` + `CategoryGrid`; E3 `ParameterRow` + categorias Transformar, Cor, Borda, Mesclagem (6 modos com miniatura), Propriedades do elemento (Texto com tamanho/negrito, Forma, Áudio, Legendas, Partículas, Elemento 3D, Precomp, Grid); transições entre estados; alça de altura. O editor antigo é apagado no fim desta fase. | todos os de painel (`transform_workspace`, `girar_da_volta`, `mescla_no_palco`, `am_gallery_ui`, `param_sheet_close`) | 6 | É a fase maior (`layer_menu.dart` 6.115 linhas). Mitigação: portar categoria a categoria mantendo os `ValueKey`s que os testes usam. |
| **4 · Efeitos e galeria** | `EffectGallery` com miniaturas geradas pelo motor e cacheadas; E4 com Pronto/Ajustar/Avançado para os 47 (preencher `montar`/`presets` dos 36); favoritos; salvar preset; assar; keyframe por parâmetro (Pro). | `nivel3_seis_efeitos`, `render_*`, `effects_*` | 4 | Custo de gerar 47 miniaturas: renderizar em isolate/lazy, 96 px, uma vez por versão do app. |
| **5 · Keyframes e easing** | ◆ na transporte; auto-keyframe ao mover no palco com alças; seleção de keyframe na timeline → E5 `CurvePresetGrid`; `CurveEditor` (Pro) com os parâmetros finos; loop; expressões em `ParameterRow` (Pro). | `graph_editor`, `curve_*`, `keyframe_*` | 3 | Seleção de keyframe por toque na barra compete com o arrasto; usar toque no losango (já são alvos de 22–24 px) e toque longo para arrastar. |
| **6 · Export, onboarding, Simples/Pro, ⚙ Projeto** | `ExportSheet` com presets (2 toques); ⚙ Projeto (proporção/resolução/fps/fundo/guias/motion blur/paleta/expostas/diagnóstico); `OnboardingCoach` (4 dicas), `EmptyState`, `HelpButton`; tabela Simples/Pro aplicada em todas as listas; SRT. | `export_*`, `estudio3d_ux` (dicas) | 3 | Fundo da composição exige campo novo no modelo (Q9). |
| **7 · QA** | Os 10 fluxos da seção 10 como testes de widget com contagem de toques; desempenho (timeline 30 camadas a 60 fps no Moto G05 e iPhone 13; arrasto de régua a 60 fps); alvos ≥ 44 pt (teste que mede todos os `GestureDetector` do editor); tablet/landscape (D e E laterais); capturas por tela para comparar com `design-reference/`. | suíte inteira | 3 | Aparelhos reais só por APK/IPA; sem emulador. |
| **8 · Ofício e dados (Pro)** | UI para dados CSV/JSON, caixa de texto, contêiner, pilha, contador; edição de 3 pontos; limpeza de voz/EQ. | novos | 3 | Só se aprovado (Q7); pode ficar para depois do beta. |

Total: **26 a 29 sessões** (Fases 1–7), mais 3 para a Fase 8. Cada fase gera APK/IPA de beta **apenas se pedido**.

---

## 6. Critérios de aceite (seção 10) — como cada fluxo fica

| Fluxo | Hoje | Depois (toques) | Onde |
|---|---|---|---|
| Novo projeto → editor | 3 (Novo, ajustar, Criar) | 3 (`+ Novo`, chip de proporção, `Criar`) | `AspectRatioChips` |
| Importar vídeo e vê-lo no playhead | 3 (`+`, Mídia, item) | 3 (E1 Mídia, item, pronto — nasce no playhead) | E1 |
| Dividir a camada no playhead | 1 (ação do cabeçote) | 1–2 (cabeçote ou ação rápida) | D/E2 |
| Mover no canvas com keyframe automático | 2 | 2 (selecionar + arrastar; auto-key ligado por padrão no Simples) | B |
| Aplicar efeito por preset | 4 (selecionar, Efeitos, Adicionar, item) | 3–4 (selecionar, Efeitos, galeria › miniatura) | E4 |
| Trocar easing de um keyframe por preset | 4 (painel, curva, preset) | 3 (tocar o losango na barra → E5 → preset) | E5 |
| Mudar opacidade com valor exato | 4 (Mesclar, Opacidade, régua, ?) — **sem digitar** | 4 (selecionar, Mesclagem, tocar o número, digitar) | `ParameterRow` |
| Exportar em 1080p | 2 sem seleção; 3 com | 2 (Exportar, preset 1080p) | A |
| Desfazer | 1 | 1 | A |
| Descobrir onde fica uma categoria | 2 (selecionar, olhar o dock) | 2 (selecionar, olhar o grid) | E2 |

Mais: nenhum modal cobre o preview (regra medida continua), inventário conferido feature a feature no fim da Fase 6, 60 fps com 30 camadas (teste com 3.000 já existe).

---

## 7. O que sai, e por quê (nada some; muda de lugar)

| Sai | Vai para | Justificativa |
|---|---|---|
| `LayerToolsDock` + `showLayerMenu` (duas superfícies) | E2 | uma superfície só (Alight) |
| `_ActionBar` que aparece/some | `QuickActionsRow` fixa em E2 | contexto muda conteúdo, não posição |
| 4 estados do cabeçalho | 1 estado | idem |
| Tiles Volume e Fade separados | 1 ação rápida "Volume" | libera 1 vaga das 7 |
| Opacidade em dois painéis | só em Mesclagem & Opacidade | um lugar por propriedade (Alight) |
| Duplicar e casca de cebola na transporte | E2 / ⚙ | transporte é reprodução (prompt §4C) |
| Engrenagem = diagnóstico | ⚙ Projeto › Diagnóstico | lugar de configurações (3 apps) |
| Aba Modelo inerte, `showAddLayerSheet`, `_SecaoFormas`, segundo "Motion blur" | apagados | código morto/duplicado |
| Menu "Mais ações" | inexistente | zero menus escondidos (regra do projeto) |

---

## 8. Perguntas abertas (preciso das respostas antes da Fase 1)

**Q1 · Simples/Pro.** Em 2026-09-03 você mandou apagar o interruptor de núcleo/estúdio e o Laboratório ("implementa TUDO já no app"). O prompt pede um toggle Simples/Pro. Proposta: o toggle volta, mas **só esconde itens de lista** (nunca muda lugar), padrão Simples para instalação nova e Pro para quem já tem projeto, com "Mais no modo Pro" no rodapé de cada grid. Aprova, ou o app continua com tudo à vista?

**Q2 · Slider.** A regra 10.1.1 proíbe sliders (`ui_1011_test.dart`): todo número usa a régua de arrasto. O prompt pede "slider + campo numérico". Proposta: `ParameterRow.number` = `[◆] [nome] [régua de arrasto AmTickRuler] [valor tocável → teclado numérico]`. A régua **é** o slider do app, e o valor tocável é o que faltava. A regra fica. Confirma?

**Q3 · Menu ⋯.** O prompt pede "duplicar / excluir / mais (⋯)" no cabeçalho E2; a regra do projeto é zero menus escondidos (dois com estado permitidos). Proposta: sem ⋯; o cabeçalho tem Duplicar · Excluir · Organizar (três ícones com estado à vista), e o resto vive na linha de ações rápidas rolável com rótulo. Confirma?

**Q4 · Altura do painel.** O prompt pede painel arrastável até ~60%; a regra do projeto é folha ≤ 40% e nunca cobrir o preview (piso 42%). Proposta: níveis 22% · 40% · "cheio" = até o topo da timeline (preview sempre visível, por volta de 55%), e a alça do preview permite ao usuário dar mais espaço ao painel quando quiser. Confirma?

**Q5 · Cores.** O editor usa acento teal (`AmColors`); a logo e a Home usam lima e violeta, que você já pediu para o editor em 2026-08-31. Proposta de tokens: acento lima para ação (Exportar, chips ativos, FAB), teal para keyframe/curva/playhead de contexto, violeta para seleção e grupo, e a paleta de 12 cores por tipo de `layer_look.dart` mantida. Confirma, ou prefere manter o teal atual?

**Q6 · Tema claro.** O prompt pede escuro padrão + claro. O app é só escuro. Fazer o claro agora custa mais uma sessão na Fase 1 e uma passada em cada painel. Entra nesta rodada ou fica para depois?

**Q7 · Os 60 métodos sem UI.** A regra "nenhuma funcionalidade pode ser removida" vale para o que é alcançável hoje; os 60 não são. Proposta: todos recebem destino (§3), e os de ofício/dados (18) + edição de 3 pontos (4) + voz/EQ ficam na **Fase 8**, depois do beta. `Bone`/`TubeLimb` (IK, não serializados) ficam sem UI. Aprova a ordem?

**Q8 · Cortes na mesma faixa.** O modelo do app é o do Alight (uma camada por linha, cada corte vira camada). O prompt pede que os pedaços fiquem na mesma faixa. Proposta: **empacotamento visual** — pedaços do mesmo vídeo que não se sobrepõem no tempo desenham na mesma linha, com "Juntar" visível na junção; o modelo não muda. Alternativa: manter uma linha por camada como hoje. Qual?

**Q9 · Fundo da composição.** Não existe no modelo (`VideoProject`). Adicionar `backgroundColor` é uma mudança pequena de domínio + serialização (retrocompatível: ausente = preto/transparente). Autoriza essa exceção à regra "não mexer no motor"?

**Q10 · Escopo do Estúdio 3D e do Edit Points.** Proposta: continuam como espaços em tela cheia abertos por "Propriedades do elemento" (o Estúdio foi reestruturado hoje e tem testes). Confirma que não entram na reescrita?

**Q11 · Tablet/landscape.** Entra na Fase 7 (D e E laterais) ou fica fora desta rodada? Não há aparelho de teste tablet; seria só por tamanho de janela nos testes.

**Q12 · Ordem de entrega.** As fases 1–3 deixam o editor antigo vivo atrás de um flag até o painel contextual novo cobrir tudo; só então o antigo é apagado. Prefere assim (sem regressão no beta) ou trocar de uma vez ao fim da Fase 1?

---

## 9. Decisões (aprovação de 2026-09-07: "faz tudo que está pedindo no doc e me entrega o app")

O desempate é o prompt. Onde o prompt e uma regra do projeto conflitam, a regra é revogada ou reinterpretada abaixo, e o teste que a codifica é ajustado na fase correspondente.

| Q | Decisão |
|---|---|
| Q1 | Toggle **Simples/Pro** volta, na top bar, persistido (`editor.pro`). Padrão Simples para instalação nova; Pro para quem já tinha projeto salvo. Pro só acrescenta. |
| Q2 | `ParameterRow.number` = `[◆][nome][régua de arrasto][valor tocável → teclado]`. A régua é o slider; a regra "sem `Slider(`" fica (protege contra sliders pequenos). |
| Q3 | Cabeçalho E2 = Duplicar · Excluir · **Mais** (botão com rótulo, ícone de grade, abre a lista completa de ações com rótulo). Não usa `ellipsis`; tudo que está em "Mais" também existe na linha de ações rápidas. |
| Q4 | Painel contextual com alça: 22% · 40% · até 60% do espaço útil; o preview encolhe junto mas nunca some (mínimo 96 px). O teto de 40% das folhas antigas deixa de valer quando elas são substituídas. |
| Q5 | Tokens: acento **lima** `#B8FF3D` (ação, Exportar, FAB, chips ativos), **teal** para keyframe/curva/playhead de contexto, **violeta** para seleção e grupo, 12 cores por tipo mantidas. |
| Q6 | Tema claro **entra**: tokens com paleta clara, `AppTheme.light`, seletor em Ajustes (Sistema / Escuro / Claro). O palco continua escuro nos dois temas. |
| Q7 | Fase 8 **entra** depois da 7 (dados/ofício, 3 pontos, voz/EQ). `Bone`/`TubeLimb` sem UI. |
| Q8 | **Empacotamento visual**: pedaços do mesmo vídeo sem sobreposição no tempo dividem a mesma linha; "Juntar" visível na junção. |
| Q9 | **Autorizado** `backgroundColor` em `VideoProject` (serializado; ausente = como hoje). |
| Q10 | Estúdio 3D e Edit Points continuam em tela cheia, abertos por Propriedades do elemento. |
| Q11 | Tablet/paisagem entra na Fase 7 por tamanho de janela (D e E laterais acima de 700 pt de largura). |
| Q12 | Reescrita **no lugar**, fase a fase, sem editor duplicado: cada fase termina compilando e com a suíte verde; os painéis antigos vivem dentro do painel contextual novo até serem substituídos na Fase 3. |

Entrega: APK (`--split-per-abi`) e IPA (tag `ipa-*`) ao fim, com versão nova.


---

## 10. Relato de entrega (2026-09-07 · beta 51, versão 1.5.0+51)

Tudo do documento foi implementado no lugar, fase a fase, com a suíte verde ao fim de cada fase (1.466 → 1.53x testes) e um commit por fase. O que segue é o que ficou feito, o que ficou diferente do plano (e por quê), e o que ainda falta.

### Feito, por fase

| Fase | Commit | O que entrou |
|---|---|---|
| 1 · Casca | `b9702bc` | Tokens (`AureaTokens`, paletas escura e clara); `EditorSession` (provider autoDispose) e `EditorLayoutMetrics` (preview nunca cede; painel encolhe; timeline ≥ 88); top bar de um estado (Voltar · nome · desfazer/refazer · ⚙ · Simples/Pro · Exportar sempre à vista); transporte com timecode tocável, loop, ◆ único e marca; painel contextual com alça e níveis 22/40/60 %; E1 barra de adicionar; E2 cabeçalho Duplicar·Excluir·Mais + ações rápidas + grade; seleção múltipla. Os botões flutuantes Dividir/Congelar saíram do cabeçote (cobriam a segunda linha). |
| 2 · Timeline | `7c970b0` | Coluna esquerda olho (`LayerMeta.hidden`, some do preview e da exportação, persiste) · cadeado (não move, não apara) · ◆; reordenar por toque longo + arrastar (o toque longo parado continua alternando a seleção múltipla); grupos: toque duplo / "Entrar" abre o grupo como composição de trabalho no controlador (filhos em tempo local; tudo que o editor faz funciona neles; sair grava num undo só; `projetoCompleto` é o que se salva e exporta); breadcrumb Projeto › Grupo; expandir timeline; empacotamento visual dos pedaços do mesmo arquivo com "Juntar" na junção; ímã com estado (persistido) e busca de camadas (Pro); `LayerKind` para cor por tipo em tudo. |
| 3 · Painel | `3f4cbdd` | `ParameterRow` [◆][nome][régua][valor tocável] (teclado aceita `1080/3` e `50%`; toque longo no nome reseta) e variantes ponto/interruptor/cor/livre; Transformar, Mesclagem (6 modos com miniatura no Simples, 27 no Pro) e Editar texto (conteúdo, fonte, tamanho, negrito, cor, animar) em linhas; Voltar escrito no cabeçalho do painel; menu modal antigo apagado (testes portados para o E2 real). |
| 4 · Efeitos | `740e112` | Os 47 efeitos com Pronto (3 presets) e Ajustar (≤ 3 números); galeria com miniatura por efeito renderizada pelo motor real sobre uma cartela e cacheada em memória e disco, busca, categorias em português, favoritos (Pro), presets de fábrica e salvos; cartão com Simples (Pronto/Ajustar) e Pro (Avançado, salvar preset, assar em keyframes). Correção de motor encontrada pelas miniaturas: o Force Motion Blur empilhava `Opacity(Positioned)` num `Stack`. O "+" saiu de cima da timeline e mora no transporte. |
| 5 · Keyframes | `7bb7b04` | Tocar o losango na barra abre o E5 (3 toques até o preset); grade de 14 presets com miniatura no Simples, editor de curva no Pro; expressões por propriedade (Pro, toque longo no valor, com o erro do motor); loop de keyframes no menu da curva (Pro); auto-key ao mover no palco no Simples sem ligar nada. |
| 6 · Export/⚙/onboarding/tema | `41ea23c` | Exportar em dois toques (presets 1080p/720p/4K); Pro com formato, tamanho, quadros, codec, qualidade, PNG, Lottie, SVG, template e **SRT**; ⚙ Projeto com nome, proporção, resolução, fps, **fundo da composição** (campo novo `backgroundColor`, pintado no preview e na exportação), casca de cebola; Pro: guias, motion blur da composição, paleta, propriedades expostas, diagnóstico; quatro dicas de primeiro uso, estado vazio com chamada, Ajuda no E1 e em ⚙; tema escuro/claro/sistema em Ajustes (o editor continua escuro). |
| 7 · QA | (este) | Tablet/paisagem acima de 700 pt: timeline e painel lado a lado (`editor-largo`); alvos de 44 pt nas barras de cima e de transporte (o Pro e o Exportar ganharam área de toque de 44 pt); fluxos da seção 10 como testes com contagem de toques (dividir 1, categoria 2, keyframe 1, opacidade exata 4, exportar 2, easing 3). |
| 8 · Ofício | (este) | Edição de 3 pontos: Entrada/Saída na régua (Pro) e ações rápidas Inserir aqui, Sobrescrever, Levantar, Extrair; voz e EQ (limpar ruído, voz, de-esser, graves/médios/agudos) na ficha de som (Pro); dados CSV em ⚙ Projeto e "vincular a uma coluna" no painel do texto (Pro). |

### O que ficou diferente do plano, e por quê

- **Transformar mantém as seis sub-abas** (Mover, Girar, Escalar, Inclinar, Pivô, Opacid.) também no Simples: os testes de caber na tela exigem as seis à vista e a navegação por abas é o que evita a pilha infinita de linhas. Inclinar e Pivô ficaram visíveis no Simples (o plano os punha no Pro).
- **Efeito assado, favoritos, Avançado, salvar preset**: Pro (tabela do §2), embora o §3 listasse favoritos no Simples.
- **Borda & Sombra, Volume/Fade, Legendas, Partículas, Elemento 3D, Grid, Precomp** continuam abrindo as fichas persistentes de antes (folhas sobre o painel), agora com Voltar no cabeçalho; não foram reescritas em `ParameterRow`. A Cena 3D e o Edit Points continuam em tela cheia (Q10).
- **Entrar no grupo** foi feito no controlador (o grupo vira a composição de trabalho) em vez de UI pura sobre `children`: era a única forma de os filhos aceitarem todas as operações sem duplicar o motor de tempo. Vínculos entre filhos continuam sem suporte, como antes.
- **Tema claro**: `AppColors` virou papéis que trocam de valor; as telas de Projetos/Ajustes acompanham. O editor é escuro por definição. Ainda não conferido a olho no aparelho.
- **Miniaturas dos efeitos** são renderizadas na primeira abertura da galeria (cartela pequena, motor real) e guardadas em disco por versão; em teste ficam na composição viva.

### O que falta (fora desta entrega)

- ~~Reescrever em `ParameterRow` as fichas que ainda são folhas~~ — **entregue em 2026-09-15 (§14)**.
- Medir no aparelho: 60 fps com 30 camadas, arrasto da régua, tema claro, miniaturas dos efeitos no Moto G05 e no iPhone 13.
- Onboarding por zona (tooltips ancorados) — as quatro dicas são um cartão único.
- ~~Expor propriedade pelo toque longo no nome do parâmetro~~ — **entregue em 2026-09-15 (§14)**.

### Como conferir

`flutter analyze` limpo (fora dos pacotes de terceiros); `flutter test` verde; capturas em `C:\Users\SnyX\Downloads\Aurea-UI\` geradas por `test/zz_ui_screenshot_test.dart` (não versionado).

---

## 11. Depois dos testadores (2026-09-07, beta 52)

Relatos: "a barra de adicionar fica aparecendo sozinha", "a rotação não gira", "aceite SVG", "abrir projetos XML sem erro" e, o mais importante, **"os testadores não estão conseguindo editar"**.

| O que mudou | Por quê |
|---|---|
| **Tocar no objeto no palco seleciona** (e o vazio tira a seleção). | Era a causa de "não consigo editar": só a barrinha da timeline selecionava, e ninguém descobre isso sozinho. |
| **Alças no palco**: a bolinha de baixo redimensiona, a de cima gira; arrastar move. A pinça continua valendo. | Um dedo é como se edita num celular. Sem alça, redimensionar exigia achar a categoria certa. |
| **A barra de adicionar só aparece pelo "+"**; qualquer toque na timeline fecha. Sem seleção, o painel é uma linha de dica e a timeline fica com o espaço. | A barra ocupava metade da tela o tempo todo sem ninguém ter pedido. |
| **Dial e pads ganham o gesto** (`AreaDeArrasto` aceita o ponteiro na hora). | O reconhecedor de rolagem aceita com 18 px e o de arrasto livre com 36: a rolagem ganhava sempre, e o dial "não girava". |
| **As 4 dicas de primeiro uso** passam a ensinar o palco. | Ensinavam a timeline, que é o caminho difícil. |
| **SVG**: arquivo entra como forma editável (um desenho por camada, com a cor de cada um), pela trilha de adicionar. | O app lia path data (ícones), não lia arquivo. |
| **Cena em XML**: importador reescrito para o formato real (`cena_xml_import.dart`). | O antigo lia 10 de 199 keyframes e achatava os grupos; agora são 24 camadas de topo, 153 keyframes, grupos, pais, olho fechado, caminhos editáveis e fade virando opacidade. |
| Barra da timeline **encolhe o conteúdo** em vez de estourar em clipes curtos. | Projetos importados têm pedaços de 266 ms. |

O que o importador de cena ainda deixa de fora, e diz na hora: mídia do aparelho de origem (`content://`), áudio, cor animada de preenchimento, e os efeitos sem equivalente (`transform2`, `squeeze`, `textspacing`, `pinchbulge`).

---

## 12. Rastreio e a mão enterrada (2026-09-07, beta 53)

Pedido: "Blobtracker FUNCIONAL e CAMERA TRACKER 3D funcional igual ao do AE", sem quebrar nada.

| O que entrou | Onde | Por quê |
|---|---|---|
| **⌖ Rastrear** nas ações rápidas de vídeo, antes de Reenquadrar e Estabilizar. | `quick_actions.dart`, `rastreio_sheet.dart` | As três leem o vídeo do mesmo jeito, mas só esta responde à pergunta que se faz primeiro ("como faço isso seguir aquilo?"). Escondida no fim da fila, ninguém achava. |
| **Grudar uma camada num blob** (com escala opcional). | `editor_controller.grudarNoBlob` | O Blob Tracker só desenhava caixas. Ver o rastreio é metade do trabalho; a outra metade é pendurar algo nele. |
| **Um toque analisa**: a folha cria o efeito de rastreio se não existir. | `rastreio_sheet.dart` | Antes eram quatro passos e era preciso saber o nome do efeito. |
| **Rastreio de câmera 3D**, com nuvem de pontos e câmera com keyframe por quadro. | `pontos_seguidos.dart`, `camera_solver3d.dart`, `algebra_numerica.dart`, `cena_do_rastreio.dart`, `camera_track_service.dart` | Sem ele, cena 3D em cima de vídeo é sempre adesivo. Detalhes em `docs/rastreio-camera-3d.md`. |
| **Recusa honesta** de plano de tripé ou cena plana, medida por resíduo de homografia. | `camera_solver3d.dart` | A solução degenerada parece certa (erro de reprojeção zero) e só se revela quando o objeto colado começa a nadar. |
| **MÃO ENTERRADA · 15 s**, quatro planos, na galeria de modelos. | `mao_enterrada_template.dart` | Cena de prova do motor 3D: malha orgânica varrida, terreno de três oitavas, céu texturizado e quatro câmeras com corte seco. |

### O que os testes pegaram, e ninguém teria visto olhando

- O chão das dunas estava **enrolado ao contrário**: sumia visto de cima, que é de onde a câmera sempre olha.
- As pontas dos dedos eram **cones** (leque até um ponto), e a palma terminava num espeto entre os dedos.
- A câmera do quarto plano, fora da própria tomada, ficava a **1726 unidades abaixo da areia** — invisível no vídeo, e um susto para quem abrisse a cena no editor.
- O rastreio de câmera aceitava **panorâmica de tripé** e devolvia uma cena inteira inventada.

### Como conferir

`flutter analyze` limpo (fora dos pacotes de terceiros); `flutter test` verde — 1588 passando, 1 pulado. Quadros da cena em `test/mao_visual_dump.dart` (pintor de CPU; a sombra do sol só aparece no motor de GPU).

---

## 13. O que o emulador mostrou (2026-09-07, beta 54)

Pedido: "use o emulador e verifique a UI... ache os erros e contras". Segue o que apareceu na tela, e a causa de cada um.

| Sintoma | Causa | Correção |
|---|---|---|
| **"Isso não funciona"** (Canto/Suave no editor de pontos). | O botão exigia um ponto **selecionado**, e quem entra no editor não tem seleção. Pior: ele era o 4º de seis num trilho vertical, e num painel baixo os três de baixo ficavam fora da tela. | As três ações desceram para uma **fileira com nome** (Canto, Apagar, Fechar), sempre visível. Sem seleção elas agem no **último ponto** — o que a pessoa acabou de cravar. |
| **Voltar "MUITO pequeno".** | Chevron de 16 com a palavra em corpo 10, os dois espremidos numa faixa de 18 px. Alvo de toque não existe em 18 px. | Faixa de 26, **só o símbolo**, chevron de 22 num alvo de 44 de largura. |
| **Preview com tarjas pretas.** | A altura vinha de uma fração fixa da tela (40%), ou seja, a proporção do **aparelho**. Um projeto 2,39:1 dentro disso sobra preto em cima e embaixo — e como o fundo do palco também é preto, o que se vê é um vazio enorme. | A altura sai da **proporção da composição** (só para encolher, nunca para crescer). O quadro encosta nas duas laterais e o que sobra vai para a timeline. |
| **Nome da camada ilegível** na timeline: "◆ABIS◆O◆c◆a 3◆". | Os losangos de keyframe eram desenhados no **meio da barra**, por cima do rótulo. | Os keyframes têm uma **tira própria** na base da barra; o nome fica na faixa de cima. |
| **A rotação não gira** (de novo, por outro caminho). | Duas causas somadas: as duas alças caíam a poucos pixels uma da outra em objeto pequeno (e a de escala é testada primeiro), e o **cartão de dicas cobria o alto do palco** — que é exatamente onde mora a alça de girar. | Afastamento mínimo de 30 px entre as alças, alças presas dentro do palco, e as quatro dicas saíram de cima do palco: agora ocupam o lugar da dica na folha de baixo. |
| **Faixa amarela de estouro** sobre o painel, em tela pequena. | Com o menu de adicionar cobrindo a timeline, sobrava uma tira de 12 px de timeline: nela não cabe nem a régua, e o conteúdo vazava 38 px. | Abaixo do mínimo útil, a timeline **cede o espaço inteiro** em vez de virar um risco quebrado. O painel de camada também cede a grade quando a folha é baixa demais. |

Fica registrado o que **não** foi corrigido: o rótulo de uma marca da régua ainda pode aparecer atrás dos botões do canto (ímã, busca, I/O). A tentativa de pôr fundo sólido ali mexeu na estrutura do Stack e derrubou o arrasto de reordenar; o ganho não pagava o risco nesta rodada.

---

## 14. O redesign profundo que faltava (2026-09-15)

Pedido do dono: "redesign profundo além da fluidez". O guia foi a própria lista "O que falta" do §10.

| O que entrou | Onde |
|---|---|
| **As fichas antigas falam a língua nova.** Som (fade, ganho, voz e EQ), Precomp, Estilo da legenda, Partículas (linhas e chips), Grade (linhas e as ANIMÁVEIS, com o diamante na própria linha: toque = keyframe, toque longo = curva), Máscara (presets e E3), Elemento 3D (tamanho/reflexo/brilho, agora em %), Propriedades do elemento das Formas (animáveis com diamante) e Extrude viraram `ParameterRow`: a linha inteira arrasta, o número digita o valor exato, o toque longo no nome reseta. Os botões avulsos de losango/curva sumiram — é a mesma gramática do Transformar em todo canto. | `audio_sheet`, `precomp_sheet`, `caption_style_sheet`, `oficio_sheets` (estilos), `layer_menu` (partículas, grade, máscara, elemento 3D, formas, extrude) |
| **Expor pelo toque longo no nome** (Pro): com só o reset, o toque longo reseta direto como sempre; com "expor" junto, um menu curto de duas ações. A lista de ⚙ Propriedades expostas deixou de ser só-remover; a Opacidade da camada é a primeira porta ligada. | `parameter_row.dart` (`onExpose`), linha de Opacidade do `layer_menu` |
| **Início: herói "Continuar editando"** — o projeto mais recente vira um cartão largo com a miniatura real, scrim, ficha e o botão com o verbo; a grade mostra o resto sem repetir. Menu do projeto com as mesmas chaves de sempre (nada de gesto escondido). | `projects_tab.dart` (`_CartaoContinuar`) |
| **Início: barra compacta com blur ao rolar** — o compacto do título grande do iOS: o cabeçalho rola embora; passando dele, surge a barra fina presa em cima (logo, nome, template, perfil). `AnimatedSwitcher`, não opacidade: barra escondida NÃO fica na árvore (tooltip e leitor de tela a achariam). | `projects_tab.dart` (`_BarraAoRolar`) |

Pinos novos: `inicio_continuar_test.dart` (herói, grade sem repetição, barra só depois de rolar) e `parameter_row_expor_test.dart` (reset direto sem menu; menu com as duas ações).

O que segue de fora, e por quê: **tooltips ancorados por zona** (onboarding é conversa para se ter acordado com o dono, não madrugada) e as **medições no aparelho** (não há aparelho aqui).
