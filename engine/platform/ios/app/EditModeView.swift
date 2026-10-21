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
enum EditDockLevel { case root, audio, text, videoClip, textClip, audioClip, otherClip }

private struct EditDockItem: Identifiable {
    let id: String
    let glyph: Character
    let label: String
    let action: () -> Void
}

/// Cores próprias da Edição: fundo preto como nos editores de vídeo, Dock um tom acima.
private enum EditColors {
    static let canvas = Color.black
    static let preview = Color(red: 0.043, green: 0.043, blue: 0.051)
    static let bar = Color(red: 0.071, green: 0.071, blue: 0.078)
    static let dockLevel = Color(red: 0.106, green: 0.106, blue: 0.122)
    static let ruler = Color(red: 0.54, green: 0.54, blue: 0.57)
}

struct EditModeView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let height: CGFloat
    @State private var level: EditDockLevel = .root
    @State private var replaceTarget: LayerItem?
    /// O corte cujas transições estão abertas (saída, entrada).
    @State private var cut: EditCut?

    var body: some View {
        let previewH = model.fullscreen ? max(0, height - 52) : min(max(height * 0.40, 160), 560)
        VStack(spacing: 0) {
            if !model.fullscreen { topBar }
            PreviewStage(height: previewH).background(EditColors.preview)
                .environment(\.layoutDirection, .leftToRight)
            playBar
            if !model.fullscreen {
                if model.panel != .none && model.panel != .dock {
                    // Painel aberto: a linha do tempo encolhe e o painel da Composição sobe.
                    EditTimelineView(compact: true, onAddMedia: addMedia, onAddAudio: { level = .audio }, onTransition: { cut = EditCut(outgoing: $0, incoming: $1) }).frame(height: 112)
                        .environment(\.layoutDirection, .leftToRight)
                    ContextSheet(metrics: EditorMetrics(topBar: 0, preview: 0, strip: 0, transport: 0, timeline: 0, sheet: 0))
                        .frame(maxHeight: .infinity)
                } else {
                    EditTimelineView(compact: false, onAddMedia: addMedia, onAddAudio: { level = .audio }, onTransition: { cut = EditCut(outgoing: $0, incoming: $1) }).frame(maxHeight: .infinity)
                        .environment(\.layoutDirection, .leftToRight)
                    dock
                }
            }
        }
        .background(EditColors.canvas.ignoresSafeArea())
        .onChange(of: model.primarySelection) { _ in syncLevel() }
        .onAppear(perform: syncLevel)
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

    // --- Topo: ✕ · resolução · ⋯ · Exportar ------------------------------------
    private var topBar: some View {
        HStack(spacing: 0) {
            Button { model.closeProject() } label: {
                CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 22).frame(width: 44, height: 44).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("common_close"))
                .accessibilityIdentifier("edit.close")
            Spacer()
            let short = min(model.compositionWidth, model.compositionHeight)
            Button { model.showProjectSettings = true } label: {
                Text(short > 0 ? "\(short)P" : "—").font(.aurea(size: 13, weight: .bold))
                    .padding(.horizontal, 10).padding(.vertical, 6)
                    .background(EditColors.dockLevel, in: RoundedRectangle(cornerRadius: 8))
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("edit.resolution")
            Menu {
                Button(AureaText.t("ui_mode_switch_to_composition")) { model.setUiMode(AureaUiMode.composition) }
                Button(AureaText.t(model.editMode ? "edit_magnet_on" : "edit_magnet_off")) { model.toggleEditMode() }
                Button(AureaText.t("editor_plano_fundo")) { model.showProjectSettings = true }
            } label: {
                CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 20).frame(width: 44, height: 44).contentShape(Rectangle())
            }.accessibilityLabel(AureaText.t("edit_more")).accessibilityIdentifier("edit.more")
            Button { if model.status.playing != 0 { model.pause() }; model.showExport = true } label: {
                Text(AureaText.t("edit_export")).font(.aurea(size: 14, weight: .bold)).foregroundStyle(AureaColors.background)
                    .padding(.horizontal, 14).padding(.vertical, 8)
                    .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 8))
            }.buttonStyle(AureaPressStyle(shrink: 0.96)).accessibilityIdentifier("edit.export").padding(.leading, 4)
        }
        .padding(.horizontal, 6).frame(height: 52).foregroundStyle(AureaColors.text)
    }

    // --- Barra de reprodução: tempo · ▶ · desfazer · refazer · tela cheia ------
    private var playBar: some View {
        let fps = max(model.compositionFps, 1)
        return ZStack {
            HStack {
                EditClockText(fps: fps, total: model.compositionDuration)
                Spacer()
                barIcon(CupertinoGlyph.ArrowUturnLeft, "common_undo", enabled: model.status.canUndo != 0, id: "edit.undo") { model.undo() }
                barIcon(CupertinoGlyph.ArrowUturnRight, "common_redo", enabled: model.status.canRedo != 0, id: "edit.redo") { model.redo() }
                barIcon(model.fullscreen ? CupertinoGlyph.FullscreenExit : CupertinoGlyph.Fullscreen, "editor_tela_cheia", enabled: true, id: "edit.fullscreen") {
                    model.fullscreen.toggle()
                }
            }
            Button { model.playPause() } label: {
                CupertinoGlyph.text(model.status.playing != 0 ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill, size: 24)
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 0.9))
                .accessibilityLabel(AureaText.t(model.status.playing != 0 ? "editor_pausar" : "editor_reproduzir"))
                .accessibilityIdentifier("edit.play")
        }
        .padding(.horizontal, 12).frame(height: 48).foregroundStyle(AureaColors.text)
    }

    private func barIcon(_ glyph: Character, _ key: String, enabled: Bool, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 20, color: enabled ? AureaColors.text : AureaColors.muted.opacity(0.4))
                .frame(width: 40, height: 40).contentShape(Rectangle())
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
                    CupertinoGlyph.text(CupertinoGlyph.ChevronBack, size: 18)
                        .frame(width: 32, height: 58).background(EditColors.bar, in: RoundedRectangle(cornerRadius: 8))
                }.buttonStyle(AureaPressStyle(shrink: 0.92)).padding(.leading, 10)
                    .accessibilityLabel(AureaText.t("edit_back")).accessibilityIdentifier("edit.dock.back")
            }
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 0) {
                    ForEach(items(for: level)) { item in
                        Button(action: item.action) {
                            VStack(spacing: 6) {
                                CupertinoGlyph.text(item.glyph, size: 24)
                                Text(item.label).font(.aurea(size: 11)).lineLimit(1)
                            }.frame(minWidth: 64, minHeight: 64).padding(.horizontal, 6).contentShape(Rectangle())
                        }.buttonStyle(AureaPressStyle(shrink: 0.92))
                            .accessibilityElement(children: .ignore)
                            .accessibilityLabel(item.label).accessibilityAddTraits(.isButton)
                            .accessibilityIdentifier("edit.dock." + item.id)
                    }
                }.padding(.horizontal, 6)
            }
        }
        .frame(height: 76).frame(maxWidth: .infinity)
        .background(level == .root ? EditColors.bar : EditColors.dockLevel)
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

    private func item(_ id: String, _ glyph: Character, _ key: String, _ action: @escaping () -> Void) -> EditDockItem {
        EditDockItem(id: id, glyph: glyph, label: AureaText.t(key), action: action)
    }

    private func items(for level: EditDockLevel) -> [EditDockItem] {
        let ids = model.layers.filter { model.selection.contains($0.id) }.map(\.id)
        let split = item("split", CupertinoGlyph.Scissors, "edit_clip_split") { model.splitAtPlayhead(ids) }
        let duplicate = item("duplicate", CupertinoGlyph.PlusSquareOnSquare, "edit_clip_duplicate") {
            model.engine.duplicateLayers(ids.map { NSNumber(value: $0) }); model.refreshModel(force: true)
        }
        let delete = item("delete", CupertinoGlyph.Trash, "edit_clip_delete") { model.deleteSelectedLayers() }
        let effects = item("effects", CupertinoGlyph.Sparkles, "edit_clip_effects") { panel(.effects) }
        let transform = item("transform", CupertinoGlyph.Move, "edit_clip_transform") { panel(.transform) }
        let opacity = item("opacity", CupertinoGlyph.CircleLefthalfFill, "edit_clip_opacity") { panel(.appearance) }
        let mask = item("mask", ShellGlyph.SquareSplit2x2, "edit_clip_mask") { panel(.mask) }
        let animations = item("animations", CupertinoGlyph.WandStars, "edit_clip_animations") { panel(.presets) }
        let filters = item("filters", CupertinoGlyph.ColorFilter, "edit_dock_filters") { if model.status.playing != 0 { model.pause() }; panel(.filters) }
        let adjust = item("adjust", CupertinoGlyph.SliderHorizontal3, "edit_dock_adjust") { if model.status.playing != 0 { model.pause() }; panel(.adjust) }
        switch level {
        case .root:
            return [
                item("edit", CupertinoGlyph.Scissors, "edit_dock_edit") {
                    if let hit = clipUnderPlayhead() { model.select(layerId: hit.id) } else { model.toast = AureaText.t("edit_no_clip_here") }
                },
                item("audio", CupertinoGlyph.MusicNote, "edit_dock_audio") { self.level = .audio },
                item("text", CupertinoGlyph.Textformat, "edit_dock_text") { self.level = .text },
                item("stickers", ShellGlyph.SquareOnCircle, "edit_dock_stickers") { shell.addCategory = 0; model.openAddLayer() },
                item("overlay", CupertinoGlyph.SquareOnSquare, "edit_dock_overlay") {
                    if model.status.playing != 0 { model.pause() }
                    model.pendingOverlayImport = true
                    shell.addPicker = .gallery
                },
                // 0.3.0: sequência de imagens (render do Blender com alfa) numa linha própria, no cabeçote.
                item("sequence", CupertinoGlyph.SquareStack3dUp, "seq_dock") {
                    if model.status.playing != 0 { model.pause() }
                    model.pendingOverlayImport = true
                    shell.sequenceChooser = true
                },
                item("effects", CupertinoGlyph.Sparkles, "edit_dock_effects") { globalEffect() },
                item("captions", CupertinoGlyph.CaptionsBubble, "edit_dock_captions") { panel(.captions) },
                item("filters", CupertinoGlyph.ColorFilter, "edit_dock_filters") { globalColor(.filters) },
                item("adjust", CupertinoGlyph.SliderHorizontal3, "edit_dock_adjust") { globalColor(.adjust) },
                item("ratio", CupertinoGlyph.Crop, "edit_dock_ratio") { model.showProjectSettings = true },
                item("background", CupertinoGlyph.Paintbrush, "edit_dock_background") { model.showProjectSettings = true },
            ]
        case .audio:
            return [
                item("music", CupertinoGlyph.MusicNote2, "edit_audio_music") { if model.status.playing != 0 { model.pause() }; shell.addPicker = .audioFile },
                item("voice", CupertinoGlyph.Mic, "edit_audio_voice") { if model.status.playing != 0 { model.pause() }; shell.voiceRecorder = true },
                item("extract", CupertinoGlyph.Waveform, "edit_clip_extract_audio") {
                    guard let hit = clipUnderPlayhead() else { model.toast = AureaText.t("edit_no_clip_here"); return }
                    _ = model.engine.extractAudio(fromLayer: hit.id); model.refreshModel(force: true)
                },
            ]
        case .text:
            return [
                item("addText", CupertinoGlyph.Textformat, "edit_text_add") { _ = model.addText() },
                item("autoCaptions", CupertinoGlyph.CaptionsBubble, "edit_text_auto_captions") { panel(.captions) },
                item("templates", CupertinoGlyph.WandStars, "edit_text_templates") { shell.addCategory = ShellAddCategories.textPresets; model.openAddLayer() },
            ]
        case .videoClip:
            guard let clip = model.selectedLayer else { return [] }
            let video = clip.kind == 1
            var list = [split, item("speed", CupertinoGlyph.Speedometer, "edit_clip_speed") { panel(.speed) }, animations, effects, filters, adjust, delete, mask]
            if video { list.append(item("volume", CupertinoGlyph.Speaker2, "edit_clip_volume") { panel(.audio) }) }
            list += [transform, opacity, duplicate]
            if clip.kind != 12 { list.append(item("replace", CupertinoGlyph.Arrow2Squarepath, "edit_clip_replace") { replaceTarget = clip }) }
            if video {
                list.append(item("freeze", ShellGlyph.Snow, "edit_clip_freeze") {
                    let created = model.engine.freezeFrame(forLayer: clip.id, frame: Int32(clamping: model.status.playhead),
                                                           hold: Int32(max(1, model.compositionFps * 3)))
                    model.refreshModel(force: true)
                    if created >= 0 { model.select(layerId: created) }
                })
                list.append(item("extract", CupertinoGlyph.Waveform, "edit_clip_extract_audio") {
                    _ = model.engine.extractAudio(fromLayer: clip.id); model.refreshModel(force: true)
                })
                list.append(item("stabilize", ShellGlyph.Viewfinder, "edit_clip_stabilize") { panel(.tracking) })
            }
            if isMain(clip) { list.append(item("toOverlay", CupertinoGlyph.SquareOnSquare, "edit_clip_to_overlay") { model.moveClipToOverlay(clip.id) }) }
            else { list.append(item("toMain", CupertinoGlyph.RectangleStack, "edit_clip_to_main") { model.moveClipToMainTrack(clip.id) }) }
            return list
        case .textClip:
            return [
                item("editText", CupertinoGlyph.Pencil, "edit_clip_edit_text") { panel(.text) },
                animations, effects, transform, split, duplicate, delete,
            ]
        case .audioClip:
            return [
                item("volume", CupertinoGlyph.Speaker2, "edit_clip_volume") { panel(.audio) },
                item("speed", CupertinoGlyph.Speedometer, "edit_clip_speed") { panel(.speed) },
                effects, split, duplicate, delete,
            ]
        case .otherClip:
            return [effects, filters, adjust, transform, opacity, mask, split, duplicate, delete]
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
//  A LINHA DO TEMPO DA EDIÇÃO (espelho de EditTimeline.kt): o cabeçote fica
//  PARADO no meio e o tempo corre por baixo dele. Arrastar na horizontal anda
//  no tempo; pinça aproxima; arrastar na vertical rola as linhas. A TRILHA
//  PRINCIPAL (miniaturas, "+" no fim) vem logo abaixo da régua.
// =============================================================================

private struct EditRow: Identifiable {
    let id: String
    let clips: [LayerItem]
    let main: Bool
    let audio: Bool
}

/// Fileiras: a principal primeiro, depois as linhas de cima para baixo na pilha, e o áudio no fim.
private func editRows(_ layers: [LayerItem], anchor: Int64) -> [EditRow] {
    func key(_ l: LayerItem) -> String { l.trackId != 0 ? "t\(l.trackId)" : "l\(l.id)" }
    let anchorRow = layers.first { $0.id == anchor }
    let mainKey = anchorRow.map(key)
    let grouped = Dictionary(grouping: layers.enumerated(), by: { key($0.element) })
    var main: EditRow?
    var others: [(EditRow, Int)] = []
    for (k, entries) in grouped {
        let clips = entries.map { $0.element }.sorted { $0.startFrame < $1.startFrame }
        // A lista do motor vem do topo da pilha para o fundo: índice menor = mais à frente.
        let front = entries.map { $0.offset }.min() ?? 0
        if k == mainKey { main = EditRow(id: k, clips: clips, main: true, audio: false) }
        else { others.append((EditRow(id: k, clips: clips, main: false, audio: clips.allSatisfy { $0.kind == 3 }), front)) }
    }
    let ordered = others.sorted { a, b in a.0.audio != b.0.audio ? !a.0.audio : a.1 < b.1 }.map { $0.0 }
    return (main.map { [$0] } ?? []) + ordered
}

private struct EditTimelineView: View {
    @EnvironmentObject private var model: AureaModel
    let compact: Bool
    let onAddMedia: () -> Void
    let onAddAudio: () -> Void
    var onTransition: (Int64, Int64) -> Void = { _, _ in }
    var body: some View {
        EditTimelineBody(clock: model.playheadClock, compact: compact, onAddMedia: onAddMedia, onAddAudio: onAddAudio,
                         onTransition: onTransition)
    }
}

private struct EditTimelineBody: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var clock: PlayheadClock
    let compact: Bool
    let onAddMedia: () -> Void
    let onAddAudio: () -> Void
    let onTransition: (Int64, Int64) -> Void
    @State private var pxPerSecond: CGFloat = 72
    @State private var zoomStart: CGFloat?
    @State private var scrubStart: Int64?
    @State private var scrollY: CGFloat = 0
    @State private var scrollStart: CGFloat?
    @State private var thumbs = TimelineThumbStrip()
    /// Onda do som por corte (par do EditWaveCache em EditTimeline.kt).
    @State private var waves = EditWaveCache()

    private let rulerH: CGFloat = 26, mainH: CGFloat = 56, rowH: CGFloat = 34, gap: CGFloat = 6

    var body: some View {
        GeometryReader { geometry in
            let fps = max(model.compositionFps, 1)
            let playhead = clock.frame
            let pxPerFrame = pxPerSecond / CGFloat(fps)
            let centerX = geometry.size.width / 2
            let rows = editRows(model.layers, anchor: model.mainTrackAnchor)
            let hasAudio = rows.contains { $0.audio }
            // Orçamento de miniaturas por quadro (o resto chega nos próximos).
            let _ = thumbs.beginFrame(48)
            ZStack(alignment: .topLeading) {
                Color.clear.contentShape(Rectangle())
                    .onTapGesture { model.clearSelection() }
                ruler(playhead: playhead, fps: fps, centerX: centerX, width: geometry.size.width)
                rowsLayer(rows: rows, hasAudio: hasAudio, playhead: playhead, pxPerFrame: pxPerFrame, centerX: centerX, width: geometry.size.width)
                // O cabeçote fixo no meio.
                Rectangle().fill(Color.white).frame(width: 2).frame(maxHeight: .infinity).offset(x: centerX - 1)
                    .allowsHitTesting(false)
                Circle().fill(Color.white).frame(width: 10, height: 10).offset(x: centerX - 5, y: rulerH - 4)
                    .allowsHitTesting(false)
                magnetToggle.padding(.leading, 6).padding(.top, 2)
            }
            .clipped()
            // Arrastar em qualquer ponto (também em cima de um corte) anda no tempo;
            // os gestos dos cortes (alças, segurar e arrastar) têm prioridade.
            .gesture(scrubGesture(fps: fps, pxPerFrame: pxPerFrame))
            .simultaneousGesture(MagnificationGesture()
                .onChanged { scale in
                    if zoomStart == nil { zoomStart = pxPerSecond }
                    pxPerSecond = min(max((zoomStart ?? pxPerSecond) * scale, 12), 640)
                }
                .onEnded { _ in zoomStart = nil })
        }
        .background(EditColors.canvas)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("edit_timeline"))
        .accessibilityIdentifier("edit.timeline")
    }

    private func scrubGesture(fps: Double, pxPerFrame: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 4)
            .onChanged { value in
                if scrubStart == nil {
                    scrubStart = clock.frame; scrollStart = scrollY
                    if model.status.playing != 0 { model.pause() }
                }
                if abs(value.translation.width) >= abs(value.translation.height) {
                    let frames = Int64((-value.translation.width / max(pxPerFrame, 0.0001)).rounded())
                    let target = min(max(0, (scrubStart ?? 0) + frames), model.compositionDuration)
                    if target != clock.frame { model.seek(toFrame: target) }
                } else {
                    scrollY = max(0, (scrollStart ?? 0) - value.translation.height)
                }
            }
            .onEnded { _ in scrubStart = nil; scrollStart = nil }
    }

    private func ruler(playhead: Int64, fps: Double, centerX: CGFloat, width: CGFloat) -> some View {
        let step = [1, 2, 5, 10, 15, 30, 60, 120, 300].first { CGFloat($0) * pxPerSecond >= 64 } ?? 600
        let now = Double(playhead) / fps
        let first = max(0, Int(now - Double(centerX / pxPerSecond)))
        let last = Int(now + Double(centerX / pxPerSecond)) + 1
        let seconds = Array(stride(from: (first / step) * step, through: last, by: step))
        return ZStack(alignment: .topLeading) {
            ForEach(seconds, id: \.self) { s in
                let x = centerX + CGFloat(Double(s) - now) * pxPerSecond
                Text(String(format: "%02d:%02d", s / 60, s % 60)).font(.aurea(size: 10)).foregroundStyle(EditColors.ruler)
                    .offset(x: x - 14, y: 6)
                Circle().fill(EditColors.ruler).frame(width: 3, height: 3)
                    .offset(x: x + CGFloat(step) * pxPerSecond / 2 - 1.5, y: rulerH * 0.62)
            }
        }
        .frame(width: width, height: rulerH, alignment: .topLeading)
        .allowsHitTesting(false)
    }

    @ViewBuilder
    private func rowsLayer(rows: [EditRow], hasAudio: Bool, playhead: Int64, pxPerFrame: CGFloat, centerX: CGFloat, width: CGFloat) -> some View {
        let top = rulerH + 8 - scrollY
        let layout = rowOffsets(rows: rows, hasAudio: hasAudio, top: top)
        ZStack(alignment: .topLeading) {
            if !rows.contains(where: \.main) {
                HStack {
                    Text(AureaText.t("edit_empty_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    Spacer()
                    addButton
                }.padding(.horizontal, 16).frame(width: width, height: mainH).offset(y: top)
            }
            ForEach(rows.indices, id: \.self) { index in
                let row = rows[index]
                let y = layout.rows[index]
                let h = row.main ? mainH : rowH
                ForEach(row.clips) { clip in
                    let left = centerX + CGFloat(Int64(clip.startFrame) - playhead) * pxPerFrame
                    let w = CGFloat(clip.endFrame - clip.startFrame) * pxPerFrame
                    if left + w > -width && left < width * 2 {
                        EditClipView(clip: clip, main: row.main, x: left, width: w, y: y, height: h,
                                     pxPerFrame: pxPerFrame, thumbs: thumbs, waves: waves, viewport: width)
                    }
                }
                if row.main {
                    // O quadradinho de TRANSIÇÃO em cada corte encostado da trilha principal.
                    ForEach(Array(row.clips.indices.dropLast()), id: \.self) { i in
                        let a = row.clips[i], b = row.clips[i + 1]
                        if a.endFrame == b.startFrame {
                            let cx = centerX + CGFloat(Int64(a.endFrame) - playhead) * pxPerFrame
                            Button { onTransition(a.id, b.id) } label: {
                                CupertinoGlyph.text(CupertinoGlyph.ArrowLeftRight, size: 12, color: .black)
                                    .frame(width: 22, height: 22).background(Color.white, in: RoundedRectangle(cornerRadius: 5))
                            }.buttonStyle(AureaPressStyle(shrink: 0.9))
                                .offset(x: cx - 11, y: y + (h - 22) / 2)
                                .accessibilityLabel(AureaText.t("edit_transition_between", a.name, b.name))
                                .accessibilityIdentifier("edit.cut.\(a.id).\(b.id)")
                        }
                    }
                    let end = row.clips.map(\.endFrame).max() ?? 0
                    let x = min(centerX + CGFloat(Int64(end) - playhead) * pxPerFrame + 10, width - 46)
                    addButton.offset(x: x, y: y + (h - 36) / 2)
                }
            }
            if let lane = layout.audioLane, !compact {
                Button(action: onAddAudio) {
                    HStack(spacing: 6) {
                        CupertinoGlyph.text(CupertinoGlyph.Plus, size: 14, color: AureaColors.muted)
                        Text(AureaText.t("edit_add_audio_lane")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    }.padding(.horizontal, 10).frame(height: rowH)
                        .background(Color(red: 0.086, green: 0.086, blue: 0.102), in: RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(AureaPressStyle(shrink: 0.96)).offset(x: centerX + 8, y: lane)
                    .accessibilityIdentifier("edit.addAudio")
            }
        }
    }

    private func rowOffsets(rows: [EditRow], hasAudio: Bool, top: CGFloat) -> (rows: [CGFloat], audioLane: CGFloat?) {
        var y = top
        if !rows.contains(where: \.main) { y += mainH + gap }
        var offsets: [CGFloat] = []
        var lane: CGFloat?
        for row in rows {
            offsets.append(y)
            y += (row.main ? mainH : rowH) + gap
            if row.main && !hasAudio { lane = y; y += rowH + gap }
        }
        return (offsets, lane)
    }

    private var addButton: some View {
        Button(action: onAddMedia) {
            CupertinoGlyph.text(CupertinoGlyph.Plus, size: 20, color: .black)
                .frame(width: 36, height: 36).background(Color.white, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle(shrink: 0.9))
            .accessibilityLabel(AureaText.t("edit_add_media")).accessibilityIdentifier("edit.add")
    }

    private var magnetToggle: some View {
        Button { model.toggleEditMode() } label: {
            CupertinoGlyph.text(CupertinoGlyph.Link, size: 13, color: model.editMode ? AureaColors.accent : AureaColors.muted)
                .frame(width: 22, height: 22)
                .background(model.editMode ? AureaColors.accentDim : EditColors.dockLevel, in: RoundedRectangle(cornerRadius: 6))
        }.buttonStyle(AureaPressStyle(shrink: 0.9))
            .accessibilityLabel(AureaText.t("edit_magnet"))
            .accessibilityAddTraits(model.editMode ? [.isButton, .isSelected] : .isButton)
            .accessibilityIdentifier("edit.magnet")
    }
}

/// Um corte. Toque escolhe; escolhido, ganha a borda branca com as alças de
/// aparar; segurar e arrastar reordena (trilha principal) ou move no tempo.
private struct EditClipView: View {
    @EnvironmentObject private var model: AureaModel
    let clip: LayerItem
    let main: Bool
    let x: CGFloat
    let width: CGFloat
    let y: CGFloat
    let height: CGFloat
    let pxPerFrame: CGFloat
    let thumbs: TimelineThumbStrip
    let waves: EditWaveCache
    /// Largura da linha do tempo: a onda só desenha o trecho que está na tela.
    let viewport: CGFloat
    @State private var trimStart: CGFloat = 0
    @State private var trimEnd: CGFloat = 0
    @State private var drag: CGFloat = 0

    private var selected: Bool { model.selection.contains(clip.id) }

    var body: some View {
        let w = max(8, width - trimStart + trimEnd)
        ZStack(alignment: .leading) {
            if main && [1, 2, 12].contains(clip.kind) {
                filmstrip
            } else if clip.kind != 3 {
                Text(clip.name).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(.white).lineLimit(1)
                    .padding(.horizontal, selected ? 18 : 8)
            }
            if clip.kind == 1 || clip.kind == 3 {
                // Vídeo: faixa escura embaixo das miniaturas (ou do nome), barras
                // subindo do pé. Áudio: a onda ocupa o corte inteiro, espelhada.
                let originX = x + trimStart + drag
                let visL = max(0, -originX), visR = min(w, viewport - originX)
                if visR > visL {
                    let bandH: CGFloat = clip.kind == 3 ? height : (main ? 16 : 10)
                    EditClipWaveform(clip: clip, waves: waves, pxPerFrame: pxPerFrame,
                                     frameAtLeft: Double(clip.startFrame) + Double((visL - trimStart) / max(pxPerFrame, 0.0001)),
                                     band: clip.kind != 3, width: visR - visL, height: bandH)
                        .frame(width: visR - visL, height: bandH)
                        .offset(x: visL)
                        .frame(width: w, height: height, alignment: .bottomLeading)
                        .allowsHitTesting(false)
                }
            }
            if clip.kind == 3 {
                Text(clip.name).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(.white).lineLimit(1)
                    .padding(.leading, selected ? 18 : 6).padding(.top, 2)
                    .frame(width: w, height: height, alignment: .topLeading)
            }
            if main && selected {
                Text(String(format: "%.1fs", Double(clip.endFrame - clip.startFrame) / max(model.compositionFps, 1)))
                    .font(.aurea(size: 10, weight: .semibold)).foregroundStyle(.white).padding(.horizontal, 4)
                    .background(Color.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 4))
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading).padding(.leading, 18).padding(.top, 3)
            }
            if selected {
                HStack(spacing: 0) {
                    handle(start: true)
                    Spacer(minLength: 0)
                    handle(start: false)
                }
            }
        }
        .frame(width: w, height: height, alignment: .leading)
        .background(main ? Color(red: 0.118, green: 0.118, blue: 0.133) : layerColor.opacity(0.85))
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.white, lineWidth: selected ? 2 : 0))
        .offset(x: x + trimStart + drag, y: y)
        .onTapGesture { model.select(layerId: clip.id) }
        .gesture(LongPressGesture(minimumDuration: 0.4).sequenced(before: DragGesture(minimumDistance: 0))
            .onChanged { value in
                if case .second(true, let dragValue?) = value {
                    if !selected { model.select(layerId: clip.id) }
                    drag = dragValue.translation.width
                }
            }
            .onEnded { _ in
                let frames = Int64((drag / max(pxPerFrame, 0.0001)).rounded())
                let target = max(0, Int64(clip.startFrame) + frames)
                if frames != 0 {
                    if main { model.reorderClip(clip.id, toFrame: target) }
                    else { model.editClipTime(6, amount: target) }
                }
                drag = 0
            })
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(selected ? AureaText.t("edit_clip_selected", clip.name) : clip.name)
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
        .accessibilityIdentifier("edit.clip.\(clip.id)")
    }

    private var layerColor: Color {
        switch clip.kind {
        case 1: return Color(red: 0.416, green: 0.322, blue: 0.878)
        case 2: return Color(red: 0.239, green: 0.435, blue: 0.851)
        case 3: return Color(red: 0.122, green: 0.549, blue: 0.576)
        case 4: return Color(red: 0.690, green: 0.478, blue: 0.086)
        case 5: return Color(red: 0.180, green: 0.580, blue: 0.349)
        case 7: return Color(red: 0.353, green: 0.290, blue: 0.478)
        default: return Color(red: 0.267, green: 0.298, blue: 0.361)
        }
    }

    /// A alça branca de aparar: arrastar mostra o novo limite; soltar manda ao motor.
    private func handle(start: Bool) -> some View {
        ZStack {
            Color.white
            RoundedRectangle(cornerRadius: 1).fill(Color.black.opacity(0.7)).frame(width: 2, height: 14)
        }
        .frame(width: 14)
        .gesture(DragGesture(minimumDistance: 1)
            .onChanged { value in if start { trimStart = value.translation.width } else { trimEnd = value.translation.width } }
            .onEnded { _ in
                let delta = start ? trimStart : trimEnd
                let frames = Int64((delta / max(pxPerFrame, 0.0001)).rounded())
                if frames != 0 {
                    if start { model.editClipTime(0, amount: Int64(clip.startFrame) + frames) }
                    else { model.editClipTime(1, amount: Int64(clip.endFrame) + frames) }
                }
                trimStart = 0; trimEnd = 0
            })
        .accessibilityLabel(AureaText.t(start ? "edit_trim_start" : "edit_trim_end"))
    }

    /// As miniaturas da mídia ao longo do corte (um ladrilho por largura de altura × 16:9).
    private var filmstrip: some View {
        let fps = max(model.compositionFps, 1)
        let bucketFrames = max(1, fps * 0.25)
        let tileW = height * 16 / 9
        let tiles = max(1, Int(width / tileW) + 1)
        let heightPx = Int((height * UIScreen.main.scale).rounded())
        return HStack(spacing: 0) {
            ForEach(0..<tiles, id: \.self) { i in
                let localFrame = (CGFloat(i) * tileW - trimStart) / max(pxPerFrame, 0.0001)
                let bucket = Int32((Double(localFrame) / bucketFrames).rounded())
                let frame = Int32(clip.startFrame) + Int32(Double(bucket) * bucketFrames)
                Group {
                    if let image = thumbs.get(model, layer: clip.id, bucket: bucket, timelineFrame: max(frame, clip.startFrame),
                                              heightPx: min(max(heightPx, 16), 256), generation: 0) {
                        Image(uiImage: image).resizable().scaledToFill()
                    } else {
                        Color(red: 0.165, green: 0.165, blue: 0.188)
                    }
                }.frame(width: tileW, height: height).clipped()
            }
        }
        .frame(width: max(8, width - trimStart + trimEnd), height: height, alignment: .leading).clipped()
    }
}

/// Onda do som de um corte (a mesma do motor que a Composição desenha), só no
/// trecho visível: um balde a cada ~1,5 pt na grade fixa do tempo (WaveGrid).
/// Par de `ClipWaveform` em EditTimeline.kt. Corte sem som não desenha nada.
private struct EditClipWaveform: View {
    @EnvironmentObject private var model: AureaModel
    let clip: LayerItem
    let waves: EditWaveCache
    let pxPerFrame: CGFloat
    /// Frame da timeline na borda esquerda desta caixa.
    let frameAtLeft: Double
    let band: Bool
    let width: CGFloat
    let height: CGFloat

    var body: some View {
        // A consulta (cache no ator principal) sai do corpo; o Canvas só desenha números.
        let fpb = WaveGrid.framesPerBucket(targetPx: 1.5, pxPerFrame: pxPerFrame)
        let toFrame = frameAtLeft + Double(width / max(pxPerFrame, 0.0001))
        let first = WaveGrid.bucketAt(max(frameAtLeft, Double(clip.startFrame)), fpb)
        let last = WaveGrid.bucketAt(min(toFrame, Double(clip.endFrame)) - 1e-6, fpb)
        let wave = pxPerFrame > 0 && width >= 1 && last >= first
            ? waves.get(model, clip: clip, fpb: fpb, generation: model.status.thumbnailGeneration, first: first, last: last)
            : nil
        let band = self.band, frameAtLeft = self.frameAtLeft, pxPerFrame = self.pxPerFrame
        Canvas { context, size in
            guard let wave, size.width >= 1, size.height >= 1 else { return }
            if band { context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.black.opacity(0.45))) }
            let stroke = max(1, CGFloat(fpb) * pxPerFrame * 0.72)
            let top = size.height * 0.12
            var path = Path()
            var b = first
            while b <= last {
                let v = CGFloat(wave.at(b)) / 255
                let x = CGFloat((Double(b) + 0.5) * fpb - frameAtLeft) * pxPerFrame
                b += 1
                if v <= 0 { continue }
                if band {
                    let h = v * (size.height - top)
                    path.move(to: CGPoint(x: x, y: size.height - h)); path.addLine(to: CGPoint(x: x, y: size.height))
                } else {
                    let half = v * (size.height - top) / 2, mid = (size.height + top) / 2
                    path.move(to: CGPoint(x: x, y: mid - half)); path.addLine(to: CGPoint(x: x, y: mid + half))
                }
            }
            context.stroke(path, with: .color(.white.opacity(band ? 0.8 : 0.75)), lineWidth: stroke)
        }
        .accessibilityHidden(true)
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
        .onChange(of: model.status.playhead / 15) { _ in loadThumbs() }
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
    @State private var wheel = 0
    @State private var hueCurves = false
    @State private var scopes = false
    @State private var scopeKind = 0
    @State private var scopeData: GradeScopes?
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
            HStack {
                Button(AureaText.t("grade_scopes")) { scopes.toggle(); loadScopes() }
                    .font(.aurea(size: 12)).padding(.horizontal, 10).frame(minHeight: 36)
                    .background(scopes ? AureaColors.accent.opacity(0.2) : AureaColors.chip).clipShape(Capsule())
                    .accessibilityIdentifier("grade.scopes")
                Spacer()
                if let effect {
                    Button(AureaText.t("grade_reset")) { model.resetColorGrade(effect: effect) }
                        .font(.aurea(size: 13)).frame(minHeight: 44).accessibilityIdentifier("grade.reset")
                }
            }.padding(.horizontal, 10)
            if scopes { scopesView }
            Picker("", selection: $tab) {
                Text(AureaText.t("grade_tab_basic")).tag(0); Text(AureaText.t("grade_tab_hsl")).tag(1)
                Text(AureaText.t("grade_tab_curves")).tag(2); Text(AureaText.t("grade_tab_wheels")).tag(3)
                Text(AureaText.t("grade_tab_more")).tag(4)
            }.pickerStyle(.segmented).padding(.horizontal, 10)
            if let effect, let layer = model.primarySelection {
                ScrollView {
                    switch tab {
                    case 0: basicTab(effect)
                    case 1: hslTab(effect)
                    case 2: curvesTab(effect, layer: layer)
                    case 3: wheelsTab(effect)
                    default: moreTab(effect, layer: layer)
                    }
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 6)
        .onAppear { effect = model.colorGrade(role: 2) }
        .onChange(of: model.primarySelection) { _ in effect = model.colorGrade(role: 2) }
        .onChange(of: model.status.modelRevision) { _ in if scopes { loadScopes() } }

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
        let w = wheels[min(max(wheel, 0), wheels.count - 1)]
        VStack(spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(wheels.indices, id: \.self) { i in
                        Button(AureaText.t(wheels[i].2)) { wheel = i }.font(.aurea(size: 12)).padding(.horizontal, 10).frame(minHeight: 36)
                            .background(i == wheel ? AureaColors.accent.opacity(0.2) : AureaColors.chip).clipShape(Capsule())
                            .accessibilityIdentifier("grade.wheel.tab.\(i)")
                    }
                }.padding(.horizontal, 10)
            }
            let value = model.gradeParam(w.0)?.value ?? [0, 0]
            GradeWheelView(x: value[safe: 0] ?? 0, y: value[safe: 1] ?? 0, label: AureaText.t(w.2),
                           onBegin: { model.beginGesture(AureaText.t(w.2)) },
                           onChange: { x, y in model.writeGradePoint(effect, index: w.0, x: x, y: y) },
                           onEnd: { model.endGesture() })
                .frame(width: 200, height: 200).accessibilityIdentifier("grade.wheel.\(w.0)")
            Text(AureaText.t("grade_wheel_hint")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
            if let luma = w.1 { GradeSliderRow(effect: effect, index: luma, label: AureaText.t("grade_luminance"), range: -100...100).padding(.horizontal, 10) }
            if w.0 == GradeParam.shadowTint || w.0 == GradeParam.highlightTint {
                GradeSliderRow(effect: effect, index: GradeParam.tintBalance, label: AureaText.t("grade_tint_balance"), range: -100...100).padding(.horizontal, 10)
            }
        }
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
        guard scopes else { return }
        let engine = model.engine
        DispatchQueue.global(qos: .userInitiated).async {
            let dict = engine.colorScopes(192)
            DispatchQueue.main.async { scopeData = AureaModel.gradeScopes(from: dict) }
        }
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
