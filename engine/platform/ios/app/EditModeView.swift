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

    /// Efeito/filtro/ajuste do projeto: uma camada de ajuste por cima, e os efeitos nela.
    private func globalEffect() {
        model.addAdjustmentLayer()
        model.openPanel(.effects)
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
                item("effects", CupertinoGlyph.Sparkles, "edit_dock_effects") { globalEffect() },
                item("captions", CupertinoGlyph.CaptionsBubble, "edit_dock_captions") { panel(.captions) },
                item("filters", CupertinoGlyph.ColorFilter, "edit_dock_filters") { globalEffect() },
                item("adjust", CupertinoGlyph.SliderHorizontal3, "edit_dock_adjust") { globalEffect() },
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
            var list = [split, item("speed", CupertinoGlyph.Speedometer, "edit_clip_speed") { panel(.speed) }, animations, effects, delete, mask]
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
            return [effects, transform, opacity, mask, split, duplicate, delete]
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
        let clips = entries.map(\.element).sorted { $0.startFrame < $1.startFrame }
        // A lista do motor vem do topo da pilha para o fundo: índice menor = mais à frente.
        let front = entries.map(\.offset).min() ?? 0
        if k == mainKey { main = EditRow(id: k, clips: clips, main: true, audio: false) }
        else { others.append((EditRow(id: k, clips: clips, main: false, audio: clips.allSatisfy { $0.kind == 3 }), front)) }
    }
    let ordered = others.sorted { a, b in a.0.audio != b.0.audio ? !a.0.audio : a.1 < b.1 }.map(\.0)
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
            ForEach(Array(rows.enumerated()), id: \.element.id) { index, row in
                let y = layout.rows[index]
                let h = row.main ? mainH : rowH
                ForEach(row.clips) { clip in
                    let left = centerX + CGFloat(Int64(clip.startFrame) - playhead) * pxPerFrame
                    let w = CGFloat(clip.endFrame - clip.startFrame) * pxPerFrame
                    if left + w > -width && left < width * 2 {
                        EditClipView(clip: clip, main: row.main, x: left, width: w, y: y, height: h,
                                     pxPerFrame: pxPerFrame, thumbs: thumbs)
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
    @State private var trimStart: CGFloat = 0
    @State private var trimEnd: CGFloat = 0
    @State private var drag: CGFloat = 0

    private var selected: Bool { model.selection.contains(clip.id) }

    var body: some View {
        let w = max(8, width - trimStart + trimEnd)
        ZStack(alignment: .leading) {
            if main && [1, 2, 12].contains(clip.kind) {
                filmstrip
            } else {
                Text(clip.name).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(.white).lineLimit(1)
                    .padding(.horizontal, selected ? 18 : 8)
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
