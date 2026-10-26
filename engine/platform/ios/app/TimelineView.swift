// Direct port of Android TimelinePainter, TimelineHit and TimelineController.
// The core remains the only owner of layers, animation tracks and media.
import SwiftUI
import UIKit
import Combine

/// 0.3.3 recursos: a cor de uma marca com a correção da área 03 (`MarkerColors.display`:
/// R no byte baixo; as marcas gravadas pela 0.3.2 com literais ARGB crus aparecem certas).
private func markerTint(_ packed: UInt32) -> Color {
    let argb = MarkerColors.display(packed)
    return Color(.sRGB, red: Double((argb >> 16) & 255) / 255, green: Double((argb >> 8) & 255) / 255,
                 blue: Double(argb & 255) / 255, opacity: 1)
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
    // 0.3.3 recursos: o marcador antigo (a paleta de seis cores da 0.3.2) volta, com a
    // correção da área 03: o motor guarda RGBA com o R no byte BAIXO (Marker::color) e
    // `MarkerColors.pack` grava a cor certa; as marcas da 0.3.2 aparecem certas (`display`).
    private let palette: [UInt32] = MarkerColors.legacy.map { MarkerColors.pack($0) }
    init(frame: Int64, color: UInt32, label: String, isNew: Bool = false) {
        original = frame
        self.isNew = isNew
        _name = State(initialValue: label)
        _frameText = State(initialValue: String(frame))
        // Marca nova: a cor padrão do motor (o amarelo da 0.3.2; par do Android).
        _color = State(initialValue: isNew ? MarkerColors.engineDefault : color)
    }
    var body: some View {
        NavigationView {
            Form {
                TextField(AureaText.t("marker_name"), text: $name, prompt: Text(AureaText.t("marker_name_hint")))
                    .onChange(of: name) { value in if value.count > 200 { name = String(value.prefix(200)) } }
                TextField(AureaText.t("marker_frame"), text: $frameText).keyboardType(.numberPad)
                Text(Timecode.format(Int32(clamping: max(0, Int64(frameText) ?? 0)), Float(model.compositionFps)))
                Button(AureaText.t("marker_at_playhead")) { frameText = String(model.status.playhead) }
                HStack(spacing: 0) {
                    let chosen = MarkerColors.legacyIndex(color)
                    ForEach(Array(palette.enumerated()), id: \.offset) { index, packed in
                        Button { color = packed } label: {
                            Circle().fill(markerTint(packed)).frame(width: chosen == index ? 32 : 22, height: chosen == index ? 32 : 22)
                                .frame(maxWidth: .infinity).frame(height: 44)
                        }.buttonStyle(.borderless).accessibilityLabel(AureaText.t("marker_color", index + 1))
                            .accessibilityIdentifier("marker.color.\(index)")
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
    /// Arrasto vertical de UM trecho (como na referência): o trecho no ar (0 = nenhum),
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
    /// Omega (área 03, D-1): moram no modelo — o ⋮ da camada abre e fecha.
    private var expandedLayers: Set<Int64> {
        get { model.timelineExpandedLayers }
        nonmutating set { model.timelineExpandedLayers = newValue }
    }
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
    /// Rolagem da lista inteira guardada enquanto a fileira compacta está à vista
    /// (`TimelineCompactScroll`): sair do compacto volta para ela.
    @State private var compactSavedScroll: CGFloat?
    /// Fade das fileiras novas (`TimelineRowFade`): camada nascida → instante
    /// (systemUptime). A leitura anterior das camadas e o projeto dela.
    @State private var rowBorn: [Int64: TimeInterval] = [:]
    @State private var fadeKnown: [Int64]?
    @State private var fadeProject = ""
    /// Relógio do fade: muda a cada tique enquanto há fileira nascendo (redesenha).
    @State private var fadeClock: TimeInterval = 0
    private var m: TimelineMetrics { TimelineMetrics(fontScale: EditorTimelineRefresh.enabled ? UIFont.preferredFont(forTextStyle: .body).pointSize / 17 : 1) }
    private var haptics: TimelineHaptics { TimelineHaptics.shared }
    private let pulse = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()

    private struct MediaTile { var localFrame: Double; var width: CGFloat; var image: UIImage }
    /// 0.3.2: `label` = o nome da marca ("" = sem nome), desenhado na régua.
    private struct Marker { var frame: Int32; var packedColor: UInt32; var kind: UInt32; var label: String = "" }
    /// `.keys`: arrasto de um losango ESCOLHIDO — a seleção de keyframes inteira anda junta.
    /// `.step`: fileira compacta — o arrasto vertical passa pelas camadas (roda).
    /// `.box`: modo "Selecionar" — arrastar no vazio desenha o retângulo de seleção dos losangos.
    /// `.lift`: toque longo + vertical num trecho — só ELE sobe/desce (a linha não vai junto).
    /// `.stretch`: Omega (P1-8) — a alça de velocidade do foco (prévia; o motor estica ao soltar).
    private enum Mode { case scrub, scroll, step, move, trimStart, trimEnd, key, keys, reorder, hold, blocked, box, lift, sweep, stretch }
    /// Varredura do modo de escolher várias camadas: as que o dedo já somou.
    @State private var sweepSeen = Set<Int64>()
    // --- Omega 0.3.2 (área 03) ------------------------------------------------------
    /// Clipes levantados no mover (pulso e sombra forte) e o começo do pulso.
    @State private var movingIds = Set<Int64>()
    @State private var liftStart: TimeInterval = 0
    /// Relógio do pulso de 130 ms: muda a cada tique enquanto ele dura (redesenha).
    @State private var pulseClock: TimeInterval = 0
    /// Régua em realce no mover: início/fim originais e os novos.
    @State private var moveOrig: (start: Int32, end: Int32)?
    @State private var moveNew: (start: Int32, end: Int32)?
    /// Alças do foco: 0 em repouso, 1 tocada, 2 arrastando, 3 escondidas (o clipe anda).
    @State private var handleState = 0
    /// Prévia da alça de velocidade: a camada e o início/fim novos.
    @State private var stretch: (id: Int64, start: Int32, end: Int32)?
    /// Inércia do scrub (P1-4): pt/s no eixo do tempo; o scrub do motor fica aberto até parar.
    @State private var scrubVelocity: CGFloat = 0
    @State private var scrubFlinging = false
    /// Depois do olho, um toque nestes 200 ms não sai do foco.
    @State private var eyeToggledAt: TimeInterval = 0
    /// A camada que estava no foco (o fade de 600 ms ao voltar à visão geral).
    @State private var focusedLayer: Int64?
    /// Clipe levantado pelo toque longo e ainda parado: o arrasto (só no tempo) o move;
    /// soltar sem mover não faz nada e a pinça não escolhe nada (par do Android).
    @State private var pendingLift: Int64?
    /// Menu ancorado na aba do tempo atual (P1-3, segurar o relógio).
    @State private var timeMenuOpen = false
    /// Layouts dos rótulos (reticências no meio), por camada e largura em degraus.
    @State private var omegaText = TimelineOmegaTextCache()
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
        /// Teto da auto-rolagem deste gesto (`GestureLimit`), congelado no começo:
        /// o `navigationEnd` vivo cresce com o próprio clipe arrastado e virava catraca.
        var limit: Int64 = GestureLimit.none
        /// Omega: a duração da camada no começo do aparar/esticar (a "Mudança" da barra de info).
        var originalDuration: Int32 = 0
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
        // Omega (P0-1): uma camada por linha, sem mudar o motor (o `trackId` fica).
        let shared = EditorTimelineRefresh.enabled ? timelineLayerRows(all, perLayer: true)
            : rowCache.shared(all, focus: model.primarySelection, tracks: focusTracks)
        if compact {
            guard let primary = model.primarySelection else { return [] }
            // Omega (P1-7): o foco mostra SÓ a linha da camada escolhida.
            if EditorTimelineRefresh.enabled { return shared.filter { $0.track == nil && $0.id == primary } }
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
            let fadeNow = fadeClock
            let pulseNow = pulseClock
            Canvas { context, canvasSize in
                // Omega 0.3.2 (área 03): a timeline da referência tem o pintor dela.
                if EditorTimelineRefresh.enabled {
                    drawOmega(&context, size: canvasSize, now: fadeNow, pulse: pulseNow)
                } else {
                    drawRows(&context, size: canvasSize, now: fadeNow)
                    drawRuler(&context, size: canvasSize)
                    drawPreviewBuffer(&context, size: canvasSize)
                    drawPlayhead(&context, size: canvasSize)
                }
            }
            .accessibilityChildren {
                if EditorTimelineRefresh.enabled { omegaAccessibility }
                else { ForEach(model.layers) { layer in
                    Button(layer.name) { model.select(layerId: layer.id, additive: false) }
                        .accessibilityValue(AureaText.t(layer.visible ? "common_on" : "common_off"))
                        .accessibilityAddTraits(model.selection.contains(layer.id) ? .isSelected : [])
                        .accessibilityAction(named: Text(AureaText.t(layer.visible ? "editor_ocultar_camada" : "editor_mostrar_camada"))) {
                            model.engine.setLayer(layer.id, visible: !layer.visible); model.refreshModel(force: true)
                        }
                        .accessibilityIdentifier("timeline.layer.\(layer.id)")
                    if layer.clipToBelow {
                        // Mesmo id do Android: o leitor de tela ouve a máscara de recorte.
                        Text(layer.name + ": " + AureaText.t("editor_recortada_camada_abaixo"))
                            .accessibilityIdentifier("timeline.layer.\(layer.id).clip")
                    }
                } }
                if !model.previewBufferRanges.isEmpty {
                    Text(AureaText.t("preview_buffer_timeline", model.previewTimelineCachedFrames))
                        .accessibilityIdentifier("timeline.preview.buffer")
                }
            }
            .background(EditorTimelineRefresh.enabled ? AureaColors.editorCanvas : AureaTimeline.background)
            .overlay {
                TimelineGestureSurface(
                    tap: { point, origin in tap(point, width: size.width, origin: origin) },
                    pan: { state, start, point, velocity in pan(state, start: start, point: point, velocity: velocity, size: size) },
                    hold: { state, start, point in hold(state, start: start, point: point, size: size) },
                    pinch: { state, scale, focus in pinch(state, scale: scale, focus: focus, width: size.width) },
                    // Omega (P1-1/P1-7): o prazo do toque longo depende do alvo.
                    holdDuration: { point in omegaHoldDuration(point, width: size.width) }
                )
            }
            // Por cima da superfície de gestos: o toque num botão não chega à timeline.
            .overlay(alignment: keyBarAlignment) { keyActionBar }
            // 0.3.3 recursos: a barra do "Selecionar várias" da 0.3.2 volta (contador ·
            // Todas · Limpar · Concluir). Na Omega fica logo acima da barra flutuante da
            // seleção múltipla (42 + 6), que mora na base da zona. Par do Android.
            .overlay(alignment: .bottom) { layerPickBar.padding(.bottom, EditorTimelineRefresh.enabled ? 48 : 0) }
            // Omega (P1-5/P1-7): barra de info do gesto (início, deslocamento, corte).
            .overlay(alignment: .top) { if let info = model.timelineInfo { omegaInfoBar(info) } }
            // Omega (P1-3): a âncora do menu do tempo atual (a zona de 100 × 38 da aba, sem toque próprio).
            .overlay(alignment: .top) {
                if EditorTimelineRefresh.enabled {
                    Color.clear.frame(width: 100, height: 38).padding(.top, 5).allowsHitTesting(false)
                        .aureaAnchoredMenu(isPresented: $timeMenuOpen, tag: "timeline.currentTime.menu") { omegaTimeMenuEntries() }
                }
            }
            .overlay(alignment: .topLeading) {
                // 0.3.3 recursos: o botão "Todas/Seleção" da 0.3.2 volta: com camada escolhida
                // (foco de uma linha), mostra todas as camadas e volta só à escolhida.
                if !model.selection.isEmpty && (EditorTimelineRefresh.enabled
                    ? (model.sheetContent == .dock || model.sheetContent == .panel || model.sheetContent == .curve)
                    : (compactDock || model.sheetContent == .panel || model.sheetContent == .curve)) {
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
                    // Nasceu já compacta (painel aberto): a rolagem da lista fica guardada para a volta.
                    if compact { compactSavedScroll = saved.scrollY }
                } else {
                    let seconds = CGFloat(model.compositionDuration) / CGFloat(fps)
                    if seconds >= Zoom.autoFitMinSeconds { pps = Zoom.autoFit(availableDp: size.width - 32, seconds: seconds) }
                    TimelineViewMemory.state[memoryKey] = (pps, scrollY)
                }
                haptics.prepare()
                refreshMarkers()
                refreshMedia(size: size)
                noteLayers(model.layers.map(\.id))
                if compact { focusedLayer = model.primarySelection }
                // 0.3.3 recursos: sem a dica "toque no relógio" (o marcador antigo voltou).
            }
            .onChange(of: guide) { snap in if snap != Snap.none { haptics.snap() } }
            .onChange(of: pps) { value in TimelineViewMemory.state[memoryKey] = (value, rememberedScroll) }
            .onChange(of: scrollY) { _ in TimelineViewMemory.state[memoryKey] = (pps, rememberedScroll) }
            .onChange(of: model.layers.map(\.id)) { ids in noteLayers(ids) }
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
            .onChange(of: compact) { entering in
                // Referência (`originalScrollY`): o compacto zera (uma fileira só) e,
                // ao sair, a lista volta na rolagem em que estava — antes zerava também.
                let next = TimelineCompactScroll.transition(entering: entering, current: scrollY, saved: compactSavedScroll,
                                                            maxScroll: entering ? 0 : maxScroll(size.height))
                scrollVelocity = 0
                compactSavedScroll = next.saved
                scrollY = next.scroll
                mediaNeedsRefresh = true
                // Omega (P1-9): ao voltar do foco, a linha da camada que estava nele surge
                // com o fade de 600 ms (exceto vídeo, como na referência).
                if EditorTimelineRefresh.enabled {
                    if entering { focusedLayer = model.primarySelection }
                    else if let id = focusedLayer {
                        focusedLayer = nil
                        if let layer = model.layers.first(where: { $0.id == id }), TimelineLayerType.of(layer.kind) != .video {
                            let now = ProcessInfo.processInfo.systemUptime
                            rowBorn[id] = now
                            fadeClock = now
                        }
                    }
                }
            }
            .onChange(of: model.primarySelection) { primary in
                if let primary, compact && EditorTimelineRefresh.enabled { focusedLayer = primary }
            }
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
        drawLegacyMarkers(&context, size: size)
        drawTimecode(&context, size: size)
    }

    /// O marcador da 0.3.2 na régua (também na Omega desde a 0.3.3): triângulo na
    /// cor da marca, fio até os tiques e o nome à direita, cortado antes da
    /// próxima marca (ou da borda) — o mesmo desenho do Android (`drawMarkers`).
    private func drawLegacyMarkers(_ context: inout GraphicsContext, size: CGSize) {
        for (index, marker) in markers.enumerated() {
            let px = x(Double(marker.frame), width: size.width), half = m.tickBottom * 0.28
            // 0.3.2: o nome da marca à direita do triângulo, cortado antes da
            // próxima marca (ou da borda) — o mesmo desenho do Android.
            if !marker.label.isEmpty {
                let nextX = index + 1 < markers.count ? x(Double(markers[index + 1].frame), width: size.width) : size.width
                let left = px + half + 3, avail = min(nextX - half, size.width) - left - 2
                if avail >= 18 && left < size.width {
                    let name = context.resolve(Text(marker.label).font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.white.opacity(0.85)))
                    context.drawLayer { layer in
                        layer.clip(to: Path(CGRect(x: left, y: 0, width: avail, height: m.tickBottom)))
                        layer.draw(name, at: CGPoint(x: left, y: 1), anchor: .topLeading)
                    }
                }
            }
            guard px >= -half && px <= size.width + half else { continue }
            let s = max(marker.kind == 1 ? half * 0.7 : half, 3.5)
            // 0.3.3 recursos: a cor com a correção da área 03 (`MarkerColors.display`).
            let color = markerTint(marker.packedColor)
            var path = Path(); path.move(to: CGPoint(x: px - s, y: 0)); path.addLine(to: CGPoint(x: px + s, y: 0))
            path.addLine(to: CGPoint(x: px, y: s * 1.4)); path.closeSubpath()
            context.fill(path, with: .color(color))
            var line = Path(); line.move(to: CGPoint(x: px, y: s * 1.4)); line.addLine(to: CGPoint(x: px, y: m.tickBottom))
            context.stroke(line, with: .color(color), lineWidth: marker.kind == 1 ? 1 : 1.5)
        }
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

    private func drawRows(_ context: inout GraphicsContext, size: CGSize, now: TimeInterval) {
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
            // Fileira recém-criada: surge com fade (TimelineRowFade, 600 ms).
            faded(&c, alpha: rowAlpha(row, now: now)) { rc in
                guard let shared = row.shared else {
                    // O trecho no ar sai da fileira dele (é desenhado sob o dedo).
                    if row.id != liftId { drawRow(&rc, row: row, top: top, width: size.width) }
                    return
                }
                // FILEIRA COMPARTILHADA: cada trecho da linha no seu tempo, lado a
                // lado. Os escolhidos por cima — contorno e alças passam por cima do
                // vizinho encostado, que é onde o dedo os pega.
                var onScreen = false, offLeft = false, offRight = false
                for segment in shared {
                    let x0 = x(Double(segment.start), width: size.width), x1 = x(Double(segment.end), width: size.width)
                    if x1 >= -m.barRadius && x0 <= size.width + m.barRadius { onScreen = true }
                    else if x0 > size.width { offRight = true }
                    else { offLeft = true }
                }
                for segment in shared where !model.selection.contains(segment.id) && segment.id != liftId { drawRow(&rc, row: segment, top: top, width: size.width, arrows: false) }
                for segment in shared where model.selection.contains(segment.id) && segment.id != liftId { drawRow(&rc, row: segment, top: top, width: size.width, arrows: false) }
                // Linha inteira fora da janela: UMA seta por lado (legenda nunca teve seta).
                if !onScreen && !model.captionTracks.contains(where: { $0.layer == shared[0].id }) {
                    let tint = AureaTimeline.tone(row.type).stripe.opacity(row.visible ? 0.9 : 0.45)
                    if offRight { drawEdgeArrow(&rc, toRight: true, top: top, width: size.width, tint: tint) }
                    if offLeft { drawEdgeArrow(&rc, toRight: false, top: top, width: size.width, tint: tint) }
                }
            }
        }
        // A PÍLULA de cada fileira, opaca, por cima das barras (elas passam por baixo).
        for index in paintIndices {
            let row = visibleRows[index]
            let rowTop = m.rowsTop + tops[index] - (compact ? 0 : scrollY) + (preview?.offsets[index] ?? 0)
            guard rowTop + rowHeight(row) >= m.rowsTop && rowTop <= size.height else { continue }
            faded(&c, alpha: rowAlpha(row, now: now)) { pc in
                guard let track = row.track else {
                    drawPill(&pc, row: row, rowTop: rowTop, width: size.width)
                    return
                }
                // Trilha de GRUPO: ▸/▾ (tocar abre/fecha os eixos), mais claro; um eixo
                // sob o grupo aberto fica sem glifo (o recuo do nome diz de quem é).
                let cy = rowTop + timelineLaneHeight / 2
                if track.group {
                    let open = openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: track))
                    glyph(&pc, open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 10, tint: AureaColors.text, x: m.laneChevronCx, y: cy)
                } else if let group = timelineTrackGroup(track), openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: group)) {
                    // eixo de um grupo aberto
                } else {
                    glyph(&pc, CupertinoGlyph.ChevronRight, size: 10, tint: AureaTimeline.gutterEye, x: m.laneChevronCx, y: cy)
                }
            }
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
                let x1 = x(Double(lifted.end), width: size.width)
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
    private func drawPill(_ context: inout GraphicsContext, row: TimelineRow, rowTop: CGFloat, width: CGFloat) {
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
        if model.timelineLayerSelectMode {
            // Escolhendo várias camadas: o olho dá lugar ao ✓ das escolhidas (TimelinePainter.kt).
            if row.segments.contains(where: { model.selection.contains($0.id) }) {
                glyph(&context, CupertinoGlyph.CheckmarkCircleFill, size: m.eyeIcon, tint: AureaColors.accent, x: m.eyeCx, y: cy)
            }
        } else {
            glyph(&context, row.visible ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: m.eyeIcon,
                  tint: row.visible ? AureaColors.text : AureaColors.muted, x: m.eyeCx, y: cy)
        }
        // Máscara de recorte: o quadradinho encolhe e encosta à direita, "pendurado"
        // na camada de baixo, com o "↳" na frente dele (TimelinePainter.kt).
        let side: CGFloat = row.clipToBelow ? m.glyphBox * 0.82 : m.glyphBox
        let box = CGRect(x: m.glyphBoxLeft + m.glyphBox - side, y: cy - side / 2, width: side, height: side)
        if row.clipToBelow {
            context.draw(Text("\u{21B3}").font(.aurea(size: 11, weight: .bold)).foregroundColor(AureaColors.muted),
                         at: CGPoint(x: box.minX - 1, y: cy), anchor: .trailing)
        }
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
        // Clip edges are temporal coordinates. Touch padding belongs to hit
        // testing; a minimum painted width must never move the end handle.
        let x0 = x(Double(row.start), width: width), x1 = x(Double(row.end), width: width)
        let selected = model.selection.contains(row.id)
        let capped = compact && selected && !EditorTimelineRefresh.enabled
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
            if keysShown && row.hasKeys {
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
            if capped || selected {
                // Centered strokes and hand-built corner arcs stay bounded;
                // a one-frame bar never paints past its true end at low zoom.
                var outline = context; outline.clip(to: Path(rect))
                let requested = capped ? m.capStroke : (model.selection.count >= 2 ? m.multiStroke : m.selStroke)
                let stroke = ClipGeometry.strokeWidth(width: rect.width, height: rect.height, requested: requested)
                if rect.width <= stroke {
                    outline.fill(Path(rect), with: .color(AureaTimeline.clipSelected))
                } else {
                    let inner = rect.insetBy(dx: stroke / 2, dy: stroke / 2)
                    let radius = ClipGeometry.cornerRadius(width: inner.width, height: inner.height, requested: m.barRadius - stroke / 2)
                    outline.stroke(capped ? cappedShape(inner) : Path(roundedRect: inner, cornerRadius: radius), with: .color(AureaTimeline.clipSelected), lineWidth: stroke)
                }
            }
            if row.track == nil && model.selection.count == 1 && selected && !row.locked {
                // A tampa "‹" já é a alça branca da ponta esquerda (o dedo ali também apara).
                let handleWidth = ClipGeometry.trimPaintWidth(clipWidth: x1 - x0, requested: m.trimWidth)
                if !capped && x0 >= m.headerColumn { drawHandle(&context, left: x0 - handleWidth, top: top, start: true, width: handleWidth) }
                if x1 <= width { drawHandle(&context, left: x1 - ClipGeometry.trimPaintEndInset(clipWidth: x1 - x0, requested: m.trimWidth), top: top, start: false, width: handleWidth) }
            }
        } else if row.track == nil && arrows {
            // Clipe fora da janela: seta na borda para o lado dele (par do
            // TimelinePainter) — linha vazia parecia camada quebrada.
            drawEdgeArrow(&context, toRight: x0 > width, top: rowTop, width: width, tint: AureaTimeline.tone(row.type).stripe.opacity(row.visible ? 0.9 : 0.45))
        }
        // "Keyframes de todas as camadas" desligado: só as escolhidas mostram os losangos.
        if KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: false, selected: model.selection.contains(row.id)) {
            // Camada não selecionada: losangos só de referência, esmaecidos.
            var keys = context
            if !KeyframeVisibility.editable(selected: model.selection.contains(row.id), keySelectMode: model.timelineKeySelectMode) {
                keys.opacity = KeyframeVisibility.lockedAlpha
            }
            drawKeys(&keys, row: row, top: top, width: width)
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
        let cy = EditorTimelineRefresh.enabled || !row.hasKeys ? top + m.bar / 2 : top + m.trackTop / 2
        // Fileira compacta, trecho escolhido: tampa "‹" + nome 13 semibold + setas ‹ › à direita.
        if !EditorTimelineRefresh.enabled && self.compact && model.selection.contains(row.id) {
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
            let nameSize: CGFloat = EditorTimelineRefresh.enabled ? 10 * m.fontScale : 11
            let name = fittedName(row.name, width: floor(avail / 12) * 12, size: nameSize)
            let resolved = context.resolve(Text(name).font(.aurea(size: nameSize, weight: .medium)).tracking(aureaTracking(-0.1)).foregroundColor(tone.text))
            context.draw(resolved, at: CGPoint(x: px, y: cy), anchor: .leading)
        }
        if !EditorTimelineRefresh.enabled && barWidth > m.menuMinBar && menuRight <= width + m.menuGlyph {
            glyph(&context, CupertinoGlyph.LineHorizontal3, size: m.menuGlyph, tint: AureaTimeline.barGrip, x: menuRight - m.menuGlyph / 2, y: top + m.bar / 2)
        }
    }

    /// Barra do clipe escolhido na fileira compacta: ponta esquerda de raio 14, direita a de sempre.
    private func cappedShape(_ rect: CGRect) -> Path {
        let l = ClipGeometry.cornerRadius(width: rect.width, height: rect.height, requested: m.capRadius)
        let r = ClipGeometry.cornerRadius(width: rect.width, height: rect.height, requested: m.barRadius)
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

    private func drawHandle(_ context: inout GraphicsContext, left: CGFloat, top: CGFloat, start: Bool, width: CGFloat) {
        guard width > 0 else { return }
        if EditorTimelineRefresh.enabled {
            context.fill(Path(roundedRect: CGRect(x: left, y: top, width: width, height: m.bar), cornerRadius: ClipGeometry.cornerRadius(width: width, height: m.bar, requested: m.trimRadius)), with: .color(AureaTimeline.clipSelected))
            if width >= 10 * m.density {
                glyph(&context, start ? CupertinoGlyph.ChevronLeft : CupertinoGlyph.ChevronRight, size: 10, tint: .black,
                      x: left + width / 2, y: top + m.bar / 2)
            }
            return
        }
        let height = m.bar - m.trimTop * 2
        context.fill(Path(roundedRect: CGRect(x: left, y: top + m.trimTop, width: width, height: height), cornerRadius: ClipGeometry.cornerRadius(width: width, height: height, requested: m.trimRadius)), with: .color(AureaTimeline.clipSelected))
        if width >= m.gripWidth * 2 {
            context.fill(Path(CGRect(x: left + (width - m.gripWidth) / 2, y: top + (m.bar - m.gripHeight) / 2, width: m.gripWidth, height: m.gripHeight)), with: .color(.black.opacity(0.38)))
        }
    }

    private func drawKeys(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, width: CGFloat) {
        let cy = top + (compact ? m.diamondCyCompact : m.diamondCyNormal)
        // Primeiro os dos OUTROS parâmetros, esmaecidos (160/255) e por baixo.
        if !row.dimInstants.isEmpty { drawDimKeys(&context, instants: row.dimInstants, cy: cy, width: width) }
        guard !row.instants.isEmpty else { return }
        var groups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
        let count = Keyframes.visibleGroups(row.instants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width, margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &groups)
        // O losango sob o cabeçote acende na cor de destaque (KeyEmphasis).
        let playhead = timelineFrame(Double(clock.frame))
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
            if i == j {
                drawKeyDiamond(&context, frame: row.instants[i], px: px, cy: cy, on: on, inSelection: inSelection, dragging: row.instants[i] == dragFrame,
                               atPlayhead: row.instants[i] == playhead)
            } else {
                let lastX = x(Double(row.instants[j]), width: width), pillWidth = max(lastX - px, m.keyPillMinWidth)
                let rect = CGRect(x: (px + lastX - pillWidth) / 2, y: cy - m.keyPillHeight / 2, width: pillWidth, height: m.keyPillHeight)
                context.fill(Path(roundedRect: rect, cornerRadius: m.keyPillHeight / 2), with: .color(inSelection ? Color(hex: 0x4DA3FF) : Color.white.opacity(0.9)))
                let border: Color = inSelection ? Color.white : Color.black.opacity(0.85)
                let borderWidth: CGFloat = inSelection ? 2 : m.diamondStroke
                context.stroke(Path(roundedRect: rect.insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.keyPillHeight / 2), with: .color(border), lineWidth: borderWidth)
                // A faixa resume chaves densas; as chaves em foco conservam seu centro temporal.
                // Boundaries paint first; selected/dragged keys paint last.
                // Keep the last occurrence so a boundary key chosen by the
                // user is not painted early and covered by a nearby anchor.
                let focus = [row.start, row.end, playhead, chosen, dragFrame]
                for (position, frame) in focus.enumerated() {
                    guard !focus.dropFirst(position + 1).contains(frame), Keyframes.groupHas(row.instants, i, j, frame) else { continue }
                    let index = lowerBound(row.instants, frame)
                    drawKeyDiamond(&context, frame: frame, px: x(Double(frame), width: width), cy: cy,
                                   on: frame == chosen, inSelection: picked?[index] == true, dragging: frame == dragFrame,
                                   atPlayhead: frame == playhead)
                }
            }
        }
    }

    /// Losangos dos outros parâmetros animados (painel focado noutro): a mesma
    /// figura branca numa camada a 160/255, sem escolha, seleção nem arrasto —
    /// só dizem "aqui também tem animação" (par do `drawDimDiamonds` do Android).
    private func drawDimKeys(_ context: inout GraphicsContext, instants: [Int32], cy: CGFloat, width: CGFloat) {
        var groups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
        let count = Keyframes.visibleGroups(instants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width, margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &groups)
        guard count > 0 else { return }
        var dim = context
        dim.opacity = context.opacity * KeyEmphasis.dimAlpha
        dim.drawLayer { layer in
            for group in 0..<count {
                let i = Int(groups[group * 2]), j = Int(groups[group * 2 + 1])
                let px = x(Double(instants[i]), width: width)
                if i == j {
                    drawKeyDiamond(&layer, frame: instants[i], px: px, cy: cy, on: false, inSelection: false, dragging: false)
                } else {
                    let lastX = x(Double(instants[j]), width: width), pillWidth = max(lastX - px, m.keyPillMinWidth)
                    let rect = CGRect(x: (px + lastX - pillWidth) / 2, y: cy - m.keyPillHeight / 2, width: pillWidth, height: m.keyPillHeight)
                    layer.fill(Path(roundedRect: rect, cornerRadius: m.keyPillHeight / 2), with: .color(Color.white.opacity(0.9)))
                    layer.stroke(Path(roundedRect: rect.insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.keyPillHeight / 2),
                                 with: .color(Color.black.opacity(0.85)), lineWidth: m.diamondStroke)
                }
            }
        }
    }

    private func drawKeyDiamond(_ context: inout GraphicsContext, frame: Int32, px: CGFloat, cy: CGFloat, on: Bool, inSelection: Bool, dragging: Bool,
                                atPlayhead: Bool = false) {
        let fill: Color
        switch KeyEmphasis.tone(chosen: on, picked: inSelection, underPlayhead: atPlayhead) {
        case .chosen: fill = AureaTimeline.keyframeOn
        case .picked: fill = Color(hex: 0x4DA3FF)
        // O losango sob o cabeçote: na cor de destaque do tema.
        case .playhead: fill = AureaColors.accent
        case .off: fill = Color.white.opacity(0.9)
        }
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
        // 0.3.3 limpeza: o mesmo peso bold do `CupertinoGlyph.text` (`AureaBoldIcon`):
        // o glifo e oito cópias deslocadas meio traço em volta. Par do `drawGlyph` do
        // TimelinePainter.kt. Tinta translúcida: tudo opaco numa camada com o alfa,
        // senão a sobreposição das cópias deixaria a borda mais forte.
        let alpha = tint.aureaAlpha
        guard alpha > 0 else { return }
        let ink = alpha < 0.999 ? tint.aureaOpaque : tint
        let text = context.resolve(Text(String(glyph)).font(CupertinoFont.font(size)).foregroundColor(ink))
        let r = size * AureaIconWeight.glyphStroke / 2
        var outer = context
        if alpha < 0.999 { outer.opacity *= alpha }
        outer.drawLayer { layer in
            if r > 0 {
                for i in 0..<8 {
                    let angle = Double(i) * Double.pi / 4
                    layer.draw(text, at: CGPoint(x: x + r * CGFloat(cos(angle)), y: y + r * CGFloat(sin(angle))))
                }
            }
            layer.draw(text, at: CGPoint(x: x, y: y))
        }
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
            let x0 = x(Double(row.start), width: size.width), x1 = x(Double(row.end), width: size.width)
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
            markers = stride(from: 0, to: values.count - values.count % 3, by: 3).map {
                let frame = values[$0].int32Value, kind = values[$0 + 2].uint32Value
                // Batidas (kind 1) não têm nome próprio na régua: só as marcas da pessoa.
                return Marker(frame: frame, packedColor: values[$0 + 1].uint32Value, kind: kind,
                              label: kind == 1 ? "" : model.engine.markerLabel(Int64(frame)))
            }
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
    /// Alças de aparar: só no trecho que é a ÚNICA escolha (Omega: só no foco, P1-7).
    private func handlesOn(_ row: TimelineRow) -> Bool {
        row.track == nil && model.selection.count == 1 && model.selection.contains(row.id) && !row.locked
            && (!EditorTimelineRefresh.enabled || compact)
    }
    /// Hit-test de UM trecho na fileira (a mesma geometria do pintor).
    private func hitSegment(_ row: TimelineRow, x px: CGFloat, y: CGFloat, width: CGFloat) -> TimelineHit {
        let x0 = x(Double(row.start), width: width), x1 = x(Double(row.end), width: width)
        // Com 2+ camadas escolhidas o arrasto move o LOTE: o losango não pega o
        // dedo (par do Android `keysEnabled = !multi()`), exceto no modo de
        // escolher keyframes.
        let multi = model.selection.count >= 2 && !model.timelineKeySelectMode
        let keysShown: Bool = !multi && KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: row.track != nil, selected: model.selection.contains(row.id))
            && KeyframeVisibility.editable(selected: model.selection.contains(row.id), keySelectMode: model.timelineKeySelectMode,
                                           isPropertyLane: row.track != nil)
        let touchable: [Int32] = keysShown ? row.instants : []
        // Trilha: só a coluna do ▸/▾ (28) é cabeçalho; fileira: a pílula (70), com o olho em x < 28.
        let lane: Bool = row.track != nil
        return TimelineHit.test(m, point: CGPoint(x: px, y: y), width: width, x0: x0, x1: x1, handles: handlesOn(row), compact: compact && model.selection.contains(row.id), instants: touchable, view: viewFrame, ppf: ppf,
                                header: lane ? m.laneHeader : m.headerColumn, eyeRight: lane ? 0 : m.eyeHitRight)
    }
    /// O que o dedo pegou: o TRECHO (na pílula de uma fileira compartilhada, a
    /// fileira); `index` do resultado é a fileira inteira sob o dedo.
    private func hit(_ point: CGPoint, width: CGFloat) -> (TimelineRow?, TimelineHit) {
        // 0.3.3 recursos: o marcador antigo voltou — o relógio é parte da régua
        // (tocar não marca, segurar não abre menu); marca-se pelo botão do
        // transporte, pela âncora do palco ou pelo menu (como na 0.3.2).
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
            let x0 = x(Double(segment.start), width: width), x1 = x(Double(segment.end), width: width)
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
        // Omega 0.3.2 (área 03): os toques da referência (P1-1, P1-2, P1-3).
        if EditorTimelineRefresh.enabled { omegaTap(point, width: width); return }
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
            // Escolhendo várias camadas o olho vira o ✓: o toque soma/tira a camada.
            if model.timelineLayerSelectMode && row.track == nil { model.toggleTimelineLayerPick(row.id); return }
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
            keyTap(row, index: touched.key)
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
                trackTap(row, track)
                return
            }
            if touched.kind == .header {
                if compact && !compactByDock { showAllLayers = true }
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

    /// Losango tocado: escolhe a camada, leva o cabeçote ao instante e abre a
    /// curva; no modo "Selecionar" soma/tira sem buscar (o mesmo de antes).
    private func keyTap(_ row: TimelineRow, index: Int) {
        guard row.keysAt.indices.contains(index) else { return }
        pause()
        let key = row.keysAt[index][0]
        let track = TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
        if model.timelineKeySelectMode {
            // Modo "Selecionar": soma/tira (trilha: 1 keyframe; resumo: o instante
            // inteiro), sem buscar (a vista É o cabeçote) e sem abrir a curva.
            model.toggleTimelineKeys(row.id, row.keysAt[index])
            if model.timelineKeySelection?.contains(key) == true {
                selectedKey = (row.id, row.instants[index], track)
            } else if let current = selectedKey, !primaryInKeySelection(current) {
                // O principal saiu da seleção: fica sem principal (nenhum losango âmbar).
                selectedKey = nil
            }
            UISelectionFeedbackGenerator().selectionChanged()
            return
        }
        model.select(layerId: row.id, additive: false)
        selectedKey = (row.id, row.instants[index], track)
        model.seek(toFrame: Int64(row.instants[index]))
        model.tapTimelineKey(row.id, key)
        if row.track?.group == true {
            // Trilha de grupo: todos os eixos daquele instante ficam escolhidos.
            model.timelineKeySelection = TimelineKeySelection(layer: row.id, keys: Set(row.keysAt[index].map { TimelineKeyRef($0) }))
        }
        let navigation = model.engine.keyframeNavigation(row.id, property: key.property, effect: key.effectIndex,
            param: key.paramIndex, compositionFrame: Int64(row.instants[index]))
        if let flags = navigation?["flags"]?.uint32Value, flags & 8 != 0,
           let start = navigation?["segmentStart"]?.int64Value {
            model.openCurve(property: key.property, effect: key.effectIndex, param: key.paramIndex,
                            time: Int32(clamping: start))
        }
    }

    /// Fundo de uma trilha de propriedade: abre o painel dela (o mesmo de antes).
    private func trackTap(_ row: TimelineRow, _ track: TimelineTrack) {
        model.select(layerId: row.id, additive: false)
        switch track.property {
        case 30: model.openPanel(.effects)
        case 31, 44:
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
        if mode == .move || mode == .key || mode == .keys || mode == .trimStart || mode == .trimEnd || mode == .box || mode == .stretch {
            let nav = model.navigationEnd
            // Sem a leitura do motor (navigationEnd = Int64.max), usa o fim das camadas.
            let navEnd = nav == Int64.max ? Int64(rows.map(\.end).max() ?? 0) : nav
            next.limit = GestureLimit.autoScrollLimit(startNavEnd: navEnd, startDuration: model.compositionDuration, fps: model.compositionFps)
        }
        let excluded = Set(mode == .move ? selected.map(\.id) : row.map { [$0.id] } ?? [])
        next.snapTargets = targets(excluding: excluded, own: row, edges: mode == .key, keys: mode == .trimStart || mode == .trimEnd)
        // Reordenar na vertical leva a FILEIRA inteira (numa linha compartilhada,
        // todos os trechos): basta um trecho travado para recusar.
        let lane: TimelineRow? = rows.indices.contains(touched.index) ? rows[touched.index] : row
        let laneLocked = lane?.segments.contains(where: \.locked) ?? false
        if mode == .move && selected.contains(where: \.locked) || (mode == .key || mode == .trimStart || mode == .trimEnd || mode == .lift || mode == .stretch) && row?.locked == true || mode == .reorder && laneLocked {
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
            if EditorTimelineRefresh.enabled {
                // Omega (P1-6): reordenar pelo fundo da linha ou pela alça ≡ não mexe na seleção.
            } else if touched.kind == .header, let lane {
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
        // Omega (P1-7/P1-8): a alça arrastada a 50 % e a duração de partida (a "Mudança").
        if next.mode == .trimStart || next.mode == .trimEnd || next.mode == .stretch, let row {
            next.originalDuration = row.end - row.start
            handleState = 2
            if next.mode == .stretch { stretch = (id: row.id, start: row.start, end: row.end); omegaSpeedInfo(old: next.originalDuration, new: next.originalDuration) }
            else if EditorTimelineRefresh.enabled { omegaTrimInfo(row.id, originalDuration: next.originalDuration) }
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
        if state == .began && EditorTimelineRefresh.enabled {
            // Omega (P1-4): eixo a 45°, o tempo com inércia; editar pede o toque longo.
            endScrubFling()
            let (row, touched) = hit(start, width: size.width)
            let dx = point.x - start.x, dy = point.y - start.y
            let horizontal = TimelinePress.horizontal(dx, dy)
            let mode: Mode
            if model.timelineLayerSelectMode && touched.kind == .header && row?.track == nil && !compact && !horizontal { mode = .sweep }
            // Trilha de propriedade aberta (só do AUREA): o losango anda direto, como antes.
            else if row?.track != nil && touched.kind == .key && TimelinePress.timeEdit(dx, dy) { mode = .key }
            else if boxStarts(row, touched) { mode = .box }
            else { mode = horizontal ? .scrub : (compact ? .blocked : .scroll) }
            begin(mode, start: start, width: size.width)
            if mode == .sweep { sweepSeen = []; sweep(at: start, start: start, width: size.width) }
        } else if state == .began {
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
            // Escolhendo várias camadas: arrastar na vertical pela coluna das
            // pílulas soma cada camada por onde o dedo passa (TimelineController.kt).
            else if model.timelineLayerSelectMode && touched.kind == .header && row?.track == nil && !compact && !horizontal { mode = .sweep }
            else { mode = horizontal ? .scrub : (compact ? .step : .scroll) }
            begin(mode, start: start, width: size.width)
            if mode == .sweep { sweepSeen = []; sweep(at: start, start: start, width: size.width) }
        }
        if state == .began || state == .changed { lastPointer = point; pendingPointer = true }
        if state == .ended {
            update(point, size: size)
            let wasScroll = gesture?.mode == .scroll
            // Omega (P1-4): o scrub solto com velocidade continua (o scrub do motor fica aberto).
            if EditorTimelineRefresh.enabled && gesture?.mode == .scrub && abs(velocity.x) >= m.flingMin {
                scrubFlinging = true
                scrubVelocity = -velocity.x
            }
            finish(cancelled: false)
            if wasScroll && abs(velocity.y) >= m.flingMin { scrollVelocity = -velocity.y }
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    private func hold(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, size: CGSize) {
        guard !pinching else { return }
        // Omega 0.3.2 (área 03): o toque longo da referência (P1-1, P1-5, P1-6, P1-7).
        if EditorTimelineRefresh.enabled { omegaHold(state, start: start, point: point, size: size); return }
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
                // Na pilha, o TRECHO sobe/desce sozinho (como na referência);
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
                else if g.hit.kind != .eye && g.hit.kind != .none {
                    // Escolhendo várias camadas, segurar parado vale o mesmo que tocar.
                    if model.timelineLayerSelectMode { model.toggleTimelineLayerPick(row.id) }
                    else {
                        if !model.selection.contains(row.id) { model.select(layerId: row.id, additive: !model.selection.isEmpty) }
                        model.changeTimelineLayerSelectMode(true)
                    }
                }
            }
            finish(cancelled: false)
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    /// Cada camada sob o dedo (na coluna das pílulas) entra na seleção uma vez.
    private func sweep(at point: CGPoint, start: CGPoint, width: CGFloat) {
        let (row, _) = hit(CGPoint(x: start.x, y: point.y), width: width)
        guard let row, row.track == nil, sweepSeen.insert(row.id).inserted, !model.selection.contains(row.id) else { return }
        model.toggleTimelineLayerPick(row.id)
        haptics.tick()
    }
    private func update(_ point: CGPoint, size: CGSize) {
        guard var g = gesture else { return }
        let delta = frame(point.x, width: size.width) - TimeAxis.frameAt(x: g.start.x, view: g.view, pxPerFrame: ppf, centerX: size.width / 2)
        switch g.mode {
        case .sweep:
            sweep(at: point, start: g.start, width: size.width)
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
                let target = Int64(GestureLimit.clampFrame(max(0, snapped == Snap.none ? timelineFrame(desired) : snapped), limit: g.limit))
                if target != g.sentFrame {
                    openUndo(&g)
                    if model.reorderClip(row.id, toFrame: target) { g.sentFrame = target }
                }
                guide = snapped
                return
            }
            let desired = timelineFrame(Double(row.start) + delta)
            let snapped = model.snapping ? Snap.span(g.snapTargets, start: desired, length: row.end - row.start, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : (start: desired, guide: Snap.none)
            let change = Int32(clamping: max(-Int64(earliest), min(Int64(GestureLimit.maxShift(maxEnd: latest, limit: g.limit)), Int64(snapped.start) - Int64(row.start))))
            guide = Int64(row.start) + Int64(change) == Int64(snapped.start) ? snapped.guide : Snap.none
            if change != g.sentDelta {
                openUndo(&g)
                for item in g.selection {
                    model.engine.setLayer(item.id, startFrame: item.startFrame + change, endFrame: item.endFrame + change, offsetFrames: item.offsetFrames, setOffset: false)
                }
                g.sentDelta = change; model.refreshModel(force: true)
                // Omega (P1-5): a régua realça os tiques novos e a barra de info segue o dedo.
                if EditorTimelineRefresh.enabled {
                    moveNew = (start: Int32(clamping: Int64(row.start) + Int64(change)), end: Int32(clamping: Int64(row.end) + Int64(change)))
                    omegaMoveInfo(start: Int32(clamping: Int64(earliest) + Int64(change)), delta: change)
                }
            }
        case .trimStart, .trimEnd:
            guard let row = g.row else { return }
            let origin = g.mode == .trimStart ? row.start : row.end
            let desired = Double(origin) + delta
            let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : Snap.none
            let target = GestureLimit.clampFrame(max(0, snapped == Snap.none ? timelineFrame(desired) : snapped), limit: g.limit)
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
                // Omega (P1-7): a barra de info com os seis campos do corte.
                if EditorTimelineRefresh.enabled { omegaTrimInfo(row.id, originalDuration: g.originalDuration) }
            }
            guide = snapped
        case .stretch:
            // Omega (P1-8): só a prévia da largura; o motor estica ao soltar (finish).
            guard let row = g.row, let current = stretch else { return }
            let left = g.hit.kind == .trimStart
            let origin = left ? row.start : row.end
            let edge = GestureLimit.clampFrame(max(0, timelineFrame(Double(origin) + delta)), limit: g.limit)
            stretch = left ? (id: current.id, start: min(edge, row.end - 1), end: current.end) : (id: current.id, start: current.start, end: max(edge, row.start + 1))
            if let s = stretch { omegaSpeedInfo(old: g.originalDuration, new: s.end - s.start) }
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
                // Omega (P1-7): `mm:ss:ff (xx,x %)` e o deslocamento na barra de info.
                if EditorTimelineRefresh.enabled { omegaKeyInfo(row, frame: target, origin: origin) }
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
        let target = min(TimeAxis.clampView(desired, durationFrames: Int32(clamping: model.compositionDuration)), Double(model.navigationEnd))
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
        // Omega (P1-8): soltar a alça de velocidade estica a camada no motor (um passo).
        if g.mode == .stretch && !cancelled, let s = stretch, let row = g.row, s.start != row.start || s.end != row.end {
            if model.stretchLayerSpan(s.id, start: Int64(s.start), end: Int64(s.end)) { haptics.light() }
        }
        // Omega (P1-4): com a inércia do scrub, o scrub do motor fica aberto até ela parar.
        let flinging = g.mode == .scrub && scrubFlinging && !cancelled
        if g.mode == .scrub && !flinging { model.engine.run { $0.scrubEnd() } }
        if autoScrubbing {
            model.engine.run { $0.scrubEnd() }
            if let held = heldView { model.optimisticPlayhead(Int64(timelineFrame(held))) }
            autoScrubbing = false
        }
        if g.undoOpen { model.engine.run { $0.endUndoGroup() } }
        model.timelineKeyDragActive = false
        gesture = nil; guide = Snap.none; reorderSource = -1; reorderTarget = -1; boxRect = nil
        if !flinging { heldView = nil; scrubFlinging = false; scrubVelocity = 0 }
        liftId = 0; liftSource = -1; liftDrop = .none
        // Omega (área 03): o clipe desce, a régua sai do realce, a barra de info some.
        movingIds = []; moveOrig = nil; moveNew = nil; handleState = 0; stretch = nil; pendingLift = nil
        if model.timelineInfo != nil { model.timelineInfo = nil }
    }

    /// Omega (P1-4): para a inércia do scrub e fecha o scrub do motor (um toque só para).
    @discardableResult private func endScrubFling() -> Bool {
        guard scrubFlinging else { return false }
        scrubFlinging = false
        scrubVelocity = 0
        model.engine.run { $0.scrubEnd() }
        if let held = heldView { model.optimisticPlayhead(Int64(timelineFrame(held))) }
        heldView = nil
        return true
    }

    /// Solta a fileira `source` sobre a fileira `target` (o mesmo do Android):
    /// o grupo dela (a camada, ou a linha inteira) vai para logo acima/abaixo
    /// do grupo do destino, num passo de desfazer. Uma camada sozinha manda o
    /// MESMO comando de sempre.
    private func commitReorder(source: Int, target: Int) {
        let list = rows
        // Omega (P0-1/P1-6): uma camada por linha; cada fileira anda sozinha.
        let keys = timelineGroupKeys(list, byLine: !EditorTimelineRefresh.enabled)
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
            finish(cancelled: true); endScrubFling(); scrollVelocity = 0; pinching = true
            pinchPlaying = model.status.playing != 0
            pinchPPS = pps; pinchFrame = frame(focus.x, width: width)
            if !pinchPlaying { model.engine.run { $0.scrubBegin() } }
            haptics.tick()
        }
        if state == .began || state == .changed {
            let next = Zoom.clamp(pinchPPS * scale)
            if abs(next - pps) > 0.001 { pps = next }
            if !pinchPlaying {
                let target = min(TimeAxis.clampView(Zoom.anchoredView(focusFrame: pinchFrame, focusX: focus.x, centerX: width / 2, pxPerFrame: ppf),
                                                durationFrames: Int32(clamping: model.compositionDuration)), Double(model.navigationEnd))
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
        if !rowBorn.isEmpty {
            // Fade das fileiras novas: o relógio anda (redesenha) até o último terminar.
            rowBorn = rowBorn.filter { now - $0.value < TimelineRowFade.duration }
            fadeClock = now
        }
        let dt = CGFloat(lastTick > 0 ? min(0.05, max(0, now - lastTick)) : 1.0 / 60)
        lastTick = now
        // Touch delivery can exceed display cadence. Apply only the newest
        // sample each tick and always flush the release position in pan/hold.
        if pendingPointer {
            pendingPointer = false
            update(lastPointer, size: size)
        }
        // Omega (P1-5): o pulso de 130 ms do clipe levantado redesenha enquanto dura.
        if !movingIds.isEmpty && now - liftStart < LiftPulse.duration + 1.0 / 60 { pulseClock = now }
        // Omega (P1-4): inércia do scrub, com a desaceleração da rolagem vertical.
        if gesture == nil && scrubFlinging {
            let desired = viewFrame + Double(scrubVelocity * dt / max(0.0001, ppf))
            let clamped = min(TimeAxis.clampView(desired, durationFrames: Int32(clamping: model.compositionDuration)), Double(model.navigationEnd))
            let advanced = abs(clamped - viewFrame) > 1e-6
            holdView(desired)
            scrubVelocity *= CGFloat(pow(0.998, Double(dt) * 1000))
            // Bateu no começo da composição (ou no fim da navegação), ou parou: fecha o scrub.
            if abs(scrubVelocity) < 12 || !advanced { endScrubFling() }
        }
        if let g = gesture {
            // Auto-rolagem em RAMPA (par do Android): faixa min(48, 1/3 da janela),
            // velocidade cresce com o quanto o dedo entrou nela, até 360 pt/s.
            var moved = false
            if g.mode == .move || g.mode == .key || g.mode == .keys || g.mode == .trimStart || g.mode == .trimEnd || g.mode == .box || g.mode == .stretch {
                let zone = AutoScroll.zone(maxZone: m.autoEdge, viewport: size.width - m.headerColumn)
                // Omega (P1-5): 38 das bordas, 1× ao entrar e até 5× na borda (120 pt/s × fator).
                let factor = EditorTimelineRefresh.enabled
                    ? OmegaScroll.edgeFactor(pos: lastPointer.x, from: g.start.x, start: 0, end: size.width,
                                             zone: AutoScroll.zone(maxZone: m.autoEdge, viewport: size.width), intent: m.autoIntent)
                    : AutoScroll.speed(pos: lastPointer.x, from: g.start.x, start: m.headerColumn, end: size.width, zone: zone, intent: m.autoIntent)
                if factor != 0 {
                    // Auto-scroll moves the presentation window; the editing gesture
                    // reapplies its absolute target and the core remains authoritative.
                    // Teto do gesto: a vista não passa de max(conteúdo, duração) + 10 s do começo.
                    let next = GestureLimit.clampView(viewFrame + Double(AutoScroll.step(factor: factor, maxSpeed: m.autoSpeed, dt: dt) / ppf), current: viewFrame, limit: g.limit)
                    let target = min(TimeAxis.clampView(next, durationFrames: Int32(clamping: model.compositionDuration)), Double(model.navigationEnd))
                    // Scrub, não seek: o seek republicava o status e relia os
                    // painéis 60 vezes por segundo (a borda engasgava).
                    if !autoScrubbing { model.engine.run { $0.scrubBegin() }; autoScrubbing = true }
                    heldView = target; model.timelineScrub(Int64(timelineFrame(target)))
                    moved = true
                }
            }
            if (g.mode == .reorder || g.mode == .box || g.mode == .lift) && !compact {
                let zone = AutoScroll.zone(maxZone: m.autoEdge, viewport: size.height - m.rowsTop)
                // Omega (P1-6): faixas próprias do reordenar (em cima até rowsTop + 19).
                let factor = EditorTimelineRefresh.enabled && g.mode == .reorder
                    ? OmegaScroll.reorderFactor(y: lastPointer.y, height: size.height, rowsTop: m.rowsTop, zone: m.autoEdge)
                    : AutoScroll.speed(pos: lastPointer.y, from: g.start.y, start: m.rowsTop, end: size.height, zone: zone, intent: m.autoIntent)
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
    /// Desenha `body` com a opacidade `alpha` aplicada ao GRUPO (uma camada só,
    /// par do `saveLayer` do Android): barra, miniaturas e losangos não somam
    /// transparência uns sobre os outros. Opaco: direto no contexto.
    private func faded(_ context: inout GraphicsContext, alpha: Double, _ body: (inout GraphicsContext) -> Void) {
        guard alpha < 1 else { body(&context); return }
        var outer = context
        outer.opacity = context.opacity * alpha
        outer.drawLayer { layer in body(&layer) }
    }

    /// A rolagem que a memória do projeto guarda: no compacto, a da lista inteira.
    private var rememberedScroll: CGFloat { compact ? (compactSavedScroll ?? scrollY) : scrollY }

    /// Nova leitura das camadas da composição aberta: as que nasceram começam o
    /// fade (par do `TimelineController.noteLayers` do Android).
    private func noteLayers(_ ids: [Int64]) {
        let previous: [Int64]? = fadeProject == memoryKey ? fadeKnown : nil
        fadeProject = memoryKey
        fadeKnown = ids
        // Omega (P1-9): o fade é da linha que SAI do foco, não da camada nova.
        if EditorTimelineRefresh.enabled { return }
        let born = TimelineRowFade.born(previous: previous, current: ids)
        guard !born.isEmpty else { return }
        let now = ProcessInfo.processInfo.systemUptime
        for id in born { rowBorn[id] = now }
        fadeClock = now
    }

    /// Opacidade da fileira agora (1 = parada). Só anima se TODOS os trechos são
    /// novos: um corte (o pedaço novo divide a linha velha) não pisca.
    private func rowAlpha(_ row: TimelineRow, now: TimeInterval) -> Double {
        guard !rowBorn.isEmpty else { return 1 }
        var oldest = TimeInterval.greatestFiniteMagnitude
        for segment in row.segments {
            guard let born = rowBorn[segment.id] else { return 1 }
            oldest = min(oldest, born)
        }
        return TimelineRowFade.alpha(now - oldest)
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
    /// `currentTime`: Omega (área 03, P1-3) — a zona de 100 × 38 do tempo atual, por cima da régua.
    enum Kind { case none, ruler, eye, header, key, trimStart, trimEnd, previous, next, back, body, currentTime }
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
        let end = handles && over && x1 <= width && x > max(x1 - ClipGeometry.trimPaintEndInset(clipWidth: x1 - x0, requested: m.trimWidth), mid) && x <= x1 - m.trimInsetEnd + m.trimWidth + m.trimTouchOut
        if key >= 0 { return TimelineHit(kind: .key, key: key) }
        if start { return TimelineHit(kind: .trimStart) }
        if end { return TimelineHit(kind: .trimEnd) }
        if !EditorTimelineRefresh.enabled && compact && over && x >= capLeft(m, x0) && x < x0 { return TimelineHit(kind: .back) }
        if !EditorTimelineRefresh.enabled && over && x >= x0 && x <= x1 && compact {
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
    /// Omega (área 03): o prazo do toque longo pelo alvo sob o dedo (nil = 0,5 s de sempre).
    var holdDuration: ((CGPoint) -> TimeInterval)? = nil
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
            if let press = gestureRecognizer as? UILongPressGestureRecognizer {
                holdStart = touch.location(in: gestureRecognizer.view)
                // Omega (P1-1/P1-7): 95 % do toque longo no cabeçalho e no clipe, 25 %
                // nas alças e no corpo do foco, 90 % no losango do foco.
                if let duration = parent.holdDuration?(holdStart) { press.minimumPressDuration = max(0.05, duration) }
            }
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

// =============================================================================
// Omega 0.3.2 (área 03): a timeline da referência — par do TimelinePainter.kt e
// do TimelineController.kt. Ordem de pintura (P0-2): linhas → cabeçalhos e
// alças ≡ → régua opaca → tiques, marcadores, aba do tempo e cursor.
// =============================================================================

/// Rótulos dos clipes com reticências no meio, por camada e largura em degraus
/// (medir texto a cada quadro custaria caro; só muda com o zoom).
@MainActor final class TimelineOmegaTextCache {
    private var names: [Int64: (bucket: Int, name: String, fitted: String)] = [:]
    func fitted(_ id: Int64, name: String, bucket: Int, width: CGFloat) -> String {
        if let hit = names[id], hit.bucket == bucket, hit.name == name { return hit.fitted }
        let font = UIFont.systemFont(ofSize: 11, weight: .medium)
        let result = MiddleEllipsis.fit(name) { (($0 as NSString).size(withAttributes: [.font: font]).width) <= width }
        if names.count > 256 { names.removeAll() }
        names[id] = (bucket, name, result)
        return result
    }
}

/// ARGB → Color (sRGB), para os pares do degradê (`ClipColors`).
private func omegaColor(_ argb: UInt32) -> Color {
    Color(.sRGB, red: Double((argb >> 16) & 0xFF) / 255, green: Double((argb >> 8) & 0xFF) / 255,
          blue: Double(argb & 0xFF) / 255, opacity: Double((argb >> 24) & 0xFF) / 255)
}

/// Losango arredondado (proporção 2:3) de altura `h`, centrado na origem.
private func omegaDiamondPath(_ h: CGFloat) -> Path {
    let hh = h / 2, hw = h / 3, k: CGFloat = 0.22
    let px: [CGFloat] = [0, hw, 0, -hw], py: [CGFloat] = [-hh, 0, hh, 0]
    var path = Path()
    for i in 0..<4 {
        let p = (i + 3) % 4, q = (i + 1) % 4
        let inPoint = CGPoint(x: px[i] + (px[p] - px[i]) * k, y: py[i] + (py[p] - py[i]) * k)
        let outPoint = CGPoint(x: px[i] + (px[q] - px[i]) * k, y: py[i] + (py[q] - py[i]) * k)
        if i == 0 { path.move(to: inPoint) } else { path.addLine(to: inPoint) }
        path.addQuadCurve(to: outPoint, control: CGPoint(x: px[i], y: py[i]))
    }
    path.closeSubpath()
    return path
}

extension TimelineView {
    // MARK: Omega · gestos

    /// "Seleção múltipla" da referência: o modo de escolher várias, ou 2+ escolhidas.
    private var omegaMulti: Bool { omegaMultiSelection(layerSelectMode: model.timelineLayerSelectMode, selected: model.selection.count) }

    /// Prazo do toque longo pelo alvo sob o dedo (base 0,4 s, a do Android 12+).
    func omegaHoldDuration(_ point: CGPoint, width: CGFloat) -> TimeInterval {
        let (row, touched) = hit(point, width: width)
        let target: OmegaHold.Target
        switch touched.kind {
        case .trimStart, .trimEnd: target = .trim
        case .body: target = .body
        case .key: target = .key
        case .header: target = row?.track == nil ? .header : .other
        default: target = .other
        }
        return OmegaHold.base * OmegaHold.fraction(target, compact: compact)
    }

    /// Toques curtos (P1-1, P1-2, P1-3). No foco, qualquer toque fora do olho, de
    /// um losango e do tempo atual sai do foco.
    private func omegaTap(_ point: CGPoint, width: CGFloat) {
        // Toque que só PAROU uma inércia não é toque (igual ao Android).
        let stoppedScroll = abs(scrollVelocity) > 1
        scrollVelocity = 0
        if endScrubFling() || stoppedScroll { return }
        let (row, touched) = hit(point, width: width)
        switch touched.kind {
        case .currentTime:
            model.toggleMarkerAt(model.status.playhead)
            return
        case .ruler:
            // 0.3.3 recursos: a régua da 0.3.2 — leva o cabeçote ao instante do dedo;
            // em cima de uma marca (12 pt) vai até ela e abre o editor (nome, quadro, cor).
            let target = Int64(max(0, timelineFrame(frame(point.x, width: width))))
            let marker = model.markerNear(target, tolerance: Int64(12 / max(0.0001, ppf)))
            model.seek(toFrame: marker ?? target)
            if let marker { model.openMarkerEditor(marker) }
            UISelectionFeedbackGenerator().selectionChanged()
            return
        case .eye:
            guard let row else { return }
            haptics.light()
            // Com a seleção múltipla o olho some: o toque ali soma/tira a camada.
            if !compact && row.track == nil && omegaMulti && !model.selection.isEmpty {
                model.toggleTimelineLayerPick(row.id)
            } else {
                model.engine.setLayer(row.id, visible: !row.visible)
                model.refreshModel(force: true)
                eyeToggledAt = ProcessInfo.processInfo.systemUptime
            }
            return
        case .key:
            guard let row, row.keysAt.indices.contains(touched.key) else { return }
            // No modo de escolher camadas o losango do resumo é parte do clipe.
            if model.timelineLayerSelectMode && row.track == nil && !model.timelineKeySelectMode {
                model.toggleTimelineLayerPick(row.id)
                UISelectionFeedbackGenerator().selectionChanged()
                return
            }
            keyTap(row, index: touched.key)
            return
        default:
            break
        }
        if compact {
            // Referência: no foco o toque curto sai dele (exceto logo depois do olho).
            if ProcessInfo.processInfo.systemUptime - eyeToggledAt < 0.2 { return }
            selectedKey = nil
            model.editorBackFromTimeline()
            return
        }
        switch touched.kind {
        case .header:
            guard let row else { return }
            if let track = row.track {
                // ▸/▾ da trilha de GRUPO: mostra/esconde as trilhas por eixo.
                if track.group {
                    let key = TimelineLaneGroupKey(layer: row.id, track: track)
                    if openGroups.contains(key) { openGroups.remove(key) } else { openGroups.insert(key) }
                    UISelectionFeedbackGenerator().selectionChanged()
                }
                return
            }
            UISelectionFeedbackGenerator().selectionChanged()
            // Com seleção múltipla: põe ou tira; sem ela: escolhe a camada, que entra em foco.
            if omegaMulti { model.toggleTimelineLayerPick(row.id) }
            else { pause(); selectedKey = nil; model.select(layerId: row.id, additive: false) }
        case .body, .trimStart, .trimEnd:
            guard let row else { return }
            if model.timelineKeySelectMode && model.timelineKeySelection?.layer == row.id { return }
            if let track = row.track { trackTap(row, track); return }
            let timelineOnly: Bool = !model.selection.isEmpty && model.selection == model.timelineOnlySelection
            switch timelineLayerTap(picking: model.timelineLayerSelectMode, compact: false, selected: model.selection.count,
                                    tappedSelected: model.selection.contains(row.id), timelineOnly: timelineOnly) {
            case .leaveCompact: selectedKey = nil; model.editorBackFromTimeline()
            case .toggle:
                selectedKey = nil
                if model.timelineLayerSelectMode { model.toggleTimelineLayerPick(row.id) }
                else { model.select(layerId: row.id, additive: true) }
                UISelectionFeedbackGenerator().selectionChanged()
            case .replace: pause(); selectedKey = nil; model.select(layerId: row.id, additive: false)
            case .deselect: selectedKey = nil; model.clearSelection()
            }
        case .none:
            selectedKey = nil
            model.clearTimelineKeySelection()
            // Com a seleção múltipla, o vazio a limpa; sem ela, a casca decide.
            if model.timelineLayerSelectMode {
                model.clearSelection()
                model.changeTimelineLayerSelectMode(false)
            } else {
                model.editorBackFromTimeline()
            }
        default:
            break
        }
    }

    /// Régua (P1-2): perto de um marcador (21 pt) vai até ele; senão, metade
    /// direita = o próximo, esquerda = o anterior. Não busca o quadro tocado.
    private func omegaRulerTap(_ px: CGFloat, width: CGFloat) {
        let frames = markers.map { Int64($0.frame) }
        if let target = MarkerNav.target(frames, x: px, width: width, playhead: model.status.playhead, near: m.omegaMarkerNear,
                                         xOf: { x(Double($0), width: width) }) {
            pause()
            model.seek(toFrame: target)
            UISelectionFeedbackGenerator().selectionChanged()
        }
        if model.timelineLayerSelectMode { return }
        model.editorBackFromTimeline()
    }

    /// O toque longo começa uma linha a reordenar? (fundo da linha ou alça ≡, na visão geral).
    private func omegaReorders(_ touched: TimelineHit, point: CGPoint, width: CGFloat) -> Bool {
        guard !compact, !omegaMulti, rows.indices.contains(touched.index), rows[touched.index].track == nil else { return false }
        return touched.kind == .none || point.x >= width - m.omegaGripZone
    }

    /// Toque longo da Omega: o que a referência faz em cada lugar.
    private func omegaHold(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, size: CGSize) {
        if state == .began {
            endScrubFling()
            let (row, touched) = hit(start, width: size.width)
            if touched.kind == .currentTime {
                // P1-3: o menu ancorado na aba do tempo (editar ou excluir o marcador do quadro).
                haptics.heavy()
                pause()
                timeMenuOpen = true
                return
            }
            if omegaReorders(touched, point: start, width: size.width) {
                begin(.reorder, start: start, width: size.width)
            } else if let row, compact && row.track == nil && (touched.kind == .trimStart || touched.kind == .trimEnd) {
                // Alças do foco (P1-7); com o painel Velocidade, as de velocidade (P1-8).
                haptics.light()
                let edge: Mode = touched.kind == .trimStart ? .trimStart : .trimEnd
                let mode: Mode = model.speedHandlesActive ? .stretch : edge
                begin(mode, start: start, width: size.width)
            } else if touched.kind == .key {
                haptics.light()
                begin(.key, start: start, width: size.width)
            } else if let row, row.track == nil && touched.kind == .body {
                // Levantar (P1-5): o clipe sobe na hora; o arrasto o leva (com a seleção
                // múltipla, todas as escolhidas). Nada é escolhido antes de o dedo andar.
                haptics.heavy()
                let now = ProcessInfo.processInfo.systemUptime
                movingIds = model.selection.contains(row.id) && model.selection.count >= 2 ? model.selection : [row.id]
                liftStart = now
                pulseClock = now
                begin(.hold, start: start, width: size.width)
                pendingLift = row.id
            } else if let row, !compact && row.track == nil && touched.kind == .header {
                // Segurar o cabeçalho (P1-1; A.01 cede à referência): seleção múltipla e varredura.
                haptics.heavy()
                pause()
                if !model.timelineLayerSelectMode {
                    if !(model.selection.count == 1 && model.selection.contains(row.id)) {
                        model.select(layerId: row.id, additive: false, openOptions: false)
                    }
                    model.changeTimelineLayerSelectMode(true)
                } else {
                    model.toggleTimelineLayerPick(row.id)
                }
                begin(.sweep, start: start, width: size.width)
                sweepSeen = [row.id]
            } else {
                begin(.hold, start: start, width: size.width)
            }
        }
        if state == .changed, gesture?.mode == .hold, let lifted = pendingLift {
            let dx = point.x - start.x, dy = point.y - start.y
            if hypot(dx, dy) >= m.axisSlop {
                pendingLift = nil
                pause()
                if !model.selection.contains(lifted) { model.select(layerId: lifted, additive: omegaMulti, openOptions: false) }
                begin(.move, start: start, width: size.width)
                omegaLift()
            }
        } else if state == .changed, let g = gesture, g.mode == .hold {
            let dx = point.x - start.x, dy = point.y - start.y
            if hypot(dx, dy) >= m.axisSlop {
                let horizontal = TimelinePress.horizontal(dx, dy)
                let vertical: Mode = compact ? .blocked : .scroll
                let mode: Mode = horizontal ? .scrub : vertical
                begin(mode, start: start, width: size.width)
            }
        }
        if state == .began || state == .changed { lastPointer = point; pendingPointer = true }
        if state == .ended {
            update(point, size: size)
            finish(cancelled: false)
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    /// Menu do tempo atual (P1-3): editar o marcador do quadro (ou criar um com nome)
    /// e excluí-lo, se houver. Introdução/final da referência não se aplicam; o
    /// quadro de capa (P2-6) ainda não existe no motor.
    func omegaTimeMenuEntries() -> [MenuEntry] {
        let frame = model.status.playhead
        var entries: [MenuEntry] = [
            .item(AureaText.t("marker_edit"), glyph: CupertinoGlyph.Bookmark, tag: "timeline.currentTime.menu.edit") {
                model.openMarkerEditor(frame)
            },
        ]
        if model.markerFrames.contains(frame) {
            entries.append(.item(AureaText.t("common_delete"), glyph: CupertinoGlyph.Trash, destructive: true,
                                 tag: "timeline.currentTime.menu.delete") { model.deleteMarker(frame) })
        }
        return entries
    }

    /// O clipe sobe: pulso de 130 ms, sombra forte, régua em realce e a barra de info.
    private func omegaLift() {
        guard let g = gesture, g.mode == .move, let row = g.row else { return }
        // O pulso começou no toque longo; só recomeça se não houve.
        if movingIds.isEmpty {
            let now = ProcessInfo.processInfo.systemUptime
            liftStart = now
            pulseClock = now
        }
        movingIds = Set(g.selection.map(\.id))
        handleState = 3
        moveOrig = (start: row.start, end: row.end)
        moveNew = (start: row.start, end: row.end)
        omegaMoveInfo(start: g.selection.map(\.startFrame).min() ?? row.start, delta: 0)
    }

    // MARK: Omega · barra de info (P1-5 / P1-7 / P1-8)
    private func signedTime(_ delta: Int32) -> String {
        (delta < 0 ? "\u{2212}" : "+") + Timecode.format(Int32(clamping: abs(Int64(delta))), fps)
    }

    func omegaMoveInfo(start: Int32, delta: Int32) {
        model.timelineInfo = [
            TimelineInfoItem(label: AureaText.t("om_tl_info_start"), value: Timecode.format(start, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_offset"), value: signedTime(delta)),
        ]
    }

    func omegaTrimInfo(_ id: Int64, originalDuration: Int32) {
        guard let layer = model.layers.first(where: { $0.id == id }) else { return }
        let duration: Int32 = layer.endFrame - layer.startFrame
        model.timelineInfo = [
            TimelineInfoItem(label: AureaText.t("om_tl_info_start"), value: Timecode.format(layer.startFrame, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_in"), value: Timecode.format(layer.offsetFrames, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_end"), value: Timecode.format(layer.endFrame, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_out"), value: Timecode.format(layer.offsetFrames &+ duration, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_duration"), value: Timecode.format(duration, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_change"), value: signedTime(duration &- originalDuration)),
        ]
    }

    func omegaSpeedInfo(old: Int32, new: Int32) {
        model.timelineInfo = [
            TimelineInfoItem(label: AureaText.t("om_tl_info_duration"), value: Timecode.format(new, fps)),
            TimelineInfoItem(label: AureaText.t("om_tl_info_change"), value: signedTime(new &- old)),
        ]
    }

    func omegaKeyInfo(_ row: TimelineRow, frame: Int32, origin: Int32) {
        let span = Double(max(1, row.end - row.start))
        let percent = min(100, max(0, Double(frame - row.start) / span * 100))
        let formatter = NumberFormatter()
        formatter.minimumFractionDigits = 1
        formatter.maximumFractionDigits = 1
        let text = formatter.string(from: NSNumber(value: percent)) ?? String(format: "%.1f", percent)
        model.timelineInfo = [
            TimelineInfoItem(label: AureaText.t("om_tl_info_key"), value: Timecode.format(frame, fps) + " (" + text + " %)"),
            TimelineInfoItem(label: AureaText.t("om_tl_info_offset"), value: signedTime(frame &- origin)),
        ]
    }

    /// Barra de info por cima da régua enquanto o dedo arrasta (a barra de reprodução pode ler o mesmo estado).
    func omegaInfoBar(_ info: [TimelineInfoItem]) -> some View {
        HStack(spacing: 4) {
            ForEach(Array(info.enumerated()), id: \.offset) { index, item in
                if index > 0 { Spacer().frame(width: 6) }
                Text(item.label).font(.aurea(size: 11)).foregroundColor(AureaColors.muted).lineLimit(1)
                Text(item.value).font(.aurea(size: 12)).monospacedDigit().foregroundColor(AureaColors.text).lineLimit(1)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(RoundedRectangle(cornerRadius: 8).fill(AureaColors.editorPanel))
        .padding(.top, 2)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("timeline.info")
        .allowsHitTesting(false)
    }

    /// A dica do marcador (P1-3), uma vez por aparelho, quando a timeline abre sem marcadores.
    func omegaMarkerHint() {
        guard EditorTimelineRefresh.enabled, !UserDefaults.standard.bool(forKey: "aurea.timeline.omegaMarkerHint") else { return }
        let model = self.model
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 900_000_000)
            guard model.markerFrames.isEmpty, !UserDefaults.standard.bool(forKey: "aurea.timeline.omegaMarkerHint") else { return }
            model.toast = AureaText.t("om_tl_marker_hint")
            UserDefaults.standard.set(true, forKey: "aurea.timeline.omegaMarkerHint")
        }
    }

    // MARK: Omega · leitor de tela (P0-3, P2-4)
    /// Cada cabeçalho é um elemento (mesmo id do Android): nome, visível/oculta,
    /// escolhida, e as ações mostrar/ocultar, pôr na seleção e mover para cima/baixo.
    @ViewBuilder var omegaAccessibility: some View {
        let layers = model.layers
        let count = layers.count
        ForEach(Array(layers.enumerated()), id: \.element.id) { index, layer in
            Button(layer.name) { model.select(layerId: layer.id, additive: false) }
                .accessibilityValue(AureaText.t(layer.visible ? "om_tl_visible" : "om_tl_hidden"))
                .accessibilityAddTraits(model.selection.contains(layer.id) ? .isSelected : [])
                .accessibilityAction(named: Text(AureaText.t(layer.visible ? "editor_ocultar_camada" : "editor_mostrar_camada"))) {
                    model.engine.setLayer(layer.id, visible: !layer.visible); model.refreshModel(force: true)
                }
                .accessibilityAction(named: Text(AureaText.t("om_tl_toggle_selection"))) {
                    if !model.timelineLayerSelectMode {
                        model.select(layerId: layer.id, additive: false, openOptions: false)
                        model.changeTimelineLayerSelectMode(true)
                    } else {
                        model.toggleTimelineLayerPick(layer.id)
                    }
                }
                .accessibilityAction(named: Text(AureaText.t("om_tl_move_up"))) {
                    guard index > 0 else { return }
                    model.engine.run { $0.setLayerOrder(layer.id, newIndex: UInt32(count - index)) }
                    model.refreshModel(force: true)
                }
                .accessibilityAction(named: Text(AureaText.t("om_tl_move_down"))) {
                    guard index < count - 1 else { return }
                    model.engine.run { $0.setLayerOrder(layer.id, newIndex: UInt32(count - index - 2)) }
                    model.refreshModel(force: true)
                }
                .accessibilityIdentifier("timeline.layer.\(layer.id)")
            Button(AureaText.t(layer.visible ? "editor_ocultar_camada" : "editor_mostrar_camada") + ": " + layer.name) {
                model.engine.setLayer(layer.id, visible: !layer.visible); model.refreshModel(force: true)
            }
            .accessibilityIdentifier("timeline.layer.\(layer.id).eye")
            if layer.clipToBelow {
                // Mesmo id do Android: o leitor de tela ouve a máscara de recorte.
                Text(layer.name + ": " + AureaText.t("editor_recortada_camada_abaixo"))
                    .accessibilityIdentifier("timeline.layer.\(layer.id).clip")
            }
        }
        Text(AureaText.t("om_tl_ruler"))
            .accessibilityAction(named: Text(AureaText.t("om_tl_prev_marker"))) { model.seekToPreviousMarker() }
            .accessibilityAction(named: Text(AureaText.t("om_tl_next_marker"))) { model.seekToFollowingMarker() }
            .accessibilityIdentifier("timeline.ruler")
        Button(AureaText.t("om_tl_current_time")) { model.toggleMarkerAt(model.status.playhead) }
            .accessibilityValue(Timecode.format(Int32(clamping: model.status.playhead), fps))
            .accessibilityHint(AureaText.t("editor_marcar_ou_desmarcar_este_instante"))
            .accessibilityAction(named: Text(AureaText.t("om_tl_time_options"))) { model.openMarkerEditor(model.status.playhead) }
            .accessibilityIdentifier("timeline.currentTime")
        // Alças do foco (P1-7/P1-8): aparar (ou esticar a velocidade) um quadro por vez.
        if compact, model.selection.count == 1, let id = model.primarySelection,
           let layer = model.layers.first(where: { $0.id == id }), !layer.locked {
            let speed = model.speedHandlesActive
            Text(AureaText.t(speed ? "om_tl_speed_handle_a11y" : "om_tl_trim_start_a11y", layer.name))
                .accessibilityAction(named: Text(AureaText.t("om_tl_frame_earlier"))) { omegaNudge(layer, start: true, by: -1, speed: speed) }
                .accessibilityAction(named: Text(AureaText.t("om_tl_frame_later"))) { omegaNudge(layer, start: true, by: 1, speed: speed) }
                .accessibilityIdentifier("timeline.clip.\(id).trimStart")
            Text(AureaText.t(speed ? "om_tl_speed_handle_a11y" : "om_tl_trim_end_a11y", layer.name))
                .accessibilityAction(named: Text(AureaText.t("om_tl_frame_earlier"))) { omegaNudge(layer, start: false, by: -1, speed: speed) }
                .accessibilityAction(named: Text(AureaText.t("om_tl_frame_later"))) { omegaNudge(layer, start: false, by: 1, speed: speed) }
                .accessibilityIdentifier("timeline.clip.\(id).trimEnd")
        }
    }

    /// Um quadro na borda do foco, pelo leitor de tela: aparar ou esticar a velocidade.
    private func omegaNudge(_ layer: LayerItem, start: Bool, by delta: Int64, speed: Bool) {
        let s = Int64(layer.startFrame), e = Int64(layer.endFrame)
        if start {
            let frame = s + delta
            guard frame >= 0, frame < e else { return }
            if speed { model.stretchLayerSpan(layer.id, start: frame, end: e) } else { model.trimStart(layer.id, at: frame) }
        } else {
            let frame = e + delta
            guard frame > s else { return }
            if speed { model.stretchLayerSpan(layer.id, start: s, end: frame) } else { model.trimEnd(layer.id, at: frame) }
        }
    }

    // MARK: Omega · pintura
    func drawOmega(_ context: inout GraphicsContext, size: CGSize, now: TimeInterval, pulse: TimeInterval) {
        _ = pulse   // lido: o pulso do clipe levantado redesenha a cada tique
        omegaRows(&context, size: size, now: now)
        // A régua é opaca: cobre os clipes que rolam para cima dela.
        context.fill(Path(CGRect(x: 0, y: 0, width: size.width, height: m.rowsTop)), with: .color(AureaColors.editorCanvas))
        omegaRuler(&context, size: size)
        for range in model.previewBufferRanges {
            guard let span = TimelinePreviewBuffer.span(range, view: viewFrame, pxPerFrame: ppf, width: size.width) else { continue }
            let rect = CGRect(x: span.lowerBound, y: m.rowsTop - TimelinePreviewBuffer.height,
                              width: span.upperBound - span.lowerBound, height: TimelinePreviewBuffer.height)
            context.fill(Path(rect), with: .color(AureaTimeline.previewBuffer))
        }
        // 0.3.3 recursos: o marcador antigo da 0.3.2 na régua — triângulo na cor da
        // marca, fio até os tiques e o NOME à direita. A bandeirola, o contorno da
        // aba e o cursor na cor da marca saíram (par do Android).
        drawLegacyMarkers(&context, size: size)
        omegaTimeTab(&context, size: size, marked: false)
        // Cursor de 1,5, interrompido na aba; a 50 % movendo.
        let tint: Color = AureaColors.playhead
        let px = x(Double(clock.frame), width: size.width)
        var line = context
        if moveOrig != nil { line.opacity = 0.5 }
        line.fill(Path(CGRect(x: px - m.playhead / 2, y: 0, width: m.playhead, height: m.omegaTabTop + 1)), with: .color(tint))
        let resume = m.omegaTabTop + m.omegaTabHeight - 1
        line.fill(Path(CGRect(x: px - m.playhead / 2, y: resume, width: m.playhead, height: max(0, size.height - resume))), with: .color(tint))
    }

    /// A câmera principal: a 1ª câmera visível de cima para baixo (P2-2; o AUREA não expõe outra).
    private func omegaMainCamera(_ list: [TimelineRow]) -> Int64? {
        list.first { $0.track == nil && $0.type == .camera && $0.visible }?.id
    }

    private func omegaRows(_ context: inout GraphicsContext, size: CGSize, now: TimeInterval) {
        let visibleRows = rows
        guard !visibleRows.isEmpty else { return }
        var tops: [CGFloat] = []
        tops.reserveCapacity(visibleRows.count + 1)
        var acc: CGFloat = 0
        for row in visibleRows { tops.append(acc); acc += rowHeight(row) }
        tops.append(acc)
        let scroll: CGFloat = compact ? 0 : scrollY
        let preview: Reorder.Preview? = reorderSource >= 0 && !compact
            ? Reorder.preview(tops: tops, ids: timelineGroupKeys(visibleRows, byLine: false), source: reorderSource, target: reorderTarget, dragTop: reorderTop - m.rowsTop + scroll)
            : nil
        var order = Array(visibleRows.indices)
        var lifted = Set<Int>()
        if let preview, preview.sourceStart >= 0 {
            order.removeSubrange(preview.sourceStart..<preview.sourceEnd)
            order.append(contentsOf: preview.sourceStart..<preview.sourceEnd)
            lifted = Set(preview.sourceStart..<preview.sourceEnd)
        }
        let multi = omegaMulti
        let hideEyes = multi && !model.selection.isEmpty && !compact
        let camera: Int64? = compact ? nil : omegaMainCamera(visibleRows)
        func topOf(_ i: Int) -> CGFloat { m.rowsTop + tops[i] - scroll + (preview?.offsets[i] ?? 0) }
        // 1) Fundo da linha da câmera principal (vermelho 10 %).
        if let camera {
            for i in order where visibleRows[i].track == nil && visibleRows[i].id == camera {
                let top = topOf(i), h = rowHeight(visibleRows[i])
                if top + h < m.rowsTop || top > size.height { continue }
                context.fill(Path(CGRect(x: 0, y: top, width: size.width, height: h)), with: .color(AureaTimeline.mainCameraRow))
            }
        }
        // 2) Os clipes; a linha arrastada (reordenar) por último, sobre o fundo dela.
        for i in order {
            let row = visibleRows[i]
            let top = topOf(i), h = rowHeight(row)
            if top + h < m.rowsTop || top > size.height { continue }
            if lifted.contains(i) { context.fill(Path(CGRect(x: 0, y: top, width: size.width, height: h)), with: .color(AureaTimeline.omegaDragRow)) }
            faded(&context, alpha: rowAlpha(row, now: now)) { rc in
                if row.track != nil { omegaLane(&rc, row: row, rowTop: top, width: size.width) }
                else { for segment in row.segments { omegaClip(&rc, row: segment, rowTop: top, width: size.width) } }
            }
        }
        // 3) Cabeçalhos e alças ≡ por cima: as barras passam por baixo deles.
        for i in order {
            let row = visibleRows[i]
            let top = topOf(i), h = rowHeight(row)
            if top + h < m.rowsTop || top > size.height { continue }
            faded(&context, alpha: rowAlpha(row, now: now)) { pc in
                if let track = row.track {
                    // Trilha de GRUPO: ▸/▾ (tocar abre/fecha os eixos); eixo de grupo aberto sem glifo.
                    let cy = top + timelineLaneHeight / 2
                    if track.group {
                        let open = openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: track))
                        glyph(&pc, open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 10, tint: AureaColors.text, x: m.laneChevronCx, y: cy)
                    } else if let group = timelineTrackGroup(track), openGroups.contains(TimelineLaneGroupKey(layer: row.id, track: group)) {
                        // eixo de um grupo aberto
                    } else {
                        glyph(&pc, CupertinoGlyph.ChevronRight, size: 10, tint: AureaTimeline.gutterEye, x: m.laneChevronCx, y: cy)
                    }
                } else {
                    let activated = !compact && multi && model.selection.contains(row.id)
                    omegaHeader(&pc, row: row, rowTop: top, height: h, activated: activated, hideEye: hideEyes)
                    if !compact { omegaGrip(&pc, rowTop: top, height: h, width: size.width) }
                }
            }
        }
        // 4) Retângulo da seleção de losangos (do AUREA) e fio do ímã.
        if let box = boxRect {
            let xa = x(box.f0, width: size.width), xb = x(box.f1, width: size.width)
            let ya = m.rowsTop + box.y0 - scrollY, yb = m.rowsTop + box.y1 - scrollY
            let rect = CGRect(x: min(xa, xb), y: min(ya, yb), width: abs(xb - xa), height: abs(yb - ya))
            context.fill(Path(rect), with: .color(AureaColors.accent.opacity(0.14)))
            context.stroke(Path(rect), with: .color(AureaColors.accent), lineWidth: m.guide)
        }
        if guide != Snap.none {
            let gx = x(Double(guide), width: size.width)
            if gx >= m.headerColumn && gx <= size.width {
                context.fill(Path(CGRect(x: gx - m.guide / 2, y: m.rowsTop, width: m.guide, height: size.height - m.rowsTop)), with: .color(AureaColors.accent))
            }
        }
    }

    /// Trilha de propriedade aberta (só do AUREA, D-1): nome e losangos editáveis.
    private func omegaLane(_ context: inout GraphicsContext, row: TimelineRow, rowTop: CGFloat, width: CGFloat) {
        let cy = rowTop + timelineLaneHeight / 2
        var line = Path()
        line.move(to: CGPoint(x: m.laneLabelX, y: rowTop + timelineLaneHeight))
        line.addLine(to: CGPoint(x: width, y: rowTop + timelineLaneHeight))
        context.stroke(line, with: .color(AureaTimeline.omegaHeaderBorder), lineWidth: 1)
        let label = context.resolve(Text(row.name).font(.aurea(size: 11)).foregroundColor(row.track?.group == true ? AureaColors.text : AureaColors.muted))
        var title = context
        title.clip(to: Path(CGRect(x: m.laneLabelX, y: rowTop, width: max(0, width - m.laneLabelX - 4), height: timelineLaneHeight)))
        title.draw(label, at: CGPoint(x: m.laneLabelX, y: cy), anchor: .leading)
        omegaKeys(&context, row: row, cy: cy, width: width, editing: true, editH: m.omegaKeyAlt * 1.2, altH: m.omegaKeyAlt)
    }

    /// Cartão do clipe (P0-4): degradê por tipo e id, zebra, tira, onda, etiqueta e rótulo.
    private func omegaClip(_ context: inout GraphicsContext, row: TimelineRow, rowTop: CGFloat, width: CGFloat) {
        // Legenda (só do AUREA): os blocos dela, como antes.
        if model.captionTracks.contains(where: { $0.layer == row.id }) {
            drawRow(&context, row: row, top: rowTop, width: width, arrows: false)
            return
        }
        let top = rowTop + m.barTop
        let selected = model.selection.contains(row.id)
        // Prévia da alça de velocidade: a largura nova enquanto o dedo arrasta.
        let preview = stretch?.id == row.id ? stretch : nil
        let startF: Int32 = preview?.start ?? row.start
        let endF: Int32 = preview?.end ?? row.end
        let x0 = x(Double(startF), width: width), x1 = x(Double(endF), width: width)
        let pad = m.barRadius * 2
        if x1 >= -pad && x0 <= width + pad {
            let left = max(x0, -pad), right = min(x1, width + pad)
            let rect = CGRect(x: left, y: top, width: right - left, height: m.bar)
            let radius = ClipGeometry.cornerRadius(width: rect.width, height: rect.height, requested: m.barRadius)
            let shape = Path(roundedRect: rect, cornerRadius: radius)
            let moving = movingIds.contains(row.id)
            // Sombra: leve sempre; mais forte e espalhada com o clipe levantado.
            if moving {
                var shadow = context
                shadow.addFilter(.shadow(color: AureaTimeline.clipLiftShadow, radius: m.omegaLiftShadow / 2, x: 0, y: m.omegaShadow * 2))
                shadow.fill(shape, with: .color(AureaTimeline.clipLiftShadow))
            } else {
                context.fill(Path(roundedRect: rect.offsetBy(dx: 0, dy: m.omegaShadow), cornerRadius: radius), with: .color(AureaTimeline.clipShadow))
            }
            var bar = context
            if moving {
                let k = LiftPulse.scale(ProcessInfo.processInfo.systemUptime - liftStart)
                bar.translateBy(x: rect.midX, y: rect.midY)
                bar.scaleBy(x: k, y: k)
                bar.translateBy(x: -rect.midX, y: -rect.midY)
            }
            bar.clip(to: shape)
            // 1. Degradê do retângulo INTEIRO do clipe (não anda com a rolagem).
            let pair = AureaTimeline.clipGradient(row.type)
            let grad = ClipColors.vary(pair.0, pair.1, id: row.id)
            bar.fill(Path(rect), with: .linearGradient(Gradient(colors: [omegaColor(grad.0), omegaColor(grad.1)]),
                                                       startPoint: CGPoint(x: x0, y: top), endPoint: CGPoint(x: max(x1, x0 + 1), y: top)))
            // 2. Zebra de quadros.
            omegaZebra(&bar, startFrame: startF, top: top, left: left, right: right, width: width)
            // 3. Tira de miniaturas, sem véu nem divisórias.
            if let tiles = thumbnails[row.id] {
                for tile in tiles {
                    let px = x(Double(row.start) - Double(row.offset) + tile.localFrame, width: width)
                    bar.draw(Image(uiImage: tile.image), in: CGRect(x: px, y: top, width: tile.width, height: m.bar))
                }
            }
            // 4. Onda de 1 pt.
            omegaWave(&bar, row: row, top: top, x0: x0, x1: x1, width: width)
            // 5. Triângulo da etiqueta no canto de cima à esquerda (catetos de meia barra).
            if row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
                var tri = Path()
                tri.move(to: CGPoint(x: x0, y: top))
                tri.addLine(to: CGPoint(x: x0 + m.bar / 2, y: top))
                tri.addLine(to: CGPoint(x: x0, y: top + m.bar / 2))
                tri.closeSubpath()
                bar.fill(tri, with: .color(AureaColors.labelPalette[Int(row.label) - 1]))
            }
            // 6. Rótulo de 11 pt na borda do clipe (14 para dentro), reticências no meio (D-3).
            omegaLabel(&bar, row: row, top: top, x0: x0, x1: x1)
            // Escolhida na visão geral: o contorno branco de 2 do AUREA.
            if selected && !compact {
                var outline = context
                outline.clip(to: Path(rect))
                let stroke = ClipGeometry.strokeWidth(width: rect.width, height: rect.height, requested: m.selStroke)
                if rect.width <= stroke {
                    outline.fill(Path(rect), with: .color(AureaTimeline.clipSelected))
                } else {
                    let inner = rect.insetBy(dx: stroke / 2, dy: stroke / 2)
                    let innerRadius = ClipGeometry.cornerRadius(width: inner.width, height: inner.height, requested: m.barRadius - stroke / 2)
                    outline.stroke(Path(roundedRect: inner, cornerRadius: innerRadius), with: .color(AureaTimeline.clipSelected), lineWidth: stroke)
                }
            }
            // Alças do foco (P1-7; com o painel Velocidade, as de velocidade, P1-8).
            if compact && selected && model.selection.count == 1 && !row.locked {
                omegaHandles(&context, x0: x0, x1: x1, top: top, width: width)
            }
        }
        // Losangos (P0-6): em edição na camada escolhida; os outros, pequenos e a 160/255.
        if KeyframeVisibility.visible(showAll: model.showAllKeyframes, isPropertyLane: false, selected: selected) {
            let editing = KeyframeVisibility.editable(selected: selected, keySelectMode: model.timelineKeySelectMode) && model.selection.count < 2
            omegaKeys(&context, row: row, cy: top + m.bar / 2, width: width, editing: editing, editH: m.omegaKeyEdit, altH: m.omegaKeyAlt)
        }
    }

    /// Zebra de quadros: faixas brancas de meio quadro, uma sim e uma não (dobra até ter 4 pt).
    private func omegaZebra(_ context: inout GraphicsContext, startFrame: Int32, top: CGFloat, left: CGFloat, right: CGFloat, width: CGFloat) {
        guard ppf > 0 else { return }
        var band = 0.5
        while CGFloat(band) * ppf < m.omegaZebraMin && band < 1e7 { band *= 2 }
        let bandPx = CGFloat(band) * ppf
        let from = max(0, Int((frame(left, width: width) - Double(startFrame)) / band))
        let to = Int(((frame(right, width: width) - Double(startFrame)) / band).rounded(.up))
        var k = from + (from & 1)
        var stripes = Path()
        var count = 0
        while k <= to && count < 4096 {
            let xa = x(Double(startFrame) + Double(k) * band, width: width)
            stripes.addRect(CGRect(x: xa, y: top, width: bandPx, height: m.bar))
            k += 2
            count += 1
        }
        context.fill(stripes, with: .color(AureaTimeline.clipZebra))
    }

    /// Onda (P0-4): barras de 1 pt centradas; no áudio a barra toda, no vídeo a faixa de baixo.
    private func omegaWave(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat) {
        guard row.type == .audio || row.type == .video, let wave = waves[row.id] else { return }
        let left = max(x0, 0), right = min(x1, width)
        guard right > left else { return }
        let audio = row.type == .audio
        let areaTop: CGFloat = audio ? top : top + m.bar * 0.6
        let bottom: CGFloat = top + m.bar
        let mid = (areaTop + bottom) / 2, half = (bottom - areaTop) / 2
        let first = WaveGrid.bucketAt(frame(left, width: width), wave.fpb), last = WaveGrid.bucketAt(frame(right, width: width), wave.fpb)
        var path = Path()
        if last >= first {
            for bucket in first...last {
                let amplitude = CGFloat(wave.at(bucket)) / 256 * half
                let px = x((Double(bucket) + 0.5) * wave.fpb, width: width)
                guard amplitude >= 0.5 && px >= left && px <= right else { continue }
                path.move(to: CGPoint(x: px, y: mid - amplitude))
                path.addLine(to: CGPoint(x: px, y: mid + amplitude))
            }
        }
        context.stroke(path, with: .color(AureaTimeline.clipWave), lineWidth: m.omegaWave)
    }

    /// Rótulo do clipe (D-3): na borda do clipe, não acompanha a rolagem; preto (branco no áudio).
    private func omegaLabel(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat) {
        guard !row.name.isEmpty else { return }
        let avail = (x1 - x0) - 2 * m.omegaLabelInset
        guard avail >= 8 else { return }
        let bucket = Int(avail / 12)
        guard bucket > 0 else { return }
        let fitted = omegaText.fitted(row.id, name: row.name, bucket: bucket, width: CGFloat(bucket) * 12)
        guard !fitted.isEmpty else { return }
        let color: Color = row.type == .audio ? AureaTimeline.clipLabelOnAudio : AureaTimeline.clipLabel
        let text = context.resolve(Text(fitted).font(.aurea(size: 11, weight: .medium)).foregroundColor(color))
        context.draw(text, at: CGPoint(x: x0 + m.omegaLabelInset, y: top + m.bar / 2), anchor: .leading)
    }

    /// Losango dos outros parâmetros (e de toda camada fora de edição): branco a 160/255, traço de 0,5.
    private func omegaAltKey(_ context: inout GraphicsContext, x px: CGFloat, cy: CGFloat, h: CGFloat) {
        let path = omegaDiamondPath(h).offsetBy(dx: px, dy: cy)
        context.fill(path, with: .color(AureaTimeline.keyAlt))
        context.stroke(path, with: .color(AureaTimeline.keyAltStroke), lineWidth: m.omegaKeyAltStroke)
    }

    /// Losango em edição: branco com traço preto. Realçado: 85 % com o contorno em
    /// destaque por trás; empilhado: dois a ±20 % (o de baixo cinza no parcial);
    /// no arrasto, só o contorno.
    private func omegaEditKey(_ context: inout GraphicsContext, x px: CGFloat, cy: CGFloat, h: CGFloat,
                              highlighted: Bool, dragging: Bool, stacked: Bool, partial: Bool) {
        if dragging {
            context.stroke(omegaDiamondPath(h).offsetBy(dx: px, dy: cy), with: .color(AureaTimeline.keyFill), lineWidth: m.omegaKeyAltStroke)
            return
        }
        if highlighted {
            context.stroke(omegaDiamondPath(h).offsetBy(dx: px, dy: cy), with: .color(AureaColors.accent),
                           style: StrokeStyle(lineWidth: m.omegaKeyRing, lineJoin: .round))
        }
        let s: CGFloat = highlighted ? 0.85 : 1
        if stacked {
            let small = omegaDiamondPath(h * 0.75 * s), dy = h * 0.2
            let lower = small.offsetBy(dx: px, dy: cy + dy), upper = small.offsetBy(dx: px, dy: cy - dy)
            context.fill(lower, with: .color(partial ? AureaTimeline.keyPartial : AureaTimeline.keyFill))
            context.stroke(lower, with: .color(AureaTimeline.keyStroke), lineWidth: m.omegaKeyStroke)
            context.fill(upper, with: .color(AureaTimeline.keyFill))
            context.stroke(upper, with: .color(AureaTimeline.keyStroke), lineWidth: m.omegaKeyStroke)
        } else {
            let path = omegaDiamondPath(h * s).offsetBy(dx: px, dy: cy)
            context.fill(path, with: .color(AureaTimeline.keyFill))
            context.stroke(path, with: .color(AureaTimeline.keyStroke), lineWidth: m.omegaKeyStroke)
        }
    }

    /// Instantes densos (a menos de 4 pt) viram uma pílula no estilo da linha (do AUREA).
    private func omegaKeyPill(_ context: inout GraphicsContext, xa: CGFloat, xb: CGFloat, cy: CGFloat, h: CGFloat, editing: Bool) {
        let ph = h * 2 / 3
        let pillWidth = max(xb - xa, ph)
        let rect = CGRect(x: (xa + xb) / 2 - pillWidth / 2, y: cy - ph / 2, width: pillWidth, height: ph)
        let path = Path(roundedRect: rect, cornerRadius: ph / 2)
        context.fill(path, with: .color(editing ? AureaTimeline.keyFill : AureaTimeline.keyAlt))
        context.stroke(path, with: .color(editing ? AureaTimeline.keyStroke : AureaTimeline.keyAltStroke),
                       lineWidth: editing ? m.omegaKeyStroke : m.omegaKeyAltStroke)
    }

    /// Os losangos de uma linha (P0-6), centrados em `cy`.
    private func omegaKeys(_ context: inout GraphicsContext, row: TimelineRow, cy: CGFloat, width: CGFloat, editing: Bool, editH: CGFloat, altH: CGFloat) {
        // Os dos outros parâmetros (sem toque) primeiro, sempre pequenos.
        if !row.dimInstants.isEmpty {
            var dimGroups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
            let count = Keyframes.visibleGroups(row.dimInstants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width,
                                                margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &dimGroups)
            for group in 0..<count {
                let i = Int(dimGroups[group * 2]), j = Int(dimGroups[group * 2 + 1])
                let xa = x(Double(row.dimInstants[i]), width: width)
                if i == j { omegaAltKey(&context, x: xa, cy: cy, h: altH) }
                else { omegaKeyPill(&context, xa: xa, xb: x(Double(row.dimInstants[j]), width: width), cy: cy, h: altH, editing: false) }
            }
        }
        guard !row.instants.isEmpty else { return }
        var groups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
        let count = Keyframes.visibleGroups(row.instants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width,
                                            margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &groups)
        let playhead = timelineFrame(Double(clock.frame))
        let selectedIndex = lowerBound(row.instants, selectedKey?.frame ?? Snap.none)
        let selectedTrackMatches = selectedIndex < row.instants.count && row.instants[selectedIndex] == selectedKey?.frame && row.keysAt[selectedIndex].contains { key in
            selectedKey?.track == TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
        }
        let chosen: Int32 = editing && selectedKey?.layer == row.id && selectedTrackMatches ? selectedKey?.frame ?? Snap.none : Snap.none
        let picked: [Bool]? = editing ? pickedInstants(row) : nil
        let dragMode: Bool = gesture?.mode == .key || gesture?.mode == .keys
        let dragTrackMatches = row.track == nil || gesture?.row?.track == nil || gesture?.row?.track == row.track
        let dragFrame: Int32 = dragMode && gesture?.row?.id == row.id && dragTrackMatches ? gesture?.keyFrame ?? Snap.none : Snap.none
        let focusCount: Int = editing && row.track == nil && row.id == model.primarySelection ? (focusTracks?.count ?? 0) : 0
        var dragX: CGFloat?
        for group in 0..<count {
            let i = Int(groups[group * 2]), j = Int(groups[group * 2 + 1])
            let xa = x(Double(row.instants[i]), width: width)
            if i == j {
                let f = row.instants[i]
                guard editing else { omegaAltKey(&context, x: xa, cy: cy, h: altH); continue }
                let dragging = f == dragFrame
                if dragging { dragX = xa }
                let highlighted = f == chosen || picked?[i] == true || f == playhead
                var distinct = 1
                if focusCount >= 2 {
                    var seen = Set<TimelineTrack>()
                    for key in row.keysAt[i] { seen.insert(TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)) }
                    distinct = seen.count
                }
                let stacked = focusCount >= 2 && (distinct >= 2 || distinct < focusCount)
                omegaEditKey(&context, x: xa, cy: cy, h: editH, highlighted: highlighted, dragging: dragging, stacked: stacked, partial: stacked && distinct < focusCount)
            } else {
                omegaKeyPill(&context, xa: xa, xb: x(Double(row.instants[j]), width: width), cy: cy, h: editing ? editH : altH, editing: editing)
                guard editing else { continue }
                // A pílula resume as chaves densas; as em foco ficam no tempo delas, por cima.
                for f in [chosen, dragFrame, playhead] where Keyframes.groupHas(row.instants, i, j, f) {
                    let fx = x(Double(f), width: width)
                    if f == dragFrame { dragX = fx }
                    omegaEditKey(&context, x: fx, cy: cy, h: editH, highlighted: f != dragFrame, dragging: f == dragFrame, stacked: false, partial: false)
                }
            }
        }
        if let dragX {
            // Balão de tempo do losango na mão (do AUREA): a mesma informação da barra de info.
            let text = context.resolve(Text(Timecode.format(dragFrame, fps)).font(.aurea(size: 10, weight: .bold)).monospacedDigit().foregroundColor(.white))
            let measured = text.measure(in: CGSize(width: 180, height: 20))
            let rect = CGRect(x: dragX - measured.width / 2 - m.balloonPadH, y: cy - editH / 2 - m.balloonGap - measured.height - m.balloonPadV * 2,
                              width: measured.width + m.balloonPadH * 2, height: measured.height + m.balloonPadV * 2)
            context.fill(Path(roundedRect: rect, cornerRadius: m.balloonRadius), with: .color(.black.opacity(0.82)))
            context.draw(text, at: CGPoint(x: rect.midX, y: rect.midY))
        }
    }

    /// Cabeçalho da linha (P0-3): pílula de x −8 a 62 (destaque na seleção
    /// múltipla), faixa da etiqueta, olho em x 17 (✓ nas escolhidas; some em todas
    /// enquanto há seleção múltipla), miniatura 20 × 23 em x 34..54, recorte e cadeado.
    private func omegaHeader(_ context: inout GraphicsContext, row: TimelineRow, rowTop: CGFloat, height: CGFloat, activated: Bool, hideEye: Bool) {
        let pillTop = rowTop + m.omegaPillTop
        let pillRect = CGRect(x: m.omegaPillLeft, y: pillTop, width: m.headerColumn - m.omegaPillLeft, height: m.pillHeight)
        let pill = Path(UIBezierPath(roundedRect: pillRect, byRoundingCorners: [.topRight, .bottomRight],
                                     cornerRadii: CGSize(width: m.pillHeight / 2, height: m.pillHeight / 2)).cgPath)
        context.fill(pill, with: .color(activated ? AureaColors.accent : AureaTimeline.omegaHeader))
        context.stroke(pill, with: .color(AureaTimeline.omegaHeaderBorder), lineWidth: m.omegaPillBorder)
        let cy = pillTop + m.pillHeight / 2
        if !activated && row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
            context.fill(Path(CGRect(x: 0, y: rowTop + m.omegaTagTop, width: m.omegaTagWidth, height: max(0, height - m.omegaTagTop - m.omegaTagBottomGap))),
                         with: .color(AureaColors.labelPalette[Int(row.label) - 1]))
        }
        if activated {
            glyph(&context, CupertinoGlyph.CheckmarkCircleFill, size: m.omegaCheckGlyph, tint: AureaColors.onAccent.opacity(0.8), x: m.eyeCx, y: cy)
        } else if !hideEye {
            glyph(&context, row.visible ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: m.omegaEyeGlyph,
                  tint: AureaColors.text.opacity(row.visible ? 0.9 : 0.5), x: m.eyeCx, y: cy)
        }
        let ink: Color = activated ? AureaColors.onAccent : AureaColors.text
        let alpha: Double = row.visible ? 1 : 0.6
        let scale: CGFloat = row.clipToBelow ? m.omegaClipScale : 1
        let bw = m.omegaThumbWidth * scale, bh = m.omegaThumbHeight * scale
        let bcx = m.omegaThumbLeft + m.omegaThumbWidth / 2 - (row.clipToBelow ? m.omegaClipShift : 0)
        var thumb = context
        thumb.opacity = alpha
        let center = CGPoint(x: bcx, y: cy)
        switch row.type {
        case .video, .image:
            if let image = pillThumbs[row.id], image.size.width > 0, image.size.height > 0 {
                let s = min(bw / image.size.width, bh / image.size.height)
                let w = image.size.width * s, h = image.size.height * s
                thumb.draw(Image(uiImage: image), in: CGRect(x: center.x - w / 2, y: center.y - h / 2, width: w, height: h))
            } else {
                glyph(&thumb, row.type.glyph, size: m.glyphIcon, tint: ink, x: center.x, y: center.y)
            }
        case .audio:
            glyph(&thumb, CupertinoGlyph.MusicNote, size: 10, tint: ink.opacity(0.8), x: center.x, y: center.y)
        case .text:
            thumb.draw(Text("T").font(.aurea(size: m.glyphText, weight: .bold)).foregroundColor(ink), at: center)
        case .shape:
            let r: CGFloat = m.shapeDot / 2 * scale
            thumb.fill(Path(ellipseIn: CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2)), with: .color(ink))
        default:
            glyph(&thumb, row.type.glyph, size: m.glyphIcon, tint: ink, x: center.x, y: center.y)
        }
        if row.clipToBelow {
            var indicator = context
            indicator.opacity = row.visible ? 1 : 0.5
            indicator.draw(Text("\u{21B3}").font(.aurea(size: 11, weight: .bold)).foregroundColor(ink),
                           at: CGPoint(x: m.omegaClipIndicatorCx, y: rowTop + m.omegaClipIndicatorCy))
        }
        if row.locked {
            glyph(&context, CupertinoGlyph.LockFill, size: 9, tint: AureaColors.muted, x: m.omegaLockCx, y: rowTop + m.omegaLockCy)
        }
    }

    /// Alça ≡ da linha: três barrinhas de 8 a 5 da borda direita (o toque longo reordena).
    private func omegaGrip(_ context: inout GraphicsContext, rowTop: CGFloat, height: CGFloat, width: CGFloat) {
        let cy = rowTop + height / 2
        let right = width - m.omegaGripRight, left = right - m.omegaGripWidth
        var shadows = Path(), bars = Path()
        for k in -1...1 {
            let y = cy + CGFloat(k) * 3 - 0.5
            shadows.addRect(CGRect(x: left, y: y + 1, width: m.omegaGripWidth, height: 1))
            bars.addRect(CGRect(x: left, y: y, width: m.omegaGripWidth, height: 1))
        }
        context.fill(shadows, with: .color(AureaTimeline.omegaGripShadow))
        context.fill(bars, with: .color(AureaTimeline.omegaGrip))
    }

    /// Alças do foco (P1-7): pílulas brancas por fora das bordas, alfa pelo estado do gesto.
    private func omegaHandles(_ context: inout GraphicsContext, x0: CGFloat, x1: CGFloat, top: CGFloat, width: CGFloat) {
        let alpha: Double
        switch handleState {
        case 1: alpha = 0.3
        case 2: alpha = 0.5
        case 3: alpha = 0
        default: alpha = 0.2
        }
        guard alpha > 0 else { return }
        var clipped = context
        clipped.clip(to: Path(CGRect(x: m.headerColumn, y: top - 2, width: max(0, width - m.headerColumn), height: m.bar + 4)))
        let speed = model.speedHandlesActive
        omegaHandle(&clipped, left: x0 - m.omegaHandle, top: top, start: true, alpha: alpha, speed: speed)
        omegaHandle(&clipped, left: x1, top: top, start: false, alpha: alpha, speed: speed)
    }

    /// Uma alça: a ponta de fora redonda e o chevron vazado (‹ ou ›; ≪ ≫ na velocidade).
    private func omegaHandle(_ context: inout GraphicsContext, left: CGFloat, top: CGFloat, start: Bool, alpha: Double, speed: Bool) {
        let rect = CGRect(x: left, y: top, width: m.omegaHandle, height: m.bar)
        let corners: UIRectCorner = start ? [.topLeft, .bottomLeft] : [.topRight, .bottomRight]
        let pill = Path(UIBezierPath(roundedRect: rect, byRoundingCorners: corners, cornerRadii: CGSize(width: m.bar / 2, height: m.bar / 2)).cgPath)
        let s = m.omegaHandleChevron
        let dir: CGFloat = start ? 1 : -1
        var chevron = Path()
        for offset in (speed ? [-s * 0.55, s * 0.55] : [CGFloat(0)]) {
            chevron.move(to: CGPoint(x: rect.midX + offset + dir * s / 2, y: rect.midY - s))
            chevron.addLine(to: CGPoint(x: rect.midX + offset - dir * s / 2, y: rect.midY))
            chevron.addLine(to: CGPoint(x: rect.midX + offset + dir * s / 2, y: rect.midY + s))
        }
        var outer = context
        outer.opacity = alpha
        outer.drawLayer { layer in
            layer.fill(pill, with: .color(AureaTimeline.trimGrip))
            layer.blendMode = .clear
            layer.stroke(chevron, with: .color(AureaTimeline.keyStroke),
                         style: StrokeStyle(lineWidth: m.omegaHandleStroke, lineCap: .round, lineJoin: .round))
        }
    }

    /// Régua (P0-5): tiques de 1 pendurados do alto — segundo 18, meio 12, quadro 5 —, sem rótulos.
    private func omegaRuler(_ context: inout GraphicsContext, size: CGSize) {
        let t0 = frame(0, width: size.width), t1 = frame(size.width, width: size.width)
        guard t1 >= 0 else { return }
        let fFirst = max(0, Int64(t0.rounded(.down))), fLast = Int64(t1.rounded(.up))
        let step = Int64(OmegaRuler.frameStep(pps: pps, fps: fps))
        let secStep = Int64(OmegaRuler.secondStep(pps: pps))
        let f = Double(fps)
        let sec0 = max(0, Int64((Double(fFirst) / f).rounded(.down))), sec1 = Int64((Double(fLast) / f).rounded(.up))
        var seconds = Path(), halves = Path(), frames = Path()
        func tick(_ path: inout Path, _ frameIndex: Int64, _ height: CGFloat) {
            let px = x(Double(frameIndex), width: size.width)
            guard px >= -1 && px <= size.width + 1 else { return }
            path.move(to: CGPoint(x: px, y: 0))
            path.addLine(to: CGPoint(x: px, y: height))
        }
        var s = (sec0 / secStep) * secStep
        while s <= sec1 {
            tick(&seconds, OmegaRuler.frameOf(Double(s), fps: fps), OmegaRuler.height(OmegaRuler.secondTick))
            s += secStep
        }
        if secStep == 1 && pps / 2 >= OmegaRuler.minTickDp && sec1 >= sec0 {
            for k in sec0...sec1 { tick(&halves, OmegaRuler.frameOf(Double(k) + 0.5, fps: fps), OmegaRuler.height(OmegaRuler.halfTick)) }
        }
        if secStep == 1 && Double(step) < f / 2 {
            var frameIndex = (fFirst / step) * step
            while frameIndex <= fLast {
                if OmegaRuler.tickClass(frameIndex, fps: fps) == OmegaRuler.frameTick {
                    tick(&frames, frameIndex, OmegaRuler.height(OmegaRuler.frameTick))
                }
                frameIndex += step
            }
        }
        context.stroke(frames, with: .color(AureaTimeline.omegaTick), lineWidth: m.omegaTick)
        context.stroke(halves, with: .color(AureaTimeline.omegaTick), lineWidth: m.omegaTick)
        context.stroke(seconds, with: .color(AureaTimeline.omegaTick), lineWidth: m.omegaTick)
        // Mover (P1-5): os tiques do início e do fim originais em branco, os novos em destaque.
        if let moveOrig, let moveNew {
            for (frameIndex, tint) in [(moveOrig.start, AureaColors.playhead), (moveOrig.end, AureaColors.playhead),
                                       (moveNew.start, AureaColors.accent), (moveNew.end, AureaColors.accent)] {
                let px = x(Double(frameIndex), width: size.width)
                let height = OmegaRuler.height(OmegaRuler.tickClass(Int64(frameIndex), fps: fps))
                context.fill(Path(CGRect(x: px - m.omegaTick / 2, y: 0, width: m.omegaTick, height: height)), with: .color(tint))
            }
        }
    }

    /// Marcadores (P1-3): fio de 1 pela régua (≈ 59 %) e a bandeirola cheia. Devolve o do quadro atual.
    private func omegaMarkers(_ context: inout GraphicsContext, size: CGSize) -> Int? {
        var atPlayhead: Int?
        let playhead = Int32(clamping: clock.frame)
        for (index, marker) in markers.enumerated() {
            if marker.frame == playhead { atPlayhead = index }
            let px = x(Double(marker.frame), width: size.width)
            guard px >= -m.omegaMarkerFlagLarge && px <= size.width + m.omegaTick else { continue }
            let tint = AureaTimeline.bookmarks[MarkerColors.appearance(marker.packedColor) % AureaTimeline.bookmarks.count]
            context.fill(Path(CGRect(x: px - m.omegaTick / 2, y: 0, width: m.omegaTick, height: m.rowsTop)), with: .color(tint.opacity(150.0 / 255.0)))
            let d = marker.frame == playhead ? m.omegaMarkerFlagLarge : m.omegaMarkerFlagSmall
            var flag = Path()
            flag.move(to: CGPoint(x: px, y: 0))
            flag.addLine(to: CGPoint(x: px, y: m.omegaMarkerFlag))
            flag.addLine(to: CGPoint(x: px - d, y: m.omegaMarkerFlag))
            flag.addLine(to: CGPoint(x: px, y: m.omegaMarkerFlagTip))
            flag.closeSubpath()
            context.fill(flag, with: .color(tint))
        }
        return atPlayhead
    }

    /// Aba do tempo atual (P0-5): 70 × 18 centrada no cursor, cantos de 2 e o de
    /// cima à esquerda chanfrado; `MM:SS:FF` branco tabular. Num marcador, o
    /// contorno de 1 na cor do primeiro papel.
    private func omegaTimeTab(_ context: inout GraphicsContext, size: CGSize, marked: Bool) {
        let cx = size.width / 2
        let text = context.resolve(Text(Timecode.format(Int32(clamping: clock.frame), fps))
            .font(.aurea(size: m.timecodeFont, weight: .bold)).monospacedDigit().foregroundColor(.white))
        let measured = text.measure(in: CGSize(width: size.width, height: m.omegaTabHeight * 2))
        let tabWidth = max(m.omegaTabMinWidth, measured.width + 2 * m.omegaTabPad)
        let left = cx - tabWidth / 2, right = cx + tabWidth / 2
        let top = m.omegaTabTop, bottom = top + m.omegaTabHeight
        let r = m.omegaTabRadius, ch = m.omegaTabChamfer
        var tab = Path()
        tab.move(to: CGPoint(x: left + ch, y: top))
        tab.addLine(to: CGPoint(x: right - r, y: top))
        tab.addQuadCurve(to: CGPoint(x: right, y: top + r), control: CGPoint(x: right, y: top))
        tab.addLine(to: CGPoint(x: right, y: bottom - r))
        tab.addQuadCurve(to: CGPoint(x: right - r, y: bottom), control: CGPoint(x: right, y: bottom))
        tab.addLine(to: CGPoint(x: left + r, y: bottom))
        tab.addQuadCurve(to: CGPoint(x: left, y: bottom - r), control: CGPoint(x: left, y: bottom))
        tab.addLine(to: CGPoint(x: left, y: top + ch))
        tab.closeSubpath()
        context.fill(tab, with: .color(AureaTimeline.omegaTimeTab))
        if marked { context.stroke(tab, with: .color(AureaTimeline.bookmarks[0]), lineWidth: m.omegaTabStroke) }
        context.draw(text, at: CGPoint(x: cx, y: (top + bottom) / 2), anchor: .center)
    }
}
