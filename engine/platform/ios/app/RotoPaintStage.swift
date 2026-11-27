// Brush controls and analysis feedback shared by both cutout effects.
import SwiftUI
import Vision
import ImageIO

let bgRemoveEffectKey = "aurea.key.background_remove"

func rotoFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "om_roto_failed_model"
    case 3: return "om_roto_failed_decode"
    default: return "roto_failed"
    }
}

func bgRemoveFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "bg_remove_failed_model"
    case 3: return "bg_remove_failed_decode"
    default: return "bg_remove_failed_frames"
    }
}

private struct CutoutTarget: Hashable {
    let layer: Int64
    let effect: UInt32
    let automatic: Bool
}

private struct CutoutStatus: Equatable {
    var done: Int64 = 0
    var total: Int64 = 0
    var running = false
    var failure: Int64 = 0
    var frameReady = false
    var framesReady: Int64 = 0
    var frames: Int64 = 0
    var strokesHere: Int64 = 0
    var hasStrokes = false
    var effectiveModel: Int64 = 0
}

@MainActor private final class CutoutMonitor: ObservableObject {
    static let shared = CutoutMonitor()
    @Published var statuses: [CutoutTarget: CutoutStatus] = [:]

    func read(_ target: CutoutTarget, _ model: AureaModel) -> CutoutStatus? {
        let brush = model.engine.rotoStatus(effect: target.effect, layer: target.layer).map(\.int64Value)
        guard brush.count >= 11, brush[10] != 0 else { return nil }
        if brush[9] != 0 {
            return .init(done: brush[0], total: brush[1], running: brush[2] != 0, failure: brush[3],
                         frameReady: brush[6] != 0, framesReady: brush[7], frames: brush[8], strokesHere: brush[4], hasStrokes: true)
        }
        guard target.automatic else { return CutoutStatus() }
        let auto = model.engine.bgRemoveStatus(effect: target.effect, layer: target.layer).map(\.int64Value)
        guard auto.count >= 9, auto[8] != 0 else { return nil }
        return .init(done: auto[0], total: auto[1], running: auto[2] != 0, failure: auto[3],
                     frameReady: auto[4] != 0, framesReady: auto[5], frames: auto[6], effectiveModel: auto[7])
    }
}

@MainActor private final class RotoPaintState: ObservableObject {
    static let shared = RotoPaintState()

    @Published var target: CutoutTarget?
    @Published var background = false
    @Published var view: UInt32 = 2
    @Published var radius: CGFloat = 24
    @Published var live: [CGPoint] = []

    func end(_ model: AureaModel) {
        guard let target else { return }
        _ = model.engine.rotoSetView(3, effect: target.effect, layer: target.layer)
        self.target = nil
        live = []
        model.refreshModel(force: true)
    }
}

@MainActor struct RotoPaintStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    var body: some View {
        if let target = roto.target, model.primarySelection == target.layer {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    let color = (roto.background ? AureaColors.danger : AureaColors.success).opacity(0.82)
                    let points = roto.live.map { CGPoint(x: placed.origin.x + $0.x * placed.scale, y: placed.origin.y + $0.y * placed.scale) }
                    let radius = roto.radius * placed.scale
                    if points.count > 1 {
                        var path = Path()
                        path.addLines(points)
                        context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: radius * 2, lineCap: .round, lineJoin: .round))
                    }
                    if let last = points.last {
                        context.fill(Path(ellipseIn: CGRect(x: last.x - radius, y: last.y - radius, width: radius * 2, height: radius * 2)), with: .color(color))
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let scale = max(placed.scale, 0.0001)
                        let point = CGPoint(x: (value.location.x - placed.origin.x) / scale,
                                            y: (value.location.y - placed.origin.y) / scale)
                        let spacing = min(4, max(1, roto.radius * scale * 0.18)) / scale
                        if let last = roto.live.last, hypot(point.x - last.x, point.y - last.y) < spacing { return }
                        roto.live.append(point)
                    }
                    .onEnded { value in
                        let scale = max(placed.scale, 0.0001)
                        let endpoint = CGPoint(x: (value.location.x - placed.origin.x) / scale,
                                               y: (value.location.y - placed.origin.y) / scale)
                        finish(target: target, scale: scale, endpoint: endpoint)
                    })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("cutout_brush"))
                .accessibilityHint(AureaText.t("cutout_brush_help"))
                .accessibilityIdentifier("stage.roto_paint")
            }
        }
    }

    private func finish(target: CutoutTarget, scale: CGFloat, endpoint: CGPoint) {
        var points = roto.live
        roto.live = []
        guard roto.target == target, model.primarySelection == target.layer, let first = points.first else { return }
        points.append(endpoint)
        let reach = points.map { hypot($0.x - first.x, $0.y - first.y) }.max() ?? 0
        let changed: Bool
        if reach * scale <= 10 {
            changed = model.engine.rotoAddClick(x: Float(first.x), y: Float(first.y), background: roto.background, effect: target.effect, forLayer: target.layer)
        } else {
            var xy: [NSNumber] = []
            xy.reserveCapacity(points.count * 2)
            for point in points {
                xy.append(NSNumber(value: Float(point.x)))
                xy.append(NSNumber(value: Float(point.y)))
            }
            changed = model.engine.rotoAddStroke(xy, background: roto.background, radius: Float(roto.radius), effect: target.effect, forLayer: target.layer)
        }
        if changed { model.refreshModel(force: true) }
    }
}

@MainActor struct CutoutStageStatus: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var monitor = CutoutMonitor.shared

    private var targets: [CutoutTarget] {
        guard let layer = model.primarySelection else { return [] }
        let automatic = fxEffectTypeId(bgRemoveEffectKey)
        let brush = fxEffectTypeId("aurea.key.rotobrush")
        return model.effects.compactMap { effect in
            guard effect.enabled, effect.typeId == automatic || effect.typeId == brush else { return nil }
            return CutoutTarget(layer: layer, effect: effect.effectId, automatic: effect.typeId == automatic)
        }
    }

    var body: some View {
        let observed = targets
        let states = observed.compactMap { monitor.statuses[$0] }
        let running = states.first { $0.running }
        let pending = states.contains { $0.frames > 0 && !$0.frameReady && $0.failure == 0 }
            || (observed.contains { !$0.automatic } && model.localAiActivity & 2 != 0)
        Group {
            if running != nil || pending {
                VStack(alignment: .leading, spacing: 4) {
                    Text(AureaText.t(running == nil ? "cutout_analyzing_frame" : "cutout_analyzing_scene"))
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(AureaColors.text)
                    if let running, running.total > 0 {
                        let done = min(max(0, running.done), running.total)
                        ProgressView(value: Double(done), total: Double(running.total))
                            .tint(AureaColors.accent)
                        Text(AureaText.t("bg_remove_progress", Int(done), Int(running.total)))
                            .font(.system(size: 12))
                            .foregroundStyle(AureaColors.muted)
                    }
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .frame(maxWidth: 264, alignment: .leading)
                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 12))
                .overlay { RoundedRectangle(cornerRadius: 12).strokeBorder(AureaColors.accent.opacity(0.35)) }
                .accessibilityElement(children: .combine)
                .accessibilityIdentifier("stage.cutout.progress")
            }
        }
        .allowsHitTesting(false)
        .task(id: observed) {
            let retained = monitor.statuses.filter { observed.contains($0.key) }
            if retained != monitor.statuses { monitor.statuses = retained }
            while !Task.isCancelled && !observed.isEmpty {
                var next = monitor.statuses
                for target in observed { if let status = monitor.read(target, model) { next[target] = status } }
                if next != monitor.statuses { monitor.statuses = next }
                let busy = next.count < observed.count || next.values.contains { $0.running || ($0.frames > 0 && !$0.frameReady && $0.failure == 0) }
                try? await Task.sleep(nanoseconds: busy ? 250_000_000 : 800_000_000)
            }
        }
    }
}

@MainActor struct RotoPaintCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    @ObservedObject private var monitor = CutoutMonitor.shared
    let effectId: UInt32
    var automatic = false

    private func chip(_ key: String, _ id: String, _ on: Bool = false, tint: Color? = nil, _ action: @escaping () -> Void) -> some View {
        let color = tint ?? AureaColors.accent
        return Button(action: action) {
            Text(AureaText.t(key))
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 12)
                .frame(minHeight: 44)
                .background(on ? color.opacity(0.16) : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .overlay { RoundedRectangle(cornerRadius: 10).strokeBorder(on ? color.opacity(0.48) : .clear) }
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }

    private func help(_ key: String) -> some View {
        Text(AureaText.t(key))
            .font(.system(size: 12))
            .foregroundStyle(AureaColors.muted)
            .fixedSize(horizontal: false, vertical: true)
    }

    var body: some View {
        if let layer = model.selectedLayer, !layer.locked,
           model.effects.first(where: { $0.effectId == effectId })?.enabled == true {
            let target = CutoutTarget(layer: layer.id, effect: effectId, automatic: automatic)
            let mine = roto.target == target
            let status = monitor.statuses[target] ?? CutoutStatus()
            VStack(alignment: .leading, spacing: 7) {
                help(!mine && automatic ? "cutout_auto_help" : "cutout_brush_help")
                ViewThatFits(in: .horizontal) {
                    HStack(spacing: 6) { actions(target, mine: mine, video: layer.kind == 1, status: status) }
                    VStack(alignment: .leading, spacing: 4) { actions(target, mine: mine, video: layer.kind == 1, status: status) }
                }
                if mine {
                    ViewThatFits(in: .horizontal) {
                        HStack(spacing: 6) { brushes(target, status: status) }
                        VStack(alignment: .leading, spacing: 4) { brushes(target, status: status) }
                    }
                    Text("\(AureaText.t("roto_brush_size")) · \(Int(roto.radius)) px")
                        .font(.system(size: 12)).foregroundStyle(AureaColors.muted)
                    Slider(value: $roto.radius, in: 4...160)
                        .tint(AureaColors.accent)
                        .accessibilityLabel(AureaText.t("roto_brush_size"))
                        .accessibilityIdentifier("fx.roto.size")
                    ViewThatFits(in: .horizontal) {
                        HStack(spacing: 6) { views(target) }
                        VStack(alignment: .leading, spacing: 4) { views(target) }
                    }
                }
                if status.frameReady { help("roto_frame_ready") }
                if layer.kind == 1, status.frames > 1, !status.running, status.framesReady > 0 {
                    Text(AureaText.t("bg_remove_clip_status", Int(status.framesReady), Int(status.frames)))
                        .font(.system(size: 12)).foregroundStyle(AureaColors.muted)
                }
                if status.failure != 0, !status.running {
                    Text(AureaText.t(status.hasStrokes ? rotoFailedKey(status.failure) : bgRemoveFailedKey(status.failure)))
                        .font(.system(size: 12)).foregroundStyle(AureaColors.danger)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if automatic, !status.hasStrokes, status.frames > 0, status.effectiveModel == 0,
                   Int(model.effectParams.first { $0.index == 10 }?.scalar ?? 0) == 1 {
                    help("bg_remove_person_unavailable")
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.bottom, 6)
            .onDisappear { if roto.target == target { roto.end(model) } }
        }
    }

    @ViewBuilder private func actions(_ target: CutoutTarget, mine: Bool, video: Bool, status: CutoutStatus) -> some View {
        if !mine {
            chip("cutout_brush", "fx.roto.paint", true) {
                roto.end(model)
                model.pause()
                if model.engine.rotoSetView(2, effect: target.effect, layer: target.layer) {
                    roto.target = target
                    roto.view = 2
                    roto.background = false
                    model.refreshModel(force: true)
                }
            }
        }
        if video, automatic || status.hasStrokes {
            let tag = status.hasStrokes ? "fx.roto.propagate" : status.running ? "fx.bg.cancel" : "fx.bg.analyze"
            chip(status.running ? "bg_remove_cancel" : status.hasStrokes ? "cutout_track" : "bg_remove_analyze", tag, status.running) {
                if status.running {
                    if status.hasStrokes { model.engine.rotoCancel() } else { model.engine.bgRemoveCancel() }
                } else {
                    if model.status.playing != 0 { model.pause() }
                    let ok = status.hasStrokes ? model.engine.rotoPropagate(effect: target.effect, layer: target.layer)
                                              : model.engine.bgRemoveAnalyze(effect: target.effect, layer: target.layer)
                    if !ok { model.toast = AureaText.t("bg_remove_failed") }
                    else {
                        var started = status
                        started.running = true
                        started.done = 0
                        started.total = status.frames
                        monitor.statuses[target] = started
                    }
                }
                if let next = monitor.read(target, model) { monitor.statuses[target] = next }
            }
        }
        if mine { chip("roto_done", "fx.roto.done", true) { roto.end(model) } }
    }

    @ViewBuilder private func brushes(_ target: CutoutTarget, status: CutoutStatus) -> some View {
        chip("cutout_brush_add", "fx.roto.object", !roto.background, tint: AureaColors.success) { roto.background = false }
        chip("cutout_brush_subtract", "fx.roto.background", roto.background, tint: AureaColors.danger) { roto.background = true }
        if status.strokesHere > 0 {
            chip("roto_undo", "fx.roto.undo") {
                if model.engine.rotoUndoStroke(effect: target.effect, layer: target.layer) { model.refreshModel(force: true) }
            }
        }
    }

    @ViewBuilder private func views(_ target: CutoutTarget) -> some View {
        ForEach([(UInt32(2), "roto_view_overlay", "fx.roto.view.overlay"),
                 (UInt32(0), "roto_view_final", "fx.roto.view.final"),
                 (UInt32(1), "roto_view_mask", "fx.roto.view.mask")], id: \.2) { mode, key, id in
            chip(key, id, roto.view == mode) {
                if model.engine.rotoSetView(mode, effect: target.effect, layer: target.layer) {
                    roto.view = mode
                    model.refreshModel(force: true)
                }
            }
        }
    }
}


// MARK: - Headbop (0.5.2)

/// Headbop: as ferramentas do topo do cartão — Analisar/Reanalisar (rostos do
/// Vision na foto, várias pessoas; pose por quadro no vídeo pelo MocapDetector),
/// progresso, tap tempo e "Converter em keyframes". Estilos são as fichas de
/// preset do motor; o pivô do pescoço é o ponto do efeito no palco.
/// Par do Android: HeadbopPanel.kt (`HeadbopCardTools`).
@MainActor struct HeadbopCardTools: View {
    @EnvironmentObject private var model: AureaModel
    let effectId: UInt32
    /// [estado, progresso ×1000, pessoas, BPM ×100, quadros, rig, BPM em uso ×100, fonte da pose, válido]
    @State private var status: [Int64] = Array(repeating: 0, count: 9)
    @State private var task: Task<Void, Never>? = nil
    @State private var detecting: Float = -1
    @State private var taps: [Double] = []
    @State private var tapStartClock: Double = 0
    @State private var tapStartComp: Double = 0
    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    private static let frameFloats = 2 + 33 * 4

    private func refresh() {
        guard let layer = model.primarySelection else { return }
        let next = model.engine.headbopStatus(effect: effectId, layer: layer).map { $0.int64Value }
        if next.count >= 9 && next[8] != 0 { status = next }
    }

    private func chip(_ text: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 14)
                .frame(minHeight: 44)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.text.opacity(0.18) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(text)
        .accessibilityIdentifier(id)
    }

    /// Foto: rostos do Vision (olhos, nariz, boca) no formato de 33 pontos, uma pessoa por rosto.
    nonisolated static func detectPhoto(_ path: String, rigOnly: Bool = false,
                                      illustratedModel: String? = nil) -> (Data, Int, Float)? {
        let url = path.contains("://") ? URL(string: path) : URL(fileURLWithPath: path)
        guard let url, let src = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateThumbnailAtIndex(src, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                  kCGImageSourceThumbnailMaxPixelSize: 1024, kCGImageSourceCreateThumbnailWithTransform: true] as CFDictionary)
        else { return nil }
        let w = Float(image.width), h = Float(image.height)
        if let illustratedModel {
            let stride = image.width * 4
            var rgba = Data(count: stride * image.height)
            let drawn = rgba.withUnsafeMutableBytes { (bytes: UnsafeMutableRawBufferPointer) -> Bool in
                guard let canvas = CGContext(data: bytes.baseAddress, width: image.width, height: image.height,
                    bitsPerComponent: 8, bytesPerRow: stride, space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)
                else { return false }
                canvas.draw(image, in: CGRect(x: 0, y: 0, width: CGFloat(image.width), height: CGFloat(image.height)))
                return true
            }
            guard drawn, let pose = AureaEngine.detectIllustratedPose(rgba, width: UInt32(image.width),
                height: UInt32(image.height), stride: UInt32(stride), modelPrefix: illustratedModel) else { return nil }
            return (pose, 1, w / max(h, 1))
        }
        let request = VNDetectFaceLandmarksRequest()
        let bodies = VNDetectHumanBodyPoseRequest()
        let input: CGImage
        if rigOnly, let canvas = CGContext(data: nil, width: image.width, height: image.height,
            bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) {
            let bounds = CGRect(x: 0, y: 0, width: CGFloat(image.width), height: CGFloat(image.height))
            canvas.setFillColor(red: 0.5, green: 0.5, blue: 0.5, alpha: 1)
            canvas.fill(bounds)
            canvas.draw(image, in: bounds)
            input = canvas.makeImage() ?? image
        } else {
            input = image
        }
        let requests: [VNRequest] = rigOnly ? [bodies] : [request, bodies]
        try? VNImageRequestHandler(cgImage: input, options: [:]).perform(requests)
        var rows: [Float] = []
        var person = 0
        for body in (bodies.results ?? []).prefix(rigOnly ? 1 : 4) {
            var frame = [Float](repeating: 0, count: frameFloats)
            MocapDetector.fill2D(body, into: &frame, at: 0)
            frame[1] = Float(person)
            rows += frame
            person += 1
        }
        for face in (request.results ?? []).prefix(4) {
            if person >= 4 { break }
            guard let lm = face.landmarks else { continue }
            let box = face.boundingBox
            func point(_ region: VNFaceLandmarkRegion2D?) -> (Float, Float)? {
                guard let pts = region?.normalizedPoints, !pts.isEmpty else { return nil }
                let x = pts.map { Float($0.x) }.reduce(0, +) / Float(pts.count)
                let y = pts.map { Float($0.y) }.reduce(0, +) / Float(pts.count)
                // Vision: y para cima, relativo à caixa do rosto → imagem com y para baixo.
                return (Float(box.minX) + x * Float(box.width), 1 - (Float(box.minY) + y * Float(box.height)))
            }
            var f = [Float](repeating: 0, count: frameFloats)
            f[1] = Float(person)
            func put(_ i: Int, _ p: (Float, Float)?) {
                guard let p else { return }
                f[2 + i * 4] = p.0; f[3 + i * 4] = p.1; f[5 + i * 4] = 0.9
            }
            put(2, point(lm.leftPupil ?? lm.leftEye)); put(5, point(lm.rightPupil ?? lm.rightEye))
            put(0, point(lm.noseCrest ?? lm.nose))
            if let lips = point(lm.outerLips) { put(9, (lips.0 - 0.02, lips.1)); put(10, (lips.0 + 0.02, lips.1)) }
            rows += f
            person += 1
        }
        guard person > 0 else { return (Data(), 0, w / max(h, 1)) }
        return (rows.withUnsafeBufferPointer { Data(buffer: $0) }, person, w / max(h, 1))
    }

    private func analyze(layer: Int64) {
        if model.status.playing != 0 { model.pause() }
        let engine = model.engine
        let path = model.layerSourcePath(layer)
        // kind é UInt32 no modelo; -1 = camada não encontrada.
        let kind: Int = model.layers.first(where: { $0.id == layer }).map { Int($0.kind) } ?? -1
        let effect = effectId
        task = Task {
            defer { task = nil; detecting = -1 }
            guard engine.headbopBegin(effect: effect, layer: layer) else { return }
            if !path.isEmpty {
                detecting = 0
                if kind == 1 {
                    if let url = path.contains("://") ? URL(string: path) : URL(fileURLWithPath: path) {
                        _ = try? await MocapDetector.run(url: url, reduced: false, imageSpace: true, progress: { p in
                            Task { @MainActor in detecting = p }
                        }, sink: { data, n, aspect in _ = engine.headbopPushFrames(data, frames: Int32(n), aspect: aspect) })
                    }
                } else if kind == 2 {
                    let found = await Task.detached { Self.detectPhoto(path) }.value
                    if let found, found.1 > 0 { _ = engine.headbopPushFrames(found.0, frames: Int32(found.1), aspect: found.2) }
                }
            }
            detecting = -1
            if Task.isCancelled { return }
            _ = await Task.detached { engine.headbopAnalyze() }.value
            model.refreshModel(force: true)
            refresh()
        }
    }

    // O corpo em partes pequenas: num bloco só o Swift não termina de inferir
    // os tipos ("unable to type-check this expression in reasonable time").
    private var running: Bool { task != nil }

    private var stateText: String {
        let rig: Int64 = status[5]
        if running { return AureaText.t("headbop_analyzing") }
        if rig >= 2 { return String(format: AureaText.t("headbop_people_found"), Int(rig - 1)) }
        if rig == 1 { return AureaText.t("headbop_no_person") }
        return AureaText.t("headbop_not_analyzed")
    }

    private var progressValue: Double {
        if detecting >= 0 { return Double(detecting) * 0.6 }
        let analyzed: Double = Double(status[1]) / 1000.0
        return 0.6 + 0.4 * analyzed
    }

    private var bpmText: String {
        let bpm: Double = Double(status[6]) / 100.0
        return String(format: AureaText.t("headbop_bpm_value"), Int(bpm.rounded()))
    }

    private var tapLabel: String {
        let base: String = AureaText.t("headbop_tap")
        return taps.count >= 2 ? base + " · \(taps.count)" : base
    }

    private func tap(layer: Int64) {
        // Tempo da composição do toque: o cabeçote no 1º toque + o relógio desde então.
        let now: Double = ProcessInfo.processInfo.systemUptime
        let fps: Double = Double(model.status.compFps > 0 ? model.status.compFps : 30)
        let last: Double = taps.last ?? 0
        if taps.isEmpty || (now - tapStartClock) - (last - tapStartComp) > 3 {
            taps = []
            tapStartClock = now
            tapStartComp = Double(model.status.playhead) / fps
        }
        taps.append(tapStartComp + (now - tapStartClock))
        if taps.count >= 2 {
            let values: [NSNumber] = taps.map { NSNumber(value: $0) }
            if model.engine.headbopSetTaps(values, effect: effectId, layer: layer) { model.refreshModel(force: true) }
        }
    }

    @ViewBuilder private func actionRow(layer: Int64) -> some View {
        HStack(spacing: 6) {
            if running {
                chip(AureaText.t("headbop_cancel"), "fx.headbop.cancel", true) {
                    model.engine.headbopCancel()
                    task?.cancel()
                }
            } else {
                chip(AureaText.t(status[5] > 0 ? "headbop_reanalyze" : "headbop_analyze"), "fx.headbop.analyze", false) {
                    analyze(layer: layer)
                }
            }
            chip(tapLabel, "fx.headbop.tap", !taps.isEmpty) { tap(layer: layer) }
            chip(AureaText.t("headbop_bake"), "fx.headbop.bake", status[7] == 1) {
                if model.engine.headbopBake(effect: effectId, layer: layer) > 0 { model.refreshModel(force: true) }
            }
        }
    }

    var body: some View {
        let layer: Int64 = model.primarySelection ?? 0
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("headbop_note"))
                .font(.system(size: 11.5)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Text(stateText)
                .font(.system(size: 11.5, weight: .semibold)).foregroundStyle(AureaColors.text)
                .accessibilityIdentifier("fx.headbop.state")
            if status[6] > 0 {
                Text(bpmText)
                    .font(.system(size: 11.5)).foregroundStyle(AureaColors.muted)
                    .accessibilityIdentifier("fx.headbop.bpm")
            }
            if running {
                ProgressView(value: progressValue)
                    .accessibilityIdentifier("fx.headbop.progress")
            }
            if status[0] == 3 && !running {
                Text(AureaText.t("headbop_failed")).font(.system(size: 11.5)).foregroundStyle(Color.red)
            }
            actionRow(layer: layer)
        }
        .onAppear { refresh() }
        .onReceive(timer) { _ in refresh() }
    }
}
