// Android spec: editor/EditModeScreen.kt e editor/EditTimeline.kt.
// A INTERFACE DE EDIÇÃO (0.2.5) — a segunda interface do AUREA, escolhida por
// projeto (a Composição continua a principal e o padrão). Estrutura especificada
// a partir da engenharia reversa do CapCut 19.8.0 (build/reference/capcut-19.8.0/
// SPEC.md); nada do app de referência entra aqui. As regras de montagem são do
// motor (linha magnética, ripple, reordenar, aparar, dividir); esta tela só as
// aciona. Os painéis são os da Composição.
import SwiftUI
import UIKit

/// Os níveis do Dock. `root` = nada escolhido; os `*Clip` seguem o tipo do corte.
enum EditDockLevel: Hashable { case root, audio, text, videoClip, textClip, audioClip, otherClip }
private struct EditDockItem: Identifiable {
    let id: String
    let icon: EditIcon
    let label: String
    let action: () -> Void
}

/// A Edição acompanha os mesmos papéis de cor do tema AUREA ativo.
private enum EditColors {
    static var canvas: Color { AureaColors.background }
    static var preview: Color { AureaColors.editorCanvas }
    static var bar: Color { AureaColors.editorTopBar }
    static var dockLevel: Color { AureaColors.editorRowPill }
    static var ruler: Color { AureaColors.muted }
}

struct EditModeView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let height: CGFloat
    @State private var level: EditDockLevel = .root
    @State private var timelineOptions = false
    @State private var previewOptions = false
    @State private var renameOpen = false
    @State private var renameDraft = ""
    @ScaledMetric(relativeTo: .caption) private var dockLabelSize: CGFloat = 12
    @State private var replaceTarget: LayerItem?
    /// O corte cujas transições estão abertas (saída, entrada).
    @State private var cut: EditCut?

    var body: some View {
        GeometryReader { geometry in
            let texting = model.textModeActive && !model.fullscreen
            let inspecting = model.panel != .none && model.panel != .dock
            let top: CGFloat = model.fullscreen ? 0 : texting ? 52 : 50
            let wide = geometry.size.width >= 600 && geometry.size.width > geometry.size.height && !texting && !model.fullscreen
            let available = max(0, geometry.size.height - top - 40)
            let lowerFloor: CGFloat = texting ? 180 : inspecting ? 220 : 170
            let preferred = geometry.size.height * (inspecting || texting ? 0.30 : 0.48)
            let previewHeight = model.fullscreen ? available : min(available, max(72, min(preferred, available - lowerFloor)))
            let lower = max(0, available - previewHeight)
            let editorHeight = wide ? max(0, geometry.size.height - top) : lower
            let timelineHeight = inspecting ? min(100, editorHeight * 0.30) : max(0, editorHeight - dockHeight)
            let workspace = wide ? AnyLayout(HStackLayout(spacing: 0)) : AnyLayout(VStackLayout(spacing: 0))
            VStack(spacing: 0) {
                if texting { TextStyleBar().frame(height: 52) }
                else if !model.fullscreen { topBar }
                workspace {
                    VStack(spacing: 0) {
                        PreviewStage(height: wide ? max(0, editorHeight - 40) : previewHeight)
                            .background(EditColors.preview)
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                            .padding(.horizontal, 8)
                            .accessibilityIdentifier("edit.preview")
                            .environment(\.layoutDirection, .leftToRight)
                        playBar
                    }.frame(maxWidth: .infinity)
                    if !model.fullscreen {
                        VStack(spacing: 0) {
                            if texting { TextEditPanel().frame(height: lower) }
                            else {
                                EditTimelineView(compact: inspecting, showMore: $timelineOptions, onAddMedia: addMedia,
                                                 onTransition: { cut = EditCut(outgoing: $0, incoming: $1) })
                                    .frame(height: timelineHeight)
                                    .environment(\.layoutDirection, .leftToRight)
                                if inspecting {
                                    ContextSheet(metrics: EditorMetrics(topBar: 0, preview: 0, strip: 0, transport: 0, timeline: 0, sheet: 0))
                                        .frame(height: max(0, editorHeight - timelineHeight))
                                        .background(EditColors.bar)
                                } else { dock }
                            }
                        }.frame(width: wide ? geometry.size.width * 0.48 : nil, height: editorHeight)
                    }
                }
            }
        }
        .overlay(alignment: .top) { if model.textModeActive && !model.fullscreen { TextStylePopupLayer(top: 52) } }
        .background(EditColors.canvas.ignoresSafeArea())
        .onChange(of: model.primarySelection) { _ in syncLevel() }
        .onAppear(perform: syncLevel)
        .alert(AureaText.t("editor_renomear"), isPresented: $renameOpen) {
            TextField(AureaText.t("edt_untitled"), text: $renameDraft).accessibilityIdentifier("edit.rename.field")
            Button(AureaText.t("common_cancel"), role: .cancel) { }
            Button(AureaText.t("common_save")) { model.renameCurrentProject(to: renameDraft) }
        }
        .sheet(isPresented: $previewOptions, onDismiss: { ViewModeState.shared.columnOpen = false }) {
            ViewModeColumn(width: 160).frame(height: 360).padding(16)
                .presentationDetents([.height(400)])
        }
        .sheet(item: $cut) { target in
            CutTransitionSheet(cut: target).environmentObject(model)
                .presentationDetents([.height(180)])
        }
        .sheet(item: $replaceTarget) { target in
            ShellMediaPicker(selectionLimit: 1, filter: .any(of: [.images, .videos])) { items in
                replaceTarget = nil
                if let (url, video) = items.first { model.replaceMedia(layer: target.id, url: url, video: video) }
            }
        }
    }

    private var dockHeight: CGFloat { max(80, dockLabelSize * 2 + 48) }

    /// O corte escolhido manda no Dock; sem corte, volta ao nível de cima (ou
    /// fica no submenu que a pessoa abriu, como Áudio ou Texto).
    private func syncLevel() {
        guard let clip = model.selectedLayer else {
            if [.videoClip, .textClip, .audioClip, .otherClip].contains(level) { level = .root }
            return
        }
        switch clip.kind {
        case 4: level = .textClip
        case 3: level = .audioClip
        case 1, 2, 12: level = .videoClip
        default: level = .otherClip
        }
    }

    // --- Topo: voltar · projeto · resolução · mais · exportar ------------------
    private var topBar: some View {
        HStack(spacing: 4) {
            Button { model.closeProject() } label: {
                EditToolIcon(icon: .back, size: 22).frame(width: 44, height: 50).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("common_close"))
                .accessibilityIdentifier("edit.close")
            Text(model.projectName).font(.aurea(size: 14, weight: .semibold))
                .lineLimit(1).truncationMode(.tail).frame(minWidth: 0, maxWidth: .infinity, alignment: .leading)
                .layoutPriority(-1).accessibilityIdentifier("edit.title")
            let short = min(model.compositionWidth, model.compositionHeight)
            Button { model.showProjectSettings = true } label: {
                HStack(spacing: 4) {
                    Text(short > 0 ? "\(short)P" : "—").font(.aurea(size: 13, weight: .semibold))
                    EditToolIcon(icon: .chevronDown, size: 10)
                }.padding(.horizontal, 8).frame(height: 28)
                    .background(AureaColors.editorRowPill, in: Capsule())
                    .frame(minHeight: 44).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("edit_dock_ratio"))
                .accessibilityIdentifier("edit.resolution")
            Menu {
                Button(AureaText.t("editor_renomear")) { renameDraft = model.projectName; renameOpen = true }
                    .accessibilityIdentifier("edit.rename")
                Button(AureaText.t("editor_ajustes_projeto")) { model.showProjectSettings = true }
                Button(AureaText.t("edit_timeline")) { timelineOptions = true }
                    .accessibilityIdentifier("edit.tools.more")
                Button(AureaText.t("fxl_view")) { ViewModeState.shared.columnOpen = true; previewOptions = true }
                    .accessibilityIdentifier("edit.previewControls")
                Button(AureaText.t(model.autoKeyTransforms ? "i18n_autokey_on" : "i18n_autokey_off")) { model.autoKeyTransforms.toggle() }
                    .accessibilityIdentifier("edit.autokey")
                Button(AureaText.t("ui_mode_switch_to_composition")) { model.setUiMode(AureaUiMode.composition) }
                Button(AureaText.t(model.editMode ? "edit_magnet_on" : "edit_magnet_off")) { model.toggleEditMode() }
                Button(AureaText.t("ae_project_import")) { model.pause(); shell.addPicker = .aeProjectImport }
                    .accessibilityIdentifier("edit.aeProjectImport")
                Button(AureaText.t("ae_import_menu")) { model.pause(); shell.addPicker = .aeImport }
                    .accessibilityIdentifier("edit.aeImport")
            } label: {
                EditToolIcon(icon: .more, size: 22).frame(width: 44, height: 50).contentShape(Rectangle())
            }.accessibilityLabel(AureaText.t("edit_more")).accessibilityIdentifier("edit.more")
            Button { model.pause(); model.openExport() } label: {
                EditToolIcon(icon: .export, color: AureaColors.accent, size: 23)
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("edit_export"))
                .accessibilityIdentifier("edit.export")
        }
        .padding(.horizontal, 10).frame(height: 50).foregroundStyle(AureaColors.text)
        .background(EditColors.bar)
    }

    // --- Barra de reprodução: tempo · ▶ · desfazer · refazer · tela cheia ------
    private var playBar: some View {
        let fps = max(model.compositionFps, 1)
        return ZStack {
            HStack {
                EditClockText(fps: fps, total: model.compositionDuration)
                    .padding(.horizontal, 8).frame(height: 28)
                    .background(AureaColors.editorRowPill, in: Capsule())
                Spacer()
                barIcon(.undo, "common_undo", enabled: model.status.canUndo != 0, id: "edit.undo") { model.undo() }
                barIcon(.redo, "common_redo", enabled: model.status.canRedo != 0, id: "edit.redo") { model.redo() }
                barIcon(model.fullscreen ? .fullscreenExit : .fullscreen, "editor_tela_cheia", enabled: true, id: "edit.fullscreen") {
                    model.fullscreen.toggle()
                }
            }
            Button { model.playPause() } label: {
                EditToolIcon(icon: model.status.playing != 0 ? .pause : .play, color: AureaColors.accent, size: 24)
                    .frame(width: 40, height: 40).background(AureaColors.accentDim, in: Circle())
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 0.9))
                .accessibilityLabel(AureaText.t(model.status.playing != 0 ? "editor_pausar" : "editor_reproduzir"))
                .accessibilityIdentifier("edit.play")
        }
        .padding(.horizontal, 10).frame(height: 40).foregroundStyle(AureaColors.text).background(AureaColors.editorBar)
    }

    private func barIcon(_ icon: EditIcon, _ key: String, enabled: Bool, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            EditToolIcon(icon: icon, color: enabled ? AureaColors.text : AureaColors.muted.opacity(0.4), size: 20)
                .frame(width: 32, height: 40).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 0.9)).disabled(!enabled)
            .accessibilityLabel(AureaText.t(key)).accessibilityIdentifier(id)
    }

    // --- O Dock em níveis ------------------------------------------------------
    private var dock: some View {
        HStack(spacing: 0) {
            if level != .root {
                Button {
                    if [.videoClip, .textClip, .audioClip, .otherClip].contains(level) { model.clearSelection() }
                    level = .root
                } label: {
                    EditToolIcon(icon: .back, size: 18)
                        .frame(width: 32, height: 58).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 0.92)).padding(.leading, 10)
                    .accessibilityLabel(AureaText.t("edit_back")).accessibilityIdentifier("edit.dock.back")
            }
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 0) {
                    ForEach(items(for: level)) { item in
                        Button(action: item.action) {
                            VStack(spacing: 6) {
                                EditToolIcon(icon: item.icon, size: 24)
                                Text(item.label).font(.aurea(size: dockLabelSize, weight: .medium)).lineLimit(2).multilineTextAlignment(.center)
                            }.frame(minWidth: 64, minHeight: 64).padding(.horizontal, 6).contentShape(Rectangle())
                        }.buttonStyle(AureaPressStyle(shrink: 0.92))
                            .accessibilityElement(children: .ignore)
                            .accessibilityLabel(item.label).accessibilityAddTraits(.isButton)
                            .accessibilityIdentifier("edit.dock." + item.id)
                    }
                }.padding(.horizontal, 6)
            }.id(level)
        }
        .padding(.vertical, 4)
        .background(EditColors.dockLevel, in: RoundedRectangle(cornerRadius: 22))
        .overlay(RoundedRectangle(cornerRadius: 22).stroke(AureaColors.editorBarLine, lineWidth: 1))
        .padding(.horizontal, 8)
        .frame(height: dockHeight).frame(maxWidth: .infinity)
        .background(EditColors.bar)
        .foregroundStyle(AureaColors.text)
        .accessibilityIdentifier("edit.dock")
    }

    private func addMedia() {
        if model.status.playing != 0 { model.pause() }
        model.pendingOverlayImport = false
        shell.addPicker = .gallery
    }

    private func panel(_ kind: AureaModel.PanelKind) { model.openPanel(kind) }

    /// Efeito do projeto: uma camada de ajuste por cima, e os efeitos nela. A
    /// camada de ajuste VAZIA sob o cabeçote é reaproveitada (antes cada toque
    /// empilhava mais um "Ajuste"; par do EditModeScreen.kt).
    private func globalEffect() {
        let frame = model.status.playhead
        if let empty = model.layers.first(where: { $0.adjustment && $0.effectCount == 0 && frame >= Int64($0.startFrame) && frame < Int64($0.endFrame) }) {
            model.select(layerId: empty.id)
        } else {
            model.addAdjustmentLayer()
        }
        model.openPanel(.effects)
    }

    /// 0.3.0: Filtros e Ajustar têm cada um a sua tela. No nível de cima valem
    /// para a camada de ajuste do projeto sob o cabeçote (reaproveitada).
    private func globalColor(_ kind: AureaModel.PanelKind) {
        if model.status.playing != 0 { model.pause() }
        let frame = model.status.playhead
        if let existing = model.layers.last(where: { $0.adjustment && frame >= Int64($0.startFrame) && frame < Int64($0.endFrame) }) {
            model.select(layerId: existing.id)
        } else {
            model.addAdjustmentLayer()
        }
        model.openPanel(kind)
    }

    private var mainAnchor: LayerItem? { model.layers.first { $0.id == model.mainTrackAnchor } }

    private func isMain(_ clip: LayerItem) -> Bool {
        guard let anchor = mainAnchor else { return false }
        return clip.id == anchor.id || (anchor.trackId != 0 && clip.trackId == anchor.trackId)
    }

    private func clipUnderPlayhead() -> LayerItem? {
        let frame = model.status.playhead
        return model.layers.first { isMain($0) && frame >= Int64($0.startFrame) && frame < Int64($0.endFrame) }
    }

    private func item(_ id: String, _ icon: EditIcon, _ key: String, _ action: @escaping () -> Void) -> EditDockItem {
        EditDockItem(id: id, icon: icon, label: AureaText.t(key), action: action)
    }

    private func items(for level: EditDockLevel) -> [EditDockItem] {
        let ids = model.editingLayers.filter { model.editingSelection.contains($0.id) }.map(\.id)
        let split = item("split", .razor, "edit_clip_split") { model.splitAtPlayhead(ids) }
        let duplicate = item("duplicate", .duplicate, "edit_clip_duplicate") {
            model.duplicateSelectedLayers(ids)
        }
        let delete = item("delete", .delete, "edit_clip_delete") { model.deleteSelectedLayers() }
        let effects = item("effects", .effects, "edit_clip_effects") { panel(.effects) }
        let transform = item("transform", .transform, "edit_clip_transform") { panel(.transform) }
        let opacity = item("opacity", .opacity, "edit_clip_opacity") { panel(.appearance) }
        let mask = item("mask", .mask, "edit_clip_mask") { panel(.mask) }
        let animations = item("animations", .presets, "edit_clip_animations") { panel(.presets) }
        let filters = item("filters", .filters, "edit_dock_filters") { if model.status.playing != 0 { model.pause() }; panel(.filters) }
        let adjust = item("adjust", .adjust, "edit_dock_adjust") { if model.status.playing != 0 { model.pause() }; panel(.adjust) }
        switch level {
        case .root:
            return [
                item("edit", .razor, "edit_dock_edit") {
                    if let hit = clipUnderPlayhead() { model.select(layerId: hit.id) } else { model.toast = AureaText.t("edit_no_clip_here") }
                },
                item("audio", .waveform, "edit_dock_audio") { self.level = .audio },
                item("text", .text, "edit_dock_text") { self.level = .text },
                item("effects", .effects, "edit_dock_effects") { globalEffect() },
                item("overlay", .overlay, "edit_dock_overlay") {
                    if model.status.playing != 0 { model.pause() }
                    model.pendingOverlayImport = true
                    shell.addPicker = .gallery
                },
                item("captions", .captions, "edit_dock_captions") { panel(.captions) },
                item("filters", .filters, "edit_dock_filters") { globalColor(.filters) },
                item("adjust", .adjust, "edit_dock_adjust") { globalColor(.adjust) },
                item("ratio", .crop, "edit_dock_ratio") { model.showProjectSettings = true },
                item("background", .background, "edit_dock_background") { model.showProjectSettings = true },
                item("stickers", .shapes, "edit_dock_stickers") { shell.addCategory = 0; model.openAddLayer() },
                // 0.3.0: sequência de imagens (render do Blender com alfa) numa linha própria, no cabeçote.
                item("sequence", .sequence, "seq_dock") {
                    if model.status.playing != 0 { model.pause() }
                    model.pendingOverlayImport = true
                    shell.sequenceChooser = true
                },
                item("templates", .presets, "edit_text_templates") { shell.addCategory = ShellAddCategories.textPresets; model.openAddLayer() },
            ]
        case .audio:
            return [
                item("music", .music, "edit_audio_music") { if model.status.playing != 0 { model.pause() }; shell.addPicker = .audioFile },
                item("voice", .voice, "edit_audio_voice") { if model.status.playing != 0 { model.pause() }; shell.voiceRecorder = true },
                item("extract", .extract, "edit_clip_extract_audio") {
                    guard let hit = clipUnderPlayhead() else { model.toast = AureaText.t("edit_no_clip_here"); return }
                    _ = model.engine.extractAudio(fromLayer: hit.id); model.refreshModel(force: true)
                },
            ]
        case .text:
            return [
                item("addText", .text, "edit_text_add") { _ = model.addText() },
                item("autoCaptions", .captions, "edit_text_auto_captions") { panel(.captions) },
                item("templates", .presets, "edit_text_templates") { shell.addCategory = ShellAddCategories.textPresets; model.openAddLayer() },
            ]
        case .videoClip:
            guard let clip = model.selectedLayer else { return [] }
            let video = clip.kind == 1
            var list = [split, item("speed", .speed, "edit_clip_speed") { panel(.speed) }, animations, effects, delete, mask, adjust, filters]
            if video { list.append(item("volume", .speaker, "edit_clip_volume") { panel(.audio) }) }
            list += [transform, opacity, duplicate]
            if clip.kind != 12 { list.append(item("replace", .replace, "edit_clip_replace") { replaceTarget = clip }) }
            // 0.4.5: Remover fundo (vídeo/imagem): efeito + análise do clipe.
            if clip.kind == 1 || clip.kind == 2 {
                list.append(item("bgRemove", .cutout, "bg_remove_action") { model.removeBackground(clip.id) })
            }
            if video {
                list.append(item("freeze", .freeze, "edit_clip_freeze") {
                    let created = model.engine.freezeFrame(forLayer: clip.id, frame: Int32(clamping: model.status.playhead),
                                                           hold: Int32(max(1, model.compositionFps * 3)))
                    model.refreshModel(force: true)
                    if created >= 0 { model.select(layerId: created) }
                })
                list.append(item("extract", .extract, "edit_clip_extract_audio") {
                    _ = model.engine.extractAudio(fromLayer: clip.id); model.refreshModel(force: true)
                })
                list.append(item("stabilize", .track, "edit_clip_stabilize") { panel(.tracking) })
                // 1.0.8: Edição automática do clipe (cortes, zoom, sons, música, voz, cor, legendas).
                list.append(item("autoEdit", .sequence, "autoedit_title") { model.pause(); model.autoEdit.open(clip.id, model: model) })
            }
            if isMain(clip) { list.append(item("toOverlay", .overlay, "edit_clip_to_overlay") { model.moveClipToOverlay(clip.id) }) }
            else { list.append(item("toMain", .mainTrack, "edit_clip_to_main") { model.moveClipToMainTrack(clip.id) }) }
            return list
        case .textClip:
            return [
                // Omega conteúdo (área 06, P2-15): "Editar texto" abre o MODO DE TEXTO e "Fonte"
                // o navegador de fontes (par do EditModeScreen.kt).
                item("editText", .pencil, "edit_clip_edit_text") { _ = model.openTextEdit() },
                item("font", .font, "edit_clip_font") { FontBrowserRequest.shared.open = true },
                animations, effects, transform, split, duplicate, delete,
            ]
        case .audioClip:
            return [
                item("volume", .speaker, "edit_clip_volume") { panel(.audio) },
                item("speed", .speed, "edit_clip_speed") { panel(.speed) },
                effects, split, duplicate, delete,
            ]
        case .otherClip:
            // Omega conteúdo (área 06, P2-15): forma → Editar forma; vetor → Editar pontos; desenho → desenho.
            var list: [EditDockItem] = []
            if model.selectedLayer?.kind == 5 {
                if model.isDrawingLayer { list.append(item("editDrawing", .draw, "om_draw_title") { panel(.drawing) }) }
                else if model.isVectorLayer { list.append(item("editPoints", .vector, "om_pts_title") { EditPointsState.shared.reset(); panel(.editPoints) }) }
                else { list.append(item("editShape", .shapeEdit, "sh_dock_edit_shape") { panel(.shapeEdit) }) }
            }
            return list + [effects, filters, adjust, transform, opacity, mask, split, duplicate, delete]
        }
    }
}

/// "mm:ss / mm:ss" lendo o relógio de quadros (anda a cada quadro no play).
private struct EditClockText: View {
    @EnvironmentObject private var model: AureaModel
    let fps: Double
    let total: Int64
    var body: some View {
        EditClockReader(clock: model.playheadClock) { frame in
            Text("\(editClock(frame, fps)) / \(editClock(total, fps))")
                .font(.aurea(size: 12.5, weight: .medium).monospacedDigit())
                .accessibilityIdentifier("edit.clock")
        }
    }
}

private struct EditClockReader<Content: View>: View {
    @ObservedObject var clock: PlayheadClock
    let content: (Int64) -> Content
    var body: some View { content(clock.frame) }
}

/// "mm:ss" do quadro (o relógio da barra de reprodução).
func editClock(_ frame: Int64, _ fps: Double) -> String {
    let seconds = Int(Double(max(0, frame)) / max(fps, 1))
    return String(format: "%02d:%02d", seconds / 60, seconds % 60)
}

// =============================================================================
//  A LINHA DO TEMPO DA EDIÇÃO (espelho de EditTimeline.kt): faixas de montagem
//  (V3, V2, V1 em cima; A1, A2, A3 embaixo), no fluxo de um editor não linear.
//
//  Adaptado ao toque: arrastar no vazio rola, a régua move o cabeçote e a pinça
//  aproxima. A ferramenta da faixa de cima decide o que o arrasto faz; o clipe
//  escolhido responde na hora e o outro pede um toque (ou segurar), para rolar
//  nunca virar edição sem querer. As regras moram no motor (EngineTracks.inl).
// =============================================================================

struct EditTrackInfo: Identifiable, Equatable {
    let id: UInt32
    let audio: Bool
    let locked: Bool
    let muted: Bool
    let solo: Bool
    let target: Bool
    let label: String
}

/// O que `Engine::query_edit_timeline` devolve, em fileiras prontas para desenhar.
struct EditTimelineData: Equatable {
    let tracks: [EditTrackInfo]
    let linkedAudio: [Int64: UInt32]
    let markIn: Int64
    let markOut: Int64

    /// De cima para baixo na tela: V3, V2, V1, A1, A2, A3.
    var lanes: [EditTrackInfo] { tracks.filter { !$0.audio } + tracks.filter(\.audio) }

    static func parse(_ raw: [NSNumber]) -> EditTimelineData? {
        guard raw.count >= 4 else { return nil }
        let trackCount = raw[0].intValue, linkedCount = raw[1].intValue
        guard trackCount >= 0, linkedCount >= 0, raw.count >= 4 + trackCount * 2 + linkedCount * 2 else { return nil }
        var video = 0, audio = 0
        var tracks: [EditTrackInfo] = []
        for i in 0..<trackCount {
            let flags = raw[5 + i * 2].intValue
            let isAudio = flags & 1 != 0
            if isAudio { audio += 1 } else { video += 1 }
            tracks.append(EditTrackInfo(id: raw[4 + i * 2].uint32Value, audio: isAudio, locked: flags & 2 != 0, muted: flags & 4 != 0,
                                        solo: flags & 8 != 0, target: flags & 16 != 0, label: isAudio ? "A\(audio)" : "V\(video)"))
        }
        var linked: [Int64: UInt32] = [:]
        for i in 0..<linkedCount {
            linked[raw[4 + trackCount * 2 + i * 2].int64Value] = raw[5 + trackCount * 2 + i * 2].uint32Value
        }
        return EditTimelineData(tracks: tracks, linkedAudio: linked, markIn: raw[2].int64Value, markOut: raw[3].int64Value)
    }
}

/// Números de `TrackEditOp` (Engine.hpp).
enum TrackOp {
    static let move: UInt32 = 0, moveInsert: UInt32 = 1, trimIn: UInt32 = 2, trimOut: UInt32 = 3
    static let rippleTrimIn: UInt32 = 4, rippleTrimOut: UInt32 = 5, roll: UInt32 = 6, slip: UInt32 = 7, slide: UInt32 = 8
    static let razor: UInt32 = 9, razorAll: UInt32 = 10, rippleDelete: UInt32 = 11, lift: UInt32 = 12, moveLinkedAudio: UInt32 = 13
}

extension AureaModel {
    @discardableResult
    func editTrackClip(_ op: UInt32, layer: Int64, frame: Int64, track: UInt32 = 0) -> Bool {
        let ok = engine.editTrackClip(op, layer: layer, frame: frame, track: track)
        if !ok { toast = AureaText.t("edit_track_blocked") }
        refreshModel(force: true)
        return ok
    }

    func setEditTrackFlag(_ track: UInt32, flag: UInt32, on: Bool) {
        _ = engine.setEditTrackFlag(track, flag: flag, on: on)
        refreshModel(force: true)
    }

    @discardableResult
    func addEditTrack(audio: Bool) -> UInt32 {
        let track = engine.addEditTrack(audio: audio)
        refreshModel(force: true)
        return track
    }

    /// Tira as faixas vazias acima das três de cada tipo (o motor recusa as que têm clipe).
    func removeEmptyEditTracks() {
        guard let tracks = editTimeline?.tracks else { return }
        for audio in [false, true] {
            for track in tracks.filter({ $0.audio == audio }).dropFirst(3).reversed() { _ = engine.removeEditTrack(track.id) }
        }
        refreshModel(force: true)
    }

    /// Marca a entrada ou a saída no cabeçote; de novo no mesmo quadro, tira a marca.
    func toggleEditMark(_ which: UInt32) {
        guard let data = editTimeline else { return }
        let frame = Int64(status.playhead)
        _ = engine.setEditMark(which, frame: (which == 0 ? data.markIn : data.markOut) == frame ? -1 : frame)
        refreshModel(force: true)
    }

    func editMarkedRange(extract: Bool) {
        if !engine.editMarkedRange(extract: extract) { toast = AureaText.t("edit_mark_needed") }
        refreshModel(force: true)
    }

    func razorAllAtPlayhead() { editTrackClip(TrackOp.razorAll, layer: 0, frame: Int64(status.playhead)) }

    /// Leva a mídia recém-importada (que entrou no fim da V1) para o cabeçote. Par do Android.
    func placeImportedClips(_ ids: [Int64], from: Int64) {
        guard uiMode != AureaUiMode.composition, editPlacement != 0 else { return }
        let op = editPlacement == 1 ? TrackOp.moveInsert : TrackOp.move
        var frame = from
        for id in ids {
            refreshModel(force: true)
            guard let row = layers.first(where: { $0.id == id }) else { continue }
            if engine.editTrackClip(op, layer: id, frame: frame, track: 0) { frame += Int64(row.endFrame - row.startFrame) }
        }
    }
}

enum EditIcon: Hashable {
    case select, ripple, roll, razor, slip, slide, snap, markIn
    case markOut, lift, extract, razorAll, addVideo, addAudio, clearTracks, placeEnd
    case placeInsert, placeOverwrite, back, chevronDown, more, export, play, pause
    case undo, redo, fullscreen, fullscreenExit, plus, close, start, lock
    case unlock, eye, eyeOff, speaker, muted, text, font, captions
    case effects, overlay, filters, adjust, crop, background, shapes, sequence
    case presets, music, voice, waveform, speed, delete, mask, replace
    case cutout, freeze, track, mainTrack, pencil, transform, duplicate, opacity
    case draw, vector, shapeEdit, cube

    var filled: Bool { self == .select || self == .play || self == .pause }
    var paths: [Path] { Self.geometry[self] ?? [] }

    // Same 24-point artwork as Android's EditToolIcons.kt. Build each path once.
    private static let geometry: [EditIcon: [Path]] = {
        let strokes: [EditIcon: [[CGFloat]]] = [
            .select: [[6, 3, 19, 13, 13, 14, 16, 20, 13, 22, 10, 16, 6, 20, 6, 3]],
            .ripple: [[9, 4, 5, 4, 3, 6, 3, 18, 5, 20, 9, 20], [8, 12, 21, 12], [16, 7, 21, 12, 16, 17]],
            .roll: [[3, 5, 6, 5, 6, 19, 3, 19], [21, 5, 18, 5, 18, 19, 21, 19], [12, 5, 12, 19], [9, 9, 6, 12, 9, 15], [15, 9, 18, 12, 15, 15]],
            .razor: [[4, 4, 8, 3, 10, 6, 8, 9, 4, 8, 4, 4], [4, 16, 8, 15, 10, 18, 8, 21, 4, 20, 4, 16], [9, 8, 21, 19], [9, 16, 21, 5]],
            .slip: [[6, 5, 3, 5, 3, 19, 6, 19], [18, 5, 21, 5, 21, 19, 18, 19], [7, 12, 17, 12], [10, 9, 7, 12, 10, 15], [14, 9, 17, 12, 14, 15]],
            .slide: [[8, 4, 16, 4, 18, 6, 18, 11, 16, 13, 8, 13, 6, 11, 6, 6, 8, 4], [3, 19, 21, 19], [6, 16, 3, 19, 6, 22], [18, 16, 21, 19, 18, 22]],
            .snap: [[4, 4, 9, 4, 9, 13, 12, 16, 15, 13, 15, 4, 20, 4, 20, 14, 17, 19, 12, 21, 7, 19, 4, 14, 4, 4], [4, 8, 9, 8], [15, 8, 20, 8]],
            .markIn: [[15, 4, 9, 4, 7, 6, 7, 18, 9, 20, 15, 20], [10, 12, 20, 12], [16, 8, 20, 12, 16, 16]],
            .markOut: [[9, 4, 15, 4, 17, 6, 17, 18, 15, 20, 9, 20], [4, 12, 14, 12], [8, 8, 4, 12, 8, 16]],
            .lift: [[12, 15, 12, 4], [8, 8, 12, 4, 16, 8], [3, 17, 3, 20, 8, 20], [21, 17, 21, 20, 16, 20]],
            .extract: [[12, 14, 12, 3], [8, 7, 12, 3, 16, 7], [3, 19, 9, 19], [6, 16, 9, 19, 6, 22], [21, 19, 15, 19], [18, 16, 15, 19, 18, 22]],
            .razorAll: [[3, 5, 9, 5], [15, 5, 21, 5], [3, 12, 9, 12], [15, 12, 21, 12], [3, 19, 9, 19], [15, 19, 21, 19], [12, 3, 12, 21]],
            .addVideo: [[4, 7, 12, 7, 14, 9, 14, 15, 12, 17, 3, 17, 3, 8, 4, 7], [19, 9, 19, 15], [16, 12, 22, 12]],
            .addAudio: [[3, 10, 3, 14], [6, 7, 6, 17], [9, 9, 9, 15], [12, 11, 12, 13], [19, 9, 19, 15], [16, 12, 22, 12]],
            .clearTracks: [[4, 6, 20, 6], [9, 6, 9, 3, 15, 3, 15, 6], [6, 6, 7, 19, 9, 21, 15, 21, 17, 19, 18, 6]],
            .placeEnd: [[3, 12, 15, 12], [11, 8, 15, 12, 11, 16], [19, 5, 21, 5, 21, 19, 19, 19]],
            .placeInsert: [[3, 20, 8, 20], [16, 20, 21, 20], [12, 3, 12, 17], [8, 13, 12, 17, 16, 13]],
            .placeOverwrite: [[12, 3, 12, 12], [8, 8, 12, 12, 16, 8], [4, 16, 20, 16, 21, 17, 21, 20, 20, 21, 4, 21, 3, 20, 3, 17, 4, 16]],
            .back: [[12, 5, 5, 12, 12, 19], [5, 12, 20, 12]],
            .chevronDown: [[5, 9, 12, 16, 19, 9]],
            .more: [[3, 12, 5, 10, 7, 12, 5, 14, 3, 12], [10, 12, 12, 10, 14, 12, 12, 14, 10, 12], [17, 12, 19, 10, 21, 12, 19, 14, 17, 12]],
            .export: [[5, 12, 5, 19, 7, 21, 17, 21, 19, 19, 19, 12], [12, 15, 12, 3], [7, 8, 12, 3, 17, 8]],
            .play: [[8, 4, 20, 12, 8, 20, 6, 19, 6, 5, 8, 4]],
            .pause: [[5, 4, 8, 4, 9, 5, 9, 19, 8, 20, 5, 20, 4, 19, 4, 5, 5, 4], [16, 4, 19, 4, 20, 5, 20, 19, 19, 20, 16, 20, 15, 19, 15, 5, 16, 4]],
            .undo: [[8, 5, 3, 10, 8, 15], [3, 10, 16, 10, 20, 14, 20, 19]],
            .redo: [[16, 5, 21, 10, 16, 15], [21, 10, 8, 10, 4, 14, 4, 19]],
            .fullscreen: [[4, 9, 4, 4, 9, 4], [15, 4, 20, 4, 20, 9], [20, 15, 20, 20, 15, 20], [9, 20, 4, 20, 4, 15]],
            .fullscreenExit: [[4, 9, 9, 9, 9, 4], [15, 4, 15, 9, 20, 9], [20, 15, 15, 15, 15, 20], [9, 20, 9, 15, 4, 15]],
            .plus: [[12, 4, 12, 20], [4, 12, 20, 12]],
            .close: [[6, 6, 18, 18], [18, 6, 6, 18]],
            .start: [[5, 5, 5, 19], [19, 5, 9, 12, 19, 19, 19, 5]],
            .lock: [[6, 11, 18, 11, 20, 13, 20, 19, 18, 21, 6, 21, 4, 19, 4, 13, 6, 11], [8, 11, 8, 7, 10, 4, 14, 4, 16, 7, 16, 11], [12, 15, 12, 17]],
            .unlock: [[6, 11, 18, 11, 20, 13, 20, 19, 18, 21, 6, 21, 4, 19, 4, 13, 6, 11], [8, 11, 8, 7, 10, 4, 14, 4, 16, 7], [12, 15, 12, 17]],
            .eye: [[2, 12, 7, 7, 12, 5, 17, 7, 22, 12, 17, 17, 12, 19, 7, 17, 2, 12], [9, 12, 12, 9, 15, 12, 12, 15, 9, 12]],
            .eyeOff: [[2, 12, 7, 7, 12, 5, 17, 7, 22, 12, 17, 17, 12, 19, 7, 17, 2, 12], [4, 3, 20, 21]],
            .speaker: [[3, 9, 6, 9, 11, 5, 11, 19, 6, 15, 3, 15, 3, 9], [15, 9, 17, 12, 15, 15], [18, 6, 21, 9, 21, 15, 18, 18]],
            .muted: [[3, 9, 6, 9, 11, 5, 11, 19, 6, 15, 3, 15, 3, 9], [15, 9, 21, 15], [21, 9, 15, 15]],
            .text: [[4, 7, 4, 5, 20, 5, 20, 7], [12, 5, 12, 19], [9, 19, 15, 19]],
            .font: [[3, 19, 9, 5, 15, 19], [5, 14, 13, 14], [21, 12, 17, 12, 16, 15, 16, 17, 18, 19, 21, 19, 21, 12]],
            .captions: [[4, 5, 20, 5, 22, 7, 22, 16, 20, 18, 10, 18, 6, 21, 6, 18, 4, 18, 2, 16, 2, 7, 4, 5], [6, 10, 11, 10], [14, 10, 18, 10], [6, 14, 18, 14]],
            .effects: [[12, 2, 14.5, 9.5, 22, 12, 14.5, 14.5, 12, 22, 9.5, 14.5, 2, 12, 9.5, 9.5, 12, 2], [19, 3, 19, 7], [17, 5, 21, 5]],
            .overlay: [[5, 9, 15, 9, 17, 11, 17, 19, 15, 21, 5, 21, 3, 19, 3, 11, 5, 9], [8, 6, 8, 5, 10, 3, 19, 3, 21, 5, 21, 14, 20, 15]],
            .filters: [[12, 3, 16, 7, 12, 11, 8, 7, 12, 3], [7, 11, 11, 15, 7, 19, 3, 15, 7, 11], [17, 11, 21, 15, 17, 19, 13, 15, 17, 11]],
            .adjust: [[3, 6, 10, 6], [14, 6, 21, 6], [10, 6, 12, 4, 14, 6, 12, 8, 10, 6], [3, 12, 6, 12], [10, 12, 21, 12], [6, 12, 8, 10, 10, 12, 8, 14, 6, 12], [3, 18, 14, 18], [18, 18, 21, 18], [14, 18, 16, 16, 18, 18, 16, 20, 14, 18]],
            .crop: [[7, 3, 7, 16, 9, 18, 21, 18], [3, 7, 15, 7, 17, 9, 17, 21]],
            .background: [[12, 3, 21, 12, 14, 19, 10, 19, 3, 12, 12, 3], [3, 12, 21, 12], [21, 15, 19, 19, 20, 21, 22, 21, 23, 19, 21, 15]],
            .shapes: [[5, 4, 15, 4, 17, 6, 17, 10], [9, 18, 5, 18, 3, 16, 3, 6, 5, 4], [16, 10, 22, 14, 20, 21, 12, 21, 10, 14, 16, 10]],
            .sequence: [[4, 5, 16, 5, 18, 7, 18, 15, 16, 17, 4, 17, 2, 15, 2, 7, 4, 5], [7, 20, 20, 20, 22, 18, 22, 9], [8, 8, 13, 11, 8, 14, 8, 8]],
            .presets: [[5, 3, 19, 3, 21, 5, 21, 19, 19, 21, 5, 21, 3, 19, 3, 5, 5, 3], [12, 6, 14, 10, 18, 12, 14, 14, 12, 18, 10, 14, 6, 12, 10, 10, 12, 6]],
            .music: [[9, 17, 9, 5, 20, 3, 20, 14], [9, 15, 6, 14, 3, 16, 3, 19, 6, 20, 9, 18, 9, 15], [20, 12, 17, 11, 14, 13, 14, 16, 17, 17, 20, 15, 20, 12], [9, 9, 20, 7]],
            .voice: [[10, 3, 14, 3, 16, 5, 16, 12, 14, 14, 10, 14, 8, 12, 8, 5, 10, 3], [5, 11, 5, 14, 9, 18, 15, 18, 19, 14, 19, 11], [12, 18, 12, 21], [8, 21, 16, 21]],
            .waveform: [[2, 12, 5, 12, 7, 7, 10, 18, 13, 4, 16, 20, 18, 12, 22, 12]],
            .speed: [[4, 18, 3, 14, 5, 8, 9, 4, 15, 4, 19, 8, 21, 14, 20, 18, 4, 18], [10, 14, 16, 8]],
            .delete: [[4, 6, 20, 6], [8, 6, 9, 3, 15, 3, 16, 6], [6, 6, 7, 19, 9, 21, 15, 21, 17, 19, 18, 6], [10, 10, 10, 16], [14, 10, 14, 16]],
            .mask: [[5, 3, 19, 3, 21, 5, 21, 19, 19, 21, 5, 21, 3, 19, 3, 5, 5, 3], [9, 9, 15, 9, 15, 15, 9, 15, 9, 9], [3, 13, 8, 8], [4, 20, 9, 15], [13, 21, 21, 13]],
            .replace: [[4, 10, 4, 7, 7, 4, 19, 4], [15, 1, 19, 4, 15, 8], [20, 14, 20, 17, 17, 20, 5, 20], [9, 16, 5, 20, 9, 23]],
            .cutout: [[3, 8, 3, 4, 7, 4], [17, 4, 21, 4, 21, 8], [21, 16, 21, 20, 17, 20], [7, 20, 3, 20, 3, 16], [9, 15, 9, 12, 12, 8, 15, 12, 15, 15, 12, 17, 9, 15]],
            .freeze: [[12, 2, 12, 22], [3, 7, 21, 17], [3, 17, 21, 7], [9, 4, 12, 7, 15, 4], [9, 20, 12, 17, 15, 20], [4, 10, 8, 10, 7, 6], [20, 14, 16, 14, 17, 18], [4, 14, 8, 14, 7, 18], [20, 10, 16, 10, 17, 6]],
            .track: [[3, 8, 3, 3, 8, 3], [16, 3, 21, 3, 21, 8], [21, 16, 21, 21, 16, 21], [8, 21, 3, 21, 3, 16], [7, 12, 12, 7, 17, 12, 12, 17, 7, 12]],
            .mainTrack: [[3, 5, 21, 5], [3, 10, 12, 10, 14, 12, 14, 17, 12, 19, 3, 19, 3, 10], [18, 14, 22, 14], [20, 12, 22, 14, 20, 16]],
            .pencil: [[4, 15, 15, 4, 17, 3, 21, 7, 20, 9, 9, 20, 3, 21, 4, 15], [14, 5, 19, 10]],
            .transform: [[12, 2, 12, 22], [2, 12, 22, 12], [9, 5, 12, 2, 15, 5], [9, 19, 12, 22, 15, 19], [5, 9, 2, 12, 5, 15], [19, 9, 22, 12, 19, 15]],
            .duplicate: [[8, 9, 19, 9, 21, 11, 21, 19, 19, 21, 8, 21, 6, 19, 6, 11, 8, 9], [3, 15, 3, 5, 5, 3, 16, 3, 18, 5], [13.5, 12, 13.5, 18], [10.5, 15, 16.5, 15]],
            .opacity: [[12, 3, 17, 5, 21, 10, 21, 14, 17, 19, 12, 21, 7, 19, 3, 14, 3, 10, 7, 5, 12, 3, 12, 21], [6, 15, 12, 9], [5, 10, 10, 5], [9, 19, 12, 16]],
            .draw: [[3, 16, 7, 5, 10, 4, 10, 9, 6, 17, 8, 20, 14, 14, 16, 13, 17, 16, 16, 19, 18, 20, 22, 17]],
            .vector: [[5, 19, 11, 6, 19, 13], [3, 19, 5, 17, 7, 19, 5, 21, 3, 19], [9, 6, 11, 4, 13, 6, 11, 8, 9, 6], [17, 13, 19, 11, 21, 13, 19, 15, 17, 13]],
            .shapeEdit: [[5, 4, 17, 4, 19, 6, 19, 11, 17, 13, 5, 13, 3, 11, 3, 6, 5, 4], [3, 19, 10, 19], [14, 19, 21, 19], [10, 19, 12, 17, 14, 19, 12, 21, 10, 19]],
            .cube: [[12, 2, 21, 7, 21, 17, 12, 22, 3, 17, 3, 7, 12, 2], [3, 7, 12, 12, 21, 7], [12, 12, 12, 22]],
        ]
        var paths: [EditIcon: [Path]] = [:]
        for (icon, segments) in strokes {
            paths[icon] = segments.map { points in
                var path = Path()
                path.move(to: CGPoint(x: points[0], y: points[1]))
                for i in stride(from: 2, to: points.count, by: 2) {
                    path.addLine(to: CGPoint(x: points[i], y: points[i + 1]))
                }
                if icon.filled { path.closeSubpath() }
                return path
            }
        }
        return paths
    }()
}

struct EditToolIcon: View {
    let icon: EditIcon
    var color: Color = AureaColors.text
    var size: CGFloat = 22

    var body: some View {
        Canvas { context, canvasSize in
            let k = canvasSize.width / 24
            var scaled = context
            scaled.scaleBy(x: k, y: k)
            for path in icon.paths {
                if icon.filled { scaled.fill(path, with: .color(color)) }
                else { scaled.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 1.7, lineCap: .round, lineJoin: .round)) }
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

private enum EditToolKind: String, CaseIterable {
    case select, ripple, roll, razor, slip, slide
    var key: String { "edit_tool_\(rawValue)" }
    var icon: EditIcon {
        switch self {
        case .select: return .select
        case .ripple: return .ripple
        case .roll: return .roll
        case .razor: return .razor
        case .slip: return .slip
        case .slide: return .slide
        }
    }
}

private enum EditPalette {
    static func hex(_ value: UInt32) -> Color {
        Color(red: Double((value >> 16) & 255) / 255, green: Double((value >> 8) & 255) / 255, blue: Double(value & 255) / 255)
    }
    static let lane = hex(0x0D0F12), head = hex(0x08090B), rulerBg = hex(0x08090B), tick = hex(0x5B606B), separator = hex(0x3A3E47)
    static let video = hex(0x1F6F8B), videoTop = hex(0x17566C), overlay = hex(0x7A2FA8), overlayTop = hex(0x5E2482)
    static let adjustment = hex(0xD016C4), adjustmentTop = hex(0xA30F99), audio = hex(0x2F6A23), audioWave = hex(0x9FE08A)
    static let linked = hex(0x17465A), linkedWave = hex(0x67C4F2), snap = hex(0xFFD54A), cut = hex(0xE8EDF5)
    static var playhead: Color { AureaColors.accent }
}

private struct LaneClip {
    let row: LayerItem
    let virtual: Bool
}

private enum EditDragKind { case trimIn, trimOut, move, slip, slide, audioLane }

private struct ClipDrag {
    let kind: EditDragKind
    let clip: LayerItem
    let neighbour: LayerItem?
    let targets: [Int32]
    var frames: Int32 = 0
    var lane: UInt32
    var snapped: Int32 = -1
}

/// Encaixe: o alvo mais perto de `frame` dentro de `reach` quadros, ou o próprio `frame`.
private func snapFrame(_ frame: Int32, _ targets: [Int32], reach: CGFloat) -> Int32 {
    var best = frame
    var distance = reach
    for t in targets {
        let d = CGFloat(abs(t - frame))
        if d < distance { distance = d; best = t }
    }
    return best
}

private struct EditTimelineView: View {
    @EnvironmentObject private var model: AureaModel
    let compact: Bool
    @Binding var showMore: Bool
    let onAddMedia: () -> Void
    var onTransition: (Int64, Int64) -> Void = { _, _ in }
    @State private var tool = EditToolKind.select
    @State private var snap = true
    @State private var revealedTrack: UInt32?

    var body: some View {
        Group {
            if let raw = model.editTimeline {
                let used = Set(model.layers.map(\.trackId)).union(raw.linkedAudio.values)
                let main = model.layers.first { $0.id == model.mainTrackAnchor }?.trackId ?? raw.tracks.first { !$0.audio }?.id
                let tracks = raw.tracks.filter { used.contains($0.id) || $0.id == main || $0.id == revealedTrack }
                let ordered = tracks.filter { $0.id == main }
                    + Array(tracks.filter { !$0.audio && $0.id != main }.reversed())
                    + tracks.filter(\.audio)
                let data = EditTimelineData(tracks: ordered, linkedAudio: raw.linkedAudio, markIn: raw.markIn, markOut: raw.markOut)
                EditTimelineBody(clock: model.playheadClock, data: data, tool: tool, snap: snap, compact: compact,
                                 onAddMedia: onAddMedia, onTransition: onTransition)
            } else { Color.clear }
        }
        .background(EditColors.canvas)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("edit_timeline"))
        .accessibilityIdentifier("edit.timeline")
        .sheet(isPresented: $showMore) {
            ScrollView { moreSheet }.presentationDetents([.medium, .large])
        }
    }

    /// O que não é ferramenta de arrasto, em três grupos.
    private var moreSheet: some View {
        VStack(alignment: .leading, spacing: 14) {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 4), spacing: 8) {
                ForEach(EditToolKind.allCases, id: \.self) { kind in
                    moreTile(kind.icon, key: kind.key, id: "edit.tool.\(kind.rawValue)", on: kind == tool) { tool = kind }
                }
                moreTile(.snap, key: "edit_snap", id: "edit.snap", on: snap) { snap.toggle() }
                moreTile(.ripple, key: model.editMode ? "edit_magnet_on" : "edit_magnet_off", id: "edit.magnet", on: model.editMode) { model.toggleEditMode() }
            }
            if let tracks = model.editTimeline?.tracks {
                ForEach(tracks) { track in
                    Menu {
                        Button(AureaText.t("edit_track_target", track.label)) {
                            revealedTrack = track.id
                            model.setEditTrackFlag(track.id, flag: 3, on: !track.target)
                            showMore = false
                        }
                            .accessibilityIdentifier("edit.track.\(track.label).target")
                        Button(AureaText.t("edit_track_lock")) { model.setEditTrackFlag(track.id, flag: 0, on: !track.locked) }
                            .accessibilityIdentifier("edit.track.\(track.label).lock")
                        Button(AureaText.t(track.audio ? "edit_track_mute" : "edit_track_visible")) { model.setEditTrackFlag(track.id, flag: 1, on: !track.muted) }
                            .accessibilityIdentifier("edit.track.\(track.label).\(track.audio ? "mute" : "visible")")
                        if track.audio {
                            Button(AureaText.t("edit_track_solo")) { model.setEditTrackFlag(track.id, flag: 2, on: !track.solo) }
                                .accessibilityIdentifier("edit.track.\(track.label).solo")
                        }
                    } label: {
                        HStack { Text(track.label); Spacer(); EditToolIcon(icon: .more, size: 16) }
                            .font(.aurea(size: 13)).padding(.horizontal, 12).frame(minHeight: 44)
                    }.accessibilityIdentifier("edit.track.\(track.label).menu")
                }
            }
            moreSection("edit_section_marks") {
                moreTile(.markIn, key: "edit_mark_in", id: "edit.markIn") { model.toggleEditMark(0) }
                moreTile(.markOut, key: "edit_mark_out", id: "edit.markOut") { model.toggleEditMark(1) }
                moreTile(.lift, key: "edit_lift", id: "edit.lift") { model.editMarkedRange(extract: false) }
                moreTile(.extract, key: "edit_extract", id: "edit.extract") { model.editMarkedRange(extract: true) }
            }
            moreSection("edit_section_tracks") {
                moreTile(.razorAll, key: "edit_razor_all", id: "edit.razorAll") { model.razorAllAtPlayhead() }
                moreTile(.addVideo, key: "edit_add_video_track", id: "edit.addVideoTrack") { revealedTrack = model.addEditTrack(audio: false) }
                moreTile(.addAudio, key: "edit_add_audio_track", id: "edit.addAudio") { revealedTrack = model.addEditTrack(audio: true) }
                moreTile(.clearTracks, key: "edit_remove_empty_tracks", id: "edit.clearTracks") { model.removeEmptyEditTracks() }
            }
            moreSection("edit_section_placement") {
                moreTile(.placeEnd, key: "edit_place_end", id: "edit.placement.0", on: model.editPlacement == 0) { model.editPlacement = 0 }
                moreTile(.placeInsert, key: "edit_place_insert", id: "edit.placement.1", on: model.editPlacement == 1) { model.editPlacement = 1 }
                moreTile(.placeOverwrite, key: "edit_place_overwrite", id: "edit.placement.2", on: model.editPlacement == 2) { model.editPlacement = 2 }
                Color.clear.frame(maxWidth: .infinity, maxHeight: 1)
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(EditColors.bar.ignoresSafeArea())
    }

    private func moreSection<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t(title)).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
            HStack(spacing: 8) { content() }
        }
    }

    private func moreTile(_ icon: EditIcon, key: String, id: String, on: Bool = false, action: @escaping () -> Void) -> some View {
        Button { action(); showMore = false } label: {
            VStack(spacing: 4) {
                EditToolIcon(icon: icon, color: on ? AureaColors.accent : AureaColors.text, size: 24)
                Text(AureaText.t(key)).font(.aurea(size: 11, weight: .medium)).lineLimit(1).minimumScaleFactor(0.8)
                    .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
            }
            .padding(.horizontal, 4).frame(maxWidth: .infinity).frame(height: 68)
            .background(on ? AureaColors.accentDim : EditColors.dockLevel, in: RoundedRectangle(cornerRadius: 11))
        }.buttonStyle(AureaPressStyle(shrink: 0.95))
            .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
            .accessibilityIdentifier(id)
    }
}

private struct EditTimelineBody: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let clock: PlayheadClock
    let data: EditTimelineData
    let tool: EditToolKind
    let snap: Bool
    let compact: Bool
    let onAddMedia: () -> Void
    let onTransition: (Int64, Int64) -> Void

    @State private var viewportWidth: CGFloat = 0
    @State private var mainAudio: [Int64: Bool] = [:]
    @State private var scrubStart: CGFloat = 0
    @State private var pxPerSecond: CGFloat = 72
    @State private var zoomStart: CGFloat?
    @State private var scrollFrames: CGFloat = 0
    @State private var scrollY: CGFloat = 0
    @State private var drag: ClipDrag?
    @State private var gestureStart: CGPoint?
    @State private var gestureLast: CGPoint = .zero
    @State private var gestureMoved = false
    @State private var scrubbing = false
    @State private var longPress: DispatchWorkItem?
    @State private var thumbs = TimelineThumbStrip()
    @State private var waves = EditWaveCache()

    private let headW: CGFloat = 0, rulerH: CGFloat = 20, gap: CGFloat = 6, edge: CGFloat = 13, slop: CGFloat = 6
    private var laneH: CGFloat { compact ? 40 : 50 }
    private var fps: Double { max(model.compositionFps, 1) }
    private var pxPerFrame: CGFloat { pxPerSecond / CGFloat(fps) }
    private var lanes: [EditTrackInfo] { data.lanes }
    private func laneTop(_ index: Int) -> CGFloat { rulerH + CGFloat(index) * (laneH + gap) }
    private var missingAudio: Bool { !model.layers.contains { $0.kind == 3 } && data.linkedAudio.isEmpty }
    private var missingText: Bool { !model.layers.contains { $0.kind == 4 } }
    private var contentBottom: CGFloat { rulerH + CGFloat(lanes.count) * (laneH + gap) + (missingAudio ? 36 : 0) + (missingText ? 36 : 0) }
    private var inlineAddLanes: some View {
        VStack(alignment: .leading, spacing: 4) {
            if missingAudio {
                Button { model.pause(); shell.addPicker = .audioFile } label: {
                    Label { Text(AureaText.t("edit_add_audio_lane")) } icon: { EditToolIcon(icon: .plus, color: AureaColors.muted, size: 14) }.font(.aurea(size: 12)).frame(minHeight: 32)
                }.accessibilityIdentifier("edit.inline.audio")
            }
            if missingText {
                Button { model.pause(); _ = model.addText() } label: {
                    Label { Text(AureaText.t("edit_text_add")) } icon: { EditToolIcon(icon: .plus, color: AureaColors.muted, size: 14) }.font(.aurea(size: 12)).frame(minHeight: 32)
                }.accessibilityIdentifier("edit.inline.text")
            }
        }.buttonStyle(.plain).foregroundStyle(AureaColors.muted).padding(.horizontal, 8)
            .frame(width: max(160, viewportWidth / 2), alignment: .leading)
    }

    private func refreshMainAudio() {
        let anchor = model.layers.first { $0.id == model.mainTrackAnchor }
        guard let main = anchor?.trackId ?? data.tracks.first(where: { !$0.audio })?.id else {
            if !mainAudio.isEmpty { mainAudio = [:] }
            return
        }
        var next: [Int64: Bool] = [:]
        for row in model.layers {
            guard anchor?.id == row.id || (main != 0 && row.trackId == main),
                  row.kind == 1 || row.kind == 3,
                  let detail = model.engine.layerDetail(row.id) else { continue }
            let flags = (detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0
            if flags & 4 != 0 { next[row.id] = flags & 1 != 0 }
        }
        if mainAudio != next { mainAudio = next }
    }
    private var sequencePrefixWidth: CGFloat { mainAudio.isEmpty ? 50 : 86 }
    private var sequenceStart: some View {
        let audio = mainAudio
        let muted = !audio.isEmpty && audio.values.allSatisfy { $0 }
        return HStack(spacing: 4) {
            if !audio.isEmpty {
                Button {
                    model.beginGesture("main audio mute")
                    model.mutate { core in for id in audio.keys { core.setLayer(id, audioMuted: !muted) } }
                    model.endGesture(); model.refreshModel(force: true)
                } label: {
                    EditToolIcon(icon: muted ? .muted : .speaker, size: 18)
                        .frame(width: 32, height: 50).contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t(muted ? "edit_mobile_main_audio_unmute" : "edit_mobile_main_audio_mute"))
                    .accessibilityIdentifier("edit.mainAudio")
            }
            Button { model.pause(); model.seek(toFrame: 0); scrollFrames = 0 } label: {
                VStack(spacing: 4) {
                    EditToolIcon(icon: .start, size: 16)
                    Text(AureaText.t("panel_inicio")).font(.aurea(size: 10)).lineLimit(1)
                }.frame(width: 50, height: 50).background(EditColors.bar, in: RoundedRectangle(cornerRadius: 5))
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("edit_mobile_sequence_start")).accessibilityIdentifier("edit.sequenceStart")
        }.foregroundStyle(AureaColors.text)
    }

    private var laneClips: [[LaneClip]] {
        let byTrack = Dictionary(grouping: model.layers, by: \.trackId)
        return lanes.map { lane in
            let real = (byTrack[lane.id] ?? []).map { LaneClip(row: $0, virtual: false) }
            let linked = lane.audio ? model.layers.filter { data.linkedAudio[$0.id] == lane.id }.map { LaneClip(row: $0, virtual: true) } : []
            return (real + linked).sorted { $0.row.startFrame < $1.row.startFrame }
        }
    }

    private func frameAt(_ x: CGFloat) -> CGFloat { scrollFrames + (x - viewportWidth / 2) / pxPerFrame }
    private func maxScroll(_ height: CGFloat) -> CGFloat { height > 0 ? max(0, contentBottom - height) : 0 }
    private func clampedScroll(_ offset: CGFloat, width: CGFloat) -> CGFloat {
        let viewport = width - headW
        guard offset.isFinite, viewport.isFinite, pxPerFrame.isFinite, viewport > 0, pxPerFrame > 0 else { return 0 }
        let lastOffset = max(0, CGFloat(model.compositionDuration))
        return min(max(0, offset), lastOffset)
    }

    var body: some View {
        GeometryReader { geometry in
            let clips = laneClips
            let size = geometry.size
            ZStack(alignment: .topLeading) {
                EditLanesCanvas(clock: clock, data: data, lanes: lanes, clips: clips, drag: drag, pxPerFrame: pxPerFrame, scrollFrames: scrollFrames,
                                scrollY: scrollY, centerX: size.width / 2, headW: headW, rulerH: rulerH, laneH: laneH, gap: gap, fps: fps, thumbs: thumbs, waves: waves) { frame in
                    follow(frame: frame, width: size.width)
                }
                .frame(width: size.width, height: size.height)
                .contentShape(Rectangle())
                .gesture(touchGesture(clips: clips, height: size.height, width: size.width))
                .simultaneousGesture(zoomGesture(width: size.width))

                inlineAddLanes
                    .offset(x: size.width / 2 - scrollFrames * pxPerFrame, y: rulerH + CGFloat(lanes.count) * (laneH + gap) - scrollY)
                sequenceStart
                    .offset(x: size.width / 2 - scrollFrames * pxPerFrame - sequencePrefixWidth, y: rulerH + 2 - scrollY)
                Button(action: onAddMedia) {
                    EditToolIcon(icon: .plus, color: AureaColors.onAction, size: 20)
                        .frame(width: 36, height: 36).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("edit_add_media"))
                    .accessibilityIdentifier("edit.add")
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                    .padding(.trailing, 4).padding(.top, rulerH + max(0, (laneH - 44) / 2))
                // Alvos do leitor de tela: não interceptam toque, só dão nome e ação ao clipe.
                ForEach(lanes.indices, id: \.self) { index in
                    ForEach(clips[index].filter { !$0.virtual }, id: \.row.id) { clip in
                        let left = size.width / 2 + (CGFloat(clip.row.startFrame) - scrollFrames) * pxPerFrame
                        let right = size.width / 2 + (CGFloat(clip.row.endFrame) - scrollFrames) * pxPerFrame
                        Color.clear
                            .frame(width: max(0, min(size.width, right) - max(headW, left)), height: laneH)
                            .offset(x: max(headW, left), y: max(rulerH, laneTop(index) - scrollY))
                            .allowsHitTesting(false)
                            .accessibilityHidden(right <= headW || left >= size.width)
                            .accessibilityElement()
                            .accessibilityLabel(model.selection.contains(clip.row.id) ? AureaText.t("edit_clip_selected", clip.row.name) : clip.row.name)
                            .accessibilityAddTraits(model.selection.contains(clip.row.id) ? [.isButton, .isSelected] : .isButton)
                            .accessibilityAction { model.select(layerId: clip.row.id) }
                            .accessibilityIdentifier("edit.clip.\(clip.row.id)")
                    }
                }
            }
            .clipped()
            .onAppear { viewportWidth = size.width; scrollFrames = clampedScroll(CGFloat(clock.frame), width: size.width); refreshMainAudio() }
            .onChange(of: model.status.modelRevision) { _ in refreshMainAudio() }
            .onChange(of: model.mainTrackAnchor) { _ in refreshMainAudio() }
            .onChange(of: model.layers) { _ in refreshMainAudio() }
            .onChange(of: data.tracks) { _ in refreshMainAudio() }
            .onChange(of: model.primarySelection) { _ in reveal(clips: clips, height: size.height) }
            .onChange(of: size.height) { h in reveal(clips: clips, height: h) }
            .onChange(of: size.width) { width in viewportWidth = width; scrollFrames = clampedScroll(scrollFrames, width: width) }
            .onChange(of: model.compositionDuration) { _ in scrollFrames = clampedScroll(scrollFrames, width: size.width) }
        }
    }

    /// Playback and scrubbing share the absolute frame under the center line.
    private func follow(frame: Int64, width: CGFloat) {
        guard drag == nil, gestureStart == nil else { return }
        scrollFrames = clampedScroll(CGFloat(frame), width: width)
    }

    private func reveal(clips: [[LaneClip]], height: CGFloat) {
        guard height > 0, let id = model.primarySelection,
              let index = clips.firstIndex(where: { $0.contains { $0.row.id == id && !$0.virtual } }) else { return }
        let top = laneTop(index) - scrollY
        var next = scrollY
        if top < rulerH { next -= rulerH - top } else if top + laneH > height { next += top + laneH - height }
        next = min(max(0, next), maxScroll(height))
        if next != scrollY { scrollY = next }
    }

    private func zoomGesture(width: CGFloat) -> some Gesture {
        MagnificationGesture()
            .onChanged { scale in
                if zoomStart == nil {
                    zoomStart = pxPerSecond
                    longPress?.cancel(); longPress = nil
                    drag = nil
                    gestureMoved = true
                }
                pxPerSecond = min(max((zoomStart ?? pxPerSecond) * scale, 4.5), 1200)
                scrollFrames = clampedScroll(scrollFrames, width: width)
            }
            .onEnded { _ in zoomStart = nil }
    }

    private func touchGesture(clips: [[LaneClip]], height: CGFloat, width: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                if gestureStart == nil { touchBegan(value.startLocation, clips: clips) }
                touchMoved(value, height: height, width: width, clips: clips)
            }
            .onEnded { value in touchEnded(value, clips: clips) }
    }

    private func hitClip(_ p: CGPoint, clips: [[LaneClip]]) -> (lane: Int, clip: LaneClip, zone: EditDragKind)? {
        guard p.x >= headW, p.y >= rulerH else { return nil }
        let y = p.y + scrollY
        guard let lane = lanes.indices.first(where: { y >= laneTop($0) && y < laneTop($0) + laneH }) else { return nil }
        let frame = frameAt(p.x)
        let edgeFrames = edge / pxPerFrame
        var near: (lane: Int, clip: LaneClip, zone: EditDragKind)?
        for clip in clips[lane] {
            let s = CGFloat(clip.row.startFrame), e = CGFloat(clip.row.endFrame)
            if frame < s - edgeFrames || frame > e + edgeFrames { continue }
            let wide = (e - s) * pxPerFrame > edge * 3
            // Clipe estreito: o miolo continua alcançável.
            let zone: EditDragKind = wide && frame <= s + edgeFrames ? .trimIn : wide && frame >= e - edgeFrames ? .trimOut : .move
            if frame >= s && frame <= e { return (lane, clip, zone) }
            near = (lane, clip, frame < s ? .trimIn : .trimOut)
        }
        return near
    }

    private func touchBegan(_ start: CGPoint, clips: [[LaneClip]]) {
        gestureStart = start
        gestureLast = start
        gestureMoved = false
        guard start.x >= headW else { return }
        if start.y < rulerH {
            if model.status.playing != 0 { model.pause() }
            scrubbing = true
            scrubStart = scrollFrames
            return
        }
        // Segurar: pega o clipe mesmo sem estar escolhido.
        let hit = hitClip(start, clips: clips)
        guard let hit else { return }
        let work = DispatchWorkItem {
            guard !gestureMoved, drag == nil else { return }
            if let began = beginDrag(hit, longPress: true, clips: clips) {
                drag = began
                UIImpactFeedbackGenerator(style: .medium).impactOccurred()
            }
        }
        longPress = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    private func beginDrag(_ hit: (lane: Int, clip: LaneClip, zone: EditDragKind), longPress: Bool, clips: [[LaneClip]]) -> ClipDrag? {
        let row = hit.clip.row
        let selected = model.selection.contains(row.id)
        guard selected || longPress || tool != .select else { return nil }
        var kind = hit.zone
        if hit.clip.virtual {
            guard hit.zone == .move else { return nil }
            kind = .audioLane
        } else {
            switch tool {
            case .slip: kind = .slip
            case .slide: kind = .slide
            case .razor: return nil
            case .roll: if hit.zone == .move { return nil }
            default: break
            }
        }
        var neighbour: LayerItem?
        if tool == .roll && kind == .trimIn {
            neighbour = clips[hit.lane].first { !$0.virtual && $0.row.endFrame == row.startFrame }?.row
            if neighbour == nil { return nil }
        }
        if model.status.playing != 0 { model.pause() }
        if !selected { model.select(layerId: row.id) }
        var targets: [Int32] = []
        if snap {
            targets.append(Int32(clock.frame))
            if data.markIn >= 0 { targets.append(Int32(data.markIn)) }
            if data.markOut >= 0 { targets.append(Int32(data.markOut)) }
            for lane in clips { for c in lane where c.row.id != row.id { targets.append(c.row.startFrame); targets.append(c.row.endFrame) } }
        }
        return ClipDrag(kind: kind, clip: row, neighbour: neighbour, targets: targets, lane: lanes[hit.lane].id)
    }

    private func touchMoved(_ value: DragGesture.Value, height: CGFloat, width: CGFloat, clips: [[LaneClip]]) {
        guard let start = gestureStart, start.x >= headW, zoomStart == nil else { return }
        if scrubbing {
            scrollFrames = clampedScroll(scrubStart - value.translation.width / pxPerFrame, width: width)
            let target = Int64(scrollFrames.rounded())
            if target != clock.frame { model.seek(toFrame: target) }
            return
        }
        if !gestureMoved && hypot(value.translation.width, value.translation.height) > slop {
            gestureMoved = true
            longPress?.cancel(); longPress = nil
            if drag == nil, let hit = hitClip(start, clips: clips) { drag = beginDrag(hit, longPress: false, clips: clips) }
        }
        guard gestureMoved || drag != nil else { return }
        if var current = drag {
            update(&current, translation: value.translation.width, location: value.location)
            drag = current
        } else {
            scrollFrames = clampedScroll(scrollFrames - (value.location.x - gestureLast.x) / pxPerFrame, width: width)
            if model.status.playing != 0 { model.pause() }
            model.seek(toFrame: Int64(scrollFrames.rounded()))
            scrollY = min(max(0, scrollY - (value.location.y - gestureLast.y)), maxScroll(height))
        }
        gestureLast = value.location
    }

    private func update(_ current: inout ClipDrag, translation: CGFloat, location: CGPoint) {
        let raw = Int32((translation / pxPerFrame).rounded())
        let reach = 9 / pxPerFrame
        var frames = raw
        var snappedAt: Int32 = -1
        if !current.targets.isEmpty {
            let start = current.clip.startFrame, end = current.clip.endFrame
            switch current.kind {
            case .trimIn:
                let s = snapFrame(start + raw, current.targets, reach: reach)
                if s != start + raw { frames = s - start; snappedAt = s }
            case .trimOut:
                let s = snapFrame(end + raw, current.targets, reach: reach)
                if s != end + raw { frames = s - end; snappedAt = s }
            case .move, .slide:
                let a = snapFrame(start + raw, current.targets, reach: reach), b = snapFrame(end + raw, current.targets, reach: reach)
                if a != start + raw { frames = a - start; snappedAt = a } else if b != end + raw { frames = b - end; snappedAt = b }
            default: break
            }
        }
        if snappedAt >= 0 && snappedAt != current.snapped { UISelectionFeedbackGenerator().selectionChanged() }
        current.frames = frames
        current.snapped = snappedAt
        if current.kind == .move || current.kind == .audioLane {
            let y = location.y + scrollY
            if location.y >= rulerH, let lane = lanes.first(where: { track in
                guard let i = lanes.firstIndex(of: track) else { return false }
                return y >= laneTop(i) && y < laneTop(i) + laneH
            }), lane.audio == (current.kind == .audioLane || current.clip.kind == 3) {
                current.lane = lane.id
            }
        }
    }

    private func touchEnded(_ value: DragGesture.Value, clips: [[LaneClip]]) {
        longPress?.cancel(); longPress = nil
        defer { gestureStart = nil; drag = nil; scrubbing = false; gestureMoved = false }
        guard let start = gestureStart, start.x >= headW else { return }
        if scrubbing { return }
        if let current = drag { commit(current); return }
        if !gestureMoved && zoomStart == nil { tap(start, clips: clips) }
    }

    private func commit(_ drag: ClipDrag) {
        let clip = drag.clip
        let ripple = tool == .ripple
        switch drag.kind {
        case .move:
            if drag.frames != 0 || drag.lane != clip.trackId {
                model.editTrackClip(ripple ? TrackOp.moveInsert : TrackOp.move, layer: clip.id, frame: Int64(max(0, clip.startFrame + drag.frames)), track: drag.lane)
            }
        case .trimIn:
            guard drag.frames != 0 else { return }
            if let left = drag.neighbour { model.editTrackClip(TrackOp.roll, layer: left.id, frame: Int64(clip.startFrame + drag.frames)) }
            else { model.editTrackClip(ripple ? TrackOp.rippleTrimIn : TrackOp.trimIn, layer: clip.id, frame: Int64(clip.startFrame + drag.frames)) }
        case .trimOut:
            guard drag.frames != 0 else { return }
            model.editTrackClip(tool == .roll ? TrackOp.roll : ripple ? TrackOp.rippleTrimOut : TrackOp.trimOut, layer: clip.id, frame: Int64(clip.endFrame + drag.frames))
        case .slip:
            if drag.frames != 0 { model.editTrackClip(TrackOp.slip, layer: clip.id, frame: Int64(drag.frames)) }
        case .slide:
            if drag.frames != 0 { model.editTrackClip(TrackOp.slide, layer: clip.id, frame: Int64(drag.frames)) }
        case .audioLane:
            if drag.lane != data.linkedAudio[clip.id] { model.editTrackClip(TrackOp.moveLinkedAudio, layer: clip.id, frame: 0, track: drag.lane) }
        }
    }

    private func tap(_ p: CGPoint, clips: [[LaneClip]]) {
        let hit = hitClip(p, clips: clips)
        if tool == .razor {
            if let hit, !hit.clip.virtual {
                var frame = Int64(frameAt(p.x).rounded())
                // Cair em cima do cabeçote é quase sempre o que se queria.
                if CGFloat(abs(frame - clock.frame)) * pxPerFrame < edge { frame = clock.frame }
                model.editTrackClip(TrackOp.razor, layer: hit.clip.row.id, frame: frame)
            }
            return
        }
        guard let hit else { model.clearSelection(); return }
        let row = hit.clip.row
        // Um corte encostado de clipes não escolhidos abre as transições dele.
        if !hit.clip.virtual && !model.selection.contains(row.id) && !lanes[hit.lane].audio {
            let frame = frameAt(p.x)
            let reach = edge * 0.75
            if abs(frame - CGFloat(row.startFrame)) * pxPerFrame < reach,
               let left = clips[hit.lane].first(where: { !$0.virtual && $0.row.endFrame == row.startFrame }) {
                onTransition(left.row.id, row.id); return
            }
            if abs(frame - CGFloat(row.endFrame)) * pxPerFrame < reach,
               let right = clips[hit.lane].first(where: { !$0.virtual && $0.row.startFrame == row.endFrame }) {
                onTransition(row.id, right.row.id); return
            }
        }
        model.select(layerId: row.id)
    }


}

/// O desenho das faixas. Só esta view observa o relógio: tocar um vídeo redesenha o
/// Canvas e não refaz a tela. Onda e miniatura saem antes dele, dos caches.
private struct EditLanesCanvas: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var clock: PlayheadClock
    let data: EditTimelineData
    let lanes: [EditTrackInfo]
    let clips: [[LaneClip]]
    let drag: ClipDrag?
    let pxPerFrame: CGFloat
    let scrollFrames: CGFloat
    let scrollY: CGFloat
    let centerX: CGFloat
    let headW: CGFloat
    let rulerH: CGFloat
    let laneH: CGFloat
    let gap: CGFloat
    let fps: Double
    let thumbs: TimelineThumbStrip
    let waves: EditWaveCache
    let onFrame: (Int64) -> Void

    private struct Paint {
        let clip: LaneClip
        let lane: EditTrackInfo
        let top: CGFloat
        let start: Int32
        let end: Int32
        let wave: EditWaveCache.Wave?
        let fpb: Double
        let first: Int64
        let last: Int64
        let thumbnails: [ClipThumbnail]
    }
    private struct ClipThumbnail {
        let image: UIImage
        let x: CGFloat
        let width: CGFloat
    }

    private func xOf(_ frame: CGFloat) -> CGFloat { centerX + (frame - scrollFrames) * pxPerFrame }

    private func geometry(_ row: LayerItem) -> (Int32, Int32) {
        var s = row.startFrame, e = row.endFrame
        guard let drag else { return (s, e) }
        if drag.clip.id == row.id {
            switch drag.kind {
            case .move, .slide: s += drag.frames; e += drag.frames
            case .trimIn: s = min(s + drag.frames, e - 1)
            case .trimOut: e = max(e + drag.frames, s + 1)
            default: break
            }
        } else if drag.neighbour?.id == row.id && drag.kind == .trimIn {
            e += drag.frames
        }
        return (s, e)
    }

    private func paint(_ clip: LaneClip, lane: EditTrackInfo, top: CGFloat, width: CGFloat) -> Paint? {
        let (s, e) = geometry(clip.row)
        let left = xOf(CGFloat(s)), right = xOf(CGFloat(e))
        guard right >= headW, left <= width else { return nil }
        var wave: EditWaveCache.Wave?
        var fpb = 1.0
        var first: Int64 = 0, last: Int64 = -1
        if clip.virtual || clip.row.kind == 3 {
            fpb = WaveGrid.framesPerBucket(targetPx: 1.5, pxPerFrame: pxPerFrame)
            let from = Double((max(left, headW) - xOf(0)) / pxPerFrame), to = Double((min(right, width) - xOf(0)) / pxPerFrame)
            first = WaveGrid.bucketAt(max(from, Double(clip.row.startFrame)), fpb)
            last = WaveGrid.bucketAt(min(to, Double(clip.row.endFrame)) - 1e-6, fpb)
            if last >= first { wave = waves.get(model, clip: clip.row, fpb: fpb, generation: model.status.thumbnailGeneration, first: first, last: last) }
        }
        var thumbnails: [ClipThumbnail] = []
        if [1, 2, 12].contains(clip.row.kind), !clip.virtual {
            let tileWidth = max(36, laneH * 16 / 9)
            let first = max(0, Int(floor((max(left, headW) - left) / tileWidth)))
            let last = max(first, Int(ceil((min(right, width) - left) / tileWidth)))
            let heightPx = min(max(Int((laneH * UIScreen.main.scale).rounded()), 16), 128)
            for tile in first..<last {
                let x = left + CGFloat(tile) * tileWidth
                let frame = Int32(clamping: Int64(clip.row.startFrame) + Int64((CGFloat(tile) * tileWidth / pxPerFrame).rounded()))
                if let image = thumbs.get(model, layer: clip.row.id, bucket: frame, timelineFrame: frame,
                                          heightPx: heightPx, generation: model.status.thumbnailGeneration) {
                    thumbnails.append(ClipThumbnail(image: image, x: x, width: tileWidth))
                }
            }
        }
        return Paint(clip: clip, lane: lane, top: top, start: s, end: e, wave: wave, fpb: fpb, first: first, last: last, thumbnails: thumbnails)
    }

    var body: some View {
        let frame = clock.frame
        GeometryReader { proxy in
            let width = proxy.size.width
            let _ = thumbs.beginFrame(12)
            let paints: [Paint] = lanes.indices.flatMap { i -> [Paint] in
                let top = rulerH + CGFloat(i) * (laneH + gap) - scrollY
                guard top + laneH >= rulerH, top <= proxy.size.height else { return [] }
                return clips[i].compactMap { clip -> Paint? in
                    if let drag, drag.kind == .move, drag.clip.id == clip.row.id, !clip.virtual { return nil }
                    return paint(clip, lane: lanes[i], top: top, width: width)
                }
            }
            let dragged: Paint? = {
                guard let drag, drag.kind == .move, let index = lanes.firstIndex(where: { $0.id == drag.lane }) else { return nil }
                return paint(LaneClip(row: drag.clip, virtual: false), lane: lanes[index], top: rulerH + CGFloat(index) * (laneH + gap) - scrollY, width: width)
            }()
            let firstAudio = lanes.firstIndex(where: \.audio)
            Canvas { context, size in
                drawContent(&context, size: size, paints: paints, dragged: dragged, firstAudio: firstAudio, playhead: frame)
            }
        }
        .onChange(of: frame) { onFrame($0) }
        .accessibilityHidden(true)
    }

    private func drawContent(_ context: inout GraphicsContext, size: CGSize, paints: [Paint], dragged: Paint?, firstAudio: Int?, playhead: Int64) {
        var lanesContext = context
        lanesContext.clip(to: Path(CGRect(x: headW, y: rulerH, width: size.width - headW, height: size.height - rulerH)))
        for i in lanes.indices {
            let top = rulerH + CGFloat(i) * (laneH + gap) - scrollY
            lanesContext.fill(Path(CGRect(x: headW, y: top, width: size.width - headW, height: laneH)), with: .color(EditPalette.lane))
            if i == firstAudio { lanesContext.fill(Path(CGRect(x: headW, y: top - 1.5, width: size.width - headW, height: 1.5)), with: .color(EditPalette.separator)) }
        }
        for paint in paints { drawClip(&lanesContext, paint, width: size.width) }
        // A marca de cada corte encostado.
        for i in lanes.indices where !lanes[i].audio {
            let top = rulerH + CGFloat(i) * (laneH + gap) - scrollY
            for (a, b) in zip(clips[i], clips[i].dropFirst()) where a.row.endFrame == b.row.startFrame {
                let cx = xOf(CGFloat(a.row.endFrame))
                guard cx > headW, cx < size.width else { continue }
                let mid = top + laneH / 2, s: CGFloat = 3.5
                var bow = Path()
                bow.move(to: CGPoint(x: cx - s, y: mid - s)); bow.addLine(to: CGPoint(x: cx + s, y: mid + s))
                bow.addLine(to: CGPoint(x: cx + s, y: mid - s)); bow.addLine(to: CGPoint(x: cx - s, y: mid + s)); bow.closeSubpath()
                lanesContext.fill(bow, with: .color(EditPalette.cut))
            }
        }
        if let dragged { drawClip(&lanesContext, dragged, width: size.width) }
        if let drag, drag.snapped >= 0 {
            let x = xOf(CGFloat(drag.snapped))
            lanesContext.fill(Path(CGRect(x: x - 0.75, y: rulerH, width: 1.5, height: size.height - rulerH)), with: .color(EditPalette.snap))
        }
        if data.markIn >= 0 || data.markOut >= 0 {
            let from = max(xOf(CGFloat(max(data.markIn, 0))), headW)
            let to = data.markOut >= 0 ? min(xOf(CGFloat(data.markOut)), size.width) : size.width
            if to > from { lanesContext.fill(Path(CGRect(x: from, y: rulerH, width: to - from, height: size.height - rulerH)), with: .color(EditPalette.playhead.opacity(0.2))) }
        }

        // Régua por cima das faixas.
        context.fill(Path(CGRect(x: headW, y: 0, width: size.width - headW, height: rulerH)), with: .color(EditPalette.rulerBg))
        let pxPerSecond = pxPerFrame * CGFloat(fps)
        let stepSeconds = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600].first { CGFloat($0) * pxPerSecond >= 64 } ?? 1200
        let step = max(1, Int((Double(stepSeconds) * fps).rounded()))
        var tick = max(0, Int(scrollFrames - centerX / pxPerFrame) / step) * step
        let lastFrame = scrollFrames + (size.width - centerX) / pxPerFrame
        while CGFloat(tick) <= lastFrame {
            let x = xOf(CGFloat(tick))
            if x >= headW {
                context.fill(Path(CGRect(x: x, y: rulerH - 8, width: 1, height: 8)), with: .color(EditPalette.tick))
                let seconds = Int((Double(tick) / fps).rounded())
                context.draw(Text(String(format: "%02d:%02d", seconds / 60, seconds % 60)).font(.system(size: 10)).foregroundColor(EditColors.ruler),
                             at: CGPoint(x: x + 3, y: 3), anchor: .topLeading)
            }
            let half = xOf(CGFloat(tick) + CGFloat(step) / 2)
            if half >= headW { context.fill(Path(CGRect(x: half, y: rulerH - 4, width: 1, height: 4)), with: .color(EditPalette.tick)) }
            tick += step
        }
        if data.markIn >= 0 { drawMark(&context, x: xOf(CGFloat(data.markIn)), direction: 1) }
        if data.markOut >= 0 { drawMark(&context, x: xOf(CGFloat(data.markOut)), direction: -1) }

        let playX = centerX
        if playX >= headW && playX <= size.width {
            context.fill(Path(CGRect(x: playX - 0.75, y: rulerH - 8, width: 1.5, height: size.height - rulerH + 8)), with: .color(EditPalette.playhead))
            var head = Path()
            head.move(to: CGPoint(x: playX, y: rulerH - 13))
            head.addLine(to: CGPoint(x: playX + 4, y: rulerH - 9))
            head.addLine(to: CGPoint(x: playX, y: rulerH - 5))
            head.addLine(to: CGPoint(x: playX - 4, y: rulerH - 9)); head.closeSubpath()
            context.fill(head, with: .color(EditPalette.playhead))
        }
    }

    private func drawMark(_ context: inout GraphicsContext, x: CGFloat, direction: CGFloat) {
        guard x >= headW else { return }
        var path = Path()
        path.move(to: CGPoint(x: x, y: rulerH - 12)); path.addLine(to: CGPoint(x: x + 5 * direction, y: rulerH - 12))
        path.addLine(to: CGPoint(x: x, y: rulerH - 7)); path.addLine(to: CGPoint(x: x, y: rulerH)); path.closeSubpath()
        context.fill(path, with: .color(EditPalette.snap))
    }

    private func drawClip(_ context: inout GraphicsContext, _ paint: Paint, width: CGFloat) {
        let row = paint.clip.row
        let left = xOf(CGFloat(paint.start)), right = xOf(CGFloat(paint.end))
        let w = max(right - left, 1), top = paint.top + 1, h = laneH - 2
        let isVisual = [1, 2, 12].contains(row.kind)
        let audioKind = row.kind == 3
        let body: Color, strip: Color
        if paint.clip.virtual { body = EditPalette.linked; strip = EditPalette.linked }
        else if audioKind { body = EditPalette.audio; strip = EditPalette.audio }
        else if row.adjustment { body = EditPalette.adjustment; strip = EditPalette.adjustmentTop }
        else if isVisual { body = EditPalette.video; strip = EditPalette.videoTop }
        else { body = EditPalette.overlay; strip = EditPalette.overlayTop }
        let alpha = paint.lane.muted || paint.lane.locked ? 0.45 : 1.0
        let rect = CGRect(x: left, y: top, width: w, height: h)
        context.fill(Path(roundedRect: rect, cornerRadius: 2), with: .color(body.opacity(alpha)))
        let visibleLeft = max(left, headW), visibleRight = min(right, width), visibleW = visibleRight - visibleLeft
        if audioKind || paint.clip.virtual {
            if let wave = paint.wave {
                let stroke = max(1, CGFloat(paint.fpb) * pxPerFrame * 0.72)
                let mid = top + h / 2
                var path = Path()
                var b = paint.first
                while b <= paint.last {
                    let v = CGFloat(wave.at(b)) / 255
                    let x = xOf(CGFloat((Double(b) + 0.5) * paint.fpb))
                    b += 1
                    if v <= 0 { continue }
                    let half = v * h / 2 * 0.92
                    path.move(to: CGPoint(x: x, y: mid - half)); path.addLine(to: CGPoint(x: x, y: mid + half))
                }
                context.stroke(path, with: .color(paint.clip.virtual ? EditPalette.linkedWave : EditPalette.audioWave), lineWidth: stroke)
            }
        } else {
            let stripH: CGFloat = isVisual ? 0 : min(14, laneH * 0.34)
            var images = context
            images.clip(to: Path(roundedRect: rect, cornerRadius: 3))
            images.opacity = alpha
            for thumb in paint.thumbnails {
                images.draw(Image(uiImage: thumb.image), in: CGRect(x: thumb.x, y: top, width: thumb.width, height: h))
            }
            if stripH > 0 { context.fill(Path(CGRect(x: left, y: top, width: w, height: stripH)), with: .color(strip.opacity(alpha))) }
            if visibleW > 16 && !isVisual {
                var label = context
                label.clip(to: Path(CGRect(x: visibleLeft + 3, y: top, width: max(0, visibleW - 6 - (row.effectCount > 0 ? 16 : 0)), height: stripH)))
                label.draw(Text(row.name).font(.system(size: 9.5)).foregroundColor(.white), at: CGPoint(x: visibleLeft + 3, y: top), anchor: .topLeading)
            }
            if row.effectCount > 0 && visibleW > 26 {
                context.draw(Text("fx").font(.system(size: 9.5)).foregroundColor(.white), at: CGPoint(x: visibleRight - 14, y: top), anchor: .topLeading)
            }
        }
        if model.selection.contains(row.id) {
            context.stroke(Path(roundedRect: rect, cornerRadius: 2), with: .color(AureaColors.accent), lineWidth: 1.5)
            if !paint.clip.virtual && w > 26 {
                let y = paint.top + laneH * 0.25, tickH = laneH * 0.5
                context.fill(Path(roundedRect: CGRect(x: left + 1, y: y, width: 3, height: tickH), cornerRadius: 1.5), with: .color(AureaColors.accent))
                context.fill(Path(roundedRect: CGRect(x: right - 4, y: y, width: 3, height: tickH), cornerRadius: 1.5), with: .color(AureaColors.accent))
            }
        }
    }
}

/// Janelas da onda por corte. O motor só é consultado quando a vista sai da
/// janela guardada, o degrau de zoom muda, o corte muda de lugar ou chega
/// pedaço novo da onda: tocando, o quadro só lê números daqui.
@MainActor final class EditWaveCache {
    struct Wave {
        let first: Int64
        let data: [UInt8]
        func at(_ k: Int64) -> Int { k < first || k >= first + Int64(data.count) ? 0 : Int(data[Int(k - first)]) }
    }

    private struct Entry {
        let fpb: Double, generation: UInt32, start: Int32, end: Int32, offset: Int32
        let w0: Int64, w1: Int64
        let wave: Wave?
    }

    private static let capacity = 4096
    private var entries: [Int64: Entry] = [:]

    func get(_ model: AureaModel, clip: LayerItem, fpb: Double, generation: UInt32, first: Int64, last: Int64) -> Wave? {
        guard last - first + 1 <= Int64(Self.capacity) else { return nil }
        if let e = entries[clip.id], e.fpb == fpb, e.generation == generation, e.start == clip.startFrame,
           e.end == clip.endFrame, e.offset == clip.offsetFrames, first >= e.w0, last < e.w1 {
            return e.wave
        }
        let win = WaveGrid.window(first: first, last: last, max: Self.capacity)
        let n = Int(max(0, min(Int64(Self.capacity), win.1 - win.0)))
        var wave: Wave?
        if n > 0, let got = model.engine.waveform(forLayer: clip.id, startFrame: Double(win.0) * fpb,
                                                   framesPerBucket: fpb, count: UInt32(n)), !got.isEmpty {
            wave = Wave(first: win.0, data: [UInt8](got.prefix(n)))
        }
        if entries.count > 64 { entries.removeAll() }
        entries[clip.id] = Entry(fpb: fpb, generation: generation, start: clip.startFrame, end: clip.endFrame,
                                 offset: clip.offsetFrames, w0: win.0, w1: win.0 + Int64(n), wave: wave)
        return wave
    }
}

/// Um corte da trilha principal: o trecho que sai e o que entra.
struct EditCut: Identifiable {
    let outgoing: Int64
    let incoming: Int64
    var id: String { "\(outgoing)-\(incoming)" }
}

/// As transições de UM corte (espelho de EditModeScreen.kt `CutTransitionSheet`):
/// os tipos do motor (0 nenhuma … 5 girar), meio segundo de cada lado.
private struct CutTransitionSheet: View {
    @EnvironmentObject private var model: AureaModel
    let cut: EditCut
    @State private var current: UInt32 = 0
    private let keys = ["edit_transition_none", "edit_transition_dissolve", "edit_transition_slide_up",
                        "edit_transition_slide_left", "edit_transition_zoom", "edit_transition_spin"]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(AureaText.t("edit_transition")).font(.aurea(size: 16, weight: .bold)).accessibilityAddTraits(.isHeader)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 10) {
                    ForEach(0..<keys.count, id: \.self) { type in
                        let active = UInt32(type) == current
                        Button {
                            if model.setCutTransition(cut.outgoing, cut.incoming, type: UInt32(type)) { current = UInt32(type) }
                        } label: {
                            Text(AureaText.t(keys[type])).font(.aurea(size: 13, weight: .semibold))
                                .foregroundStyle(active ? AureaColors.accent : AureaColors.text)
                                .multilineTextAlignment(.center).lineLimit(2)
                                .padding(.horizontal, 12).frame(minWidth: 76, minHeight: 56)
                                .background(active ? AureaColors.accentDim : AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
                        }.buttonStyle(AureaPressStyle(shrink: 0.95))
                            .accessibilityAddTraits(active ? [.isButton, .isSelected] : .isButton)
                            .accessibilityIdentifier("edit.transition.\(type)")
                    }
                }
            }
        }
        .padding(16).frame(maxWidth: .infinity, alignment: .leading)
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.editorPanel.ignoresSafeArea())
        .onAppear { current = model.engine.cutTransition(into: cut.incoming) }
    }
}

// =============================================================================
//  FILTROS e AJUSTAR do modo de edição (0.3.0) — par do ColorToolsPanels.kt.
//  Cada um tem a sua tela (antes Efeitos, Filtros e Ajustar abriam o mesmo
//  editor). Os dois mexem na "Correção de cor" do motor (aurea.color.grade),
//  cada um numa instância própria (papel 1 = Filtro, 2 = Ajustar).
// =============================================================================
enum GradeParam {
    static let log: UInt32 = 0, temperature: UInt32 = 1, tint: UInt32 = 2, exposure: UInt32 = 3, contrast: UInt32 = 4
    static let pivot: UInt32 = 5, highlights: UInt32 = 6, shadows: UInt32 = 7, whites: UInt32 = 8, blacks: UInt32 = 9
    static let brightness: UInt32 = 10, saturation: UInt32 = 11, vibrance: UInt32 = 12
    static let lift: UInt32 = 13, liftLuma: UInt32 = 14, gamma: UInt32 = 15, gammaLuma: UInt32 = 16
    static let gain: UInt32 = 17, gainLuma: UInt32 = 18, offset: UInt32 = 19, offsetLuma: UInt32 = 20
    static let curves: UInt32 = 21, hueCurves: UInt32 = 22, hsl: UInt32 = 23
    static let qualifier: UInt32 = 47, qHue: UInt32 = 48, qWidth: UInt32 = 49, qSoft: UInt32 = 54, qInvert: UInt32 = 55, qShow: UInt32 = 56
    static let qHueShift: UInt32 = 57, qSat: UInt32 = 58, qExposure: UInt32 = 59
    static let fade: UInt32 = 60, shadowTint: UInt32 = 61, highlightTint: UInt32 = 62, tintBalance: UInt32 = 63
    static let sharpen: UInt32 = 64, vignette: UInt32 = 65, grain: UInt32 = 68, lutIntensity: UInt32 = 71, mix: UInt32 = 76, look: UInt32 = 78
}

private let gradeLookKeys = ["look_cinema", "look_warm", "look_cool", "look_vintage", "look_mono", "look_noir", "look_vivid",
                             "look_faded", "look_sunset", "look_teal", "look_film", "look_anime", "look_matte"]

struct GradeScopes {
    let columns: Int
    let histogram: [UInt32]
    let waveform: [UInt16]
    let parade: [UInt16]
    let vectorscope: [UInt16]
}

extension AureaModel {
    /// A Correção de cor com o papel `role` na camada escolhida (criada na primeira vez).
    func colorGrade(role: UInt32) -> UInt32? {
        guard let layer = primarySelection else { return nil }
        let id = engine.ensureColorGrade(forLayer: layer, role: role)
        guard id >= 0 else { return nil }
        refreshSelectedLayer()
        loadParams(layerId: layer, effectId: UInt32(id))
        return UInt32(id)
    }
    func gradeParam(_ index: UInt32) -> EffectParamItem? { effectParams.first { $0.index == index } }
    /// Um componente (o mesmo contrato do EffectsView: animado → chave no cabeçote).
    func writeGrade(_ effect: UInt32, index: UInt32, component: Int = 0, value: Float) {
        guard value.isFinite, let layer = primarySelection, let param = gradeParam(index) else { return }
        let count = fxComponentCount(Int(param.type))
        if param.animated {
            engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component), time: localPlayhead, value: value)
        } else if count <= 1 {
            engine.setEffect(effect, forLayer: layer, paramIndex: index, value: value)
        } else {
            var v = Array((param.value + [0, 0, 0, 1]).prefix(4)); v[component] = value
            engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: v[0], g: v[1], b: v[2], a: v[3])
        }
        commitPendingCommands(); loadParams(layerId: layer, effectId: effect)
    }
    /// O ponto inteiro (roda de cor) num comando só.
    func writeGradePoint(_ effect: UInt32, index: UInt32, x: Float, y: Float) {
        guard x.isFinite, y.isFinite, let layer = primarySelection, let param = gradeParam(index) else { return }
        if param.animated {
            engine.keyParameter(layer, property: 31, effect: effect, param: index * 4, time: localPlayhead, value: x)
            engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + 1, time: localPlayhead, value: y)
        } else {
            let v = Array((param.value + [0, 0, 0, 1]).prefix(4))
            engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: x, g: y, b: v[2], a: v[3])
        }
        commitPendingCommands(); loadParams(layerId: layer, effectId: effect)
    }
    func applyColorLook(_ look: Int32, effect: UInt32) {
        guard let layer = primarySelection else { return }
        if engine.applyColorLook(look, effect: effect, forLayer: layer) { refreshModel(force: true); loadParams(layerId: layer, effectId: effect) }
    }
    func resetColorGrade(effect: UInt32) {
        guard let layer = primarySelection else { return }
        if engine.resetColorGrade(effect: effect, forLayer: layer) { refreshModel(force: true); loadParams(layerId: layer, effectId: effect) }
    }
    nonisolated static func lookImages(from dict: [String: Any]?) -> [UIImage]? {
        guard let dict, let data = dict["data"] as? Data,
              let w = (dict["width"] as? NSNumber)?.intValue, let h = (dict["height"] as? NSNumber)?.intValue,
              let n = (dict["count"] as? NSNumber)?.intValue, w > 0, h > 0, n > 0 else { return nil }
        let bytes = w * h * 4
        return (0..<n).compactMap { k in
            guard data.count >= (k + 1) * bytes else { return nil }
            return UIImage.fromRGBA(data.subdata(in: (k * bytes)..<((k + 1) * bytes)), width: w, height: h)
        }
    }
    nonisolated static func gradeScopes(from dict: [String: Any]?) -> GradeScopes? {
        guard let dict, let cols = (dict["columns"] as? NSNumber)?.intValue else { return nil }
        func shorts(_ key: String) -> [UInt16] {
            guard let d = dict[key] as? Data else { return [] }
            return d.withUnsafeBytes { Array($0.bindMemory(to: UInt16.self)) }
        }
        let hist = (dict["histogram"] as? [NSNumber])?.map(\.uint32Value) ?? []
        return GradeScopes(columns: cols, histogram: hist, waveform: shorts("waveform"), parade: shorts("parade"), vectorscope: shorts("vectorscope"))
    }
}

/// Um controle deslizante de parâmetro da Correção de cor (um passo de desfazer por arrasto).
private struct GradeSliderRow: View {
    @EnvironmentObject private var model: AureaModel
    let effect: UInt32
    let index: UInt32
    let label: String
    let range: ClosedRange<Float>
    var component: Int = 0
    @State private var editing = false
    var body: some View {
        let value = model.gradeParam(index)?.value[safe: component] ?? 0
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.text).lineLimit(1)
                Spacer()
                Text(range.upperBound - range.lowerBound > 20 ? "\(Int(value.rounded()))" : String(format: "%.2f", value))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
            Slider(value: Binding(get: { Double(min(max(value, range.lowerBound), range.upperBound)) },
                                  set: { model.writeGrade(effect, index: index, component: component, value: Float($0)) }),
                   in: Double(range.lowerBound)...Double(range.upperBound),
                   onEditingChanged: { active in
                       if active && !editing { editing = true; model.beginGesture(label) }
                       if !active && editing { editing = false; model.endGesture() }
                   })
                .frame(minHeight: 44).accessibilityLabel(label).accessibilityIdentifier("grade.slider.\(index)")
        }.padding(.horizontal, 4)
    }
}

private extension Array {
    subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil }
}

struct EditFiltersView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var effect: UInt32?
    @State private var thumbs: [UIImage] = []
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let effect {
                let look = Int((model.gradeParam(GradeParam.look)?.scalar ?? -1).rounded())
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(-1..<gradeLookKeys.count, id: \.self) { value in
                            let label = AureaText.t(value < 0 ? "grade_none" : gradeLookKeys[value])
                            Button { model.applyColorLook(Int32(value), effect: effect) } label: {
                                VStack(spacing: 4) {
                                    ZStack {
                                        RoundedRectangle(cornerRadius: 10).fill(AureaColors.chip)
                                        if let image = thumbs[safe: value + 1] { Image(uiImage: image).resizable().scaledToFill() }
                                    }
                                    .frame(width: 70, height: 70).clipShape(RoundedRectangle(cornerRadius: 10))
                                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(value == look ? AureaColors.accent : .clear, lineWidth: 2))
                                    Text(label).font(.aurea(size: 11)).lineLimit(1)
                                        .foregroundStyle(value == look ? AureaColors.accent : AureaColors.text)
                                }.frame(width: 74)
                            }.buttonStyle(.plain).accessibilityLabel(label).accessibilityIdentifier("grade.look.\(value)")
                        }
                    }.padding(.horizontal, 10)
                }
                if look >= 0 {
                    GradeSliderRow(effect: effect, index: GradeParam.mix, label: AureaText.t("grade_intensity"), range: 0...100)
                        .padding(.horizontal, 10)
                }
                if model.selectedLayer?.adjustment == true {
                    Text(AureaText.t("grade_global_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.horizontal, 14)
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 8)
        .onAppear { effect = model.colorGrade(role: 1); loadThumbs() }
        .onChange(of: model.primarySelection) { _ in effect = model.colorGrade(role: 1); loadThumbs() }
        // Só com o play PARADO (0.3.0): cada captura espera os quadros exatos
        // com a GPU presa e a prévia congelava/ficava preta tocando.
        .onChange(of: model.status.playhead / 15) { _ in if model.status.playing == 0 { loadThumbs() } }
        .onChange(of: model.status.playing) { playing in if playing == 0 { loadThumbs() } }
    }
    private func loadThumbs() {
        // O motor fora da thread principal (o modelo é @MainActor); as imagens
        // são montadas de volta nela.
        let engine = model.engine
        DispatchQueue.global(qos: .userInitiated).async {
            var dict: [String: Any]? = nil
            for attempt in 0..<6 {
                dict = engine.colorLookThumbnails(144)
                if dict != nil { break }
                Thread.sleep(forTimeInterval: 0.16 * Double(attempt + 1))
            }
            let result = dict
            DispatchQueue.main.async { if let images = AureaModel.lookImages(from: result) { thumbs = images } }
        }
    }
}

struct EditAdjustView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var effect: UInt32?
    @State private var tab = 0
    @State private var tool = 0
    @State private var band = 0
    @State private var hueCurves = false
    @State private var scopes = false
    @State private var scopeKind = 0
    @State private var scopeData: GradeScopes?
    @State private var scopeJob: DispatchWorkItem?
    @State private var scopeBusy = false
    @State private var scopeDirty = false
    @State private var scopeVisible = false
    private var nodes: [EffectItem] { model.effects.filter { $0.typeId == fxEffectTypeId("aurea.color.grade") } }
    private let tools: [(UInt32, String, ClosedRange<Float>, Float)] = [
        (GradeParam.brightness, "grade_brightness", -100...100, 0), (GradeParam.contrast, "grade_contrast", -100...100, 0),
        (GradeParam.saturation, "grade_saturation", 0...200, 100), (GradeParam.exposure, "grade_exposure", -5...5, 0),
        (GradeParam.temperature, "grade_temperature", -100...100, 0), (GradeParam.tint, "grade_tint", -100...100, 0),
        (GradeParam.highlights, "grade_highlights", -100...100, 0), (GradeParam.shadows, "grade_shadows", -100...100, 0),
        (GradeParam.whites, "grade_whites", -100...100, 0), (GradeParam.blacks, "grade_blacks", -100...100, 0),
        (GradeParam.vibrance, "grade_vibrance", -100...100, 0), (GradeParam.sharpen, "grade_sharpen", 0...100, 0),
        (GradeParam.vignette, "grade_vignette", -100...100, 0), (GradeParam.grain, "grade_grain", 0...100, 0),
        (GradeParam.fade, "grade_fade", 0...100, 0),
    ]
    private let bands: [(String, Color)] = [
        ("grade_band_red", Color(red: 0.90, green: 0.22, blue: 0.21)), ("grade_band_orange", Color(red: 0.98, green: 0.55, blue: 0)),
        ("grade_band_yellow", Color(red: 0.99, green: 0.85, blue: 0.21)), ("grade_band_green", Color(red: 0.26, green: 0.63, blue: 0.28)),
        ("grade_band_aqua", Color(red: 0, green: 0.67, blue: 0.76)), ("grade_band_blue", Color(red: 0.12, green: 0.53, blue: 0.90)),
        ("grade_band_purple", Color(red: 0.56, green: 0.14, blue: 0.67)), ("grade_band_magenta", Color(red: 0.85, green: 0.11, blue: 0.38)),
    ]
    private let wheels: [(UInt32, UInt32?, String)] = [
        (GradeParam.lift, GradeParam.liftLuma, "grade_lift"), (GradeParam.gamma, GradeParam.gammaLuma, "grade_gamma"),
        (GradeParam.gain, GradeParam.gainLuma, "grade_gain"), (GradeParam.offset, GradeParam.offsetLuma, "grade_offset"),
        (GradeParam.shadowTint, nil, "grade_shadow_tint"), (GradeParam.highlightTint, nil, "grade_highlight_tint"),
    ]
    var body: some View {
        VStack(spacing: 6) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 4) {
                    ForEach(Array(nodes.enumerated()), id: \.element.effectId) { index, node in
                        if index > 0 { Text("→").foregroundStyle(AureaColors.muted) }
                        Button { selectNode(node.effectId) } label: {
                            Text(AureaText.t("grade_node_number", index + 1)).font(.aurea(size: 12, weight: .medium))
                                .padding(.horizontal, 12).frame(height: 40)
                                .background(node.effectId == effect ? AureaColors.accent.opacity(0.2) : AureaColors.chip,
                                            in: RoundedRectangle(cornerRadius: 6))
                        }.buttonStyle(.plain).accessibilityIdentifier("grade.node.\(node.effectId)")
                            .accessibilityAddTraits(node.effectId == effect ? .isSelected : [])
                    }
                    Button(AureaText.t("grade_add_node")) {
                        if let node = model.colorGrade(role: 3) { selectNode(node) }
                    }
                        .frame(minHeight: 44).accessibilityIdentifier("grade.node.add")
                }
            }.accessibilityIdentifier("grade.nodes")
            HStack {
                Button(AureaText.t("grade_scopes")) { scopes.toggle(); loadScopes() }
                    .font(.aurea(size: 12)).padding(.horizontal, 10).frame(minHeight: 36)
                    .background(scopes ? AureaColors.accent.opacity(0.2) : AureaColors.chip).clipShape(Capsule())
                    .accessibilityIdentifier("grade.scopes")
                Spacer()
                if let effect, let node = nodes.first(where: { $0.effectId == effect }), let layer = model.primarySelection {
                    Toggle(AureaText.t("grade_node_enabled"), isOn: Binding(get: { node.enabled }, set: { enabled in
                        model.mutate { $0.setEffect(effect, forLayer: layer, enabled: enabled) }
                        model.refreshSelectedLayer()
                    })).labelsHidden().accessibilityLabel(AureaText.t("grade_node_enabled"))
                        .accessibilityIdentifier("grade.node.enabled")
                    Menu {
                        Button(AureaText.t("grade_node_previous")) { moveNode(effect, direction: -1) }
                        Button(AureaText.t("grade_node_next")) { moveNode(effect, direction: 1) }
                        Button(AureaText.t("common_delete")) { model.removeEffectWithUndo(effect) }
                    } label: { Text("⋯").frame(width: 44, height: 44) }
                        .accessibilityLabel(AureaText.t("grade_node_actions")).accessibilityIdentifier("grade.node.menu")
                }
                if let effect {
                    Button(AureaText.t("grade_reset")) { model.resetColorGrade(effect: effect) }
                        .font(.aurea(size: 13)).frame(minHeight: 44).accessibilityIdentifier("grade.reset")
                }
            }.padding(.horizontal, 10)
            if scopes { scopesView }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(["grade_tab_basic", "grade_tab_hsl", "grade_tab_curves", "grade_tab_wheels", "grade_tab_more", "grade_window"].enumerated()), id: \.offset) { index, label in
                        Button { tab = index } label: {
                            Text(AureaText.t(label)).font(.aurea(size: 12, weight: .medium))
                                .padding(.horizontal, 12).frame(minHeight: 40)
                                .background(tab == index ? AureaColors.accent.opacity(0.18) : AureaColors.chip,
                                            in: RoundedRectangle(cornerRadius: 6))
                        }.buttonStyle(.plain).accessibilityAddTraits(tab == index ? .isSelected : [])
                    }
                }.padding(.horizontal, 10)
            }
            if let effect, let layer = model.primarySelection {
                ScrollView {
                    switch tab {
                    case 0: basicTab(effect)
                    case 1: hslTab(effect)
                    case 2: curvesTab(effect, layer: layer)
                    case 3: wheelsTab(effect)
                    case 5: windowTab(effect)
                    default: moreTab(effect, layer: layer)
                    }
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 6)
        .onAppear { scopeVisible = true; effect = model.colorGrade(role: 2) ?? nodes.last?.effectId }
        .onChange(of: model.primarySelection) { _ in effect = model.colorGrade(role: 2) ?? nodes.last?.effectId }
        .onChange(of: model.effects) { _ in synchronizeNode() }
        .onChange(of: model.status.modelRevision) { _ in if scopes && model.status.playing == 0 { loadScopes() } }
        .onChange(of: model.status.playhead) { _ in if model.status.playing == 0 { loadScopes() } }
        .onChange(of: model.status.playing) { _ in if model.status.playing == 0 { loadScopes() } }
        .onDisappear { scopeVisible = false; scopeJob?.cancel(); scopeJob = nil }

    }

    private func selectNode(_ id: UInt32) {
        effect = id
        if let layer = model.primarySelection { model.loadParams(layerId: layer, effectId: id) }
    }

    private func synchronizeNode() {
        if let effect, nodes.contains(where: { $0.effectId == effect }) { return }
        if let node = nodes.last { selectNode(node.effectId) }
        else { effect = model.colorGrade(role: 2) }
    }

    private func moveNode(_ id: UInt32, direction: Int) {
        guard let layer = model.primarySelection, let index = nodes.firstIndex(where: { $0.effectId == id }),
              nodes.indices.contains(index + direction) else { return }
        let target = nodes[index + direction].effectId
        let all = model.engine.effects(forLayer: layer).compactMap { ($0["effectId"] as? NSNumber)?.uint32Value }
        guard let destination = all.firstIndex(of: target) else { return }
        model.mutate { $0.moveEffect(id, inLayer: layer, to: UInt32(destination)) }
        model.refreshSelectedLayer()
    }

    private func windowTab(_ effect: UInt32) -> some View {
        let shape = Int(model.gradeParam(79)?.value.first ?? 0)
        return VStack(spacing: 8) {
            Picker(AureaText.t("grade_window"), selection: Binding(get: { shape }, set: {
                model.writeGrade(effect, index: 79, value: Float($0))
            })) {
                Text(AureaText.t("grade_none")).tag(0)
                Text(AureaText.t("grade_window_rectangle")).tag(1)
                Text(AureaText.t("grade_window_ellipse")).tag(2)
            }.pickerStyle(.segmented).accessibilityIdentifier("grade.window")
            if shape > 0 {
                GradeSliderRow(effect: effect, index: 80, label: "X", range: 0...1, component: 0)
                GradeSliderRow(effect: effect, index: 80, label: "Y", range: 0...1, component: 1)
                GradeSliderRow(effect: effect, index: 81, label: AureaText.t("grade_window_width"), range: 0.01...2, component: 0)
                GradeSliderRow(effect: effect, index: 81, label: AureaText.t("grade_window_height"), range: 0.01...2, component: 1)
                GradeSliderRow(effect: effect, index: 82, label: AureaText.t("grade_window_rotation"), range: -180...180)
                GradeSliderRow(effect: effect, index: 83, label: AureaText.t("grade_q_softness"), range: 0...100)
                ForEach([84, 85], id: \.self) { index in
                    Toggle(AureaText.t(index == 84 ? "grade_q_invert" : "grade_q_show"), isOn: Binding(
                        get: { (model.gradeParam(UInt32(index))?.value.first ?? 0) > 0.5 },
                        set: { model.writeGrade(effect, index: UInt32(index), value: $0 ? 1 : 0) }))
                }
            }
        }.padding(.horizontal, 10)
    }
    @ViewBuilder private func basicTab(_ effect: UInt32) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(tools.indices, id: \.self) { i in
                        let t = tools[i]
                        let v = model.gradeParam(t.0)?.scalar ?? t.3
                        let changed = abs(v - t.3) > 0.001
                        Button { tool = i } label: {
                            VStack(spacing: 4) {
                                Text(changed ? "\(Int(v.rounded()))" : "•").font(.aurea(size: 11))
                                    .frame(width: 40, height: 40).background(i == tool ? AureaColors.accent.opacity(0.2) : AureaColors.chip)
                                    .clipShape(Circle()).foregroundStyle(changed ? AureaColors.accent : AureaColors.muted)
                                Text(AureaText.t(t.1)).font(.aurea(size: 11)).lineLimit(1)
                                    .foregroundStyle(i == tool ? AureaColors.accent : AureaColors.text)
                            }.frame(width: 68)
                        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(t.1)).accessibilityIdentifier("grade.tool.\(t.0)")
                    }
                }.padding(.horizontal, 10)
            }
            let t = tools[min(max(tool, 0), tools.count - 1)]
            GradeSliderRow(effect: effect, index: t.0, label: AureaText.t(t.1), range: t.2).padding(.horizontal, 10)
        }
    }
    @ViewBuilder private func hslTab(_ effect: UInt32) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                ForEach(bands.indices, id: \.self) { i in
                    Circle().fill(bands[i].1).frame(width: 32, height: 32)
                        .overlay(Circle().stroke(AureaColors.text, lineWidth: i == band ? 3 : 0))
                        .onTapGesture { band = i }.accessibilityLabel(AureaText.t(bands[i].0)).accessibilityIdentifier("grade.band.\(i)")
                    if i < bands.count - 1 { Spacer(minLength: 0) }
                }
            }.padding(.horizontal, 12)
            Text(AureaText.t(bands[band].0)).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).padding(.horizontal, 14)
            let base = GradeParam.hsl + UInt32(band * 3)
            GradeSliderRow(effect: effect, index: base, label: AureaText.t("grade_hue"), range: -100...100)
            GradeSliderRow(effect: effect, index: base + 1, label: AureaText.t("grade_saturation"), range: -100...100)
            GradeSliderRow(effect: effect, index: base + 2, label: AureaText.t("grade_luminance"), range: -100...100)
        }.padding(.horizontal, 10)
    }
    @ViewBuilder private func curvesTab(_ effect: UInt32, layer: Int64) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Picker("", selection: $hueCurves) { Text("RGB").tag(false); Text(AureaText.t("grade_hue")).tag(true) }
                .pickerStyle(.segmented).accessibilityIdentifier("grade.curves.mode")
            if hueCurves {
                ColorCurveEditor(layer: layer, effect: effect, param: GradeParam.hueCurves,
                                 channelNames: [AureaText.t("grade_hue_vs_hue"), AureaText.t("grade_hue_vs_sat"),
                                                AureaText.t("grade_hue_vs_luma"), AureaText.t("grade_luma_vs_sat")])
                    .id("hue-\(effect)")
            } else {
                ColorCurveEditor(layer: layer, effect: effect, param: GradeParam.curves).id("rgb-\(effect)")
            }
        }.padding(.horizontal, 10)
    }
    @ViewBuilder private func wheelsTab(_ effect: UInt32) -> some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 140), spacing: 8)], spacing: 14) {
            ForEach(wheels.indices, id: \.self) { index in
                let wheel = wheels[index]
                let value = model.gradeParam(wheel.0)?.value ?? [0, 0]
                VStack(spacing: 6) {
                    Text(AureaText.t(wheel.2)).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    GradeWheelView(x: value[safe: 0] ?? 0, y: value[safe: 1] ?? 0, label: AureaText.t(wheel.2),
                                   onBegin: { model.beginGesture(AureaText.t(wheel.2)) },
                                   onChange: { x, y in model.writeGradePoint(effect, index: wheel.0, x: x, y: y) },
                                   onEnd: { model.endGesture() })
                        .frame(width: 140, height: 140).accessibilityIdentifier("grade.wheel.\(wheel.0)")
                    if let luma = wheel.1 {
                        GradeSliderRow(effect: effect, index: luma, label: AureaText.t("grade_luminance"), range: -100...100)
                    }
                    if wheel.0 == GradeParam.shadowTint || wheel.0 == GradeParam.highlightTint {
                        GradeSliderRow(effect: effect, index: GradeParam.tintBalance, label: AureaText.t("grade_tint_balance"), range: -100...100)
                    }
                }
            }
        }.padding(8)
    }
    @ViewBuilder private func moreTab(_ effect: UInt32, layer: Int64) -> some View {
        let profiles = [AureaText.t("grade_log_none"), "S-Log3 (Sony)", "LogC3 (ARRI)", "V-Log (Panasonic)", "Apple Log", "Cineon"]
        let profile = Int((model.gradeParam(GradeParam.log)?.scalar ?? 0).rounded())
        let qualifier = (model.gradeParam(GradeParam.qualifier)?.scalar ?? 0) >= 0.5
        VStack(alignment: .leading, spacing: 8) {
            Text(AureaText.t("grade_log_profile")).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(profiles.indices, id: \.self) { i in
                        Button(profiles[i]) { model.writeGrade(effect, index: GradeParam.log, value: Float(i)) }
                            .font(.aurea(size: 12)).padding(.horizontal, 10).frame(minHeight: 36)
                            .background(i == profile ? AureaColors.accent.opacity(0.2) : AureaColors.chip).clipShape(Capsule())
                            .accessibilityIdentifier("grade.log.\(i)")
                    }
                }
            }
            Text(AureaText.t("grade_lut")).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
            CubeLutImportRow(layer: layer, effect: effect)
            GradeSliderRow(effect: effect, index: GradeParam.lutIntensity, label: AureaText.t("grade_lut_intensity"), range: 0...100)
            Toggle(AureaText.t("grade_secondary"), isOn: Binding(get: { qualifier },
                set: { model.writeGrade(effect, index: GradeParam.qualifier, value: $0 ? 1 : 0) }))
                .font(.aurea(size: 13)).accessibilityIdentifier("grade.secondary")
            if qualifier {
                GradeSliderRow(effect: effect, index: GradeParam.qHue, label: AureaText.t("grade_q_hue"), range: 0...360)
                GradeSliderRow(effect: effect, index: GradeParam.qWidth, label: AureaText.t("grade_q_width"), range: 1...180)
                GradeSliderRow(effect: effect, index: GradeParam.qSoft, label: AureaText.t("grade_q_softness"), range: 0...100)
                ForEach([(GradeParam.qShow, "grade_q_show"), (GradeParam.qInvert, "grade_q_invert")], id: \.0) { item in
                    Toggle(AureaText.t(item.1), isOn: Binding(get: { (model.gradeParam(item.0)?.scalar ?? 0) >= 0.5 },
                        set: { model.writeGrade(effect, index: item.0, value: $0 ? 1 : 0) }))
                        .font(.aurea(size: 13)).accessibilityIdentifier("grade.switch.\(item.0)")
                }
                GradeSliderRow(effect: effect, index: GradeParam.qHueShift, label: AureaText.t("grade_q_hue_shift"), range: -180...180)
                GradeSliderRow(effect: effect, index: GradeParam.qSat, label: AureaText.t("grade_saturation"), range: -100...100)
                GradeSliderRow(effect: effect, index: GradeParam.qExposure, label: AureaText.t("grade_exposure"), range: -3...3)
            }
            GradeSliderRow(effect: effect, index: GradeParam.pivot, label: AureaText.t("grade_pivot"), range: 0...100)
        }.padding(.horizontal, 10)
    }
    private func loadScopes() {
        guard scopes, scopeVisible, model.status.playing == 0 else { return }
        scopeDirty = true
        scopeJob?.cancel()
        let job = DispatchWorkItem {
            guard scopes, scopeVisible, model.status.playing == 0, !scopeBusy else { return }
            scopeJob = nil
            scopeBusy = true
            scopeDirty = false
            let engine = model.engine, project = model.projectURL, layer = model.primarySelection
            DispatchQueue.global(qos: .userInitiated).async {
                let dict = engine.colorScopes(192)
                DispatchQueue.main.async {
                    scopeBusy = false
                    if scopeVisible && scopes && model.projectURL == project && model.primarySelection == layer {
                        scopeData = AureaModel.gradeScopes(from: dict)
                    }
                    if scopeDirty { loadScopes() }
                }
            }
        }
        scopeJob = job
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: job)
    }
    @ViewBuilder private var scopesView: some View {
        VStack(spacing: 4) {
            Picker("", selection: $scopeKind) {
                Text(AureaText.t("grade_scope_histogram")).tag(0); Text(AureaText.t("grade_scope_waveform")).tag(1)
                Text(AureaText.t("grade_scope_parade")).tag(2); Text(AureaText.t("grade_scope_vectorscope")).tag(3)
            }.pickerStyle(.segmented)
            Canvas { context, size in
                guard let s = scopeData else { return }
                switch scopeKind {
                case 0:
                    let peak = CGFloat(max(1, s.histogram.max() ?? 1))
                    let colors: [Color] = [.red, .green, .blue, .white]
                    for c in 0..<4 where s.histogram.count >= (c + 1) * 256 {
                        var path = Path()
                        for i in 0..<256 {
                            let p = CGPoint(x: CGFloat(i) / 255 * size.width, y: size.height - CGFloat(s.histogram[c * 256 + i]) / peak * size.height)
                            if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
                        }
                        context.stroke(path, with: .color(colors[c].opacity(c == 3 ? 0.55 : 0.8)), lineWidth: 1.2)
                    }
                case 1, 2:
                    let parade = scopeKind == 2
                    let groups = parade ? 3 : 1
                    let cols = max(1, s.columns)
                    let source = parade ? s.parade : s.waveform
                    let peak = Double(max(1, source.max() ?? 1))
                    let cw = size.width / CGFloat(cols * groups), ch = size.height / 128
                    let tint: [Color] = [.red, .green, .blue]
                    for g in 0..<groups { for x in 0..<cols { for yb in 0..<128 {
                        let idx = parade ? (g * cols + x) * 128 + yb : x * 128 + yb
                        guard idx < source.count, source[idx] > 0 else { continue }
                        let a = min(1, 0.15 + 0.85 * (Double(source[idx]) / peak).squareRoot())
                        context.fill(Path(CGRect(x: CGFloat(g * cols + x) * cw, y: size.height - CGFloat(yb + 1) * ch, width: cw + 0.5, height: ch + 0.5)),
                                     with: .color((parade ? tint[g] : Color(red: 0.62, green: 1, blue: 0.66)).opacity(a)))
                    } } }
                default:
                    let side = min(size.width, size.height), ox = (size.width - side) / 2, cell = side / 128
                    let peak = Double(max(1, s.vectorscope.max() ?? 1))
                    context.stroke(Path(ellipseIn: CGRect(x: ox, y: 0, width: side, height: side)), with: .color(.white.opacity(0.2)))
                    for vy in 0..<128 { for vx in 0..<128 {
                        let v = s.vectorscope[safe: vy * 128 + vx] ?? 0
                        guard v > 0 else { continue }
                        let a = min(1, 0.2 + 0.8 * (Double(v) / peak).squareRoot())
                        context.fill(Path(CGRect(x: ox + CGFloat(vx) * cell, y: CGFloat(vy) * cell, width: cell + 0.5, height: cell + 0.5)),
                                     with: .color(Color(red: 0.62, green: 1, blue: 0.66).opacity(a)))
                    } }
                }
            }
            .frame(height: 118).background(Color(white: 0.06)).accessibilityIdentifier("grade.scope.view")
        }.padding(.horizontal, 10)
    }
}

/// Roda de cor: o ponto no disco é (cos, sen) da matiz × força, como o `wheel()`
/// do color_grade.frag (matiz 0 = vermelho, sentido anti-horário com y para cima).
private struct GradeWheelView: View {
    let x: Float
    let y: Float
    let label: String
    let onBegin: () -> Void
    let onChange: (Float, Float) -> Void
    let onEnd: () -> Void
    @State private var dragging = false
    var body: some View {
        GeometryReader { geo in
            let r = min(geo.size.width, geo.size.height) / 2
            let center = CGPoint(x: geo.size.width / 2, y: geo.size.height / 2)
            ZStack {
                // Cores no sentido horário a partir das 3 h: matiz = 1 − t.
                Circle().fill(AngularGradient(gradient: Gradient(colors: (0...12).map { Color(hue: Double((12 - $0) % 12) / 12, saturation: 1, brightness: 1) }),
                                              center: .center, startAngle: .degrees(0), endAngle: .degrees(360)))
                Circle().fill(RadialGradient(colors: [Color(white: 0.55), Color(white: 0.55).opacity(0)], center: .center, startRadius: 0, endRadius: r))
                Circle().stroke(Color.white.opacity(0.35), lineWidth: 1)
                Circle().stroke(Color.white, lineWidth: 2.5).frame(width: 18, height: 18)
                    .background(Circle().fill(Color.black.opacity(0.45)).frame(width: 22, height: 22))
                    .position(x: center.x + CGFloat(min(max(x, -1), 1)) * r, y: center.y - CGFloat(min(max(y, -1), 1)) * r)
            }
            .contentShape(Circle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                if !dragging { dragging = true; onBegin() }
                var px = Float((value.location.x - center.x) / r), py = Float(-(value.location.y - center.y) / r)
                let d = (px * px + py * py).squareRoot()
                if d > 1 { px /= d; py /= d }
                onChange(px, py)
            }.onEnded { _ in if dragging { dragging = false; onEnd() } })
            .simultaneousGesture(TapGesture(count: 2).onEnded { onBegin(); onChange(0, 0); onEnd() })
        }
        .accessibilityLabel(label)
    }
}
