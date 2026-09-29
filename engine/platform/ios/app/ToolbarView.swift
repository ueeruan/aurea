// Port of editor/TopBars.kt, Transport.kt and ChromeKit.kt.
import SwiftUI
import UIKit
import PhotosUI

struct TopBarView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    private var layer: LayerItem? { model.selectedLayer }
    private var ids: [NSNumber] { model.selection.map { NSNumber(value: $0) } }
    private var count: Int { model.selection.count }
    private var parent: Int64 { (model.detail["parentId"] as? NSNumber)?.int64Value ?? 0 }

    var body: some View {
        Group {
            if count >= 2 { batchBar }
            // Seção da camada aberta (redesenho 2026-09-29): `‹` + título centrado (SectionTopBar.kt).
            else if layer != nil, let title = sectionTitle { sectionBar(title) }
            else if let layer { layerBar(layer) }
            else { projectBar }
        }
        .frame(height: EditorLayout.topBar)
        .background(AureaColors.editorCanvas)
        // Abertos pelo menu da engrenagem (ShellOverlayHost) ou pela lupa das barras.
        .sheet(isPresented: $shell.performanceTest) { PerformanceTestPanel() }
        .sheet(isPresented: $shell.commandSearch) { CommandSearchView().environmentObject(model).environmentObject(shell) }
    }

    /// Título da seção aberta da camada (par do `panelSectionTitle` do Android);
    /// nil sem seção (doca, nada aberto ou o painel de exportar).
    private var sectionTitle: String? {
        switch model.panel {
        case .none, .dock, .exportPanel: return nil
        case .transform: return AureaText.t("sh_dock_transform")
        case .text: return AureaText.t("text_options")
        case .effects: return AureaText.t("panel_efeitos")
        case .layer3D: return AureaText.t("panel_material_ambiente")
        case .appearance: return AureaText.t("panel_mistura_opacidade")
        case .speed: return AureaText.t("panel_tempo_velocidade")
        case .clipEdit: return "Slip · Roll · Slide"
        case .audio: return AureaText.t("panel_som")
        case .shape: return AureaText.t("panel_cor_preenchimento")
        case .shapeEdit: return AureaText.t("panel_editar_forma")
        case .mask: return AureaText.t("panel_mascara_recorte")
        case .textAnimation: return AureaText.t("panel_animacao")
        case .curve: return AureaText.t("panel_easing_curve")
        case .presets: return AureaText.t("panel_presets")
        case .particles: return AureaText.t("panel_particulas")
        case .tracking: return AureaText.t("panel_rastreio")
        case .captions: return AureaText.t("panel_legendas")
        case .vector: return AureaText.t("panel_vetor")
        case .aiVideo: return AureaText.t("panel_ai_video")
        }
    }

    /// A BARRA DA SEÇÃO (Efeitos.dc.html, par do SectionTopBar.kt): só `‹` + o
    /// título centrado (17 pt, negrito) no fundo #1E2636. O `‹` fecha a seção e
    /// volta às ferramentas da camada (a doca), onde moram nome, vincular, lixeira e `⋯`.
    private func sectionBar(_ title: String) -> some View {
        ZStack {
            Color(red: 0x1E / 255, green: 0x26 / 255, blue: 0x36 / 255)
            Text(title)
                .font(.aurea(size: 17, weight: .bold))
                .foregroundStyle(AureaColors.text)
                .lineLimit(1).truncationMode(.tail)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 56)
            HStack(spacing: 0) {
                ShellBarButton(glyph: CupertinoGlyph.ChevronLeft, description: AureaText.t("pn_back_to_layer_tools"), size: 24, width: 44, height: 44) { model.panel = .none }
                Spacer(minLength: 0)
            }.padding(.leading, 6)
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("editor.sectionBar")
    }

    private func searchButton(tint: Color = AureaColors.text) -> some View {
        Button {
            if model.status.playing != 0 { model.playPause() }
            shell.commandSearch = true
        } label: { Image(systemName: "magnifyingglass").foregroundStyle(tint).frame(width: 44, height: 44) }
            .buttonStyle(.plain).accessibilityLabel(AureaText.t("edt_cmd_search_desc")).accessibilityIdentifier("commandSearchOpen")
    }

    /// Barra do PROJETO (nada escolhido), redesenho 2026-09-29: ‹ · nome · ⚙ · exportar.
    /// Selecionar camada, buscar, o menu ⋮, os ajustes e o teste de desempenho
    /// moram no menu da engrenagem (seção "Projeto" e as de sempre abaixo).
    private var projectBar: some View {
        HStack(spacing: 6) {
            if model.engine.precompDepth > 0 {
                ShellBarButton(glyph: CupertinoGlyph.ChevronLeft, description: AureaText.t("editor_voltar_composicao_principal"), size: 24, width: 44, height: 44) { model.editorBack() }
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("editor_editando_grupo")).font(.aurea(size: 11)).foregroundStyle(AureaColors.accent)
                    Text(model.composition["name"] as? String ?? model.projectName).font(.aurea(size: 17, weight: .bold)).lineLimit(1).truncationMode(.tail)
                }.frame(maxWidth: .infinity, alignment: .leading)
            } else {
                ShellBarButton(glyph: CupertinoGlyph.ChevronLeft, description: AureaText.t("editor_projetos"), size: 24, width: 44, height: 44) { model.editorBack() }
                ShellInlineName(name: model.projectName, placeholder: AureaText.t("edt_untitled"), limit: 320, size: 17, weight: .bold) { model.renameCurrentProject(to: $0) }
            }
            ShellBarButton(glyph: CupertinoGlyph.Gear, description: AureaText.t("editor_ajustes_projeto_mais"), size: 22, width: 44, height: 44) { open(.timelineMenu) }
                .accessibilityIdentifier("editor.projectMenu")
            Button { model.openExport() } label: {
                CupertinoGlyph.text(CupertinoGlyph.SquareArrowUp, size: 22, color: AureaColors.onAccent)
                    .frame(width: 46, height: 46)
                    .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 8))
                    .contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("editor_exportar"))
        }.padding(.leading, 6).padding(.trailing, 10)
    }
    private func layerBar(_ row: LayerItem) -> some View {
        HStack(spacing: 0) {
            ShellBarButton(glyph: CupertinoGlyph.ChevronLeft, description: AureaText.t("editor_voltar_tirar_selecao"), width: 44) { model.editorBack() }
            ShellInlineName(name: row.name, placeholder: AureaText.t("ios_unnamed_layer"), enabled: !row.locked) { value in
                model.mutate { $0.setLayer(row.id, name: value) }; model.refreshModel(force: true)
            }
            .id(row.id)
            // Esconder/mostrar a caixa da seleção no preview (ela tampa o que está por baixo).
            ShellBarButton(glyph: model.hideSelectionBox ? CupertinoGlyph.EyeSlash : CupertinoGlyph.Square,
                           description: AureaText.t(model.hideSelectionBox ? "editor_mostrar_caixa_selecao" : "editor_esconder_caixa_selecao"),
                           size: 19, width: 44, tint: model.hideSelectionBox ? AureaColors.accent : AureaColors.text) { model.hideSelectionBox.toggle() }
                .accessibilityIdentifier("stage.selectionBox")
            searchButton()
            linkMenuButton(tint: parent != 0 ? AureaColors.accent : AureaColors.text, width: 44)
            ShellBarButton(glyph: CupertinoGlyph.Trash, description: AureaText.t("editor_excluir_camada"), size: 19, width: 44) { removeSelection() }
            Button { open(.layerMenu) } label: { MaterialGlyph("filled.MoreHoriz", size: 22, color: AureaColors.text).frame(width: 44, height: 44) }
                .buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_mais_acoes_camada"))
        }.padding(.trailing, 4)
    }
    /// Lote: o mesmo conteúdo de antes, agora sobre o fundo do editor (a
    /// contagem no destaque diz que é um lote).
    private var batchBar: some View {
        HStack(spacing: 0) {
            ShellBarButton(glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_cancelar_selecao"), size: 18, width: 44, tint: AureaColors.text) { model.clearSelection() }
            Text(AureaText.t("ios_layers_selected_n", count)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.accent)
                .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
            // Ficar com UMA só: a lista troca o lote por ela num toque.
            pickLayerButton(tint: AureaColors.text, width: 40)
            searchButton()
            linkMenuButton(tint: AureaColors.text)
            ShellBarButton(glyph: ShellGlyph.FolderBadgePlus, description: AureaText.t("editor_agrupar"), size: 19, tint: AureaColors.text) { model.groupSelection() }
            if model.layers.contains(where: { model.selection.contains($0.id) && $0.kind == 12 }) {
                ShellBarButton(glyph: ShellGlyph.SquareSplit2x2, description: AureaText.t("editor_desagrupar"), size: 19, tint: AureaColors.text) {
                    for row in model.layers where model.selection.contains(row.id) && row.kind == 12 { model.ungroup(row.id) }
                }
            }
            ShellBarButton(glyph: CupertinoGlyph.Trash, description: AureaText.t("editor_excluir_selecao"), size: 19, tint: AureaColors.text) { removeSelection() }
            ShellBarButton(glyph: model.status.playing != 0 ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill,
                           description: AureaText.t(model.status.playing != 0 ? "editor_pausar" : "editor_reproduzir"), size: 20, tint: AureaColors.text) { model.playPause() }
        }.padding(.trailing, 2)
    }
    private func pickLayerButton(tint: Color, width: CGFloat) -> some View {
        ShellBarButton(glyph: CupertinoGlyph.RectangleStack, description: AureaText.t("editor_selecionar_uma_camada"), size: 19, width: width, tint: tint) { open(.searchLayers) }
            .accessibilityIdentifier("layerPicker.open")
    }
    private func linkMenuButton(tint: Color, width: CGFloat = 40) -> some View {
        GeometryReader { bounds in
            ShellBarButton(glyph: count == 1 && parent != 0 ? CupertinoGlyph.LinkCircleFill : CupertinoGlyph.Link,
                description: AureaText.t(parent != 0 ? "editor_vinculada_outra_camada_trocar" : "editor_vincular_outra_camada"), size: 20, width: width, tint: tint) {
                if model.status.playing != 0 { model.playPause() }
                shell.linkIds = Array(model.selection); shell.linkAnchor = bounds.frame(in: .global)
            }.accessibilityIdentifier("link.open")
        }.frame(width: width, height: 44)
    }
    private func open(_ value: ShellSheet) {
        if model.status.playing != 0 { model.playPause() }
        if value == .projectSettings { model.showProjectSettings = true } else { shell.sheet = value }
    }
    private func act(_ action: () -> Void) { shell.sheet = nil; action() }
    private func link(_ id: Int64) {
        if model.status.playing != 0 { model.playPause() }
        model.setParentMany(Array(model.selection), parent: id)
    }
    private func removeSelection() {
        model.deleteSelectedLayers()
    }
}

private struct ShellInlineName: View {
    let name: String
    let placeholder: String
    var limit = Int.max
    var enabled = true
    var size: CGFloat = 14
    var weight: Font.Weight = .semibold
    let rename: (String) -> Void
    @State private var editing = false
    @State private var value = ""
    @FocusState private var focused: Bool
    var body: some View {
        Group {
            if editing {
                TextField(placeholder, text: $value).textInputAutocapitalization(.sentences)
                    .focused($focused).submitLabel(.done).onSubmit(commit)
                    .padding(.horizontal, 8).padding(.vertical, 6).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    .padding(.trailing, 6).onAppear { focused = true }
                    .onChange(of: focused) { hasFocus in if !hasFocus { commit() } }
                    .onChange(of: value) { new in if new.count > limit { value = String(new.prefix(limit)) } }
            } else {
                Text(name.isEmpty ? placeholder : name).foregroundStyle(name.isEmpty ? StageInk.white40 : AureaColors.text)
                    .truncationMode(.tail)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 12).contentShape(Rectangle())
                    .onTapGesture { if enabled { value = name; editing = true } }
            }
        }.font(.aurea(size: size, weight: weight)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
    }
    private func commit() {
        guard editing else { return }; editing = false
        let text = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty && text != name { rename(text) }
    }
}

/// Transporte do redesenho 2026-09-29: 7 botões espalhados (desfazer, refazer,
/// início, play, fim, duplicar, tela cheia), alvos de 44 (o play, 48). Marca,
/// copiar/colar e a lupa da prévia moram no menu da engrenagem; copiar/colar
/// também no segurar do duplicar.
struct TransportView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var body: some View {
        Group {
            if model.stageManipulating { StageInfoBar() } else { buttons }
        }.frame(height: EditorLayout.transport).background(AureaColors.editorCanvas)
    }

    private var buttons: some View {
        HStack(spacing: 0) {
            undoButton
            Spacer(minLength: 0)
            redoButton
            Spacer(minLength: 0)
            startButton
            Spacer(minLength: 0)
            playButton
            Spacer(minLength: 0)
            endButton
            Spacer(minLength: 0)
            duplicateButton
            Spacer(minLength: 0)
            fullscreenButton
        }.padding(.horizontal, 6)
    }

    private var undoButton: some View {
        ShellBarButton(glyph: CupertinoGlyph.ArrowUturnLeft, description: AureaText.t("editor_desfazer"), size: 22, width: 44, height: 44,
                       enabled: model.status.canUndo != 0, disabledTint: AureaColors.transportDisabled) { model.undo() }
    }
    private var redoButton: some View {
        ShellBarButton(glyph: CupertinoGlyph.ArrowUturnRight, description: AureaText.t("editor_refazer"), size: 22, width: 44, height: 44,
                       enabled: model.status.canRedo != 0, disabledTint: AureaColors.transportDisabled) { model.redo() }
    }
    /// Tocar vai ao quadro 0; segurar é o passo de antes (marca, keyframe ou quadro anterior).
    private var startButton: some View {
        ShellBarButton(glyph: CupertinoGlyph.ArrowLeftToLine, description: AureaText.t("editor_ir_inicio_segure_anterior"), size: 24, width: 44, height: 44,
                       onLongPress: { model.stepTransport(-1) }, action: { model.seek(toFrame: 0) })
    }
    /// Tocar vai ao fim; segurar é o passo de antes (próxima marca, keyframe ou quadro).
    private var endButton: some View {
        ShellBarButton(glyph: CupertinoGlyph.ArrowRightToLine, description: AureaText.t("editor_ir_fim_segure_proximo"), size: 24, width: 44, height: 44,
                       onLongPress: { model.stepTransport(1) }, action: { model.seek(toFrame: model.compositionDuration) })
    }
    private var playButton: some View {
        ZStack(alignment: .bottomTrailing) {
            ShellBarButton(glyph: model.status.playing != 0 ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill,
                           description: AureaText.t(model.looping ? "editor_repeticao_ligada_segure_desligar" : (model.status.playing != 0 ? "editor_pausar" : "editor_reproduzir_segure_repetir")),
                           size: 26, width: 48, height: 48, tint: model.looping ? AureaColors.accent : AureaColors.text,
                           onLongPress: { model.setLooping(!model.looping) }, action: { model.playPause() })
            if model.looping { CupertinoGlyph.text(CupertinoGlyph.Repeat, size: 11, color: AureaColors.accent).padding(.trailing, 6).padding(.bottom, 7).allowsHitTesting(false) }
        }
    }
    /// Tocar duplica as camadas escolhidas (apagado sem nenhuma); segurar abre copiar e colar.
    private var duplicateButton: some View {
        let empty: Bool = model.selection.isEmpty
        return ShellBarButton(glyph: CupertinoGlyph.PlusSquareOnSquare, description: AureaText.t("editor_duplicar_camada_segure_copiar"), size: 22, width: 44, height: 44,
                              tint: empty ? AureaColors.transportDisabled : AureaColors.text,
                              onLongPress: {
                                  if model.status.playing != 0 { model.playPause() }
                                  shell.sheet = .copyPaste
                              }, action: {
                                  guard !model.selection.isEmpty else { return }
                                  model.engine.duplicateLayers(model.selection.map { NSNumber(value: $0) })
                                  model.refreshModel(force: true)
                              })
            .accessibilityIdentifier("transport.duplicate")
    }
    private var fullscreenButton: some View {
        ShellBarButton(glyph: model.fullscreen ? CupertinoGlyph.FullscreenExit : CupertinoGlyph.Fullscreen,
                       description: AureaText.t(model.fullscreen ? "editor_sair_tela_cheia" : "editor_tela_cheia"), size: 22, width: 44, height: 44) {
            model.fullscreen.toggle(); model.invalidatePreview()
        }
    }
}

private struct StageInfoBar: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        let position = StageGeom.floats(model.detail["position"])
        let scale = StageGeom.floats(model.detail["scale"])
        let rotation = StageGeom.floats(model.detail["rotation"])
        HStack(spacing: 0) {
            pair("X", String(Int((position.first ?? 0).rounded())))
            pair("Y", String(Int((position.count > 1 ? position[1] : 0).rounded())))
            pair(AureaText.t("editor_escala"), "\(Int(((scale.first ?? 1) * 100).rounded()))%")
            pair(AureaText.t("editor_rotacao"), String(format: "%.1f°", rotation.count > 2 ? rotation[2] : 0).replacingOccurrences(of: ".", with: ","))
        }.padding(.horizontal, 12).frame(height: EditorLayout.transport).background(AureaColors.editorCanvas)
    }
    private func pair(_ label: String, _ value: String) -> some View {
        VStack(spacing: 0) {
            Text(label).font(.aurea(size: 10.5)).foregroundStyle(StageInk.white40)
            Text(value).font(.aurea(size: 13, weight: .semibold)).monospacedDigit().foregroundStyle(AureaColors.text)
        }.lineLimit(1).frame(maxWidth: .infinity)
    }
}

struct FullscreenTimeBar: View {
    @EnvironmentObject private var model: AureaModel
    @State private var dragging = false
    @State private var wasPlaying = false
    var body: some View {
        HStack(spacing: 12) {
            Text(ShellClock.tenths(model.status.playhead, Float(model.compositionFps))).foregroundStyle(AureaColors.text)
            GeometryReader { geometry in
                let fraction = model.compositionDuration > 1 ? min(1, max(0, Double(model.status.playhead) / Double(model.compositionDuration - 1))) : 0
                Canvas { context, size in
                    let y = size.height / 2, x = size.width * fraction
                    var track = Path(); track.move(to: CGPoint(x: 0, y: y)); track.addLine(to: CGPoint(x: size.width, y: y))
                    context.stroke(track, with: .color(AureaColors.track), style: StrokeStyle(lineWidth: dragging ? 5 : 3, lineCap: .round))
                    var progress = Path(); progress.move(to: CGPoint(x: 0, y: y)); progress.addLine(to: CGPoint(x: x, y: y))
                    context.stroke(progress, with: .color(AureaColors.accent), style: StrokeStyle(lineWidth: dragging ? 5 : 3, lineCap: .round))
                    let r: CGFloat = dragging ? 9 : 7
                    context.fill(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 2)), with: .color(AureaColors.text))
                }.contentShape(Rectangle())
                    .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                        let fraction = min(1, max(0, value.location.x / max(1, geometry.size.width)))
                        let frame = Int64((fraction * CGFloat(max(0, model.compositionDuration - 1))).rounded())
                        if !dragging {
                            wasPlaying = model.status.playing != 0
                            if wasPlaying { model.playPause() }
                            model.engine.run { $0.scrubBegin() }; dragging = true
                        }
                        model.engine.run { $0.scrub(toFrame: frame) }; model.optimisticPlayhead(frame)
                    }.onEnded { _ in
                        model.engine.run { $0.scrubEnd() }; dragging = false
                        if wasPlaying { model.playPause() }; wasPlaying = false
                    })
            }
            Text(ShellClock.tenths(model.compositionDuration, Float(model.compositionFps))).foregroundStyle(AureaColors.muted)
        }.font(.aurea(size: 12, weight: .semibold)).monospacedDigit().padding(.horizontal, 14).frame(height: StageDim.fullscreenTimeBar)
            .background(AureaColors.editorTopBar)
            .onDisappear { if dragging { model.engine.run { $0.scrubEnd() }; dragging = false; if wasPlaying { model.playPause() }; wasPlaying = false } }
    }
}

private struct CopyPasteMenu: View {
    @EnvironmentObject private var model: AureaModel
    let dismiss: () -> Void
    private var ids: [NSNumber] { model.selection.map { NSNumber(value: $0) } }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ShellMenuSection("editor_copiar_colar")
            ShellMenuRow(CupertinoGlyph.DocOnDoc, "editor_copiar_camada", enabled: !ids.isEmpty) { act { model.engine.copyLayers(ids) } }
            ShellMenuRow(CupertinoGlyph.DocOnClipboard, "editor_colar_camada_cabecote", enabled: model.engine.clipboardState & 1 != 0) { act { model.engine.pasteLayers(model.status.playhead); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.PlusSquareOnSquare, "editor_duplicar_camada", enabled: !ids.isEmpty) { act { model.engine.duplicateLayers(ids); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.CheckmarkSquare, "editor_selecionar_todas_camadas", enabled: model.layers.count >= 2) { act { model.selectAll() } }
            ShellMenuRow(CupertinoGlyph.Square, "editor_limpar_selecao") { act { model.clearSelection() } }
            ShellMenuSection("editor_estilo_efeitos")
            ShellMenuRow(CupertinoGlyph.Paintbrush, "editor_copiar_estilo", enabled: !ids.isEmpty) { act { if let id = model.primarySelection { model.engine.copyStyle(id) } } }
            ShellMenuRow(ShellGlyph.PaintbrushFill, "editor_colar_estilo", enabled: !ids.isEmpty && model.engine.clipboardState & 2 != 0, detail: "editor_mesclagem_opacidade_efeitos_cores") { act { model.engine.pasteStyle(ids); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.Sparkles, "editor_copiar_efeitos", enabled: !ids.isEmpty) { act { if let id = model.primarySelection { model.engine.copyEffects(id) } } }
            ShellMenuRow(CupertinoGlyph.WandStars, "editor_colar_efeitos", enabled: !ids.isEmpty && model.engine.clipboardState & 4 != 0) { act { model.engine.pasteEffects(ids); model.refreshModel(force: true) } }
            ShellMenuSection("editor_keyframes")
            ShellMenuRow(CupertinoGlyph.DocOnDoc, "editor_copiar_keyframes_cabecote", enabled: !ids.isEmpty) { act { if let id = model.primarySelection { model.engine.copyKeyframes(id, atFrame: Int32(clamping: model.status.playhead)) } } }
            ShellMenuRow(CupertinoGlyph.DocOnClipboard, "editor_colar_keyframes_cabecote", enabled: !ids.isEmpty && model.engine.clipboardState & 8 != 0) { act { model.engine.pasteKeyframes(ids, atFrame: Int32(clamping: model.status.playhead)); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.DocOnDoc, "editor_copiar_animacao", enabled: !ids.isEmpty, detail: "editor_copiar_animacao_detalhe") { act { model.copyAnimation() } }
            ShellMenuRow(CupertinoGlyph.DocOnClipboard, "editor_colar_animacao", enabled: !ids.isEmpty && model.engine.clipboardState & 8 != 0) { act { model.engine.pasteKeyframes(ids, atFrame: Int32(clamping: model.status.playhead)); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.WandStars, "editor_otimizar_keyframes", enabled: !ids.isEmpty, detail: "editor_otimizar_keyframes_detalhe") { act { model.optimizeKeyframes() } }
        }
    }
    private func act(_ action: () -> Void) { dismiss(); action() }
}

private struct ShellMenuHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}
private struct ShellMenu<Content: View>: View {
    let maxHeight: CGFloat
    let bottomInset: CGFloat
    let onDismiss: () -> Void
    @ViewBuilder let content: () -> Content
    @State private var contentHeight: CGFloat = 1000
    @State private var drag: CGFloat = 0
    var body: some View {
        VStack(spacing: 0) {
            Capsule().fill(StageInk.menuHandle).frame(width: 36, height: 4).padding(.top, 6)
                .frame(maxWidth: .infinity).contentShape(Rectangle().inset(by: -12))
                .gesture(DragGesture().onChanged { drag = max(0, $0.translation.height) }.onEnded { value in
                    if value.translation.height > 60 || value.predictedEndTranslation.height > 140 { onDismiss() }; drag = 0
                })
            ScrollView {
                VStack(alignment: .leading, spacing: 0, content: content).padding(.bottom, 12 + bottomInset)
                    .background(GeometryReader { bounds in Color.clear.preference(key: ShellMenuHeightKey.self, value: bounds.size.height) })
            }.frame(height: min(maxHeight, contentHeight))
        }.onPreferenceChange(ShellMenuHeightKey.self) { contentHeight = $0 }
            .background(AureaColors.editorPanel).foregroundStyle(AureaColors.text)
            .clipShape(ShellTopCorners(radius: 18)).offset(y: drag)
    }
}
private struct ShellTopCorners: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path { Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topLeft, .topRight], cornerRadii: CGSize(width: radius, height: radius)).cgPath) }
}

private struct ShellMenuSection: View {
    let key: String
    init(_ key: String) { self.key = key }
    var body: some View {
        Text(AureaText.t(key)).font(.aurea(size: 12, weight: .semibold)).tracking(0.2).foregroundStyle(AureaColors.muted)
            .padding(.horizontal, 20).padding(.top, 14).padding(.bottom, 4)
    }
}
private struct ShellMenuRow: View {
    let glyph: Character
    let key: String
    var title: String? = nil
    var enabled = true
    var checked: Bool? = nil
    var detail: String? = nil
    var danger = false
    /// Símbolo do sistema no lugar do glifo (ex.: a lupa da prévia, a mesma do botão antigo).
    var symbol: String? = nil
    let action: () -> Void
    init(_ glyph: Character, _ key: String, title: String? = nil, enabled: Bool = true, checked: Bool? = nil,
         detail: String? = nil, danger: Bool = false, symbol: String? = nil, action: @escaping () -> Void) {
        self.glyph = glyph; self.key = key; self.title = title; self.enabled = enabled; self.checked = checked
        self.detail = detail; self.danger = danger; self.symbol = symbol; self.action = action
    }
    var body: some View {
        Button(action: action) {
            HStack(spacing: 14) {
                if let symbol {
                    Image(systemName: symbol).font(.system(size: 17)).foregroundStyle(color).frame(width: 20, height: 20)
                } else {
                    CupertinoGlyph.text(glyph, size: 20, color: color).frame(width: 20, height: 20)
                }
                VStack(alignment: .leading, spacing: 0) {
                    Text(title ?? AureaText.t(key)).font(.aurea(size: 15)).foregroundStyle(color)
                    if let detail { Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                }.frame(maxWidth: .infinity, alignment: .leading)
                if checked == true { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 18, color: AureaColors.accent) }
            }.padding(.horizontal, 20).padding(.vertical, 8).frame(minHeight: 48)
        }.buttonStyle(.plain).disabled(!enabled)
            // O glifo da fonte de ícones entrava no rótulo como vazio (", Colar efeitos").
            .accessibilityLabel(title ?? AureaText.t(key))
            .accessibilityHint(detail.map { AureaText.t($0) } ?? "")
            .accessibilityAddTraits(checked == true ? .isSelected : [])
    }
    private var color: Color { !enabled ? StageInk.disabledMuted : danger ? AureaColors.danger : AureaColors.text }
}

// One editor-sized host for bottom sheets, dialogs and anchored shell popups.
@MainActor struct ShellOverlayHost: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @State private var search = ""
    @State private var linkHeight: CGFloat = 420
    @FocusState private var searchFocused: Bool
    /// "Substituir mídia" / "Informações da mídia" (ProjectTransfer.swift).
    @State private var replaceTarget: LayerRef?
    @State private var infoTarget: LayerRef?
    private var layer: LayerItem? { model.selectedLayer }
    private var ids: [NSNumber] { model.selection.map { NSNumber(value: $0) } }
    private var searchHits: [LayerItem] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        return model.layers.filter { row in
            query.isEmpty || row.name.localizedCaseInsensitiveContains(query) || (row.kind == 4 && (model.engine.text(forLayer: row.id)?["content"] as? String ?? "").localizedCaseInsensitiveContains(query))
        }
    }
    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                if let sheet = shell.sheet {
                    switch sheet {
                    case .renameLayer:
                        AureaNamePrompt(title: AureaText.t("editor_renomear"), initial: layer?.name ?? "", onConfirm: { name in
                            if let layer, !layer.locked { model.mutate { $0.setLayer(layer.id, name: name) }; model.refreshModel(force: true) }
                        }, onDismiss: dismiss)
                    case .goToTime: ShellTimeDialog(initial: shell.timeInput, onDismiss: dismiss)
                    case .projectSettings: EmptyView()
                    default:
                        StageInk.menuScrim.ignoresSafeArea().onTapGesture(perform: dismiss)
                        ShellMenu(maxHeight: geometry.size.height * (sheet == .searchLayers ? 0.7 : 0.8), bottomInset: geometry.safeAreaInsets.bottom, onDismiss: dismiss) {
                            switch sheet {
                            case .layerMenu: layerMenu
                            case .timelineMenu: timelineMenu
                            case .copyPaste: CopyPasteMenu(dismiss: dismiss)
                            case .searchLayers: searchMenu
                            default: EmptyView()
                            }
                        }.id(sheet).frame(maxHeight: .infinity, alignment: .bottom).ignoresSafeArea(edges: .bottom)
                    }
                }
                if let anchor = shell.linkAnchor {
                    Color.clear.contentShape(Rectangle()).ignoresSafeArea().onTapGesture { shell.linkAnchor = nil }
                    let size = CGSize(width: max(1, min(300, geometry.size.width - 16)), height: min(420, min(geometry.size.height - 16, linkHeight)))
                    let pos = popupPosition(anchor, size, geometry, gap: 0, flip: false)
                    ShellLinkPopup(ids: shell.linkIds, dismiss: { shell.linkAnchor = nil }, onHeight: { linkHeight = $0 }).frame(width: size.width, height: size.height).offset(x: pos.x, y: pos.y)
                }
                if let anchor = shell.resolutionAnchor {
                    Color.clear.contentShape(Rectangle()).ignoresSafeArea().onTapGesture { shell.resolutionAnchor = nil }
                    let pos = popupPosition(anchor, CGSize(width: 220, height: 252), geometry, gap: 4, flip: true)
                    resolutionPopup.frame(width: 220).offset(x: pos.x, y: pos.y)
                }
            }.onChange(of: shell.sheet) { sheet in if sheet == .searchLayers { search = "" } }
        }
        .allowsHitTesting(shell.sheet != nil || shell.linkAnchor != nil || shell.resolutionAnchor != nil)
        .sheet(item: $replaceTarget) { target in
            ShellMediaPicker(filter: .any(of: [.images, .videos])) { url, video in
                replaceTarget = nil
                if let url { model.replaceMedia(layer: target.id, url: url, video: video) }
            }
        }
        .sheet(item: $infoTarget) { target in MediaInfoSheetView(path: model.layerSourcePath(target.id)) }
    }
    private func popupPosition(_ anchor: CGRect, _ size: CGSize, _ geometry: GeometryProxy, gap: CGFloat, flip: Bool) -> CGPoint {
        let origin = geometry.frame(in: .global).origin
        let x = (anchor.maxX - origin.x - size.width).clamped(to: 8...max(8, geometry.size.width - size.width - 8))
        var y = anchor.maxY - origin.y + gap
        if y + size.height > geometry.size.height - 8 { y = flip ? anchor.minY - origin.y - gap - size.height : geometry.size.height - size.height - 8 }
        return CGPoint(x: x, y: max(8, y))
    }
    private var resolutionPopup: some View {
        VStack(spacing: 0) {
            Button(model.rawPlayback ? AureaText.t("ios_raw_back_to_compositor") : "AUREA RAW PLAYBACK TEST") {
                shell.resolutionAnchor = nil; model.toggleRawPlayback()
            }.font(.aurea(size: 11)).padding(8).accessibilityIdentifier("rawPlaybackToggle")
            ForEach(Array(["AUTO", "Full", "1/2", "1/4", "1/8"].enumerated()), id: \.offset) { index, name in
                let denominator: UInt32 = [1, 1, 2, 4, 8][index]
                let on = index == 0 ? model.status.previewAuto != 0 : model.status.previewAuto == 0 && model.status.previewDenominator == denominator
                Button {
                    shell.resolutionAnchor = nil; model.setPreviewScale(num: 1, den: denominator, auto: index == 0)
                } label: {
                    HStack(spacing: 10) {
                        Rectangle().fill(on ? AureaColors.accent : Color.clear).frame(width: 4, height: 24)
                        Text(name).font(.aurea(size: 13)).foregroundStyle(on ? AureaColors.accent : AureaColors.text).frame(maxWidth: .infinity, alignment: .leading)
                    }.padding(.trailing, 10).frame(height: 40)
                }.buttonStyle(AureaPressStyle(shrink: 1))
            }
        }.padding(.vertical, 4).background(AureaColors.pill, in: RoundedRectangle(cornerRadius: 8))
    }
    private func dismiss() { shell.dismiss(); UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil) }
    private func act(_ action: () -> Void) { dismiss(); action() }
    private func removeSelection() {
        model.deleteSelectedLayers()
    }
    @ViewBuilder private var layerMenu: some View {
        if let row = layer {
            let index = model.layers.firstIndex(where: { $0.id == row.id }) ?? 0
            let inside = model.status.playhead > Int64(row.startFrame) && model.status.playhead < Int64(row.endFrame)
            ShellMenuSection("editor_camada")
            ShellMenuRow(CupertinoGlyph.Pencil, "editor_renomear", enabled: !row.locked) { shell.sheet = .renameLayer }
            ShellMenuRow(row.locked ? ShellGlyph.LockOpenFill : CupertinoGlyph.LockFill, row.locked ? "editor_desbloquear_camada" : "editor_bloquear_camada",
                         detail: row.locked ? "editor_volta_aceitar_movimento_edicao" : "editor_nao_aceita_movimento_corte_nem_edicao") { model.mutate { $0.setLayer(row.id, locked: !row.locked) }; model.refreshModel(force: true) }
            ShellMenuRow(row.visible ? CupertinoGlyph.EyeSlash : CupertinoGlyph.Eye, row.visible ? "editor_ocultar_camada" : "editor_mostrar_camada") { model.mutate { $0.setLayer(row.id, visible: !row.visible) }; model.refreshModel(force: true) }
            ShellMenuRow(CupertinoGlyph.Speaker2, "editor_solo", checked: row.solo, detail: "editor_alguma_camada_solo_previa_som_so") { model.mutate { $0.setLayer(row.id, solo: !row.solo) }; model.refreshModel(force: true) }
            // Linha Magnética: só em trecho com FONTE (vídeo, imagem, áudio ou
            // pré-composição). Texto, forma, nulo, ajuste, câmera e partículas
            // são composição, não montagem.
            if row.kind == 1 || row.kind == 2 || row.kind == 3 || row.kind == 12 {
                ShellMenuRow(CupertinoGlyph.Link, "editor_linha_magnetica", checked: row.magnetic,
                             detail: "editor_linha_magnetica_detalhe") { act { model.setLayerMagneticTrack(row.id, !row.magnetic) } }
            }
            if row.kind != 3 {
                ShellMenuRow(CupertinoGlyph.SliderHorizontal3, "editor_camada_ajuste", checked: row.adjustment, detail: "editor_efeitos_desta_camada_valem_todas_baixo") { model.mutate { $0.setLayer(row.id, adjustment: !row.adjustment) }; model.refreshModel(force: true) }
                if row.adjustment {
                    // Camadas afetadas: todas abaixo → só a logo abaixo (um grupo conta
                    // como uma) → só as escolhidas (a lista marcada do app antigo).
                    let scope = model.engine.adjustmentScope(row.id)
                    ShellMenuRow(CupertinoGlyph.SliderHorizontal3, "la_affected",
                                 title: AureaText.t("la_affected") + " · " + AureaText.t(scope == 1 ? "la_affected_below" : scope == 2 ? "la_affected_chosen" : "la_affected_all"),
                                 detail: scope == 2 ? "la_affected_chosen_hint" : "la_affected_hint") { model.engine.setAdjustmentScope((scope + 1) % 3, forLayer: row.id); model.refreshModel(force: true) }
                    if scope == 2 {
                        let below = Array(model.layers.dropFirst(index + 1)).filter { $0.kind != 3 }
                        let chosen = Set(model.engine.adjustmentTargets(row.id).map(\.int64Value))
                        if below.isEmpty { ShellMenuRow(CupertinoGlyph.SliderHorizontal3, "la_affected_none_below", enabled: false) {} }
                        ForEach(below) { b in
                            ShellMenuRow(CupertinoGlyph.SliderHorizontal3, "la_affected_chosen", title: b.name, checked: chosen.contains(b.id)) {
                                model.engine.setAdjustmentTarget(b.id, on: !chosen.contains(b.id), forLayer: row.id); model.refreshModel(force: true)
                            }
                        }
                    }
                }
                ShellMenuRow(CupertinoGlyph.Grid, "editor_guia_nao_exporta", checked: row.guide, detail: "editor_aparece_aqui_editor_fica_fora_video") { model.mutate { $0.setLayer(row.id, guide: !row.guide) }; model.refreshModel(force: true) }
            }
            ShellMenuRow(CupertinoGlyph.PlusSquareOnSquare, "editor_duplicar") { act { model.engine.duplicateLayers(ids); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.DocOnDoc, "editor_copiar_camada") { act { model.engine.copyLayers(ids) } }
            ShellMenuRow(CupertinoGlyph.DocOnClipboard, "editor_colar_camada_cabecote", enabled: model.engine.clipboardState & 1 != 0) { act { model.engine.pasteLayers(model.status.playhead); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.Paintbrush, "editor_copiar_estilo") { act { model.engine.copyStyle(row.id) } }
            ShellMenuRow(ShellGlyph.PaintbrushFill, "editor_colar_estilo", enabled: model.engine.clipboardState & 2 != 0) { act { model.engine.pasteStyle(ids); model.refreshModel(force: true) } }
            ShellMenuRow(CupertinoGlyph.ArrowUpToLine, "editor_trazer_frente", enabled: index > 0) { act { model.reorderLayer(row.id, displayIndex: index - 1) } }
            ShellMenuRow(CupertinoGlyph.ArrowDownToLine, "editor_enviar_tras", enabled: index < model.layers.count - 1) { act { model.reorderLayer(row.id, displayIndex: index + 1) } }
            ShellMenuSection("editor_etiqueta")
            LazyVGrid(columns: Array(repeating: GridItem(.fixed(34), spacing: 2), count: 8), spacing: 2) {
                ForEach(0...AureaColors.labelPalette.count, id: \.self) { label in
                    Button { model.mutate { $0.setLayer(row.id, label: UInt32(label)) }; model.refreshModel(force: true) } label: {
                        ZStack {
                            if label == 0 { Circle().stroke(AureaColors.muted, lineWidth: 1.5); CupertinoGlyph.text(ShellGlyph.Nosign, size: 14, color: AureaColors.muted) }
                            else { Circle().fill(AureaColors.labelPalette[label - 1]) }
                        }.frame(width: 24, height: 24).frame(width: 34, height: 34)
                            .overlay { if row.label == UInt32(label) { Circle().stroke(AureaColors.accent, lineWidth: 2) } }
                    }.buttonStyle(.plain)
                }
            }.padding(.horizontal, 14).padding(.vertical, 4)
            if row.kind != 3 {
                ShellMenuSection("editor_grupo")
                // Alternar grupo (app antigo): agrupa as escolhidas; num grupo, desagrupa.
                ShellMenuRow(ShellGlyph.SquareSplit2x2, "la_toggle_group", detail: "la_toggle_group_hint") { act { model.toggleGroup() } }
                if row.kind == 12 {
                    ShellMenuRow(CupertinoGlyph.ArrowDownRightSquare, "editor_editar_grupo") { act { model.openGroup(row.id) } }
                    ShellMenuRow(ShellGlyph.SquareSplit2x2, "editor_desagrupar") { act { model.ungroup(row.id) } }
                    let through = model.engine.groupCameraPassThrough(row.id) == 1
                    ShellMenuRow(CupertinoGlyph.Camera, "la_group_camera", checked: through, detail: "la_group_camera_hint") {
                        if model.engine.setGroupCameraPassThrough(!through, forLayer: row.id) {
                            model.toast = AureaText.t(through ? "app_group_camera_sealed" : "app_group_camera_reaches")
                        }
                        model.refreshModel(force: true)
                    }
                } else { ShellMenuRow(CupertinoGlyph.RectangleStack, "editor_converter_grupo") { act { model.groupSelection() } } }
                // Pôr esta camada num grupo que já existe aqui / tirar do grupo aberto.
                ForEach(model.layers.filter { $0.kind == 12 && $0.id != row.id }) { group in
                    ShellMenuRow(CupertinoGlyph.RectangleStack, "la_add_to_group", title: AureaText.t("la_add_to_group") + " · " + group.name) {
                        act {
                            let why = model.engine.addLayers([NSNumber(value: row.id)], toGroup: group.id)
                            if !why.isEmpty { model.toast = AureaText.t("app_group_add_failed", why) }
                            model.refreshModel(force: true)
                        }
                    }
                }
                if model.engine.precompDepth > 0 {
                    ShellMenuRow(CupertinoGlyph.ArrowUturnLeft, "la_remove_from_group") {
                        act {
                            let why = model.engine.removeLayerFromGroup(row.id)
                            if !why.isEmpty { model.toast = AureaText.t("app_group_remove_failed", why) }
                            model.refreshModel(force: true)
                        }
                    }
                }
            }
            if row.kind == 1 || row.kind == 2 || row.kind == 3 {
                ShellMenuSection("sh_menu_media")
                if row.kind == 1 || row.kind == 2 {
                    ShellMenuRow(CupertinoGlyph.Arrow2Squarepath, "layer_replace_media", enabled: !row.locked,
                                 detail: "layer_replace_media_detail") { act { replaceTarget = LayerRef(id: row.id) } }
                }
                ShellMenuRow(CupertinoGlyph.InfoCircle, "media_info_title") { act { infoTarget = LayerRef(id: row.id) } }
                if row.kind == 1 {
                    ShellMenuRow(CupertinoGlyph.MusicNote2, "editor_extrair_audio", detail: "editor_som_vira_camada_propria_video_fica") { act { _ = model.engine.extractAudio(fromLayer: row.id); model.refreshModel(force: true) } }
                }
                if row.kind == 1 || row.kind == 3 { ShellMenuRow(CupertinoGlyph.Speaker2, "sh_menu_volume") { act { model.openPanel(.audio) } } }
            }
            ShellMenuSection("editor_movimento")
            let timeFlags = (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0
            ShellMenuRow(CupertinoGlyph.Speedometer, "editor_desfoque_movimento", checked: timeFlags & 2 != 0,
                         detail: "editor_borra_direcao_movimento_obturador_nas_configuracoes") {
                act { model.mutate { $0.setMotionBlur(timeFlags & 2 == 0, forLayer: row.id) }; model.refreshModel(force: true) }
            }
            if row.kind == 1 {
                ShellMenuRow(CupertinoGlyph.Speedometer, "editor_desfoque_movimento_video", checked: timeFlags & 32 != 0,
                             detail: "editor_borra_mexe_dentro_video_pelos_vetores") {
                    act { model.mutate { $0.setVectorBlur(forLayer: row.id, amount: timeFlags & 32 == 0 ? 1 : 0) }; model.refreshModel(force: true) }
                }
            }
            ShellMenuSection("editor_tempo")
            ShellMenuRow(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote", enabled: inside && !row.locked) { act { model.trimStart(row.id, at: model.status.playhead) } }
            ShellMenuRow(CupertinoGlyph.Scissors, "editor_dividir_cabecote", enabled: inside && !row.locked) { act { model.splitAtPlayhead([row.id]) } }
            ShellMenuRow(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote", enabled: inside && !row.locked) { act { model.trimEnd(row.id, at: model.status.playhead) } }
            if row.kind == 1 || row.kind == 3 { ShellMenuRow(CupertinoGlyph.Speedometer, "sh_menu_speed_remap") { act { model.openPanel(.speed) } } }
            if row.kind == 1 || row.kind == 3 { ShellMenuRow(CupertinoGlyph.Scissors, "Slip · Roll · Slide") { act { model.openPanel(.clipEdit) } } }
            if row.kind == 1 {
                ShellMenuRow(ShellGlyph.Snow, "sh_menu_freeze_frame", enabled: inside) { act { let created = model.engine.freezeFrame(forLayer: row.id, frame: Int32(clamping: model.status.playhead), hold: Int32(max(1, model.compositionFps * 3))); model.refreshModel(force: true); if created >= 0 { model.select(layerId: created) } } }
                ShellMenuSection("editor_rastreio")
                ShellMenuRow(ShellGlyph.Viewfinder, "editor_rastrear_ponto", detail: "editor_cria_nulo_segue_ponto_ligue_outras") { act { model.select(layerId: row.id); model.openPanel(.tracking); model.beginPointPick(stabilize: false) } }
                ShellMenuRow(ShellGlyph.Viewfinder, "editor_estabilizar_pelo_ponto", detail: "editor_move_video_ponto_ficar_parado_tela") { act { model.select(layerId: row.id); model.openPanel(.tracking); model.beginPointPick(stabilize: true) } }
            }
            ShellMenuSection("editor_mais")
            ShellMenuRow(CupertinoGlyph.Trash, "editor_excluir_camada", danger: true) { act { removeSelection() } }
        }
    }
    /// Menu da ENGRENAGEM da barra do projeto: a seção "Projeto" nova primeiro
    /// (ajustes, busca, copiar/colar, marca, lupa, aparar), depois o de sempre.
    @ViewBuilder private var timelineMenu: some View {
        projectMenuSection
        ShellMenuSection("sh_menu_selection")
        ShellMenuRow(CupertinoGlyph.CheckmarkSquare, "editor_selecionar_todas_camadas", enabled: model.layers.count >= 2) { act { model.selectAll() } }
        ShellMenuRow(CupertinoGlyph.RectangleStack, "edt_tl_pick_layers", enabled: !model.layers.isEmpty, checked: model.timelineLayerSelectMode, detail: "edt_tl_pick_layers_detail") { act { model.changeTimelineLayerSelectMode(!model.timelineLayerSelectMode) } }
        ShellMenuRow(CupertinoGlyph.Square, "editor_limpar_selecao") { act { model.clearSelection() } }
        ShellMenuRow(CupertinoGlyph.RectangleStack, "editor_selecionar_uma_camada", enabled: !model.layers.isEmpty) { shell.sheet = .searchLayers }
        ShellMenuSection("editor_reproducao_previa")
        ShellMenuRow(CupertinoGlyph.Repeat, "editor_reproducao_loop", checked: model.looping) { act { model.setLooping(!model.looping) } }
        ShellMenuRow(CupertinoGlyph.Fullscreen, model.fullscreen ? "editor_sair_tela_cheia" : "editor_tela_cheia") { act { model.fullscreen.toggle() } }
        ShellMenuRow(CupertinoGlyph.Speedometer, "editor_desfoque_movimento_composicao", checked: model.compMotionBlur, detail: "editor_camadas_desfoque_movimento_so_borram_isto") { model.setCompositionMotionBlur(!model.compMotionBlur) }
        if model.compMotionBlur {
            ShellMenuRow(CupertinoGlyph.CircleLefthalfFill, "sh_menu_shutter", title: AureaText.t("sh_menu_shutter", String(Int(model.shutterAngle))), detail: "editor_toque_trocar_90_180_270_360") {
                model.changeShutterAngle([Float(90), 180, 270, 360].first(where: { $0 > model.shutterAngle }) ?? 90)
            }
        }
        ShellMenuRow(ShellGlyph.WaveformPathEcg, "editor_diagnostico_tela", checked: model.hudVisible, detail: "editor_quadros_segundo_tempos_gpu_memoria_decodificador") { model.toggleHud() }
        ShellMenuSection("editor_edicao")
        ShellMenuRow(CupertinoGlyph.Link, "editor_edicao", title: "Snapping", checked: model.snapping) { act { model.snapping.toggle() } }
        ShellMenuRow(CupertinoGlyph.SuitDiamond, "editor_keyframes_todas_camadas", checked: model.showAllKeyframes, detail: "editor_keyframes_todas_camadas_detalhe") { act { model.showAllKeyframes.toggle() } }
        ShellMenuRow(CupertinoGlyph.Link, "editor_timeline_magnetica_modo_edicao", checked: model.editMode, detail: "editor_aparar_empurra_camadas_seguintes_excluir_fecha") { act { model.toggleEditMode() } }
        ShellMenuRow(ShellGlyph.ScissorsAlt, "editor_remover_espacos_vazios") { act { model.removeGaps() } }
        ShellMenuSection("editor_marcas_ritmo")
        // Editar a marca do cabeçote (o segurar do antigo botão da marca); tocar a
        // régua em cima de uma marca também abre o editor dela.
        ShellMenuRow(CupertinoGlyph.Pencil, "marker_edit", enabled: model.markerFrames.contains(model.status.playhead)) { act { model.editMarkerAtPlayhead() } }
        ShellMenuRow(ShellGlyph.BookmarkSolid, "editor_ir_proxima_marca", enabled: !model.markerFrames.isEmpty) { act { model.seekToNextMarker() } }
        ShellMenuSection("editor_mais")
        ShellMenuRow(CupertinoGlyph.RectangleStack, "editor_agrupar_camadas_escolhidas", enabled: !model.selection.isEmpty) { act { model.groupSelection() } }
        ShellMenuRow(CupertinoGlyph.Speedometer, "ios_perf_test_a11y") { act { shell.performanceTest = true } }
    }
    /// A seção "Projeto" do menu da engrenagem (o que saiu da barra do topo e do transporte).
    @ViewBuilder private var projectMenuSection: some View {
        let marked: Bool = model.markerFrames.contains(model.status.playhead)
        let zoomLock: Bool = StageViewZoom.shared.zoomLock
        ShellMenuSection("editor_projeto_cbe9")
        ShellMenuRow(CupertinoGlyph.Gear, "editor_ajustes_projeto") { act { pause(); model.showProjectSettings = true } }
        ShellMenuRow(CupertinoGlyph.Search, "edt_cmd_search_desc") { act { pause(); shell.commandSearch = true } }
            .accessibilityIdentifier("commandSearchOpen")
        ShellMenuRow(CupertinoGlyph.DocOnClipboard, "editor_copiar_colar") { shell.sheet = .copyPaste }
        ShellMenuRow(marked ? ShellGlyph.BookmarkSolid : CupertinoGlyph.Bookmark, "editor_marcar_ou_desmarcar_este_instante", checked: marked) { act { model.toggleMarkerAt(model.status.playhead) } }
        ShellMenuRow(CupertinoGlyph.Search, zoomLock ? "stage_zoom_view_on" : "stage_zoom_view_off", checked: zoomLock, symbol: "plus.magnifyingglass") { act { StageViewZoom.shared.zoomLock.toggle() } }
        ShellMenuRow(ShellGlyph.ScissorsAlt, "editor_aparar_projeto_cabecote", enabled: model.status.playhead > 0) { act { model.trimProjectAtPlayhead() } }
    }
    private func pause() { if model.status.playing != 0 { model.playPause() } }
    /// SELECIONAR UMA CAMADA (Menus.kt SearchLayersSheet): todas as camadas
    /// (miniatura ou selo do tipo + nome); tocar escolhe SÓ ela e fecha. A busca
    /// só aparece com muitas camadas e o teclado não abre sozinho.
    @ViewBuilder private var searchMenu: some View {
        ShellMenuSection("editor_selecionar_uma_camada")
        if model.layers.count >= 7 || !search.isEmpty {
            TextField(AureaText.t("editor_nome_ou_texto_camada"), text: $search).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                .focused($searchFocused).padding(.horizontal, 12).padding(.vertical, 10).frame(minHeight: 40)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10)).padding(.horizontal, 16).padding(.vertical, 4)
        }
        if !search.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && searchHits.isEmpty {
            Text(AureaText.t("sh_menu_no_layer_matches", search.trimmingCharacters(in: .whitespacesAndNewlines))).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.horizontal, 20).padding(.vertical, 12)
        }
        ForEach(searchHits) { row in
            Button { act { model.select(layerId: row.id, additive: false) } } label: {
                HStack(spacing: 12) {
                    ShellLayerThumbnail(row: row)
                    Text(row.name.isEmpty ? AureaText.t("editor_camada") : row.name).font(.aurea(size: 15)).foregroundStyle(row.selected ? AureaColors.accent : AureaColors.text).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    if row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
                        Circle().fill(AureaColors.labelPalette[Int(row.label) - 1]).frame(width: 10, height: 10)
                    }
                    if row.selected { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 16, color: AureaColors.accent) }
                }.padding(.horizontal, 16).frame(height: 56)
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(row.name)
        }
    }

}

private struct ShellTimeDialog: View {
    @EnvironmentObject private var model: AureaModel
    let initial: String
    let onDismiss: () -> Void
    @State private var text = ""
    @FocusState private var focused: Bool
    var body: some View {
        AureaAlert(title: AureaText.t("editor_ir_tempo"), confirmLabel: AureaText.t("editor_ir"), onConfirm: seek, onDismiss: onDismiss, extra: AnyView(
            TextField(AureaText.t("editor_segundos_ou_mm_ss_ms"), text: $text).font(.aurea(size: 15)).monospacedDigit().foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                .focused($focused).submitLabel(.go).onSubmit { onDismiss(); seek() }
                .padding(.horizontal, 8).padding(.vertical, 7).background(AureaColors.stage, in: RoundedRectangle(cornerRadius: 7)).padding(.top, 12)
        )).onAppear { text = initial; focused = true }
    }
    private func seek() { if let frame = ShellClock.parseToFrame(text, Float(model.compositionFps)) { model.seek(toFrame: frame) } }
}

private struct ShellLinkPopup: View {
    @EnvironmentObject private var model: AureaModel
    let ids: [Int64]
    let dismiss: () -> Void
    let onHeight: (CGFloat) -> Void
    private var candidates: [LayerItem] { model.parentCandidatesForAll(ids) }
    private var current: Int64 {
        let parents = Set(model.layers.filter { ids.contains($0.id) }.map { row -> Int64 in
            (model.engine.layerDetail(row.id)?["parentId"] as? NSNumber)?.int64Value ?? 0
        })
        return parents.count == 1 ? parents.first! : -1
    }
    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                newNullRow
                row(AureaText.t("editor_nenhum"), id: 0, bold: true) { CupertinoGlyph.text(ShellGlyph.Nosign, size: 22).frame(width: 44, height: 44) }
                ForEach(candidates) { layer in row(layer.name, id: layer.id) { ShellLayerThumbnail(row: layer) } }
                if candidates.isEmpty { Text(AureaText.t("editor_nenhuma_outra_camada_seguir_crie_nulo")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.horizontal, 16).padding(.vertical, 12) }
            }.background(GeometryReader { bounds in Color.clear.preference(key: ShellMenuHeightKey.self, value: bounds.size.height) })
        }.onPreferenceChange(ShellMenuHeightKey.self, perform: onHeight)
            .background(AureaColors.pill, in: RoundedRectangle(cornerRadius: 12)).clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.border, lineWidth: 1))
    }
    /// "Novo nulo": cria o nulo no centro das escolhidas e vincula todas a ele.
    private var newNullRow: some View {
        let type: TimelineLayerType = .null
        return Button {
            dismiss(); model.parentSelectionToNewNull(ids)
        } label: {
            HStack(spacing: 14) {
                ZStack {
                    type.color
                    CupertinoGlyph.text(type.glyph, size: 20, color: .white)
                }.frame(width: 44, height: 44).clipShape(RoundedRectangle(cornerRadius: 6))
                Text(AureaText.t("editor_vincular_novo_nulo")).font(.aurea(size: 15, weight: .bold)).foregroundStyle(AureaColors.text)
                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.horizontal, 12).frame(height: 56).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("link.newNull")
    }
    private func row<Thumb: View>(_ title: String, id: Int64, bold: Bool = false, @ViewBuilder thumb: () -> Thumb) -> some View {
        Button {
            let previous = current; dismiss(); if previous != id { model.setParentMany(ids, parent: id) }
        } label: {
            HStack(spacing: 14) {
                thumb()
                Text(title).font(.aurea(size: 15, weight: bold ? .bold : .medium)).foregroundStyle(current == id ? AureaColors.accent : AureaColors.text).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                if current == id { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 16, color: AureaColors.accent) }
            }.padding(.horizontal, 12).frame(height: 56).background(current == id ? AureaColors.accentDim : bold ? AureaColors.chip : Color.clear)
        }.buttonStyle(AureaPressStyle(shrink: 1))
    }
}

@MainActor private struct ShellLayerThumbnail: View {
    @EnvironmentObject private var model: AureaModel
    let row: LayerItem
    @State private var image: UIImage?
    @State private var cache = TimelineThumbStrip()
    private var type: TimelineLayerType { TimelineLayerType.of(row.kind) }
    var body: some View {
        ZStack {
            type.color
            if let image { Image(uiImage: image).resizable().scaledToFill() }
            else { CupertinoGlyph.text(type.glyph, size: 20, color: .white) }
        }.frame(width: 44, height: 44).clipShape(RoundedRectangle(cornerRadius: 6))
            .onAppear(perform: load).onChange(of: model.status.thumbnailGeneration) { _ in load() }
    }
    private func load() {
        guard row.kind == 1 || row.kind == 2 else { return }
        let fps = Float(model.compositionFps), bucket = Thumbs.bucketOf(Double(row.offsetFrames), Float(model.compositionFps))
        let frame = row.kind == 2 ? row.startFrame : Int32(clamping: Int64(Thumbs.requestLocalFrame(bucket, fps)) + Int64(row.startFrame) - Int64(row.offsetFrames))
        image = cache.get(model, layer: row.id, bucket: bucket, timelineFrame: frame, heightPx: Int((44 * UIScreen.main.scale).clamped(to: 16...256)), generation: model.status.thumbnailGeneration)
    }
}
