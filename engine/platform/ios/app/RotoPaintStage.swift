// Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects).
// "Pintar recorte" no cartão liga o modo: o palco recebe traços de Objeto
// (verde) ou Fundo (vermelho); o motor (EngineRoto.cpp) leva os pontos da
// composição para a camada, guarda no efeito e refaz os recortes.
import SwiftUI
import Vision
import ImageIO

/// O aviso de falha pelo motivo do motor (Renderer::roto_status [3]).
func rotoFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "om_roto_failed_model"
    case 3: return "om_roto_failed_decode"
    default: return "roto_failed"
    }
}

@MainActor final class RotoPaintState: ObservableObject {
    static let shared = RotoPaintState()

    @Published var layer: Int64 = 0
    @Published var effect: UInt32 = 0
    @Published var active = false
    var previousView: UInt32 = 0
    @Published var background = false
    /// Visualização DENTRO do modo pintar: 0 final, 1 máscara, 2 sobreposição.
    @Published var view: UInt32 = 2
    /// Raio do pincel em px da composição.
    @Published var radius: CGFloat = 24
    /// Traço em curso, em px da composição (desenho ao vivo).
    @Published var live: [CGPoint] = []
    /// [propagados, total, propagando, falhou, traços aqui, traços total,
    ///  quadro atual pronto, quadros do clipe prontos, quadros do clipe, há traços]
    @Published var status: [Int64] = Array(repeating: 0, count: 10)

    func painting(_ model: AureaModel) -> Bool { active && model.primarySelection == layer }

    func end(_ model: AureaModel) {
        guard active else { return }
        _ = model.engine.rotoSetView(previousView, effect: effect, layer: layer)
        active = false
        live = []
        model.refreshModel(force: true)
    }

    func refresh(_ model: AureaModel) {
        status = model.engine.rotoStatus(effect: effect, layer: layer).map { $0.int64Value }
        if status.count < 10 { status = Array(repeating: 0, count: 10) }
    }
}

@MainActor struct RotoPaintStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    var body: some View {
        if roto.painting(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    let color = roto.background ? Color(red: 0.9, green: 0.28, blue: 0.3).opacity(0.8)
                                                : Color(red: 0.19, green: 0.77, blue: 0.42).opacity(0.8)
                    let pts = roto.live.map { CGPoint(x: placed.origin.x + $0.x * placed.scale, y: placed.origin.y + $0.y * placed.scale) }
                    let r = roto.radius * placed.scale
                    if pts.count > 1 {
                        var path = Path()
                        path.addLines(pts)
                        context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: r * 2, lineCap: .round, lineJoin: .round))
                    }
                    if let last = pts.last {
                        context.fill(Path(ellipseIn: CGRect(x: last.x - r, y: last.y - r, width: r * 2, height: r * 2)), with: .color(color))
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        let p = CGPoint(x: (v.location.x - placed.origin.x) / max(placed.scale, 0.0001),
                                        y: (v.location.y - placed.origin.y) / max(placed.scale, 0.0001))
                        roto.live.append(p)
                    }
                    .onEnded { _ in finish(scale: placed.scale) })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("roto_paint"))
                .accessibilityHint(AureaText.t("roto_hint"))
                .accessibilityIdentifier("stage.roto_paint")
            }
        }
    }

    /// Um toque seleciona o objeto (RotoClick no motor); arrastar pinta um traço.
    /// Soltar manda tudo ao motor, em um passo de desfazer.
    private func finish(scale: CGFloat) {
        let pts = roto.live
        roto.live = []
        guard let first = pts.first else { return }
        let reach = pts.map { hypot($0.x - first.x, $0.y - first.y) }.max() ?? 0
        if reach * scale <= 10 {
            if model.engine.rotoAddClick(x: Float(first.x), y: Float(first.y), background: roto.background, effect: roto.effect, forLayer: roto.layer) {
                model.refreshModel(force: true)
                roto.refresh(model)
            }
            return
        }
        var xy: [NSNumber] = []
        xy.reserveCapacity(pts.count * 2)
        for p in pts { xy.append(NSNumber(value: Float(p.x))); xy.append(NSNumber(value: Float(p.y))) }
        if model.engine.rotoAddStroke(xy, background: roto.background, radius: Float(roto.radius), effect: roto.effect, forLayer: roto.layer) {
            model.refreshModel(force: true)
            roto.refresh(model)
        }
    }
}

/// Dentro do cartão do Rotobrush: o botão "Pintar recorte" e, no modo, os controles.
@MainActor struct RotoPaintCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    let effectId: UInt32
    let previousView: UInt32
    private let timer = Timer.publish(every: 0.3, on: .main, in: .common).autoconnect()

    private func chip(_ key: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        chipText(AureaText.t(key), key, id, on, action)
    }

    private func chipText(_ text: String, _ key: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 12)
                .frame(minHeight: 36)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.text.opacity(0.18) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(text)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }

    var body: some View {
        let layer = model.primarySelection ?? 0
        let mine = roto.active && roto.layer == layer && roto.effect == effectId
        VStack(alignment: .leading, spacing: 6) {
            if !mine {
                chip("roto_paint", "fx.roto.paint", false) {
                    // Outra sessão aberta: devolve a visualização dela antes.
                    roto.end(model)
                    model.pause()
                    roto.background = false
                    roto.view = 2
                    roto.layer = layer
                    roto.effect = effectId
                    roto.previousView = previousView
                    roto.active = true
                    _ = model.engine.rotoSetView(2, effect: effectId, layer: layer)
                    roto.refresh(model)
                    model.refreshModel(force: true)
                }
            } else {
                Text(AureaText.t("roto_hint"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                // O que está acontecendo, SEMPRE visível: este quadro, o clipe, falha.
                if roto.status[9] != 0 {
                    let ready = roto.status[6] != 0
                    Text(AureaText.t(ready ? "roto_frame_ready" : "roto_frame_pending"))
                        .font(.system(size: 11.5, weight: .semibold))
                        .foregroundStyle(ready ? AureaColors.success : AureaColors.text)
                        .accessibilityIdentifier("fx.roto.frame")
                    if roto.status[8] > 1 {
                        Text(String(format: AureaText.t("roto_clip_status"), Int(roto.status[7]), Int(roto.status[8])))
                            .font(.system(size: 11.5))
                            .foregroundStyle(AureaColors.muted)
                            .accessibilityIdentifier("fx.roto.clip")
                    }
                    // Bugs 0.3.4: status[3] traz o motivo (2 rede/4 memória,
                    // 3 leitura do vídeo); o aviso diz o que fazer para cada um.
                    if roto.status[3] != 0 && roto.status[2] == 0 {
                        Text(AureaText.t(rotoFailedKey(roto.status[3])))
                            .font(.system(size: 11.5))
                            .foregroundStyle(AureaColors.danger)
                            .fixedSize(horizontal: false, vertical: true)
                            .accessibilityIdentifier("fx.roto.failed")
                    }
                }
                // Ver o resultado sem sair do modo pintar.
                HStack(spacing: 6) {
                    Text(AureaText.t("roto_view_label")).font(.system(size: 11.5)).foregroundStyle(AureaColors.muted)
                    ForEach([(UInt32(0), "roto_view_final", "fx.roto.view.final"),
                             (UInt32(2), "roto_view_overlay", "fx.roto.view.overlay"),
                             (UInt32(1), "roto_view_mask", "fx.roto.view.mask")], id: \.2) { mode, key, id in
                        chip(key, id, roto.view == mode) {
                            roto.view = mode
                            _ = model.engine.rotoSetView(mode, effect: effectId, layer: layer)
                            model.refreshModel(force: true)
                        }
                    }
                }
                HStack(spacing: 6) {
                    chip("roto_object", "fx.roto.object", !roto.background) { roto.background = false }
                    chip("roto_background", "fx.roto.background", roto.background) { roto.background = true }
                    chip("roto_undo", "fx.roto.undo", false) {
                        if model.engine.rotoUndoStroke(effect: effectId, layer: layer) {
                            model.refreshModel(force: true)
                            roto.refresh(model)
                        }
                    }
                }
                Text(AureaText.t("roto_brush_size"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                Slider(value: $roto.radius, in: 4...160)
                    .accessibilityLabel(AureaText.t("roto_brush_size"))
                    .accessibilityIdentifier("fx.roto.size")
                HStack(spacing: 6) {
                    let propagateKey = roto.status[8] > 1 ? "roto_propagate_count" : "roto_propagate"
                    chipText(roto.status[8] > 1 ? String(format: AureaText.t(propagateKey), Int(roto.status[8])) : AureaText.t(propagateKey),
                             propagateKey, "fx.roto.propagate", roto.status[2] != 0) {
                        if roto.status[2] != 0 { model.engine.rotoCancel() }
                        else { _ = model.engine.rotoPropagate(effect: effectId, layer: layer) }
                        roto.refresh(model)
                    }
                    chip("roto_done", "fx.roto.done", false) {
                        roto.end(model)
                    }
                }
                if roto.status[2] != 0 && roto.status[1] > 0 {
                    Text(String(format: AureaText.t("roto_progress"), Int(roto.status[0]), Int(roto.status[1])))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.roto.progress")
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onReceive(timer) { _ in if mine { roto.refresh(model) } }
        // Fechar o painel encerra o modo pintar (Stage Android: DisposableEffect):
        // o palco deixa de comer toques e a sobreposição não fica no preview.
        .onDisappear {
            if roto.active && roto.layer == layer && roto.effect == effectId { roto.end(model) }
        }
    }
}

// =============================================================================
//  "Remover fundo" (0.4.5, aurea.key.background_remove). O recorte por quadro é
//  do motor (EngineBgRemove.cpp: cache salvo com o projeto, estável no tempo);
//  aqui ficam a análise do clipe (progresso, cancelar), o estado deste quadro e
//  os avisos. Os controles de refino são as linhas normais do efeito.
//  Par Android: BgRemoveControls (editor/panels/BgRemovePanel.kt).
// =============================================================================

let bgRemoveEffectKey = "aurea.key.background_remove"

/// O aviso de falha pelo motivo do motor (DepthMapService::Failure).
func bgRemoveFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "bg_remove_failed_model"
    case 3: return "bg_remove_failed_decode"
    default: return "bg_remove_failed_frames"
    }
}

/// No topo do cartão do Remover fundo: análise do clipe, progresso e estado.
@MainActor struct BgRemoveCardTools: View {
    @EnvironmentObject private var model: AureaModel
    let effectId: UInt32
    /// [analisados, total, analisando, falhou, quadro pronto, prontos no clipe, quadros do clipe, modelo, válido]
    @State private var status: [Int64] = Array(repeating: 0, count: 9)
    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    @State private var ticks = 0

    private func refresh() {
        guard let layer = model.primarySelection else { return }
        let next = model.engine.bgRemoveStatus(effect: effectId, layer: layer).map { $0.int64Value }
        // Render ocupado (válido = 0): fica o último estado, sem piscar.
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

    var body: some View {
        let layer = model.primarySelection ?? 0
        let video = model.layers.first { $0.id == layer }?.kind == 1
        let running = status[2] != 0
        let clipFrames = status[6]
        let readyFrames = status[5]
        let requestedPerson = Int(model.effectParams.first { $0.index == 10 }?.scalar ?? 0) == 1
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("bg_remove_note"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            if status[8] != 0 {
                let ready = status[4] != 0
                Text(AureaText.t(ready ? "bg_remove_frame_ready" : "bg_remove_frame_pending"))
                    .font(.system(size: 11.5, weight: .semibold))
                    .foregroundStyle(ready ? AureaColors.success : AureaColors.text)
                    .accessibilityIdentifier("fx.bg.frame")
                if video && clipFrames > 1 {
                    Text(readyFrames >= clipFrames ? AureaText.t("bg_remove_clip_done")
                         : String(format: AureaText.t("bg_remove_clip_status"), Int(readyFrames), Int(clipFrames)))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.bg.clip")
                }
                if requestedPerson && status[7] == 0 {
                    Text(AureaText.t("bg_remove_person_unavailable"))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("fx.bg.person")
                }
                if status[3] != 0 && !running {
                    Text(AureaText.t(bgRemoveFailedKey(status[3])))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.danger)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("fx.bg.failed")
                }
            }
            if video {
                HStack(spacing: 6) {
                    if running {
                        chip(AureaText.t("bg_remove_cancel"), "fx.bg.cancel", true) {
                            model.engine.bgRemoveCancel()
                            status[2] = 0
                        }
                    } else if readyFrames < clipFrames || clipFrames <= 1 {
                        let label = clipFrames > 1 ? String(format: AureaText.t("bg_remove_analyze_count"), Int(clipFrames))
                                                   : AureaText.t("bg_remove_analyze")
                        chip(label, "fx.bg.analyze", false) {
                            if model.status.playing != 0 { model.pause() }
                            if model.engine.bgRemoveAnalyze(effect: effectId, layer: layer) {
                                status[2] = 1; status[0] = 0; status[1] = Swift.max(1, clipFrames)
                            } else {
                                model.toast = AureaText.t("bg_remove_failed")
                            }
                        }
                    }
                }
                if running && status[1] > 0 {
                    let done = Swift.min(Swift.max(0, status[0]), status[1])
                    let label = String(format: AureaText.t("bg_remove_progress"), Int(done), Int(status[1]))
                    ProgressView(value: Double(done), total: Double(status[1]))
                        .tint(AureaColors.keyframe)
                        .accessibilityLabel(label)
                        .accessibilityIdentifier("fx.bg.progressBar")
                    Text(label)
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.bg.progress")
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onAppear { refresh() }
        .onReceive(timer) { _ in
            // Analisando ou com este quadro pendente: 4×/s; parado, ~1×/s.
            ticks += 1
            if status[2] != 0 || status[4] == 0 || ticks % 4 == 0 { refresh() }
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
    nonisolated private static func detectPhoto(_ path: String) -> (Data, Int, Float)? {
        let url = path.contains("://") ? URL(string: path) : URL(fileURLWithPath: path)
        guard let url, let src = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateThumbnailAtIndex(src, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                  kCGImageSourceThumbnailMaxPixelSize: 1024, kCGImageSourceCreateThumbnailWithTransform: true] as CFDictionary)
        else { return nil }
        let w = Float(image.width), h = Float(image.height)
        let request = VNDetectFaceLandmarksRequest()
        try? VNImageRequestHandler(cgImage: image, options: [:]).perform([request])
        var rows: [Float] = []
        var person = 0
        for face in (request.results ?? []).prefix(4) {
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
        let kind = model.layers.first { $0.id == layer }?.kind ?? -1
        let effect = effectId
        task = Task {
            defer { task = nil; detecting = -1 }
            guard engine.headbopBegin(effect: effect, layer: layer) else { return }
            if !path.isEmpty {
                detecting = 0
                if kind == 1 {
                    if let url = path.contains("://") ? URL(string: path) : URL(fileURLWithPath: path) {
                        _ = try? await MocapDetector.run(url: url, reduced: false, progress: { p in
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
