// Direct port of Android TimelinePainter, TimelineHit and TimelineController.
// The core remains the only owner of layers, animation tracks and media.
import SwiftUI
import UIKit
import Combine

private func markerTint(_ packed: UInt32) -> Color {
    Color(.sRGB, red: Double(packed & 255) / 255, green: Double((packed >> 8) & 255) / 255,
          blue: Double((packed >> 16) & 255) / 255, opacity: 1)
}

struct MarkerEditorSheet: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    let original: Int64
    let isNew: Bool
    @State private var name: String
    @State private var frameText: String
    @State private var color: UInt32
    @State private var failure = false
    private let palette: [UInt32] = [0xFFF7C34F, 0xFF4D6EFF, 0xFF70D56B, 0xFFFFB75B, 0xFFD67BDB, 0xFFFFFFFF]
    init(frame: Int64, color: UInt32, label: String, isNew: Bool = false) {
        original = frame
        self.isNew = isNew
        _name = State(initialValue: label)
        _frameText = State(initialValue: String(frame))
        _color = State(initialValue: color)
    }
    var body: some View {
        NavigationView {
            Form {
                TextField(AureaText.t("new_project_name"), text: $name)
                    .onChange(of: name) { value in if value.count > 200 { name = String(value.prefix(200)) } }
                TextField(AureaText.t("marker_frame"), text: $frameText).keyboardType(.numberPad)
                Text(Timecode.format(Int32(clamping: max(0, Int64(frameText) ?? 0)), Float(model.compositionFps)))
                Button(AureaText.t("marker_at_playhead")) { frameText = String(model.status.playhead) }
                HStack {
                    ForEach(Array(palette.enumerated()), id: \.offset) { index, packed in
                        Button { color = packed } label: {
                            Circle().fill(markerTint(packed)).frame(width: color == packed ? 32 : 22, height: color == packed ? 32 : 22)
                                .frame(width: 40, height: 44)
                        }.buttonStyle(.borderless).accessibilityLabel(AureaText.t("marker_color", index + 1))
                    }
                }
                Text(AureaText.t(failure ? "marker_error" : "marker_hint")).font(.footnote)
                if !isNew {
                    Button(AureaText.t("common_delete"), role: .destructive) { model.deleteMarker(original); dismiss() }
                }
            }
            .navigationTitle(AureaText.t("marker_edit"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("common_cancel")) { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button(AureaText.t("common_save")) {
                        if let target = Int64(frameText), model.editMarker(from: isNew ? -1 : original, to: target, color: color, label: name) { dismiss() }
                        else { failure = true }
                    }
                }
            }
        }
    }
}

/// Como o relógio (MM:SS:FF) aparece sobre o cabeçote: sublinhado (editor
/// principal) ou numa caixa com borda no destaque (estado da camada/efeitos).
enum TimecodeStyle { case underline, box }

@MainActor
struct TimelineView: View {
    @EnvironmentObject private var model: AureaModel
    /// Camada escolhida com a doca aberta (layout de celular): a timeline vira a
    /// fileira única dela, como com painel aberto (EditorScreen.kt `compactDock`).
    var compactDock = false
    @State private var showAllLayers = false
    /// Estilo do relógio sobre o cabeçote (o mesmo parâmetro do Android).
    var timecodeStyle: TimecodeStyle = .underline
    /// O cabeçote a cada quadro da tela durante o play (ver `PlayheadClock`).
    @EnvironmentObject private var clock: PlayheadClock
    @State private var pps = Zoom.defaultPPS
    @State private var scrollY: CGFloat = 0
    @State private var heldView: Double?
    @State private var gesture: Interaction?
    @State private var pinchPPS = Zoom.defaultPPS
    @State private var pinchFrame = 0.0
    @State private var pinching = false
    /// A pinça começou com o play rodando: zoom em volta do cabeçote, sem scrub.
    @State private var pinchPlaying = false
    @State private var guide = Snap.none
    @State private var selectedKey: (layer: Int64, frame: Int32, track: TimelineTrack?)?
    @State private var reorderSource = -1
    @State private var reorderTarget = -1
    @State private var reorderTop: CGFloat = 0
    @State private var reorderGrabOffset: CGFloat = 0
    /// Arrasto vertical de UM trecho (Alight Motion): o trecho no ar (0 = nenhum),
    /// o topo dele na janela (segue o dedo), a fileira de onde saiu e onde cai.
    @State private var liftId: Int64 = 0
    @State private var liftTop: CGFloat = 0
    @State private var liftGrabOffset: CGFloat = 0
    @State private var liftSource = -1
    @State private var liftDrop = TimelineRowDrop.Target.none
    @State private var scrollVelocity: CGFloat = 0
    @State private var lastPointer = CGPoint.zero
    @State private var pendingPointer = false
    @State private var lastTick: TimeInterval = 0
    @State private var mediaNeedsRefresh = false
    @State private var thumbnails: [Int64: [MediaTile]] = [:]
    /// Miniatura do quadradinho da pílula (imagem/vídeo), por fileira.
    @State private var pillThumbs: [Int64: UIImage] = [:]
    @State private var waves: [Int64: TimelineWaveStrip.Entry] = [:]
    @State private var markers: [Marker] = []
    @State private var markersRevision: UInt32 = .max
    @State private var markersFrames: [Int64] = []
    /// Camadas com as trilhas abertas: VÁRIAS de uma vez, cada uma com o seu ▸/▾.
    @State private var expandedLayers: Set<Int64> = []
    /// Trilhas de grupo (Posição, Escala...) abertas nos eixos.
    @State private var openGroups: Set<TimelineLaneGroupKey> = []
    /// Retângulo da seleção de losangos (cantos no CONTEÚDO: frame e y com a rolagem).
    @State private var boxRect: (f0: Double, y0: CGFloat, f1: Double, y1: CGFloat)?
    @State private var rowCache = TimelineRowCache()
    @State private var thumbCache = TimelineThumbStrip()
    @State private var waveCache = TimelineWaveStrip(capacity: 2048)
    /// Auto-rolagem nas bordas virou scrub do motor (par do Android `holdView`):
    /// abre um scrub só na primeira volta e fecha no `finish`.
    @State private var autoScrubbing = false
    private let m = TimelineMetrics()
    private var haptics: TimelineHaptics { TimelineHaptics.shared }
    private let pulse = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()

    private struct MediaTile { var localFrame: Double; var width: CGFloat; var image: UIImage }
    private struct Marker { var frame: Int32; var packedColor: UInt32; var kind: UInt32 }
    /// `.keys`: arrasto de um losango ESCOLHIDO — a seleção de keyframes inteira anda junta.
    /// `.step`: fileira compacta — o arrasto vertical passa pelas camadas (roda).
    /// `.box`: modo "Selecionar" — arrastar no vazio desenha o retângulo de seleção dos losangos.
    /// `.lift`: toque longo + vertical num trecho — só ELE sobe/desce (a linha não vai junto).
    private enum Mode { case scrub, scroll, step, move, trimStart, trimEnd, key, keys, reorder, hold, blocked, box, lift }
    private struct Interaction {
        var mode: Mode
        var start: CGPoint
        var view: Double
        var scroll: CGFloat
        var row: TimelineRow?
        var hit: TimelineHit
        var selection: [LayerItem]
        var snapTargets: [Int32]
        var keyIndex: Int = -1
        var keyFrame: Int32 = 0
        var movingKeys: [KeyframeItem] = []
        var keyLimits: (lo: Int32, hi: Int32) = (0, 0)
        /// `.keys`: instante do losango pego no começo (keyFrame = grabFrame + sentDelta).
        var grabFrame: Int32 = 0
        var sentDelta: Int32 = 0
        /// `.move` numa linha magnética: último ALVO absoluto já enviado ao
        /// motor (a reordenação é medida em frame, não em deslocamento).
        var sentFrame: Int64 = -1
        /// `.box`: a seleção de keyframes do começo do arrasto (o retângulo SOMA a ela).
        var boxBase: TimelineKeySelection?
        var undoOpen = false
    }
    /// Painel aberto: sempre a fileira única. Doca aberta: a fileira única SÓ
    /// sem trilhas de propriedade abertas e fora do modo de escolher keyframes —
    /// senão a timeline volta inteira e dá para mexer (par do Timeline.kt).
    private var compact: Bool {
        !showAllLayers && timelineCompact(panel: model.sheetContent == .panel || model.sheetContent == .curve,
                        dock: compactDock && model.sheetContent == .dock,
                        tracksOpen: tracksOpen,
                        selectingKeys: model.timelineKeySelectMode || model.timelineLayerSelectMode)
    }
    /// Compacta só por causa da doca (a calha abre as trilhas em vez de sair).
    private var compactByDock: Bool { compact && compactDock && model.sheetContent == .dock }
    /// Trilhas abertas de uma camada que ainda existe.
    private var tracksOpen: Bool {
        guard !expandedLayers.isEmpty else { return false }
        return model.layers.contains { expandedLayers.contains($0.id) }
    }
    private var fps: Float { TimeAxis.safeFps(Float(model.compositionFps)) }
    /// Chave do zoom/rolagem guardados: o projeto aberto.
    private var memoryKey: String { model.projectURL?.path ?? model.projectName }
    private var ppf: CGFloat { TimeAxis.pxPerFrame(pps: pps, density: 1, fps: fps) }
    private var viewFrame: Double { heldView ?? Double(clock.frame) }
    private var focusTracks: [TimelineTrack]? {
        model.panel == .curve ? [TimelineTrack(property: Int(model.curveProperty), effect: model.curveEffect, param: model.curveParam)] : model.timelineFocus
    }
    private var rows: [TimelineRow] {
        let cached = rowCache.build(model.layers, model.keyframes)
        let all = rowCache.focused(cached, id: model.primarySelection, tracks: focusTracks, layers: model.layers, keys: model.keyframes)
        // Uma fileira por LINHA. No compacto, a linha inteira da escolhida: depois
        // de um split os dois pedaços continuam à vista (só o trecho escolhido
        // fazia a metade nova sumir, e o corte parecia não ter funcionado).
        let shared = rowCache.shared(all, focus: model.primarySelection, tracks: focusTracks)
        if compact {
            guard let primary = model.primarySelection else { return [] }
            return shared.filter { $0.segment(primary) != nil }
        }
        return rowCache.expanded(shared, ids: expandedLayers, groups: openGroups, revision: model.status.modelRevision, keys: model.keyframes, effects: { id in
            model.engine.effects(forLayer: id).map { row in
                EffectItem(effectId: (row["effectId"] as? NSNumber)?.uint32Value ?? 0,
                    typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                    name: row["name"] as? String ?? "",
                    enabled: row["enabled"] as? Bool ?? true,
                    paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0,
                    known: row["known"] as? Bool ?? true)
            }
        })
    }
    private func x(_ frame: Double, width: CGFloat) -> CGFloat {
        TimeAxis.xOf(frame: frame, view: viewFrame, pxPerFrame: ppf, centerX: width / 2)
    }
    private func frame(_ x: CGFloat, width: CGFloat) -> Double {
        TimeAxis.frameAt(x: x, view: viewFrame, pxPerFrame: ppf, centerX: width / 2)
    }
    private func maxScroll(_ height: CGFloat) -> CGFloat {
        compact ? 0 : max(0, rowTop(rows.count) + m.bottomPad - (height - m.rowsTop))
    }

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            Canvas { context, canvasSize in
                drawRows(&context, size: canvasSize)
                drawRuler(&context, size: canvasSize)
                drawPreviewBuffer(&context, size: canvasSize)
                drawPlayhead(&context, size: canvasSize)
            }
            .accessibilityChildren {
                if !model.previewBufferRanges.isEmpty {
                    Text(AureaText.t("preview_buffer_timeline", model.previewTimelineCachedFrames))
                        .accessibilityIdentifier("timeline.preview.buffer")
                }
            }
            .background(AureaTimeline.background)
            .overlay {
                TimelineGestureSurface(
                    tap: { point, origin in tap(point, width: size.width, origin: origin) },
                    pan: { state, start, point, velocity in pan(state, start: start, point: point, velocity: velocity, size: size) },
                    hold: { state, start, point in hold(state, start: start, point: point, size: size) },
                    pinch: { state, scale, focus in pinch(state, scale: scale, focus: focus, width: size.width) }
                )
            }
            // Por cima da superfície de gestos: o toque num botão não chega à timeline.
            .overlay(alignment: keyBarAlignment) { keyActionBar }
            .overlay(alignment: .bottom) { layerPickBar }
            .overlay(alignment: .topLeading) {
                if !model.selection.isEmpty && (compactDock || model.sheetContent == .panel || model.sheetContent == .curve) {
                    Button { showAllLayers.toggle() } label: {
                        Text(AureaText.t(showAllLayers ? "timeline_selection_short" : "timeline_all_short"))
                            .font(.aurea(size: 12)).lineLimit(1).frame(width: 84, height: 44)
                    }.accessibilityLabel(AureaText.t(showAllLayers ? "timeline_selected_only" : "timeline_show_all"))
                        .accessibilityIdentifier("timeline.showAllLayers")
                }
            }
            .onAppear {
                // A timeline some e volta (painel, doca, rotação): o zoom e a
                // rolagem do projeto voltam como estavam. Ajustar à duração só
                // na PRIMEIRA vez de cada projeto (par do Android).
                if let saved = TimelineViewMemory.state[memoryKey] {
                    pps = saved.pps; scrollY = min(saved.scrollY, maxScroll(size.height))
                } else {
                    let seconds = CGFloat(model.compositionDuration) / CGFloat(fps)
                    if seconds >= Zoom.autoFitMinSeconds { pps = Zoom.autoFit(availableDp: size.width - 32, seconds: seconds) }
                    TimelineViewMemory.state[memoryKey] = (pps, scrollY)
                }
                haptics.prepare()
                refreshMarkers()
                refreshMedia(size: size)
            }
            .onChange(of: guide) { snap in if snap != Snap.none { haptics.snap() } }
            .onChange(of: pps) { value in TimelineViewMemory.state[memoryKey] = (value, scrollY) }
            .onChange(of: scrollY) { value in TimelineViewMemory.state[memoryKey] = (pps, value) }
            .onChange(of: model.status.thumbnailGeneration) { _ in mediaNeedsRefresh = true }
            .onChange(of: model.memoryCacheEpoch) { _ in
                thumbCache = TimelineThumbStrip()
                thumbnails = [:]; pillThumbs = [:]
                mediaNeedsRefresh = true
            }
            .onChange(of: model.status.playhead) { _ in mediaNeedsRefresh = true }
            .onChange(of: model.status.modelRevision) { _ in refreshMarkers(); mediaNeedsRefresh = true }
            .onChange(of: model.markerFrames) { _ in refreshMarkers() }
            .onChange(of: scrollY) { _ in mediaNeedsRefresh = true }
            .onChange(of: pps) { _ in mediaNeedsRefresh = true }
            .onChange(of: heldView) { _ in mediaNeedsRefresh = true }
            .onChange(of: size) { _ in scrollY = min(scrollY, maxScroll(size.height)); mediaNeedsRefresh = true }
            .onChange(of: model.primarySelection) { _ in revealSelection(size: size); mediaNeedsRefresh = true }
            .onChange(of: model.curveSelectedTime) { time in
                if !model.timelineKeyDragActive, let time, let id = model.primarySelection, let row = segmentRow(id) {
                    selectedKey = (id, Keyframes.toTimeline(time, row.start, row.offset),
                        TimelineTrack(property: Int(model.curveProperty), effect: model.curveEffect, param: model.curveParam))
                    // Fora do modo de escolha, a seleção da barra acompanha o keyframe do gráfico.
                    if !model.timelineKeySelectMode, let current = model.timelineKeySelection, current.layer == id {
                        let ref = TimelineKeyRef(property: model.curveProperty, effect: model.curveEffect, param: model.curveParam, time: time)
                        let next = TimelineKeySelection(layer: id, keys: [ref])
                        if next != current { model.timelineKeySelection = next }
                    }
                }
            }
            .onChange(of: compact) { _ in scrollY = 0; mediaNeedsRefresh = true }
            .onReceive(pulse) { _ in tick(size: size) }
            .onDisappear { finish(cancelled: true); finishPinch() }
            .clipped()
        }
        // A timeline não espelha em árabe: quadro 0 à esquerda, o tempo anda
        // para a direita, e as barras de ação por cima seguem o mesmo eixo
        // (par do KeepLtr do TimelineHost). Os gestos (UIKit) já são absolutos.
        .keepLtr()
    }

    // MARK: Barra de ações da seleção de keyframes (par do KeyActionBar do Android)
    /// Com painel aberto (timeline compacta, uma linha) só o "Selecionar" cabe,
    /// por cima da régua; ligar o modo fecha o painel e a barra inteira aparece
    /// embaixo. Todos = todos os keyframes da camada que a timeline mostra;
    /// Colar = no cabeçote; Duplicar = 1 frame depois do último escolhido.
    private var keyBarAlignment: Alignment { compact ? .topTrailing : .bottom }

    @ViewBuilder private var keyActionBar: some View {
        if let selection = model.timelineKeySelection {
            let mode: Bool = model.timelineKeySelectMode
            let count: Int = selection.count
            let title: String = mode ? AureaText.t("panel_selecionar") + " · \(count)" : AureaText.t("panel_selecionar")
            if compact {
                keyAction(title, id: "timeline.keys.select", active: mode) { model.changeTimelineKeySelectMode(!mode) }
                    .background(RoundedRectangle(cornerRadius: 14).fill(Color.black.opacity(0.8)))
                    .padding(.trailing, 6)
            } else {
                let canPaste: Bool = model.engine.clipboardState & 8 != 0
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 0) {
                        keyAction(title, id: "timeline.keys.select", active: mode) { model.changeTimelineKeySelectMode(!mode) }
                        keyAction(AureaText.t("common_all"), id: "timeline.keys.all") { model.selectAllTimelineKeys() }
                        keyAction(AureaText.t("common_copy"), id: "timeline.keys.copy", enabled: count > 0 && !selection.crossLayer) { model.copyTimelineKeys() }
                        keyAction(AureaText.t("common_paste"), id: "timeline.keys.paste", enabled: canPaste) { model.pasteTimelineKeys() }
                        keyAction(AureaText.t("common_duplicate"), id: "timeline.keys.duplicate", enabled: count > 0 && !selection.crossLayer) { model.duplicateTimelineKeys() }
                        keyAction(AureaText.t("common_delete"), id: "timeline.keys.delete", enabled: count > 0, danger: true) { model.deleteTimelineKeys() }
                        keyAction(AureaText.t("editor_concluir"), id: "timeline.keys.done") { model.clearTimelineKeySelection() }
                    }
                    .padding(.horizontal, 4)
                }
                .fixedSize(horizontal: false, vertical: true)
                .background(RoundedRectangle(cornerRadius: 14).fill(Color.black.opacity(0.8)))
                .padding(.horizontal, 8)
                .padding(.bottom, 6)
            }
        }
    }

    // MARK: Barra do modo "Selecionar várias camadas" (par do LayerPickBar do Android)
    /// Mesmo desenho da barra dos keyframes: contador, Todas, Limpar e Concluir.
    /// Some quando há keyframes escolhidos (a barra deles manda).
    @ViewBuilder private var layerPickBar: some View {
        if model.timelineLayerSelectMode && model.timelineKeySelection == nil {
            let count: Int = model.selection.count
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 0) {
                    keyAction(AureaText.t("panel_selecionar") + " · \(count)", id: "timeline.layers.select", active: true) { model.changeTimelineLayerSelectMode(false) }
                    keyAction(AureaText.t("common_all"), id: "timeline.layers.all", enabled: model.layers.count >= 2) { model.selectAll() }
                    keyAction(AureaText.t("editor_limpar_selecao"), id: "timeline.layers.clear", enabled: count > 0) { model.clearSelection() }
                    keyAction(AureaText.t("editor_concluir"), id: "timeline.layers.done") { model.changeTimelineLayerSelectMode(false) }
                }
                .padding(.horizontal, 4)
            }
            .fixedSize(horizontal: false, vertical: true)
            .background(RoundedRectangle(cornerRadius: 14).fill(Color.black.opacity(0.8)))
            .padding(.horizontal, 8)
            .padding(.bottom, 6)
        }
    }

    /// Botão da barra: alvo de 44 pt no mínimo.
    private func keyAction(_ title: String, id: String, enabled: Bool = true, active: Bool = false,
                           danger: Bool = false, action: @escaping () -> Void) -> some View {
        let tint: Color
        if !enabled { tint = AureaColors.muted }
        else if danger { tint = AureaColors.danger }
        else if active { tint = AureaColors.accent }
        else { tint = Color.white }
        let fill: Color = active ? AureaColors.accent.opacity(0.28) : Color.clear
        return Button(action: action) {
            Text(title)
                .font(.aurea(size: 13))
                .lineLimit(1)
                .foregroundColor(tint)
                .padding(.horizontal, 12)
                .frame(minWidth: 44, minHeight: 44)
                .background(RoundedRectangle(cornerRadius: 10).fill(fill))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .accessibilityIdentifier(id)
    }

    /// O losango principal (âmbar) faz parte da seleção de keyframes?
    private func primaryInKeySelection(_ current: (layer: Int64, frame: Int32, track: TimelineTrack?)) -> Bool {
        guard let keys = model.timelineKeySelection, keys.layer == current.layer, let track = current.track,
              let layer = model.layers.first(where: { $0.id == current.layer }) else { return false }
        let local: Int32 = Keyframes.toLocal(current.frame, layer.startFrame, layer.offsetFrames)
        let ref = TimelineKeyRef(property: UInt32(clamping: track.property), effect: track.effect, param: track.param, time: local)
        return keys.keys.contains(ref)
    }

    /// Instantes da linha com algum keyframe da seleção (paralelo a `instants`); nil = nenhum.
    private func pickedInstants(_ row: TimelineRow) -> [Bool]? {
        guard let selection = model.timelineKeySelection, !selection.on(row.id).isEmpty else { return nil }
        var out = [Bool](repeating: false, count: row.keysAt.count)
        var any = false
        for index in 0..<row.keysAt.count where selection.containsAny(on: row.id, row.keysAt[index]) {
            out[index] = true
            any = true
        }
        return any ? out : nil
    }

    // MARK: Android painter
    /// Régua (0..30): riscos de 1 pt do zero (no cabeçote fixo) para a direita,
    /// base em y 22; rótulos pequenos e apagados; as marcas por cima.
    private func drawRuler(_ context: inout GraphicsContext, size: CGSize) {
        context.fill(Path(CGRect(x: 0, y: 0, width: size.width, height: m.rowsTop)), with: .color(AureaTimeline.background))
        let steps = RulerSteps.of(pps: pps, fps: fps)
        let t0 = frame(0, width: size.width) / Double(fps)
        let t1 = frame(size.width, width: size.width) / Double(fps)
        var major = Path(), minor = Path()
        if t1 >= 0 {
            let first = max(0, Int(floor(max(0, t0) / steps.majorSeconds)))
            let last = max(first, Int(ceil(t1 / steps.majorSeconds)))
            for k in first...last {
                let seconds = Double(k) * steps.majorSeconds
                let px = x(seconds * Double(fps), width: size.width)
                major.move(to: CGPoint(x: px, y: m.tickMajorTop)); major.addLine(to: CGPoint(x: px, y: m.tickBottom))
                if !steps.frameMinors && steps.subdivisions > 1 {
                    for j in 1..<steps.subdivisions {
                        let sx = x((seconds + Double(j) * steps.majorSeconds / Double(steps.subdivisions)) * Double(fps), width: size.width)
                        if sx >= -1 && sx <= size.width + 1 {
                            minor.move(to: CGPoint(x: sx, y: m.tickMinorTop)); minor.addLine(to: CGPoint(x: sx, y: m.tickMinorBottom))
                        }
                    }
                }
                if steps.labels && px >= -40 && px <= size.width {
                    let label = Text(Timecode.rulerLabel(timelineFrame(seconds))).font(.aurea(size: 9, weight: .medium)).monospacedDigit().foregroundColor(AureaTimeline.rulerMajor)
                    context.draw(context.resolve(label), at: CGPoint(x: px + m.tickLabelGap, y: 0), anchor: .topLeading)
                }
            }
            if steps.frameMinors {
                let firstFrame = max(0, Int64(ceil(t0 * Double(fps))))
                let lastFrame = Int64(floor(t1 * Double(fps)))
                if lastFrame >= firstFrame {
                    for f in firstFrame...lastFrame {
                        let sec = Double(f) / Double(fps)
                        if abs(sec - (sec / steps.majorSeconds).rounded() * steps.majorSeconds) * Double(fps) >= 0.5 {
                            let px = x(Double(f), width: size.width)
                            minor.move(to: CGPoint(x: px, y: m.tickMinorTop)); minor.addLine(to: CGPoint(x: px, y: m.tickMinorBottom))
                        }
                    }
                }
            }
        }
        context.stroke(minor, with: .color(AureaTimeline.rulerMinor), lineWidth: m.tickMinorWidth)
        context.stroke(major, with: .color(AureaTimeline.rulerMajor), lineWidth: m.tickMajorWidth)
        for marker in markers {
            let px = x(Double(marker.frame), width: size.width), half = m.tickBottom * 0.28
            guard px >= -half && px <= size.width + half else { continue }
            let s = max(marker.kind == 1 ? half * 0.7 : half, 3.5)
            let color = Color(.sRGB, red: Double(marker.packedColor & 255) / 255,
                              green: Double((marker.packedColor >> 8) & 255) / 255,
                              blue: Double((marker.packedColor >> 16) & 255) / 255, opacity: 1)
            var path = Path(); path.move(to: CGPoint(x: px - s, y: 0)); path.addLine(to: CGPoint(x: px + s, y: 0))
            path.addLine(to: CGPoint(x: px, y: s * 1.4)); path.closeSubpath()
            context.fill(path, with: .color(color))
            var line = Path(); line.move(to: CGPoint(x: px, y: s * 1.4)); line.addLine(to: CGPoint(x: px, y: m.tickBottom))
            context.stroke(line, with: .color(color), lineWidth: marker.kind == 1 ? 1 : 1.5)
        }
        drawTimecode(&context, size: size)
    }


    private func drawPreviewBuffer(_ context: inout GraphicsContext, size: CGSize) {
        for range in model.previewBufferRanges {
            guard let span = TimelinePreviewBuffer.span(range, view: viewFrame, pxPerFrame: ppf, width: size.width) else { continue }
            let rect = CGRect(x: span.lowerBound, y: m.rulerTicks - TimelinePreviewBuffer.height,
                              width: span.upperBound - span.lowerBound, height: TimelinePreviewBuffer.height)
            context.fill(Path(rect), with: .color(AureaTimeline.previewBuffer))
        }
    }

    /// O relógio na faixa 30..60, centrado no cabeçote: sublinhado (traço de 2
    /// da largura do texto em y 52..54) ou numa caixa com borda no destaque.
    private func drawTimecode(_ context: inout GraphicsContext, size: CGSize) {
        let text = Timecode.format(Int32(clamping: self.clock.frame), fps)
        let cx = size.width / 2
        let clock = Text(text).font(.aurea(size: m.timecodeFont, weight: .bold)).monospacedDigit().foregroundColor(.white)
        let resolved = context.resolve(clock)
        let measured = resolved.measure(in: CGSize(width: size.width, height: m.timecodeBottom))
        let midY = (m.timecodeTop + m.underlineTop) / 2
        context.draw(resolved, at: CGPoint(x: cx, y: midY), anchor: .center)
        context.fill(Path(CGRect(x: cx - measured.width / 2, y: m.underlineTop, width: measured.width, height: m.underlineHeight)), with: .color(.white))
    }

    /// Fio branco de 1 pt, com a mesma origem temporal dos clipes e losangos.
    /// A faixa do relógio interrompe o fio; a seleção não muda sua posição.
    private func drawPlayhead(_ context: inout GraphicsContext, size: CGSize) {
        let cx = x(Double(clock.frame), width: size.width)
        let tint = AureaColors.playhead
        let top = min(m.playheadTop, size.height)
        context.fill(Path(CGRect(x: cx - m.playhead / 2, y: top, width: m.playhead, height: size.height - top)), with: .color(tint))
        context.fill(Path(CGRect(x: cx - m.playhead / 2, y: 0, width: m.playhead, height: m.rulerTicks)), with: .color(tint))
    }

    private func drawRows(_ context: inout GraphicsContext, size: CGSize) {
        var c = context
        c.clip(to: Path(CGRect(x: 0, y: m.rowsTop, width: size.width, height: max(0, size.height - m.rowsTop))))
        let visibleRows = rows
        // Topo de cada linha somado UMA vez: `rowTop(i)` refaz `rows` (que
        // compara todas as camadas e keyframes) — no laço, isso era quadrático.
        var tops: [CGFloat] = []
        tops.reserveCapacity(visibleRows.count + 1)
        var acc: CGFloat = 0
        for row in visibleRows { tops.append(acc); acc += rowHeight(row) }
        tops.append(acc)
        let preview = reorderSource >= 0 && !compact ? Reorder.preview(tops: tops, ids: timelineGroupKeys(visibleRows), source: reorderSource, target: reorderTarget, dragTop: reorderTop - m.rowsTop + scrollY) : nil
        var paintIndices = Array(visibleRows.indices)
        if let preview, preview.sourceStart >= 0 {
            paintIndices.removeSubrange(preview.sourceStart..<preview.sourceEnd)
            paintIndices.append(contentsOf: preview.sourceStart..<preview.sourceEnd)
            let y = m.rowsTop + preview.gapTop - scrollY
            c.fill(Path(CGRect(x: 0, y: y, width: size.width, height: m.reorderLine)), with: .color(AureaColors.accent))
        }
        for index in paintIndices {
            let row = visibleRows[index]
            let top = m.rowsTop + tops[index] - (compact ? 0 : scrollY) + (preview?.offsets[index] ?? 0)
            if top + rowHeight(row) < m.rowsTop || top > size.height { continue }
            if let preview, index >= preview.sourceStart && index < preview.sourceEnd {
                let rect = Path(CGRect(x: 0, y: top, width: size.width, height: rowHeight(row)))
                c.fill(rect, with: .color(AureaTimeline.background))
                c.fill(rect, with: .color(AureaColors.accent.opacity(0.14)))
            }
            if liftDrop.kind == .join && liftDrop.row == index {
                // A linha que recebe o trecho no ar acende inteira.
                c.fill(Path(CGRect(x: m.headerColumn, y: top, width: size.width - m.headerColumn, height: rowHeight(row))),
                       with: .color(AureaColors.accent.opacity(0.18)))
            }
            guard let shared = row.shared else {
                // O trecho no ar sai da fileira dele (é desenhado sob o dedo).
                if row.id != liftId { drawRow(&c, row: row, top: top, width: size.width) }
                continue
            }
            // FILEIRA COMPARTILHADA: cada trecho da linha no seu tempo, lado a
            // lado. Os escolhidos por cima — contorno e alças passam por cima do
            // vizinho encostado, que é onde o dedo os pega.
            var onScreen = false, offLeft = false, offRight = false
            for segment in shared {
                let x0 = x(Double(segment.start), width: size.width), x1 = max(x(Double(segment.end), width: size.width), x0 + m.barMinWidth)
                if x1 >= -m.barRadius && x0 <= size.width + m.barRadius { onScreen = true }
                else if x0 > size.width { offRight = true }
                else { offLeft = true }
            }
            for segment in shared where !model.selection.contains(segment.id) && segment.id != liftId { drawRow(&c, row: segment, top: top, width: size.width, arrows: false) }
            for segment in shared where model.selection.contains(segment.id) && segment.id != liftId { drawRow(&c, row: segment, top: top, width: size.width, arrows: false) }
            // Linha inteira fora da janela: UMA seta por lado (legenda nunca teve seta).
            if !onScreen && !model.captionTracks.contains(where: { $0.layer == shared[0].id }) {
                let tint = AureaTimeline.tone(row.type).stripe.opacity(row.visible ? 0.9 : 0.45)
                if offRight { drawEdgeArrow(&c, toRight: true, top: top, width: size.width, tint: tint) }
                if offLeft { drawEdgeArrow(&c, toRight: false, top: top, width: size.width, tint: tint) }
            }
        }
        // A PÍLULA de cada fileira, opaca, por cima das barras (elas passam por baixo).
        for index in paintIndices {
            let row = visibleRows[index]
            let rowTop = m.rowsTop + tops[index] - (compact ? 0 : scrollY) + (preview?.offsets[index] ?? 0)
            guard rowTop + rowHeight(row) >= m.rowsTop && rowTop <= size.height else { continue }
            if let track = row.track {
                // Trilha de GRUPO: ▸/▾ (tocar abre/fecha os eixos), mais claro; um eixo
                // sob o grupo aberto fica sem glifo (o recuo do nome diz de quem é).
                let cy = rowTop + timelineLaneHeight / 2
                if track.group {
                    let open = openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: track))
                    glyph(&c, open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 10, tint: AureaColors.text, x: m.laneChevronCx, y: cy)
                } else if let group = timelineTrackGroup(track), openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: group)) {
                    // eixo de um grupo aberto
                } else {
                    glyph(&c, CupertinoGlyph.ChevronRight, size: 10, tint: AureaTimeline.gutterEye, x: m.laneChevronCx, y: cy)
                }
                continue
            }
            drawPill(&c, row: row, rowTop: rowTop)
        }
        if guide != Snap.none {
            let gx = x(Double(guide), width: size.width)
            if gx >= m.headerColumn && gx <= size.width {
                c.fill(Path(CGRect(x: gx - m.guide / 2, y: m.rowsTop, width: m.guide, height: size.height - m.rowsTop)), with: .color(AureaColors.accent))
            }
        }
        // Trecho no ar (arrasto vertical de UM trecho): o traço de inserção entre
        // as fileiras e o trecho sob o dedo, por cima de tudo.
        if liftId != 0 && !compact {
            if liftDrop.kind == .insert && liftDrop.lineY.isFinite {
                let y = m.rowsTop + liftDrop.lineY - scrollY
                c.fill(Path(CGRect(x: 0, y: y - m.reorderLine / 2, width: size.width, height: m.reorderLine)), with: .color(AureaColors.accent))
                c.fill(Path(ellipseIn: CGRect(x: m.headerColumn - m.reorderDot, y: y - m.reorderDot, width: m.reorderDot * 2, height: m.reorderDot * 2)),
                       with: .color(AureaColors.accent))
            }
            if let lifted = visibleRows.lazy.compactMap({ $0.segment(liftId) }).first {
                let x0 = x(Double(lifted.start), width: size.width)
                let x1 = max(x(Double(lifted.end), width: size.width), x0 + m.barMinWidth)
                c.fill(Path(CGRect(x: x0, y: liftTop, width: x1 - x0, height: rowHeight(lifted))), with: .color(AureaColors.accent.opacity(0.22)))
                drawRow(&c, row: lifted, top: liftTop, width: size.width, arrows: false)
            }
        }
        // Retângulo da seleção de losangos (cantos presos ao conteúdo).
        if let box = boxRect {
            let xa = x(box.f0, width: size.width), xb = x(box.f1, width: size.width)
            let ya = m.rowsTop + box.y0 - scrollY, yb = m.rowsTop + box.y1 - scrollY
            let rect = CGRect(x: min(xa, xb), y: min(ya, yb), width: abs(xb - xa), height: abs(yb - ya))
            c.fill(Path(rect), with: .color(AureaColors.accent.opacity(0.14)))
            c.stroke(Path(rect), with: .color(AureaColors.accent), lineWidth: m.guide)
        }
    }

    /// A pílula colada à esquerda (0..78, 28 de alto, cantos 0 à esquerda e 14
    /// à direita): olho em x 16 (riscado se oculta), quadradinho 22 × 22 do tipo
    /// em x 32..54 e o cadeado pequeno em x 66. No lote ganha o destaque; com as
    /// trilhas abertas, o quadradinho ganha a borda no destaque.
    private func drawPill(_ context: inout GraphicsContext, row: TimelineRow, rowTop: CGFloat) {
        let top = rowTop + m.pillTop
        let rect = CGRect(x: 0, y: top, width: m.headerColumn, height: m.pillHeight)
        let shape = Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topRight, .bottomRight],
                                      cornerRadii: CGSize(width: m.pillRadius, height: m.pillRadius)).cgPath)
        let inBatch = model.selection.count >= 2 && row.segments.contains { model.selection.contains($0.id) }
        let open = row.segments.contains { expandedLayers.contains($0.id) }
        context.fill(shape, with: .color(AureaColors.editorRowPill))
        if inBatch {
            context.fill(shape, with: .color(AureaColors.accent.opacity(0.22)))
            context.stroke(shape, with: .color(AureaColors.accent), lineWidth: m.glyphBoxStroke)
        }
        let cy = top + m.pillHeight / 2
        glyph(&context, row.visible ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: m.eyeIcon,
              tint: row.visible ? AureaColors.text : AureaColors.muted, x: m.eyeCx, y: cy)
        let box = CGRect(x: m.glyphBoxLeft, y: cy - m.glyphBox / 2, width: m.glyphBox, height: m.glyphBox)
        var inner = context
        inner.opacity = row.visible ? 1 : AureaTimeline.hiddenAlpha + 0.2
        drawGlyphBox(&inner, row: row, box: box)
        if open {
            context.stroke(Path(roundedRect: box.insetBy(dx: m.glyphBoxStroke / 2, dy: m.glyphBoxStroke / 2), cornerRadius: m.glyphBoxRadius),
                           with: .color(AureaColors.accent), lineWidth: m.glyphBoxStroke)
        }
        if row.locked {
            glyph(&context, CupertinoGlyph.LockFill, size: m.gutterLock, tint: AureaColors.muted, x: m.lockCx, y: cy)
        }
    }

    /// O quadradinho do tipo: "T" no texto, ponto âmbar na forma, a miniatura
    /// (cortada para preencher) na imagem/vídeo, a nota no áudio, o glifo do tipo no resto.
    private func drawGlyphBox(_ context: inout GraphicsContext, row: TimelineRow, box: CGRect) {
        let shape = Path(roundedRect: box, cornerRadius: m.glyphBoxRadius)
        let center = CGPoint(x: box.midX, y: box.midY)
        let media: Bool = row.type == .video || row.type == .image
        if media, let image = pillThumbs[row.id], image.size.width > 0, image.size.height > 0 {
            var clipped = context
            clipped.clip(to: shape)
            let scale: CGFloat = max(box.width / image.size.width, box.height / image.size.height)
            let w: CGFloat = image.size.width * scale
            let h: CGFloat = image.size.height * scale
            clipped.draw(Image(uiImage: image), in: CGRect(x: center.x - w / 2, y: center.y - h / 2, width: w, height: h))
            return
        }
        let fill: Color = media ? AureaTimeline.tone(row.type).body : AureaColors.editorGlyphBox
        context.fill(shape, with: .color(fill))
        switch row.type {
        case .text:
            context.draw(Text("T").font(.aurea(size: m.glyphText, weight: .bold)).foregroundColor(AureaColors.text), at: center)
        case .shape:
            let r: CGFloat = m.shapeDot / 2
            context.fill(Path(ellipseIn: CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2)), with: .color(AureaTimeline.pillShapeDot))
        case .audio:
            glyph(&context, CupertinoGlyph.MusicNote, size: m.glyphIcon, tint: AureaTimeline.pillAudioNote, x: center.x, y: center.y)
        case .video, .image:
            glyph(&context, row.type.glyph, size: m.glyphIcon, tint: AureaTimeline.tone(row.type).text, x: center.x, y: center.y)
        default:
            glyph(&context, row.type.glyph, size: m.glyphIcon, tint: AureaColors.muted, x: center.x, y: center.y)
        }
    }

    /// Seta na borda: o clipe está para aquele lado (linha vazia parecia camada
    /// quebrada). `top` = topo da FILEIRA; a seta fica no meio da barra.
    private func drawEdgeArrow(_ context: inout GraphicsContext, toRight: Bool, top: CGFloat, width: CGFloat, tint: Color) {
        let tip = toRight ? width - 10 : m.headerColumn + 10, back = toRight ? tip - 7 : tip + 7
        let cy = top + m.barTop + m.bar / 2
        var arrow = Path(); arrow.move(to: CGPoint(x: tip, y: cy))
        arrow.addLine(to: CGPoint(x: back, y: cy - 6)); arrow.addLine(to: CGPoint(x: back, y: cy + 6)); arrow.closeSubpath()
        context.fill(arrow, with: .color(tint))
    }

    /// Um TRECHO (a fileira inteira, quando ela tem um só). `arrows` = falso na
    /// fileira compartilhada: quem decide a seta da borda é a fileira.
    /// `rowTop` = topo da FILEIRA; a barra começa `barTop` abaixo (2 sob a pílula).
    private func drawRow(_ context: inout GraphicsContext, row: TimelineRow, top rowTop: CGFloat, width: CGFloat, arrows: Bool = true) {
        if let track = row.track {
            // Trilha BAIXA (16 pt): nome e losangos na mesma faixa, centrados;
            // os losangos por cima do nome (par do TimelinePainter). O nome começa
            // em x 30, logo depois da coluna do ▸/▾ (não depois da pílula).
            let cy = rowTop + timelineLaneHeight / 2
            var line = Path(); line.move(to: CGPoint(x: m.laneLabelX, y: rowTop + timelineLaneHeight)); line.addLine(to: CGPoint(x: width, y: rowTop + timelineLaneHeight))
            context.stroke(line, with: .color(.white.opacity(0.06)), lineWidth: 1)
            let label = context.resolve(Text(row.name).font(.aurea(size: 11)).foregroundColor(track.group ? AureaColors.text : AureaColors.muted))
            var title = context
            title.clip(to: Path(CGRect(x: m.laneLabelX, y: rowTop, width: max(0, width - m.laneLabelX - 4), height: timelineLaneHeight)))
            title.draw(label, at: CGPoint(x: m.laneLabelX, y: cy), anchor: .leading)
            drawKeys(&context, row: row, top: cy - m.diamondCyNormal, width: width)
            return
        }
        let top = rowTop + m.barTop
        if let track = model.captionTracks.first(where: { $0.layer == row.id }) {
            for block in track.segments {
                let left = x(Double(block.start) + Double(row.start) - Double(row.offset), width: width)
                let right = x(Double(block.end) + Double(row.start) - Double(row.offset), width: width) - 2
                if right < 0 || left > width || right <= left { continue }
                let rect = CGRect(x: max(0, left), y: top, width: min(width, right) - max(0, left), height: m.bar)
                var clipped = context
                clipped.clip(to: Path(rect))
                let tone = AureaTimeline.tone(row.type)
                clipped.fill(Path(roundedRect: rect, cornerRadius: m.barRadius), with: .color(model.selection.contains(row.id) ? tone.stripe.opacity(0.7) : tone.body))
                clipped.draw(Text(block.text).font(.aurea(size: 11)).foregroundColor(tone.text), at: CGPoint(x: rect.minX + 5, y: top + 7), anchor: .topLeading)
            }
            return
        }
        let x0 = x(Double(row.start), width: width), x1 = max(x(Double(row.end), width: width), x0 + m.barMinWidth)
        let selected = model.selection.contains(row.id)
        let capped = compact && selected
        let visualLeft = capped ? TimelineHit.capLeft(m, x0) : x0
        if x1 >= -m.barRadius && visualLeft <= width + m.barRadius {
            let left = max(visualLeft, -m.capRadius * 2), right = min(x1, width + m.barRadius * 2)
            let bodyLeft = min(max(x0, left), right)
            let rect = CGRect(x: left, y: top, width: right - left, height: m.bar)
            // Fileira compacta, clipe escolhido: ponta esquerda bem redonda (raio 14).
            let shape = capped ? cappedShape(rect) : Path(roundedRect: rect, cornerRadius: m.barRadius)
            // Bloco no tom médio do tipo; camada oculta: a fileira inteira a 40 %.
            let tone = AureaTimeline.tone(row.type)
            var bar = context; bar.clip(to: shape)
            // A decoração externa não muda a origem de miniaturas, ondas ou keyframes.
            bar.clip(to: Path(CGRect(x: bodyLeft, y: top, width: right - bodyLeft, height: m.bar)))
            bar.opacity = row.visible ? 1 : AureaTimeline.hiddenAlpha
            bar.fill(shape, with: .color(tone.body))
            if row.track == nil, let tiles = thumbnails[row.id], !tiles.isEmpty {
                drawFilmstrip(&bar, row: row, tiles: tiles, top: top, x0: x0, x1: x1, width: width)
            }
            // Trilho dos losangos só quando a linha tem keyframe à vista.
            let keysShown = KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: false, selected: selected)
            if keysShown && !row.instants.isEmpty {
                bar.fill(Path(CGRect(x: left, y: top + m.trackTop, width: right - left, height: m.track)), with: .color(.black.opacity(0.22)))
            }
            if row.track == nil { drawWave(&bar, row: row, top: top, x0: x0, x1: x1, width: width, tone: tone) }
            // Faixa sólida de 3 pt na borda esquerda SÓ quando a camada tem etiqueta de cor.
            if row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
                bar.fill(Path(CGRect(x: x0, y: top, width: m.stripe, height: m.bar)), with: .color(AureaColors.labelPalette[Int(row.label) - 1]))
            }
            if capped {
                // A borda direita da tampa coincide com o início temporal do clipe.
                let capL = TimelineHit.capLeft(m, x0), capR = min(x0, right)
                if capR > capL {
                    var cap = context; cap.clip(to: shape)
                    cap.fill(Path(CGRect(x: capL, y: top, width: capR - capL, height: m.bar)), with: .color(AureaTimeline.clipSelected))
                    glyph(&cap, CupertinoGlyph.ChevronLeft, size: m.capGlyph, tint: Color(red: 0x16 / 255, green: 0x1C / 255, blue: 0x2A / 255), x: capL + m.capWidth / 2, y: top + m.bar / 2)
                }
            }
            // `arrows` falso = trecho de fileira compartilhada: o cadeado dele fica na barra.
            drawContent(&bar, row: row, top: top, x0: x0, x1: x1, width: width, tone: tone, segmentLock: !arrows)
            if capped {
                // Fileira compacta: contorno branco de 1,5 com a mesma ponta redonda.
                context.stroke(cappedShape(rect.insetBy(dx: m.capStroke / 2, dy: m.capStroke / 2)), with: .color(AureaTimeline.clipSelected), lineWidth: m.capStroke)
            } else if selected {
                let stroke = model.selection.count >= 2 ? m.multiStroke : m.selStroke
                context.stroke(Path(roundedRect: rect.insetBy(dx: stroke / 2, dy: stroke / 2), cornerRadius: m.barRadius - stroke / 2), with: .color(AureaTimeline.clipSelected), lineWidth: stroke)
            }
            if row.track == nil && model.selection.count == 1 && selected && !row.locked {
                // A tampa "‹" já é a alça branca da ponta esquerda (o dedo ali também apara).
                if !capped && x0 >= m.headerColumn { drawHandle(&context, left: x0 - m.trimInsetStart, top: top) }
                if x1 <= width { drawHandle(&context, left: x1 - m.trimInsetEnd, top: top) }
            }
        } else if row.track == nil && arrows {
            // Clipe fora da janela: seta na borda para o lado dele (par do
            // TimelinePainter) — linha vazia parecia camada quebrada.
            drawEdgeArrow(&context, toRight: x0 > width, top: rowTop, width: width, tint: AureaTimeline.tone(row.type).stripe.opacity(row.visible ? 0.9 : 0.45))
        }
        // "Keyframes de todas as camadas" desligado: só as escolhidas mostram os losangos.
        if KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: false, selected: model.selection.contains(row.id)) {
            drawKeys(&context, row: row, top: top, width: width)
        }
    }

    /// Tira de miniaturas da imagem/vídeo com divisórias finas; um véu leve à
    /// esquerda deixa o nome legível por cima da imagem.
    private func drawFilmstrip(_ context: inout GraphicsContext, row: TimelineRow, tiles: [MediaTile], top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat) {
        var dividers = Path()
        for tile in tiles {
            let px = x(Double(row.start) - Double(row.offset) + tile.localFrame, width: width)
            context.draw(Image(uiImage: tile.image), in: CGRect(x: px, y: top, width: tile.width, height: m.bar))
            if px > x0 + 1 { dividers.addRect(CGRect(x: px, y: top, width: m.lightLine, height: m.bar)) }
        }
        context.fill(dividers, with: .color(AureaTimeline.filmDivider))
        let veil = Gradient(stops: [.init(color: .black.opacity(0.35), location: 0), .init(color: .black.opacity(0), location: 0.5)])
        let barRect = CGRect(x: x0, y: top, width: max(1, x1 - x0), height: m.bar)
        context.fill(Path(barRect), with: .linearGradient(veil, startPoint: CGPoint(x: x0, y: top), endPoint: CGPoint(x: x1, y: top)))
    }

    /// Conteúdo do clipe: [‹] · nome (11 pt, 8 de recuo) · ◇ · [›] ou o ≡ escuro
    /// perto do fim, na tinta do tipo. O tipo e o cadeado moram na pílula.
    private func drawContent(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat, tone: ClipTone, segmentLock: Bool) {
        let barWidth = x1 - x0
        let cl = TimelineHit.contentLeft(m, x0, x1), cr = TimelineHit.contentRight(m, x0, x1, width)
        guard cr > cl else { return }
        // Sem losangos o conteúdo centra na barra; com eles, sobe para a faixa de cima.
        let cy = row.instants.isEmpty ? top + m.bar / 2 : top + m.trackTop / 2
        // Fileira compacta, trecho escolhido: tampa "‹" + nome 13 semibold + setas ‹ › à direita.
        if self.compact && model.selection.contains(row.id) {
            drawCappedContent(&context, row: row, cy: cy, x0: x0, x1: x1, width: width, cr: cr)
            return
        }
        var px = cl
        // Trecho travado dentro de uma fileira compartilhada: o cadeado fica na barra
        // (a pílula só trava quando a linha inteira trava).
        if row.locked && segmentLock && barWidth > m.iconMinBar {
            glyph(&context, CupertinoGlyph.LockFill, size: m.lockIcon, tint: tone.text, x: px + m.lockIcon / 2, y: cy)
            px += m.lockIcon + (barWidth > m.lockGapMinBar ? m.lockGap : 0)
        }
        let menuRight = x1 - (barWidth < m.narrowBar ? m.padRNarrow : m.padR)
        let right = barWidth > m.menuMinBar ? min(cr, menuRight - m.menuGlyph) : cr
        let avail = right - px
        if barWidth > m.nameMinBar && !row.name.isEmpty && avail > 8 {
            let name = fittedName(row.name, width: floor(avail / 12) * 12)
            let resolved = context.resolve(Text(name).font(.aurea(size: 11, weight: .medium)).tracking(aureaTracking(-0.1)).foregroundColor(tone.text))
            context.draw(resolved, at: CGPoint(x: px, y: cy), anchor: .leading)
        }
        if barWidth > m.menuMinBar && menuRight <= width + m.menuGlyph {
            glyph(&context, CupertinoGlyph.LineHorizontal3, size: m.menuGlyph, tint: AureaTimeline.barGrip, x: menuRight - m.menuGlyph / 2, y: top + m.bar / 2)
        }
    }

    /// Barra do clipe escolhido na fileira compacta: ponta esquerda de raio 14, direita a de sempre.
    private func cappedShape(_ rect: CGRect) -> Path {
        let l = min(m.capRadius, rect.height / 2), r = min(m.barRadius, rect.height / 2)
        var p = Path()
        p.move(to: CGPoint(x: rect.minX + l, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX - r, y: rect.minY))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.minY + r), radius: r, startAngle: .degrees(-90), endAngle: .degrees(0), clockwise: false)
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - r))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.maxY - r), radius: r, startAngle: .degrees(0), endAngle: .degrees(90), clockwise: false)
        p.addLine(to: CGPoint(x: rect.minX + l, y: rect.maxY))
        p.addArc(center: CGPoint(x: rect.minX + l, y: rect.maxY - l), radius: l, startAngle: .degrees(90), endAngle: .degrees(180), clockwise: false)
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + l))
        p.addArc(center: CGPoint(x: rect.minX + l, y: rect.minY + l), radius: l, startAngle: .degrees(180), endAngle: .degrees(270), clockwise: false)
        p.closeSubpath()
        return p
    }

    /// Conteúdo do clipe escolhido na fileira compacta (Efeitos.dc.html): depois
    /// da tampa "‹", o nome 13 pt semibold; as setas ‹ › de trocar de camada juntas
    /// na ponta direita (mesma geometria do `TimelineHit`).
    private func drawCappedContent(_ context: inout GraphicsContext, row: TimelineRow, cy: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat, cr: CGFloat) {
        let contentStart = max(x0, m.headerColumn)
        let next = TimelineHit.nextArrowLeft(m, x0, x1, width), prev = next - m.arrowSlot
        let arrows = prev >= contentStart
        if arrows {
            glyph(&context, CupertinoGlyph.ChevronLeft, size: m.arrowGlyph, tint: .white.opacity(0.7), x: prev + m.arrowSlot / 2, y: cy)
            glyph(&context, CupertinoGlyph.ChevronRight, size: m.arrowGlyph, tint: .white.opacity(0.7), x: next + m.arrowSlot / 2, y: cy)
        }
        let px = contentStart + m.capNameGap
        let avail = (arrows ? prev : cr) - px
        guard !row.name.isEmpty && avail > 8 else { return }
        let name = fittedName(row.name, width: floor(avail / 12) * 12, size: 13, weight: .semibold)
        let resolved = context.resolve(Text(name).font(.aurea(size: 13, weight: .semibold)).foregroundColor(.white))
        context.draw(resolved, at: CGPoint(x: px, y: cy), anchor: .leading)
    }

    private func drawHandle(_ context: inout GraphicsContext, left: CGFloat, top: CGFloat) {
        context.fill(Path(roundedRect: CGRect(x: left, y: top + m.trimTop, width: m.trimWidth, height: m.bar - m.trimTop * 2), cornerRadius: m.trimRadius), with: .color(AureaTimeline.clipSelected))
        context.fill(Path(CGRect(x: left + (m.trimWidth - m.gripWidth) / 2, y: top + (m.bar - m.gripHeight) / 2, width: m.gripWidth, height: m.gripHeight)), with: .color(.black.opacity(0.38)))
    }

    private func drawKeys(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, width: CGFloat) {
        guard !row.instants.isEmpty else { return }
        var groups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
        let count = Keyframes.visibleGroups(row.instants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width, margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &groups)
        let cy = top + (compact ? m.diamondCyCompact : m.diamondCyNormal)
        let selectedIndex = lowerBound(row.instants, selectedKey?.frame ?? Snap.none)
        let selectedTrackMatches = selectedIndex < row.instants.count && row.instants[selectedIndex] == selectedKey?.frame && row.keysAt[selectedIndex].contains { key in
            selectedKey?.track == TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
        }
        let chosen = selectedKey?.layer == row.id && selectedTrackMatches ? selectedKey?.frame ?? Snap.none : Snap.none
        // Seleção de keyframes da timeline: azul com anel branco (o principal segue âmbar).
        let picked: [Bool]? = pickedInstants(row)
        let dragMode: Bool = gesture?.mode == .key || gesture?.mode == .keys
        let dragTrackMatches = row.track == nil || gesture?.row?.track == nil || gesture?.row?.track == row.track
        let dragFrame = dragMode && gesture?.row?.id == row.id && dragTrackMatches ? gesture?.keyFrame ?? Snap.none : Snap.none
        for group in 0..<count {
            let i = Int(groups[group * 2]), j = Int(groups[group * 2 + 1])
            let px = x(Double(row.instants[i]), width: width)
            let on = Keyframes.groupHas(row.instants, i, j, chosen)
            var inSelection = false
            if let picked {
                for k in i...j where picked[k] { inSelection = true; break }
            }
            let fill: Color = on ? AureaTimeline.keyframeOn : (inSelection ? Color(hex: 0x4DA3FF) : Color.white.opacity(0.9))
            if i == j {
                drawKeyDiamond(&context, frame: row.instants[i], px: px, cy: cy, on: on, inSelection: inSelection, dragging: row.instants[i] == dragFrame)
            } else {
                let lastX = x(Double(row.instants[j]), width: width), pillWidth = max(lastX - px, m.keyPillMinWidth)
                let rect = CGRect(x: (px + lastX - pillWidth) / 2, y: cy - m.keyPillHeight / 2, width: pillWidth, height: m.keyPillHeight)
                context.fill(Path(roundedRect: rect, cornerRadius: m.keyPillHeight / 2), with: .color(inSelection ? Color(hex: 0x4DA3FF) : Color.white.opacity(0.9)))
                let border: Color = inSelection ? Color.white : Color.black.opacity(0.85)
                let borderWidth: CGFloat = inSelection ? 2 : m.diamondStroke
                context.stroke(Path(roundedRect: rect.insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.keyPillHeight / 2), with: .color(border), lineWidth: borderWidth)
                // A faixa resume chaves densas; as chaves em foco conservam seu centro temporal.
                let focus = [timelineFrame(Double(clock.frame)), chosen, dragFrame]
                for (position, frame) in focus.enumerated() {
                    guard !focus.prefix(position).contains(frame), Keyframes.groupHas(row.instants, i, j, frame) else { continue }
                    let index = lowerBound(row.instants, frame)
                    drawKeyDiamond(&context, frame: frame, px: x(Double(frame), width: width), cy: cy,
                                   on: frame == chosen, inSelection: picked?[index] == true, dragging: frame == dragFrame)
                }
            }
        }
    }

    private func drawKeyDiamond(_ context: inout GraphicsContext, frame: Int32, px: CGFloat, cy: CGFloat, on: Bool, inSelection: Bool, dragging: Bool) {
        let fill: Color = on ? AureaTimeline.keyframeOn : (inSelection ? Color(hex: 0x4DA3FF) : Color.white.opacity(0.9))
        let side = m.diamond * (dragging ? m.keyDragScale : 1)
        var diamond = context; diamond.translateBy(x: px, y: cy); diamond.rotate(by: .degrees(45))
        let glow = side / 2 * 1.3
        diamond.fill(Path(roundedRect: CGRect(x: -glow, y: -glow, width: glow * 2, height: glow * 2), cornerRadius: m.diamondRadius * 1.5), with: .color(on ? AureaTimeline.keyframeOn.opacity(0.35) : .black.opacity(0.3)))
        diamond.fill(Path(roundedRect: CGRect(x: -side / 2, y: -side / 2, width: side, height: side), cornerRadius: m.diamondRadius), with: .color(fill))
        diamond.stroke(Path(roundedRect: CGRect(x: -side / 2, y: -side / 2, width: side, height: side).insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.diamondRadius), with: .color(.black.opacity(0.85)), lineWidth: m.diamondStroke)
        if inSelection {
            // Anel branco POR FORA do contorno: escolhido se lê em qualquer fundo.
            let ring: CGFloat = 2
            let outer: CGFloat = side / 2 + ring / 2 + m.diamondStroke / 2
            let ringRect = CGRect(x: -outer, y: -outer, width: outer * 2, height: outer * 2)
            diamond.stroke(Path(roundedRect: ringRect, cornerRadius: m.diamondRadius + ring), with: .color(.white), lineWidth: ring)
        }
        if dragging {
            let text = context.resolve(Text(Timecode.format(frame, fps)).font(.aurea(size: 10, weight: .bold)).monospacedDigit().foregroundColor(.white))
            let measured = text.measure(in: CGSize(width: 180, height: 20))
            let rect = CGRect(x: px - measured.width / 2 - m.balloonPadH, y: cy - side * 1.4142135 / 2 - m.balloonGap - measured.height - m.balloonPadV * 2, width: measured.width + m.balloonPadH * 2, height: measured.height + m.balloonPadV * 2)
            context.fill(Path(roundedRect: rect, cornerRadius: m.balloonRadius), with: .color(.black.opacity(0.82)))
            context.draw(text, at: CGPoint(x: rect.midX, y: rect.midY))
        }
    }

    private func drawWave(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat, tone: ClipTone) {
        guard let wave = waves[row.id] else { return }
        let left = max(x0 + m.stripe, 0), right = min(x1, width)
        guard right > left else { return }
        let audio = row.type == .audio
        let areaTop = top + m.trackTop * (audio ? 0.62 : 0.8), bottom = top + m.bar - m.lightLine
        let mid = (areaTop + bottom) / 2, half = (bottom - areaTop) / 2
        let first = WaveGrid.bucketAt(frame(left, width: width), wave.fpb), last = WaveGrid.bucketAt(frame(right, width: width), wave.fpb)
        var path = Path()
        if last >= first {
            for bucket in first...last {
                let amplitude = CGFloat(wave.at(bucket)) / 255 * half
                let px = x((Double(bucket) + 0.5) * wave.fpb, width: width)
                guard amplitude >= 0.5 && px >= left && px <= right else { continue }
                path.move(to: CGPoint(x: px, y: mid - amplitude)); path.addLine(to: CGPoint(x: px, y: mid + amplitude))
            }
        }
        // Onda no tom médio do tipo (a do áudio mais forte que a do vídeo).
        context.stroke(path, with: .color(tone.wave.opacity(audio ? 0.9 : 0.6)), lineWidth: max(1, CGFloat(wave.fpb) * ppf * 0.72))
    }

    private func glyph(_ context: inout GraphicsContext, _ glyph: Character, size: CGFloat, tint: Color, x: CGFloat, y: CGFloat) {
        context.draw(Text(String(glyph)).font(CupertinoFont.font(size)).foregroundColor(tint), at: CGPoint(x: x, y: y))
    }
    private func fittedName(_ name: String, width: CGFloat, size: CGFloat = 11, weight: UIFont.Weight = .medium) -> String {
        let attributes: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: size, weight: weight), .kern: size == 11 ? -0.1 : 0]
        if (name as NSString).size(withAttributes: attributes).width <= width { return name }
        let letters = Array(name)
        var lo = 0, hi = letters.count
        while lo < hi {
            let n = (lo + hi + 1) / 2
            if ((String(letters.prefix(n)) + "…") as NSString).size(withAttributes: attributes).width <= width { lo = n } else { hi = n - 1 }
        }
        return String(letters.prefix(lo)) + "…"
    }

    private func refreshMedia(size: CGSize) {
        guard size.width > 0 && size.height > 0 else { return }
        thumbCache.beginFrame(6)
        let first = max(0, rowIndex(scrollY))
        let visible = rows.dropFirst(first).prefix(Int(size.height / 28) + 2)
        var next: [Int64: [MediaTile]] = [:], nextWaves: [Int64: TimelineWaveStrip.Entry] = [:]
        // Quadradinho da pílula: o 1º quadro do trecho (o mesmo balde e altura da
        // tira da barra, então divide o cache com ela).
        var nextPill: [Int64: UIImage] = [:]
        for row in visible where row.track == nil {
            let head = row.segments[0]
            guard head.hasThumbs else { continue }
            let bucket = head.type == .image ? -1 : Thumbs.bucketOf(Double(head.offset), fps)
            let request = head.type == .image ? head.start : Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), head.start, head.offset)
            if let image = thumbCache.get(model, layer: head.id, bucket: bucket, timelineFrame: request, heightPx: Int(m.bar), generation: model.status.thumbnailGeneration) {
                nextPill[row.id] = image
            } else if let old = pillThumbs[row.id] {
                nextPill[row.id] = old
            }
        }
        // Miniaturas e som são de cada TRECHO (a fileira compartilhada tem vários).
        for row in visible.flatMap({ $0.segments }) where row.track == nil {
            let x0 = x(Double(row.start), width: size.width), x1 = max(x(Double(row.end), width: size.width), x0 + m.barMinWidth)
            let left = max(x0, 0), right = min(x1, size.width)
            guard right > left else { continue }
            if row.hasThumbs {
                let tileWidth = m.bar * thumbCache.aspect(model, row.id)
                let origin = x(Double(row.start) - Double(row.offset), width: size.width)
                let start = max(0, Int(floor((left - origin) / tileWidth))), end = Int(floor((right - origin) / tileWidth))
                var tiles: [MediaTile] = []
                if end >= start {
                    for index in start...end {
                        let local = Double(CGFloat(index) * tileWidth / ppf)
                        let bucket = row.type == .image ? -1 : Thumbs.bucketOf(local, fps)
                        let request = row.type == .image ? row.start : Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), row.start, row.offset)
                        if let image = thumbCache.get(model, layer: row.id, bucket: bucket, timelineFrame: request, heightPx: Int(m.bar), generation: model.status.thumbnailGeneration) {
                            tiles.append(MediaTile(localFrame: local, width: tileWidth, image: image))
                        }
                    }
                }
                next[row.id] = tiles
            }
            if row.type == .audio || row.type == .video {
                let fpb = WaveGrid.framesPerBucket(targetPx: 1.5, pxPerFrame: ppf)
                let firstBucket = WaveGrid.bucketAt(frame(max(x0 + m.stripe, 0), width: size.width), fpb), lastBucket = WaveGrid.bucketAt(frame(right, width: size.width), fpb)
                nextWaves[row.id] = waveCache.get(model, layer: row.id, generation: model.status.modelRevision &+ model.status.thumbnailGeneration, fpb: fpb, first: firstBucket, last: lastBucket)
            }
        }
        thumbnails = next; waves = nextWaves; pillThumbs = nextPill
    }

    // Marker updates are independent of thumbnail work and its frame budget.
    private func refreshMarkers() {
        let markerKey = (model.status.modelRevision, model.markerFrames)
        if markerKey.0 != markersRevision || markerKey.1 != markersFrames {
            markersRevision = markerKey.0; markersFrames = markerKey.1
            let values = model.engine.markers()
            markers = stride(from: 0, to: values.count - values.count % 3, by: 3).map { Marker(frame: values[$0].int32Value, packedColor: values[$0 + 1].uint32Value, kind: values[$0 + 2].uint32Value) }
        }
    }

    // MARK: Android controller and hit priorities
    /// LINHA MAGNÉTICA é a faixa de montagem do vídeo: ela cresce para a forma
    /// de onda do som caber legível, que é o que se olha ao cortar. Os vizinhos
    /// acompanham, porque as posições saem de `rowTop`, não de um múltiplo fixo.
    private func rowHeight(_ row: TimelineRow) -> CGFloat {
        // Fileira compartilhada: a altura do trecho mais alto dela.
        if let shared = row.shared { return shared.reduce(0) { max($0, rowHeight($1)) } }
        if row.track != nil { return timelineLaneHeight }
        if row.magnetic, row.type == .video { return m.row * 1.9 }
        return m.row
    }
    private func rowTop(_ index: Int) -> CGFloat { rowTop(index, in: rows) }
    private func rowTop(_ index: Int, in list: [TimelineRow]) -> CGFloat { list.prefix(max(0, index)).reduce(0) { $0 + rowHeight($1) } }
    private func rowIndex(_ y: CGFloat) -> Int {
        guard y >= 0 else { return -1 }
        var bottom: CGFloat = 0
        for (index, row) in rows.enumerated() {
            bottom += rowHeight(row)
            if y < bottom { return index }
        }
        return rows.count
    }

    /// O trecho `id`, esteja ele sozinho na fileira ou dividindo a linha.
    private func segmentRow(_ id: Int64) -> TimelineRow? {
        for row in rows { if let segment = row.segment(id) { return segment } }
        return nil
    }
    /// Alças de aparar: só no trecho que é a ÚNICA escolha.
    private func handlesOn(_ row: TimelineRow) -> Bool {
        row.track == nil && model.selection.count == 1 && model.selection.contains(row.id) && !row.locked
    }
    /// Hit-test de UM trecho na fileira (a mesma geometria do pintor).
    private func hitSegment(_ row: TimelineRow, x px: CGFloat, y: CGFloat, width: CGFloat) -> TimelineHit {
        let x0 = x(Double(row.start), width: width), x1 = max(x(Double(row.end), width: width), x0 + m.barMinWidth)
        // Com 2+ camadas escolhidas o arrasto move o LOTE: o losango não pega o
        // dedo (par do Android `keysEnabled = !multi()`), exceto no modo de
        // escolher keyframes.
        let multi = model.selection.count >= 2 && !model.timelineKeySelectMode
        let keysShown: Bool = !multi && KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: row.track != nil, selected: model.selection.contains(row.id))
        let touchable: [Int32] = keysShown ? row.instants : []
        // Trilha: só a coluna do ▸/▾ (28) é cabeçalho; fileira: a pílula (70), com o olho em x < 28.
        let lane: Bool = row.track != nil
        return TimelineHit.test(m, point: CGPoint(x: px, y: y), width: width, x0: x0, x1: x1, handles: handlesOn(row), compact: compact && model.selection.contains(row.id), instants: touchable, view: viewFrame, ppf: ppf,
                                header: lane ? m.laneHeader : m.headerColumn, eyeRight: lane ? 0 : m.eyeHitRight)
    }
    /// O que o dedo pegou: o TRECHO (na pílula de uma fileira compartilhada, a
    /// fileira); `index` do resultado é a fileira inteira sob o dedo.
    private func hit(_ point: CGPoint, width: CGFloat) -> (TimelineRow?, TimelineHit) {
        if point.y < m.rowsTop { return (nil, TimelineHit(kind: .ruler)) }
        let index = rowIndex(point.y - m.rowsTop + (compact ? 0 : scrollY))
        let current = rows
        guard current.indices.contains(index) else { return (nil, TimelineHit(kind: .none)) }
        let row = current[index]
        let y = point.y - m.rowsTop - rowTop(index) + (compact ? 0 : scrollY)
        // y medido do topo da BARRA (a barra começa `barTop` abaixo do topo da fileira).
        let localY = row.track == nil ? y - m.barTop : m.diamondCyNormal
        if row.track != nil && !compact { return laneHit(current, index: index, x: point.x, contentY: point.y - m.rowsTop + scrollY, width: width) }
        guard let shared = row.shared else {
            var result = hitSegment(row, x: point.x, y: localY, width: width)
            if row.track != nil && result.kind != .key { result = TimelineHit(kind: .body) }
            result.index = index
            return (row, result)
        }
        // FILEIRA COMPARTILHADA: a pílula é da fileira; o resto é de UM trecho.
        if point.x < m.headerColumn {
            var result = hitSegment(shared[0], x: point.x, y: localY, width: width)
            result.index = index
            return (row, result)
        }
        // Prioridade (a mesma do Android): alça/losango do trecho escolhido >
        // o trecho sob o dedo (o escolhido, se as barras mínimas se cruzam;
        // senão o de cima no desenho) > qualquer folga de toque que responda.
        var best: (row: TimelineRow, hit: TimelineHit, score: Int)?
        for segment in shared {
            let result = hitSegment(segment, x: point.x, y: localY, width: width)
            if result.kind == .none { continue }
            let x0 = x(Double(segment.start), width: width), x1 = max(x(Double(segment.end), width: width), x0 + m.barMinWidth)
            let score: Int
            if handlesOn(segment) && (result.kind == .trimStart || result.kind == .trimEnd || result.kind == .key) { score = 4 }
            else if result.kind == .back && model.selection.contains(segment.id) { score = 4 }
            else if point.x >= x0 && point.x <= x1 { score = model.selection.contains(segment.id) ? 3 : 2 }
            else { score = 1 }
            let bestScore: Int = best?.score ?? 0
            if score > bestScore || (score == 2 && bestScore == 2) { best = (row: segment, hit: result, score: score) }
        }
        guard let chosen = best else { return (shared[0], TimelineHit(kind: .none, index: index)) }
        var result = chosen.hit
        result.index = index
        return (chosen.row, result)
    }
    /// Toque numa TRILHA baixa (par do `laneHit` do Android): o losango da trilha
    /// sob o dedo; senão o da vizinha mais perto (alvo ≥ 32 pt). Na calha, a
    /// trilha de GRUPO devolve `.header` (abre/fecha os eixos); o resto é `.body`.
    private func laneHit(_ list: [TimelineRow], index: Int, x px: CGFloat, contentY: CGFloat, width: CGFloat) -> (TimelineRow?, TimelineHit) {
        var tops: [CGFloat] = []
        tops.reserveCapacity(list.count + 1)
        var acc: CGFloat = 0
        for row in list { tops.append(acc); acc += rowHeight(row) }
        tops.append(acc)
        let order = TimelineLaneTouch.order(isLane: { list.indices.contains($0) && list[$0].track != nil }, tops: tops, index: index, y: contentY, reach: m.laneTouchReach)
        for j in order where list.indices.contains(j) {
            let lane = list[j]
            var result = hitSegment(lane, x: px, y: m.diamondCyNormal, width: width)
            if result.kind == .key || (result.kind == .header && lane.track?.group == true) {
                result.index = j
                return (lane, result)
            }
        }
        return (list[index], TimelineHit(kind: .body, index: index))
    }
    /// `origin`: onde o dedo pousou (nil = o próprio ponto, toque sintetizado).
    private func tap(_ point: CGPoint, width: CGFloat, origin: CGPoint? = nil) {
        // Toque que só PAROU a rolagem inércia não é toque (igual ao Android).
        let stoppedFling = abs(scrollVelocity) > 1
        scrollVelocity = 0
        if stoppedFling { return }
        let (row, touched) = hit(point, width: width)
        // Na fileira compacta, sair (tirar a seleção) só com toque de verdade: um
        // arrasto curto que o pan não chegou a pegar passa do slop do Android
        // (8) e não desmarca — arrastar na barra compacta só rola.
        if compact, let origin, hypot(point.x - origin.x, point.y - origin.y) > m.axisSlop,
           [TimelineHit.Kind.none, .body, .trimStart, .trimEnd].contains(touched.kind) {
            return
        }
        switch touched.kind {
        case .ruler:
            // Régua = buscar. Não cria marca: ela divide a faixa com o relógio
            // e o topo do cabeçote, e cada busca deixava uma marca "do nada".
            // Tocar em cima de uma marca (até 12 pt) abre o editor dela.
            let target = Int64(max(0, timelineFrame(frame(point.x, width: width))))
            let marker = model.markerNear(target, tolerance: Int64(12 / max(0.0001, ppf)))
            model.seek(toFrame: marker ?? target)
            if let marker { model.openMarkerEditor(marker) }
            UISelectionFeedbackGenerator().selectionChanged()
        case .none:
            selectedKey = nil; model.clearTimelineKeySelection()
            // Escolhendo várias camadas, errar o clipe não desfaz o lote (sai-se pelo "Concluir").
            if !model.timelineLayerSelectMode { model.editorBackFromTimeline() }
        case .eye:
            guard let row else { return }
            // Numa fileira compartilhada o olho vale para a LINHA toda, num passo de desfazer.
            let visible = !row.visible
            if row.shared != nil { model.engine.run { $0.beginUndoGroup() } }
            for segment in row.segments where segment.visible != visible || row.shared == nil {
                model.engine.setLayer(segment.id, visible: visible)
            }
            if row.shared != nil { model.engine.run { $0.endUndoGroup() } }
            model.refreshModel(force: true)
        case .previous, .next:
            guard let selected = model.primarySelection, let index = model.layers.firstIndex(where: { $0.id == selected }) else { return }
            let neighbor = index + (touched.kind == .previous ? 1 : -1)
            if model.layers.indices.contains(neighbor) { model.select(layerId: model.layers[neighbor].id, additive: false) }
        case .key:
            guard let row, row.keysAt.indices.contains(touched.key) else { return }
            // No modo de escolher camadas o losango do resumo é parte do clipe.
            if model.timelineLayerSelectMode && row.track == nil && !model.timelineKeySelectMode {
                model.toggleTimelineLayerPick(row.id)
                UISelectionFeedbackGenerator().selectionChanged()
                return
            }
            pause()
            let key = row.keysAt[touched.key][0]
            let track = TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
            if model.timelineKeySelectMode {
                // Modo "Selecionar": soma/tira (trilha: 1 keyframe; resumo: o instante
                // inteiro), sem buscar (a vista É o cabeçote) e sem abrir a curva.
                model.toggleTimelineKeys(row.id, row.keysAt[touched.key])
                if model.timelineKeySelection?.contains(key) == true {
                    selectedKey = (row.id, row.instants[touched.key], track)
                } else if let current = selectedKey, !primaryInKeySelection(current) {
                    // O principal saiu da seleção: fica sem principal (nenhum losango âmbar).
                    selectedKey = nil
                }
                UISelectionFeedbackGenerator().selectionChanged()
                return
            }
            model.select(layerId: row.id, additive: false)
            selectedKey = (row.id, row.instants[touched.key], track)
            model.seek(toFrame: Int64(row.instants[touched.key]))
            model.tapTimelineKey(row.id, key)
            if row.track?.group == true {
                // Trilha de grupo: todos os eixos daquele instante ficam escolhidos.
                model.timelineKeySelection = TimelineKeySelection(layer: row.id, keys: Set(row.keysAt[touched.key].map { TimelineKeyRef($0) }))
            }
            model.openCurve(property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)
        default:
            guard let row else { return }
            // Escolhendo keyframes, o corpo da camada da seleção (barra ou trilha) não
            // abre painel nem doca; a pílula ainda abre/fecha as trilhas.
            if touched.kind != .header && model.timelineKeySelectMode && model.timelineKeySelection?.layer == row.id { return }
            if touched.kind == .header, let track = row.track {
                // ▸/▾ da trilha de GRUPO: mostra/esconde as trilhas por eixo.
                if track.group {
                    let key = TimelineLaneGroupKey(layer: row.id, track: track)
                    if openGroups.contains(key) { openGroups.remove(key) } else { openGroups.insert(key) }
                    UISelectionFeedbackGenerator().selectionChanged()
                }
                return
            }
            if let track = row.track {
                model.select(layerId: row.id, additive: false)
                switch track.property {
                case 30: model.openPanel(.effects)
                case 31:
                    model.openPanel(.effects)
                    model.loadParams(layerId: row.id, effectId: track.effect)
                case 32: model.openPanel(.audio)
                case 33: model.openPanel(.textAnimation)
                case 34: model.openPanel(.vector)
                case 43:
                    model.openPanel(.mask)
                    model.selectedMask = track.effect; model.maskDrawing = false; model.selectedMaskPoint = nil
                    model.timelineFocus = [track]
                case 35: model.openPanel(.shape)
                case 36: model.openPanel(.particles)
                case 37: model.openPanel(.layer3D)
                case 39: model.openPanel(.speed)
                default: model.openPanel(.transform)
                }
                return
            }
            if touched.kind == .header && (!compact || compactByDock) {
                // Fileira compartilhada: abre as trilhas do trecho escolhido nela
                // (senão do primeiro); tocar de novo fecha, seja qual for o aberto.
                // Na fileira compacta da doca o ícone do tipo ABRE as trilhas (e a
                // timeline volta inteira) em vez de sair.
                // Cada fileira tem o seu ▸/▾: as outras camadas abertas continuam abertas.
                if !compact && row.segments.contains(where: { expandedLayers.contains($0.id) }) { expandedLayers.subtract(row.segments.map(\.id)) }
                else { expandedLayers.insert((row.segments.first { model.selection.contains($0.id) } ?? row.segments[0]).id) }
                UISelectionFeedbackGenerator().selectionChanged()
                return
            }
            let timelineOnly: Bool = !model.selection.isEmpty && model.selection == model.timelineOnlySelection
            switch timelineLayerTap(picking: model.timelineLayerSelectMode, compact: compact, selected: model.selection.count,
                                    tappedSelected: model.selection.contains(row.id), timelineOnly: timelineOnly) {
            case .leaveCompact: selectedKey = nil; model.editorBackFromTimeline()
            case .toggle:
                selectedKey = nil
                if model.timelineLayerSelectMode { model.toggleTimelineLayerPick(row.id) }
                else { model.select(layerId: row.id, additive: true) }
                UISelectionFeedbackGenerator().selectionChanged()
            case .replace: pause(); selectedKey = nil; model.select(layerId: row.id, additive: false)
            // Tocar de novo na escolhida a solta (a doca fecha: volta a barra de adicionar).
            case .deselect: selectedKey = nil; model.clearSelection()
            }
        }
    }

    private func pause() { if model.status.playing != 0 { model.playPause() } }
    /// Modo "Selecionar": arrastar no VAZIO (ou no fundo de uma trilha) desenha o
    /// retângulo e escolhe os losangos dentro; fora do modo, rola/scrub.
    private func boxStarts(_ row: TimelineRow?, _ touched: TimelineHit) -> Bool {
        model.timelineKeySelectMode && model.timelineKeySelection != nil && !compact
            && (touched.kind == .none || (touched.kind == .body && row?.track != nil))
    }
    private func targets(excluding: Set<Int64>, own: TimelineRow?, edges: Bool, keys: Bool) -> [Int32] {
        var result = [Int32(0)] + markers.map(\.frame)
        if !compact {
            // Cada TRECHO é um alvo (o vizinho na mesma fileira também).
            for row in rows.flatMap({ $0.segments }) where !excluding.contains(row.id) { result += [row.start, row.end] + row.instants }
        }
        if let own {
            if edges { result += [own.start, own.end] }
            if keys { result += own.instants }
        }
        return Snap.sortedDistinct(result)
    }
    private func begin(_ mode: Mode, start: CGPoint, width: CGFloat) {
        scrollVelocity = 0
        let (row, touched) = hit(start, width: width)
        if mode == .move, let row, !model.selection.contains(row.id) {
            model.select(layerId: row.id, additive: false, openOptions: false)
        }
        var selected = model.layers.filter { model.selection.contains($0.id) }
        if mode == .move, let row, !selected.contains(where: { $0.id == row.id }), let item = model.layers.first(where: { $0.id == row.id }) { selected = [item] }
        var next = Interaction(mode: mode, start: start, view: viewFrame, scroll: scrollY, row: row, hit: touched, selection: selected, snapTargets: [])
        if mode == .step { next.scroll = 0 }
        let excluded = Set(mode == .move ? selected.map(\.id) : row.map { [$0.id] } ?? [])
        next.snapTargets = targets(excluding: excluded, own: row, edges: mode == .key, keys: mode == .trimStart || mode == .trimEnd)
        // Reordenar na vertical leva a FILEIRA inteira (numa linha compartilhada,
        // todos os trechos): basta um trecho travado para recusar.
        let lane: TimelineRow? = rows.indices.contains(touched.index) ? rows[touched.index] : row
        let laneLocked = lane?.segments.contains(where: \.locked) ?? false
        if mode == .move && selected.contains(where: \.locked) || (mode == .key || mode == .trimStart || mode == .trimEnd || mode == .lift) && row?.locked == true || mode == .reorder && laneLocked {
            next.mode = .blocked
            haptics.warning()
        }
        // Losango ESCOLHIDO (lote de 2+ ou modo de escolha): a seleção inteira anda junta.
        if next.mode == .key, let row, row.instants.indices.contains(touched.key),
           let original = model.timelineKeySelection,
           original.count >= 2 || model.timelineKeySelectMode, original.containsAny(on: row.id, row.keysAt[touched.key]) {
            var peers: [Int64: [KeyframeItem]] = [:]
            for layer in original.layerIds {
                peers[layer] = (model.keyframes[layer] ?? []).filter { original.on(layer).contains(TimelineKeyRef($0)) }
                    .flatMap { model.graphKeyGroup(layer, $0) }
            }
            let keys = original.plusAll(peers)
            model.timelineKeySelection = keys
            next.mode = .keys
            next.keyIndex = touched.key
            next.grabFrame = row.instants[touched.key]
            next.keyFrame = next.grabFrame
            // Cada camada da seleção (pode abranger várias) fica dentro do próprio clipe.
            next.keyLimits = keys.shiftLimits { id in
                guard let clip = segmentRow(id) else { return nil }
                return (start: clip.start, end: clip.end, offset: clip.offset)
            }
        }
        if next.mode == .key, let row, row.instants.indices.contains(touched.key) {
            next.keyIndex = touched.key; next.keyFrame = row.instants[touched.key]
            var refs = Set<TimelineKeyRef>()
            next.movingKeys = row.keysAt[touched.key].flatMap { model.graphKeyGroup(row.id, $0) }.filter { key in
                refs.insert(TimelineKeyRef(property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)).inserted
            }
            let instants = row.instants.enumerated().filter { i, _ in row.keysAt[i].contains { candidate in
                next.movingKeys.contains { $0.property == candidate.property && $0.effectIndex == candidate.effectIndex && $0.paramIndex == candidate.paramIndex }
            } }.map { $0.element }
            next.keyLimits = Keyframes.dragLimits(instants, instants.firstIndex(of: next.keyFrame) ?? 0, start: row.start, end: row.end)
            if let key = next.movingKeys.first { selectedKey = (row.id, next.keyFrame, TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)) }
            // Re-selecting the same layer closed its dock and changed the
            // geometry beneath the active gesture.
            if model.selection != [row.id] { model.select(layerId: row.id, additive: false, openOptions: false) }
        }
        if next.mode == .reorder, let row {
            reorderSource = rows.indices.contains(touched.index) ? touched.index : (rows.firstIndex(where: { $0.id == row.id }) ?? -1)
            reorderTarget = reorderSource
            reorderTop = m.rowsTop + rowTop(max(0, reorderSource)) - scrollY
            reorderGrabOffset = start.y - reorderTop
            if touched.kind == .header, let lane {
                // Pela pílula: escolhe o primeiro trecho só se nenhum da fileira está escolhido.
                if !lane.segments.contains(where: { model.selection.contains($0.id) }) {
                    model.select(layerId: lane.segments[0].id, additive: false, openOptions: false)
                }
            } else if !model.selection.contains(row.id) {
                model.select(layerId: row.id, additive: false, openOptions: false)
            }
        }
        if next.mode == .lift, let row {
            // O trecho sai da fileira e segue o dedo; a linha dele fica.
            liftSource = rows.lastIndex(where: { $0.segment(row.id) != nil }) ?? touched.index
            liftId = row.id
            liftTop = m.rowsTop + rowTop(max(0, liftSource)) - scrollY
            liftGrabOffset = start.y - liftTop
            liftDrop = .none
            if !model.selection.contains(row.id) { model.select(layerId: row.id, additive: false, openOptions: false) }
        }
        if next.mode != .hold && next.mode != .blocked && next.mode != .scroll && next.mode != .step { pause() }
        // O dedo pegou algo para editar: um toque leve (par do Android `light()`).
        switch next.mode {
        case .move, .trimStart, .trimEnd, .key, .keys: haptics.light()
        case .reorder, .lift: haptics.heavy()
        default: break
        }
        if next.mode == .scrub {
            heldView = next.view; model.engine.run { $0.scrubBegin() }
        }
        if next.mode == .box {
            next.boxBase = model.timelineKeySelection
            let f0 = frame(start.x, width: width), y0 = start.y - m.rowsTop + scrollY
            boxRect = (f0, y0, f0, y0)
        }
        model.timelineKeyDragActive = next.mode == .key || next.mode == .keys
        gesture = next
    }

    private func pan(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, velocity: CGPoint, size: CGSize) {
        guard !pinching else { return }
        if state == .began {
            let (row, touched) = hit(start, width: size.width)
            let dx = point.x - start.x, dy = point.y - start.y
            // Lista inteira: a rolagem vertical ganha já em ~31° (scrollWins);
            // fileira compacta: o empate de 45° (o vertical ali troca de camada).
            let horizontal = compact ? TimelinePress.horizontal(dx, dy) : !TimelinePress.scrollWins(dx, dy)
            // Editar (losango, alça, mover) exige eixo claro, 2:1.
            let edit = TimelinePress.timeEdit(dx, dy)
            let mode: Mode
            if edit && touched.kind == .key { mode = .key }
            else if edit && touched.kind == .trimStart { mode = .trimStart }
            else if edit && touched.kind == .trimEnd { mode = .trimEnd }
            // Modo "Selecionar": arrastar no VAZIO (ou no fundo de uma trilha) desenha o
            // retângulo e escolhe os losangos dentro; fora do modo, rola/scrub.
            else if boxStarts(row, touched) { mode = .box }
            else { mode = horizontal ? .scrub : (compact ? .step : .scroll) }
            begin(mode, start: start, width: size.width)
        }
        if state == .began || state == .changed { lastPointer = point; pendingPointer = true }
        if state == .ended {
            update(point, size: size)
            let wasScroll = gesture?.mode == .scroll
            finish(cancelled: false)
            if wasScroll && abs(velocity.y) >= m.flingMin { scrollVelocity = -velocity.y }
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    private func hold(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, size: CGSize) {
        guard !pinching else { return }
        if state == .began {
            begin(.hold, start: start, width: size.width)
            // Só quando o dedo está sobre algo que o toque longo pega (clipe,
            // pílula, losango); no vazio e na régua não vibra (par do Android).
            if let g = gesture, g.row != nil, g.hit.kind != .none, g.hit.kind != .ruler { haptics.medium() }
        }
        if state == .changed, let g = gesture, g.mode == .hold {
            let dx = point.x - start.x, dy = point.y - start.y
            if hypot(dx, dy) >= m.axisSlop {
                let horizontal = compact ? TimelinePress.horizontal(dx, dy) : !TimelinePress.scrollWins(dx, dy)
                // Só um eixo CLARO edita: tempo 2:1 move, pilha 2:1 reordena; a diagonal só rola.
                let time = TimelinePress.timeEdit(dx, dy), stack = TimelinePress.stackEdit(dx, dy)
                let mode: Mode
                if boxStarts(g.row, g.hit) { mode = .box }
                else if (g.row?.track != nil && g.hit.kind != .key) || g.row == nil || g.hit.kind == .none || g.hit.kind == .ruler || g.hit.kind == .eye { mode = horizontal ? .scrub : (compact ? .step : .scroll) }
                else if g.hit.kind == .key { mode = time ? .key : (compact ? .step : .scroll) }
                else if g.hit.kind == .header { mode = !time && !compact ? .reorder : .blocked }
                else if time || compact { mode = .move }
                // Na pilha, o TRECHO sobe/desce sozinho (como no Alight Motion);
                // a linha inteira anda pela pílula.
                else { mode = stack ? .lift : .scroll }
                if mode == .move, let row = g.row, !model.selection.contains(row.id) {
                    model.select(layerId: row.id, additive: model.selection.count >= 2, openOptions: false)
                }
                begin(mode, start: start, width: size.width)
            }
        }
        if state == .began || state == .changed { lastPointer = point; pendingPointer = true }
        if state == .ended {
            update(point, size: size)
            if let g = gesture, g.mode == .hold, let row = g.row {
                if g.row?.track != nil {
                    tap(start, width: size.width)
                } else if g.hit.kind == .header {
                    // Segurar a pílula trava/destrava: a linha toda numa fileira compartilhada.
                    let locked = !row.locked
                    if row.shared != nil { model.engine.run { $0.beginUndoGroup() } }
                    for segment in row.segments where segment.locked != locked || row.shared == nil {
                        model.engine.setLayer(segment.id, locked: locked)
                    }
                    if row.shared != nil { model.engine.run { $0.endUndoGroup() } }
                    model.refreshModel(force: true)
                } else if g.hit.kind == .key { tap(start, width: size.width) }
                else if !compact && g.hit.kind != .eye && g.hit.kind != .none {
                    // Escolhendo várias camadas, segurar parado vale o mesmo que tocar.
                    if model.timelineLayerSelectMode { model.toggleTimelineLayerPick(row.id) }
                    else if model.selection.isEmpty { model.select(layerId: row.id, additive: false, openOptions: false) }
                    else if !(model.selection.count == 1 && model.selection.contains(row.id)) { model.select(layerId: row.id, additive: true, openOptions: false) }
                }
            }
            finish(cancelled: false)
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    private func update(_ point: CGPoint, size: CGSize) {
        guard var g = gesture else { return }
        let delta = frame(point.x, width: size.width) - TimeAxis.frameAt(x: g.start.x, view: g.view, pxPerFrame: ppf, centerX: size.width / 2)
        switch g.mode {
        case .scrub:
            let desired = g.view - Double((point.x - g.start.x) / ppf)
            holdView(desired)
        case .scroll:
            scrollY = min(maxScroll(size.height), max(0, g.scroll - (point.y - g.start.y)))
        case .step:
            // Fileira compacta: sem lista para rolar, o arrasto vertical troca de
            // camada como as setas ‹ › (dedo para cima = a de baixo). `scroll`
            // guarda quanto do arrasto já virou passo.
            let steps = TimelinePress.compactSteps(g.start.y - point.y - g.scroll, row: m.row)
            guard steps != 0 else { return }
            g.scroll += CGFloat(steps) * m.row
            gesture = g
            guard let selected = model.primarySelection, let index = model.layers.firstIndex(where: { $0.id == selected }) else { return }
            let neighbor = index + (steps > 0 ? 1 : -1)
            if model.layers.indices.contains(neighbor) {
                model.select(layerId: model.layers[neighbor].id, additive: false)
                haptics.tick()
            }
        case .lift:
            liftTop = point.y - liftGrabOffset
            guard let row = g.row else { return }
            let list = rows
            var tops: [CGFloat] = []
            tops.reserveCapacity(list.count + 1)
            var acc: CGFloat = 0
            for r in list { tops.append(acc); acc += rowHeight(r) }
            tops.append(acc)
            // Fora da timeline: soltar ali cancela.
            let outside = point.x < 0 || point.x > size.width || point.y < 0 || point.y > size.height
            let next = outside ? TimelineRowDrop.Target.none
                : TimelineRowDrop.target(rows: list, tops: tops, y: point.y - m.rowsTop + scrollY, moving: row, source: liftSource)
            if next != liftDrop {
                if next.kind != .none { haptics.tick() }
                liftDrop = next
            }
        case .reorder:
            reorderTop = point.y - reorderGrabOffset
            let target = min(max(0, rowIndex(point.y - m.rowsTop + scrollY)), max(0, rows.count - 1))
            if target != reorderTarget { reorderTarget = target; haptics.tick() }
        case .move:
            guard let row = g.row, let earliest = g.selection.map(\.startFrame).min(), let latest = g.selection.map(\.endFrame).max() else { return }
            // LINHA MAGNÉTICA: arrastar na horizontal REORDENA a fita em vez de
            // soltar o trecho no tempo — os vizinhos abrem espaço e a linha
            // volta a ficar encostada. Com a linha desligada vale o movimento
            // livre de sempre.
            if row.magnetic, g.selection.count == 1 {
                let desired = Double(row.start) + delta
                let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : Snap.none
                let target = Int64(max(0, snapped == Snap.none ? timelineFrame(desired) : snapped))
                if target != g.sentFrame {
                    openUndo(&g)
                    if model.reorderClip(row.id, toFrame: target) { g.sentFrame = target }
                }
                guide = snapped
                return
            }
            let desired = timelineFrame(Double(row.start) + delta)
            let snapped = model.snapping ? Snap.span(g.snapTargets, start: desired, length: row.end - row.start, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : (start: desired, guide: Snap.none)
            let change = Int32(clamping: max(-Int64(earliest), min(Int64(Int32.max) - Int64(latest), Int64(snapped.start) - Int64(row.start))))
            guide = Int64(row.start) + Int64(change) == Int64(snapped.start) ? snapped.guide : Snap.none
            if change != g.sentDelta {
                openUndo(&g)
                for item in g.selection {
                    model.engine.setLayer(item.id, startFrame: item.startFrame + change, endFrame: item.endFrame + change, offsetFrames: item.offsetFrames, setOffset: false)
                }
                g.sentDelta = change; model.refreshModel(force: true)
            }
        case .trimStart, .trimEnd:
            guard let row = g.row else { return }
            let origin = g.mode == .trimStart ? row.start : row.end
            let desired = Double(origin) + delta
            let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : Snap.none
            let target = max(0, snapped == Snap.none ? timelineFrame(desired) : snapped)
            let change = Int32(clamping: Int64(target) - Int64(origin))
            if change != g.sentDelta {
                openUndo(&g)
                if g.mode == .trimStart {
                    if model.editMode || row.magnetic, let current = model.layers.first(where: { $0.id == row.id }) {
                        model.trimStart(row.id, at: Int64(current.startFrame) + Int64(change) - Int64(g.sentDelta))
                        if let changed = model.layers.first(where: { $0.id == row.id }) {
                            g.sentDelta += current.duration - changed.duration
                        }
                    } else { model.trimStart(row.id, at: Int64(target)) }
                } else { model.trimEnd(row.id, at: Int64(target)) }
                if g.mode != .trimStart || !(model.editMode || row.magnetic) { g.sentDelta = change }
            }
            guide = snapped
        case .key:
            guard let row = g.row, row.instants.indices.contains(g.keyIndex) else { return }
            let origin = row.instants[g.keyIndex]
            let desired = Double(origin) + delta
            // O cabeçote no instante de origem (o toque no losango o levou até lá) não segura o arrasto.
            let magnet = KeyDrag.playheadMagnet(timelineFrame(viewFrame), origin: origin)
            let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: magnet, tol: Double(m.snapKey / ppf)) : Snap.none
            let quantized = KeyDrag.quantize(desired, current: g.keyFrame, pxPerFrame: ppf, hysteresisPx: m.keyDragHysteresis)
            let target = min(g.keyLimits.hi, max(g.keyLimits.lo, snapped == Snap.none ? quantized : snapped))
            if target != g.keyFrame {
                openUndo(&g)
                let source = row.toLocal(g.keyFrame), destination = row.toLocal(target)
                let references = g.movingKeys.flatMap { key in
                    [NSNumber(value: key.property), NSNumber(value: key.effectIndex), NSNumber(value: key.paramIndex), NSNumber(value: source)]
                }
                // Commit all axes together and only advance the drag source
                // after acceptance. A refused collision must not lose the key.
                guard model.engine.keyframeSelection(row.id, references: references, action: 1, delta: destination - source) > 0 else { guide = Snap.none; break }
                g.keyFrame = target
                haptics.tick() // um tique por frame que o losango anda
                if let key = g.movingKeys.first { selectedKey = (row.id, target, TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)) }
                // Fora do modo de escolha, a seleção da barra acompanha o losango movido.
                if !model.timelineKeySelectMode, model.timelineKeySelection != nil, let key = g.movingKeys.first {
                    var moved: KeyframeItem = key
                    moved.time = destination
                    model.tapTimelineKey(row.id, moved)
                }
                model.curveSelectedTime = destination
                model.refreshModel(force: true)
            }
            guide = snapped == target ? snapped : Snap.none
        case .keys:
            // Cada passo manda ao motor só o INCREMENTO desde o último aceito;
            // colisão recusada deixa a seleção onde estava.
            let desired: Double = Double(g.grabFrame) + delta
            let magnet: Int32 = KeyDrag.playheadMagnet(timelineFrame(viewFrame), origin: g.grabFrame)
            let snapped: Int32 = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: magnet, tol: Double(m.snapKey / ppf)) : Snap.none
            let target: Int32 = snapped == Snap.none ? KeyDrag.quantize(desired, current: g.keyFrame, pxPerFrame: ppf, hysteresisPx: m.keyDragHysteresis) : snapped
            let raw: Int64 = Int64(target) - Int64(g.grabFrame)
            let want: Int32 = Int32(clamping: min(Int64(g.keyLimits.hi), max(Int64(g.keyLimits.lo), raw)))
            if want != g.sentDelta {
                openUndo(&g)
                let step: Int32 = Int32(clamping: Int64(want) - Int64(g.sentDelta))
                // O losango principal acompanha, se ele faz parte da seleção.
                var primaryMoves = false
                if let current = selectedKey { primaryMoves = primaryInKeySelection(current) }
                if model.shiftTimelineKeys(step) {
                    if primaryMoves, let current = selectedKey {
                        let frame: Int32 = Int32(clamping: Int64(current.frame) + Int64(step))
                        selectedKey = (current.layer, frame, current.track)
                    }
                    g.sentDelta = want
                    g.keyFrame = Int32(clamping: Int64(g.grabFrame) + Int64(want))
                    haptics.tick()
                }
            }
            guide = snapped != Snap.none && snapped == g.keyFrame ? snapped : Snap.none
        case .box:
            // Cantos no CONTEÚDO: a auto-rolagem nas bordas estende o retângulo.
            guard let base = g.boxBase else { return }
            let f0 = TimeAxis.frameAt(x: g.start.x, view: g.view, pxPerFrame: ppf, centerX: size.width / 2)
            let y0 = g.start.y - m.rowsTop + g.scroll
            let f1 = frame(point.x, width: size.width), y1 = point.y - m.rowsTop + scrollY
            boxRect = (f0, y0, f1, y1)
            let list = rows
            var tops: [CGFloat] = []
            tops.reserveCapacity(list.count + 1)
            var acc: CGFloat = 0
            for row in list { tops.append(acc); acc += rowHeight(row) }
            tops.append(acc)
            let diamondCy = m.barTop + m.diamondCyNormal
            let showAll = model.showAllKeyframes, selection = model.selection
            let picked = TimelineBoxSelect.pick(rows: list, tops: tops, keyCy: { $0.track != nil ? timelineLaneHeight / 2 : diamondCy },
                frameLo: f0, frameHi: f1, yLo: y0, yHi: y1,
                keysVisible: { KeyframeVisibility.visible(showAll: showAll, isPropertyLane: $0.track != nil, selected: selection.contains($0.id)) })
            let next = base.plusAll(picked)
            if model.timelineKeySelectMode && next != model.timelineKeySelection { model.timelineKeySelection = next }
        case .hold, .blocked: break
        }
        gesture = g
    }
    private func openUndo(_ g: inout Interaction) {
        if !g.undoOpen { model.engine.run { $0.beginUndoGroup() }; g.undoOpen = true }
    }
    private func holdView(_ desired: Double) {
        let target = TimeAxis.clampView(desired, durationFrames: Int32(clamping: model.compositionDuration))
        let frame = Int64(timelineFrame(target))
        heldView = target
        model.engine.run { $0.scrub(toFrame: frame) }; model.optimisticPlayhead(frame)
    }
    private func finish(cancelled: Bool) {
        pendingPointer = false
        guard let g = gesture else { return }
        if g.mode == .reorder && !cancelled, g.row != nil, rows.indices.contains(reorderTarget), reorderTarget != reorderSource {
            commitReorder(source: reorderSource, target: reorderTarget)
        }
        if g.mode == .lift && !cancelled, let row = g.row, liftDrop.kind != .none,
           model.moveLayerToRow(row.id, anchor: liftDrop.anchor, mode: liftDrop.mode) {
            haptics.light()
            UIAccessibility.post(notification: .announcement,
                                 argument: AureaText.t(liftDrop.kind == .join ? "track2_row_joined" : "track2_row_moved"))
        }
        // VoiceOver: o arrasto só com gesto anuncia onde o keyframe ficou (par do Android).
        if !cancelled, g.mode == .key || g.mode == .keys, let row = g.row {
            let origin: Int32 = g.mode == .keys ? g.grabFrame : (row.instants.indices.contains(g.keyIndex) ? row.instants[g.keyIndex] : g.keyFrame)
            if g.keyFrame != origin {
                UIAccessibility.post(notification: .announcement, argument: Timecode.format(g.keyFrame, Float(model.compositionFps)))
            }
        }
        if g.mode == .scrub { model.engine.run { $0.scrubEnd() } }
        if autoScrubbing {
            model.engine.run { $0.scrubEnd() }
            if let held = heldView { model.optimisticPlayhead(Int64(timelineFrame(held))) }
            autoScrubbing = false
        }
        if g.undoOpen { model.engine.run { $0.endUndoGroup() } }
        model.timelineKeyDragActive = false
        gesture = nil; heldView = nil; guide = Snap.none; reorderSource = -1; reorderTarget = -1; boxRect = nil
        liftId = 0; liftSource = -1; liftDrop = .none
    }

    /// Solta a fileira `source` sobre a fileira `target` (o mesmo do Android):
    /// o grupo dela (a camada, ou a linha inteira) vai para logo acima/abaixo
    /// do grupo do destino, num passo de desfazer. Uma camada sozinha manda o
    /// MESMO comando de sempre.
    private func commitReorder(source: Int, target: Int) {
        let list = rows
        let keys = timelineGroupKeys(list)
        guard keys.indices.contains(source), keys.indices.contains(target), keys[source] != keys[target] else { return }
        var block = Set<Int64>(), anchors = Set<Int64>()
        for (i, row) in list.enumerated() where row.track == nil {
            if keys[i] == keys[source] { for s in row.segments { block.insert(s.id) } }
            else if keys[i] == keys[target] { for s in row.segments { anchors.insert(s.id) } }
        }
        let order = model.layers.map(\.id)
        // A fileira do destino está onde está a camada MAIS ALTA dela.
        guard let anchor = order.first(where: { anchors.contains($0) }) else { return }
        let moves = TimelineRowOrder.moves(order: order, block: block, anchor: anchor, up: target < source)
        guard !moves.isEmpty else { return }
        let count = order.count
        model.engine.run { $0.beginUndoGroup() }
        for move in moves {
            model.engine.run { $0.setLayerOrder(move.id, newIndex: UInt32(count - 1 - move.index)) }
        }
        model.engine.run { $0.endUndoGroup() }
        haptics.light()
        model.refreshModel(force: true)
    }

    /// PINÇA (par do Android): âncora no ponto entre os dedos. TOCANDO, não
    /// pausa e não fala com o motor — o zoom fica em volta do cabeçote e o play
    /// segue. Parado, a âncora move a janela e o motor faz scrub LEVE (sem
    /// republicar o status a cada passo; só no fim).
    private func pinch(_ state: UIGestureRecognizer.State, scale: CGFloat, focus: CGPoint, width: CGFloat) {
        if state == .began {
            finish(cancelled: true); scrollVelocity = 0; pinching = true
            pinchPlaying = model.status.playing != 0
            pinchPPS = pps; pinchFrame = frame(focus.x, width: width)
            if !pinchPlaying { model.engine.run { $0.scrubBegin() } }
            haptics.tick()
        }
        if state == .began || state == .changed {
            let next = Zoom.clamp(pinchPPS * scale)
            if abs(next - pps) > 0.001 { pps = next }
            if !pinchPlaying {
                let target = TimeAxis.clampView(Zoom.anchoredView(focusFrame: pinchFrame, focusX: focus.x, centerX: width / 2, pxPerFrame: ppf),
                                                durationFrames: Int32(clamping: model.compositionDuration))
                heldView = target
                model.timelineScrub(Int64(timelineFrame(target)))
            }
        }
        if state == .ended || state == .cancelled || state == .failed { finishPinch() }
    }
    private func finishPinch() {
        guard pinching else { return }
        if !pinchPlaying {
            model.engine.run { $0.scrubEnd() }
            if let held = heldView { model.optimisticPlayhead(Int64(timelineFrame(held))) }
        }
        pinching = false; pinchPlaying = false; heldView = nil
    }
    private func tick(size: CGSize) {
        let now = ProcessInfo.processInfo.systemUptime
        let dt = CGFloat(lastTick > 0 ? min(0.05, max(0, now - lastTick)) : 1.0 / 60)
        lastTick = now
        // Touch delivery can exceed display cadence. Apply only the newest
        // sample each tick and always flush the release position in pan/hold.
        if pendingPointer {
            pendingPointer = false
            update(lastPointer, size: size)
        }
        if let g = gesture {
            // Auto-rolagem em RAMPA (par do Android): faixa min(48, 1/3 da janela),
            // velocidade cresce com o quanto o dedo entrou nela, até 360 pt/s.
            var moved = false
            if g.mode == .move || g.mode == .key || g.mode == .keys || g.mode == .trimStart || g.mode == .trimEnd || g.mode == .box {
                let zone = AutoScroll.zone(maxZone: m.autoEdge, viewport: size.width - m.headerColumn)
                let factor = AutoScroll.speed(pos: lastPointer.x, from: g.start.x, start: m.headerColumn, end: size.width, zone: zone, intent: m.autoIntent)
                if factor != 0 {
                    // Auto-scroll moves the presentation window; the editing gesture
                    // reapplies its absolute target and the core remains authoritative.
                    let target = TimeAxis.clampView(viewFrame + Double(AutoScroll.step(factor: factor, maxSpeed: m.autoSpeed, dt: dt) / ppf), durationFrames: Int32(clamping: model.compositionDuration))
                    // Scrub, não seek: o seek republicava o status e relia os
                    // painéis 60 vezes por segundo (a borda engasgava).
                    if !autoScrubbing { model.engine.run { $0.scrubBegin() }; autoScrubbing = true }
                    heldView = target; model.timelineScrub(Int64(timelineFrame(target)))
                    moved = true
                }
            }
            if (g.mode == .reorder || g.mode == .box || g.mode == .lift) && !compact {
                let zone = AutoScroll.zone(maxZone: m.autoEdge, viewport: size.height - m.rowsTop)
                let factor = AutoScroll.speed(pos: lastPointer.y, from: g.start.y, start: m.rowsTop, end: size.height, zone: zone, intent: m.autoIntent)
                if factor != 0 {
                    scrollY = min(maxScroll(size.height), max(0, scrollY + AutoScroll.step(factor: factor, maxSpeed: m.autoSpeed, dt: dt)))
                    moved = true
                }
            }
            if moved { update(lastPointer, size: size) }
        } else if abs(scrollVelocity) > 1 {
            let next = min(maxScroll(size.height), max(0, scrollY + scrollVelocity * dt))
            // Inércia com a desaceleração do UIScrollView (0,998 por ms): a lista
            // desliza como qualquer lista do iOS; 0,94 por quadro parava seco.
            if next == scrollY { scrollVelocity = 0 } else {
                scrollY = next; scrollVelocity *= CGFloat(pow(0.998, Double(dt) * 1000))
                if abs(scrollVelocity) < 12 { scrollVelocity = 0 }
            }
        }
        if mediaNeedsRefresh || thumbCache.starved {
            mediaNeedsRefresh = false
            refreshMedia(size: size)
        }
    }
    private func revealSelection(size: CGSize) {
        guard !compact, gesture == nil, let id = model.primarySelection, let index = rows.firstIndex(where: { $0.segment(id) != nil }) else { return }
        let top = rowTop(index), bottom = top + rowHeight(rows[index])
        let visibleHeight = max(0, size.height - m.rowsTop)
        if top < scrollY { scrollY = top }
        else if bottom > scrollY + visibleHeight { scrollY = min(maxScroll(size.height), max(0, bottom - visibleHeight)) }
    }
}

/// The same priority and geometry as Android RowHit; drawing and hit testing
/// share TimelineMetrics so a handle cannot be grabbed beneath the header.
private struct TimelineHit {
    enum Kind { case none, ruler, eye, header, key, trimStart, trimEnd, previous, next, back, body }
    var kind: Kind
    var key = -1
    /// A fileira sob o dedo (numa fileira compartilhada, a linha inteira); −1 = nenhuma.
    var index = -1
    static func contentLeft(_ m: TimelineMetrics, _ x0: CGFloat, _ x1: CGFloat) -> CGFloat {
        max(x0, m.headerColumn) + (x1 - x0 < m.narrowBar ? m.padLNarrow : m.padL)
    }
    static func contentRight(_ m: TimelineMetrics, _ x0: CGFloat, _ x1: CGFloat, _ width: CGFloat) -> CGFloat {
        min(x1, width) - (x1 - x0 < m.narrowBar ? m.padRNarrow : m.padR)
    }
    /// A tampa fica antes da borda temporal real; o scroll nunca a prende ao cabeçalho.
    static func capLeft(_ m: TimelineMetrics, _ x0: CGFloat) -> CGFloat { x0 - m.capWidth }
    /// Onde a seta › começa; a ‹ fica logo antes dela (as duas juntas na ponta direita).
    static func nextArrowLeft(_ m: TimelineMetrics, _ x0: CGFloat, _ x1: CGFloat, _ width: CGFloat) -> CGFloat {
        contentRight(m, x0, x1, width) - m.arrowSlot
    }
    /// `point.y` medido do topo da BARRA (a fileira vai de −barTop a row − barTop).
    /// `header`: largura da pílula (fileira) ou da coluna do ▸/▾ (trilha); `eyeRight`:
    /// o olho ocupa x < eyeRight (0 = sem olho).
    static func test(_ m: TimelineMetrics, point: CGPoint, width: CGFloat, x0: CGFloat, x1: CGFloat, handles: Bool, compact: Bool, instants: [Int32], view: Double, ppf: CGFloat,
                     header: CGFloat, eyeRight: CGFloat) -> TimelineHit {
        let x = point.x, y = point.y
        guard y >= -m.barTop && y < m.row - m.barTop else { return TimelineHit(kind: .none) }
        // Pílula: x < 28 é o olho (mostra/esconde); o resto é o cabeçalho (tocar
        // abre/fecha as trilhas, segurar trava/reordena).
        if x < header { return TimelineHit(kind: x < eyeRight ? .eye : .header) }
        // Dois anéis (par do TimelineHit.kt): o NÚCLEO de 28 ganha de tudo; a
        // FOLGA até 48 pt ganha do corpo e do vazio, mas cede às alças, à tampa
        // e às setas — beta "difícil mover o keyframe".
        var key = -1, keyWide = -1
        let keyCy = compact ? m.diamondCyCompact : m.diamondCyNormal
        if abs(y - keyCy) <= m.keyTouchHalf && !instants.isEmpty {
            let i = Keyframes.nearestIndex(instants, TimeAxis.frameAt(x: x, view: view, pxPerFrame: ppf, centerX: width / 2))
            let px = TimeAxis.xOf(frame: Double(instants[i]), view: view, pxPerFrame: ppf, centerX: width / 2)
            let dx = abs(px - x)
            if dx <= m.keyTouchHalf { key = i } else if dx <= m.keyHitHalf { keyWide = i }
        }
        let over = y < m.bodyHitBottom, mid = (x0 + x1) / 2
        let start = handles && over && x0 >= header && x >= x0 - m.trimInsetStart - m.trimTouchOut && x < min(x0 - m.trimInsetStart + m.trimWidth, mid)
        let end = handles && over && x1 <= width && x > max(x1 - m.trimInsetEnd, mid) && x <= x1 - m.trimInsetEnd + m.trimWidth + m.trimTouchOut
        if key >= 0 { return TimelineHit(kind: .key, key: key) }
        if start { return TimelineHit(kind: .trimStart) }
        if end { return TimelineHit(kind: .trimEnd) }
        if compact && over && x >= capLeft(m, x0) && x < x0 { return TimelineHit(kind: .back) }
        if over && x >= x0 && x <= x1 && compact {
            // Redesenho 2026-09-29: a tampa branca "‹" na ponta esquerda volta
            // (sai da seção); as setas de trocar de camada ‹ › moram juntas na
            // ponta direita (par do TimelineHit.kt).
            let next = nextArrowLeft(m, x0, x1, width), prev = next - m.arrowSlot
            if prev >= max(x0, m.headerColumn) {
                if x >= next && x <= next + m.arrowSlot + m.arrowTouchPad { return TimelineHit(kind: .next) }
                if x >= prev - m.arrowTouchPad && x < next { return TimelineHit(kind: .previous) }
            }
        }
        if keyWide >= 0 { return TimelineHit(kind: .key, key: keyWide) }
        if over && x >= x0 && x <= x1 { return TimelineHit(kind: .body) }
        return TimelineHit(kind: .none)
    }
}

/// Vibrações da timeline com os geradores PREPARADOS uma vez (criar um por
/// toque atrasava a primeira vibração e às vezes a perdia). `snap` = o encaixe
/// num alvo (borda, cabeçote, keyframe, marca) ao mover, aparar ou arrastar
/// losango — o tique do Android `setGuide`.
@MainActor
final class TimelineHaptics {
    static let shared = TimelineHaptics()
    private let selection = UISelectionFeedbackGenerator()
    private let lightImpact = UIImpactFeedbackGenerator(style: .light)
    private let mediumImpact = UIImpactFeedbackGenerator(style: .medium)
    private let heavyImpact = UIImpactFeedbackGenerator(style: .heavy)
    private let notice = UINotificationFeedbackGenerator()
    func prepare() { selection.prepare(); lightImpact.prepare() }
    func snap() { selection.selectionChanged(); selection.prepare() }
    func tick() { selection.selectionChanged(); selection.prepare() }
    func light() { lightImpact.impactOccurred(); lightImpact.prepare() }
    func medium() { mediumImpact.impactOccurred() }
    func heavy() { heavyImpact.impactOccurred() }
    func warning() { notice.notificationOccurred(.warning) }
}

/// Zoom e rolagem da timeline por projeto: sobrevivem à timeline sumir e
/// voltar (o layout de celular recria a vista quando a doca/painel abre).
@MainActor
enum TimelineViewMemory {
    static var state: [String: (pps: CGFloat, scrollY: CGFloat)] = [:]
}

/// UIKit only arbitrates touch ownership. All coordinates remain in the same
/// point space as Canvas; it neither stores nor edits the project.
@MainActor
private struct TimelineGestureSurface: UIViewRepresentable {
    /// (onde soltou, onde pousou)
    var tap: (CGPoint, CGPoint) -> Void
    var pan: (UIGestureRecognizer.State, CGPoint, CGPoint, CGPoint) -> Void
    var hold: (UIGestureRecognizer.State, CGPoint, CGPoint) -> Void
    var pinch: (UIGestureRecognizer.State, CGFloat, CGPoint) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UIView {
        let view = UIView(); view.backgroundColor = .clear; view.isMultipleTouchEnabled = true
        #if DEBUG
        if ProcessInfo.processInfo.environment["AUREA_UI_TEST_PROBE"] == "1" {
            view.isAccessibilityElement = true
            view.accessibilityIdentifier = "aurea.parity.timeline"
            view.accessibilityLabel = AureaText.t("i18n_timeline_surface")
        }
        #endif
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tapped(_:)))
        let pan = UIPanGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.panned(_:)))
        pan.maximumNumberOfTouches = 1
        let hold = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.held(_:)))
        // Toque longo só com o dedo QUIETO: um dedo que rasteja 4 pt em 500 ms está
        // começando a rolar — o hold falha e o pan (que espera por ele) assume.
        hold.minimumPressDuration = 0.5; hold.allowableMovement = 4
        let pinch = UIPinchGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.pinched(_:)))
        pan.require(toFail: hold); tap.require(toFail: pan); tap.require(toFail: hold)
        let recognizers: [UIGestureRecognizer] = [tap, pan, hold, pinch]
        for recognizer in recognizers { recognizer.delegate = context.coordinator; view.addGestureRecognizer(recognizer) }
        return view
    }
    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.parent = self }
    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var parent: TimelineGestureSurface
        private var holdStart = CGPoint.zero
        private var tapStart = CGPoint.zero
        private var panStart = CGPoint.zero
        init(_ parent: TimelineGestureSurface) { self.parent = parent }
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
            gestureRecognizer is UIPinchGestureRecognizer || otherGestureRecognizer is UIPinchGestureRecognizer
        }
        /// Onde o dedo pousou: o toque que andou além do slop não é toque.
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldReceive touch: UITouch) -> Bool {
            if gestureRecognizer is UITapGestureRecognizer { tapStart = touch.location(in: gestureRecognizer.view) }
            if gestureRecognizer is UIPanGestureRecognizer { panStart = touch.location(in: gestureRecognizer.view) }
            if gestureRecognizer is UILongPressGestureRecognizer { holdStart = touch.location(in: gestureRecognizer.view) }
            return true
        }
        @objc func tapped(_ sender: UITapGestureRecognizer) {
            if sender.state == .ended { parent.tap(sender.location(in: sender.view), tapStart) }
        }
        @objc func panned(_ sender: UIPanGestureRecognizer) {
            let point = sender.location(in: sender.view)
            parent.pan(sender.state, panStart, point, sender.velocity(in: sender.view))
        }
        @objc func held(_ sender: UILongPressGestureRecognizer) {
            let point = sender.location(in: sender.view)
            parent.hold(sender.state, holdStart, point)
        }
        @objc func pinched(_ sender: UIPinchGestureRecognizer) { parent.pinch(sender.state, sender.scale, sender.location(in: sender.view)) }
    }
}
