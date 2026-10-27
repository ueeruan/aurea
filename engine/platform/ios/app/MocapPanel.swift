// =============================================================================
//  Motion Capture (0.3.6 BETA), iOS — par do Android (mocap/MocapDetector.kt,
//  mocap/MocapFlow.kt, editor/panels/MocapSection.kt).
//
//  Detecção NO APARELHO com o Vision: VNDetectHumanBodyPoseRequest (19 pontos
//  2D) e, no iOS 17+, VNDetectHumanBodyPose3DRequest (17 pontos em metros).
//  Os quadros vêm do AVAssetReader (decodificação em sequência, um por vez,
//  no passo do modo reduzido) e viram o formato do motor (rig/Mocap.hpp):
//  33 pontos na ordem do modelo de corpo, ausentes com confiança 0. As
//  regras (suavização, 3D, rotações, retargeting, foot lock, keys) estão no
//  motor C++; aqui só se detecta e se conduz o painel.
// =============================================================================
import AVFoundation
import PhotosUI
import SwiftUI
import UIKit
import UniformTypeIdentifiers
import Vision

// MARK: - Detecção (Vision)

enum MocapDetector {
    static let frameFloats = 2 + 33 * 4
    static let batch = 32

    struct Failure: Error {}

    /// Analisa o vídeo e manda os quadros para `sink` (Data de floats, quantos,
    /// largura/altura). `progress` 0..1. Cancelável (Task). Devolve os quadros analisados.
    static func run(url: URL, reduced: Bool, progress: @escaping (Float) -> Void,
                    sink: @escaping (Data, Int, Float) -> Void) async throws -> Int {
        let asset = AVURLAsset(url: url)
        guard let track = try await asset.loadTracks(withMediaType: .video).first else { throw Failure() }
        let duration = try await asset.load(.duration).seconds
        let transform = try await track.load(.preferredTransform)
        let natural = try await track.load(.naturalSize)
        guard duration > 0 else { throw Failure() }
        let reader = try AVAssetReader(asset: asset)
        // Um quadro por vez (o Vision reduz para a rede dele); o modo reduzido
        // analisa metade dos quadros.
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
        ])
        output.alwaysCopiesSampleData = false
        guard reader.canAdd(output) else { throw Failure() }
        reader.add(output)
        guard reader.startReading() else { throw Failure() }
        defer { reader.cancelReading() }

        let orientation = Self.orientation(transform)
        let rotated = orientation == .right || orientation == .left
        let aspect = Float(rotated ? natural.height / natural.width : natural.width / natural.height)
        let interval = 1.0 / (reduced ? 15.0 : 30.0)
        var nextTime = -Double.infinity
        var buffer = [Float](repeating: 0, count: batch * frameFloats)
        var pending = 0
        var analyzed = 0
        // Um modo por vídeo (o motor não mistura metros com a imagem): decide no
        // primeiro quadro com gente — 3D se o Vision 3D (iOS 17+) também a achou.
        var mode = 0   // 0 indeciso · 1 3D · 2 2D

        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            let time = CMSampleBufferGetPresentationTimeStamp(sample).seconds
            guard time.isFinite, time >= nextTime - 1e-4, let pixels = CMSampleBufferGetImageBuffer(sample) else { continue }
            nextTime = time + interval
            let base = pending * frameFloats
            buffer[base] = Float(time)
            buffer[base + 1] = 0
            for i in 2..<frameFloats { buffer[base + i] = 0 }
            let handler = VNImageRequestHandler(cvPixelBuffer: pixels, orientation: orientation, options: [:])
            let request2D = VNDetectHumanBodyPoseRequest()
            if #available(iOS 17.0, *), mode != 2 {
                let request3D = VNDetectHumanBodyPose3DRequest()
                try? handler.perform([request2D, request3D])
                if mode == 0, request2D.results?.first != nil { mode = request3D.results?.first != nil ? 1 : 2 }
                if mode == 1, let body = request3D.results?.first {
                    _ = fill3D(body, flat: request2D.results?.first, into: &buffer, at: base)
                }
            } else {
                try? handler.perform([request2D])
                if mode == 0, request2D.results?.first != nil { mode = 2 }
            }
            if mode == 2, let body = request2D.results?.first { fill2D(body, into: &buffer, at: base) }
            buffer[base + 1] = mode == 1 ? 1 : 0
            pending += 1
            analyzed += 1
            if pending == batch {
                sink(buffer.withUnsafeBufferPointer { Data(buffer: $0) }, pending, aspect)
                pending = 0
            }
            progress(Float(min(1, time / duration)))
        }
        try Task.checkCancellation()
        if reader.status == .failed { throw Failure() }
        if pending > 0 {
            sink(buffer.withUnsafeBufferPointer { Data(buffer: UnsafeBufferPointer(rebasing: $0[0..<(pending * frameFloats)])) }, pending, aspect)
        }
        progress(1)
        return analyzed
    }

    /// Vision 2D (19 pontos) → ordem de 33; y para baixo; sem profundidade.
    private static func fill2D(_ body: VNHumanBodyPoseObservation, into buffer: inout [Float], at base: Int) {
        guard let points = try? body.recognizedPoints(.all) else { return }
        let map: [(VNHumanBodyPoseObservation.JointName, Int)] = [
            (.nose, 0), (.leftEye, 2), (.rightEye, 5), (.leftEar, 7), (.rightEar, 8),
            (.leftShoulder, 11), (.rightShoulder, 12), (.leftElbow, 13), (.rightElbow, 14),
            (.leftWrist, 15), (.rightWrist, 16), (.leftHip, 23), (.rightHip, 24),
            (.leftKnee, 25), (.rightKnee, 26), (.leftAnkle, 27), (.rightAnkle, 28),
        ]
        for (name, index) in map {
            guard let p = points[name], p.confidence > 0 else { continue }
            let o = base + 2 + index * 4
            buffer[o] = Float(p.location.x)
            buffer[o + 1] = Float(1 - p.location.y)
            buffer[o + 2] = 0
            // A confiança do Vision é mais baixa que a do ML Kit para o mesmo ponto
            // visível (≈0,3–0,6): reescala para o corte de 0,5 do motor (calibração inicial).
            buffer[o + 3] = min(1, Float(p.confidence) / 0.6)
        }
    }

    /// Vision 3D (17 pontos, metros, relativos à câmera) → ordem de 33 em metros
    /// no contrato do motor (x direita, y para baixo, z para longe). Sem orelhas
    /// nem nariz no 3D: saem da cabeça e do tronco (a cabeça não gira sozinha).
    @available(iOS 17.0, *)
    private static func fill3D(_ body: VNHumanBodyPose3DObservation, flat: VNHumanBodyPoseObservation?,
                               into buffer: inout [Float], at base: Int) -> Bool {
        typealias J = VNHumanBodyPose3DObservation.JointName
        func point(_ name: J) -> SIMD3<Float>? {
            guard let m = try? body.cameraRelativePosition(name) else { return nil }
            // Câmera: x direita, y cima, olhando para −z → contrato: y baixo, z longe.
            return SIMD3<Float>(m.columns.3.x, -m.columns.3.y, -m.columns.3.z)
        }
        let map: [(J, Int)] = [
            (.leftShoulder, 11), (.rightShoulder, 12), (.leftElbow, 13), (.rightElbow, 14),
            (.leftWrist, 15), (.rightWrist, 16), (.leftHip, 23), (.rightHip, 24),
            (.leftKnee, 25), (.rightKnee, 26), (.leftAnkle, 27), (.rightAnkle, 28),
        ]
        var got: [Int: SIMD3<Float>] = [:]
        for (name, index) in map { if let p = point(name) { got[index] = p } }
        guard let ls = got[11], let rs = got[12], got[23] != nil, got[24] != nil else { return false }
        // Espelho? O lado esquerdo do 3D tem de cair do mesmo lado da imagem que o do 2D.
        var flip: Float = 1
        if let flat, let l = try? flat.recognizedPoint(.leftShoulder), let r = try? flat.recognizedPoint(.rightShoulder),
           l.confidence > 0.2, r.confidence > 0.2, ((l.location.x - r.location.x) > 0) != ((ls.x - rs.x) > 0) {
            flip = -1
        }
        func write(_ p: SIMD3<Float>, _ index: Int, _ confidence: Float = 0.9) {
            let o = base + 2 + index * 4
            buffer[o] = p.x * flip; buffer[o + 1] = p.y; buffer[o + 2] = p.z; buffer[o + 3] = confidence
        }
        for (index, p) in got { write(p, index) }
        if let head = point(.centerHead), let neck = point(.centerShoulder) {
            let side = simd_normalize(ls - rs)
            let up = simd_normalize(neck - head)            // y para baixo: aponta para baixo
            var front = simd_cross(up, side)                // para a câmera quando de frente
            if simd_length(front) < 1e-4 { front = SIMD3<Float>(0, 0, -1) }
            front = simd_normalize(front)
            write(head + side * 0.08, 7, 0.6)
            write(head - side * 0.08, 8, 0.6)
            write(head + front * 0.1 + up * 0.02, 0, 0.6)
        }
        return true
    }

    /// Orientação do vídeo (a transformação da trilha) para o Vision.
    private static func orientation(_ t: CGAffineTransform) -> CGImagePropertyOrientation {
        switch (t.a, t.b, t.c, t.d) {
        case (0, 1, -1, 0): return .right
        case (0, -1, 1, 0): return .left
        case (-1, 0, 0, -1): return .down
        default: return .up
        }
    }
}

// MARK: - Fluxo

@MainActor
final class MocapFlow: ObservableObject {
    enum Step { case needRig, choose, analyzing, ready }

    private let engine: AureaEngine
    let layer: Int64
    @Published private(set) var status: Int32
    @Published private(set) var step: Step
    @Published private(set) var progress: Float = 0
    /// 0 nenhum · 1 sem pessoa · 2 vídeo ilegível · 3 falha ao gravar as keys
    @Published var error = 0
    @Published private(set) var report: [Float] = []
    @Published private(set) var previewOn = false
    @Published private(set) var applied = -1
    @Published var intensity: Float = 1
    @Published var smoothing: Float = 0.5
    @Published var speed: Float = 1
    @Published var yaw: Float = 0
    @Published var floor: Float = 0
    @Published var footLock = true
    @Published var reduced = false
    var onChanged: () -> Void = {}

    private var task: Task<Void, Never>?
    private var tune: Task<Void, Never>?

    init(engine: AureaEngine, layer: Int64) {
        self.engine = engine
        self.layer = layer
        let s = engine.mocapBegin(layer)
        status = s
        step = s >= 2 ? .choose : .needRig
    }

    private var params: [NSNumber] {
        [intensity, smoothing, speed, yaw, floor, footLock ? 1 : 0, 1, 1].map { NSNumber(value: $0) }
    }

    private var lastCheck = Date.distantPast

    func recheckRig() {
        guard step == .needRig, Date().timeIntervalSince(lastCheck) > 1 else { return }
        lastCheck = Date()
        status = engine.mocapBegin(layer)
        if status >= 2 { step = .choose }
    }

    func analyze(_ url: URL) {
        guard status >= 2 else { return }
        cancel()
        error = 0; applied = -1; report = []; previewOn = false
        step = .analyzing; progress = 0
        let engine = self.engine, layer = self.layer, reduced = self.reduced, params = self.params
        task = Task { [weak self] in
            status_update: do {
                let begun = engine.mocapBegin(layer)
                await MainActor.run { self?.status = begun }
                let frames = try await MocapDetector.run(url: url, reduced: reduced, progress: { p in
                    Task { @MainActor in self?.progress = 0.85 * p }
                }, sink: { data, n, aspect in
                    _ = engine.mocapPushFrames(data, frames: Int32(n), aspect: aspect)
                })
                guard frames >= 2 else { await MainActor.run { self?.error = 1; self?.step = .choose }; break status_update }
                let poll = Task { @MainActor in
                    while !Task.isCancelled {
                        self?.progress = 0.85 + 0.15 * engine.mocapProgress()
                        try? await Task.sleep(nanoseconds: 100_000_000)
                    }
                }
                let ok = await Task.detached(priority: .userInitiated) { engine.mocapAnalyze(params) }.value
                poll.cancel()
                try Task.checkCancellation()
                await MainActor.run {
                    guard let self else { return }
                    if !ok { self.error = 1; self.step = .choose; return }
                    self.report = engine.mocapReport().map(\.floatValue)
                    self.progress = 1
                    self.step = .ready
                    self.setPreview(true)
                }
            } catch is CancellationError {
                // cancel() já voltou a tela e avisou o motor.
            } catch {
                await MainActor.run { self?.error = 2; self?.step = .choose }
            }
            // Arquivo temporário do seletor/câmera.
            try? FileManager.default.removeItem(at: url)
        }
    }

    func cancel() {
        task?.cancel(); task = nil
        engine.mocapCancel()
        if step == .analyzing { step = .choose }
    }

    func setPreview(_ on: Bool) {
        previewOn = engine.mocapPreview(on) && on
        onChanged()
    }

    func retune() {
        guard step == .ready else { return }
        tune?.cancel()
        let engine = self.engine, params = self.params
        tune = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 120_000_000)
            guard !Task.isCancelled else { return }
            let ok = await Task.detached(priority: .userInitiated) { engine.mocapAnalyze(params) }.value
            guard ok, let self else { return }
            self.report = engine.mocapReport().map(\.floatValue)
            self.onChanged()
        }
    }

    func apply() {
        guard step == .ready else { return }
        tune?.cancel()
        let engine = self.engine
        Task { [weak self] in
            let n = await Task.detached(priority: .userInitiated) { engine.mocapApply() }.value
            guard let self else { return }
            self.previewOn = false
            self.applied = Int(n)
            if n < 0 { self.error = 3 }
            self.onChanged()
        }
    }

    func dispose() {
        task?.cancel(); tune?.cancel()
        engine.mocapCancel()
        engine.mocapEnd()
        onChanged()
    }

    /// Membros com mais de 25% dos quadros incertos (0 tronco … 5 perna dir.).
    var uncertainLimbs: [Int] {
        guard report.count >= 10 else { return [] }
        return (0..<6).filter { report[4 + $0] > 0.25 }
    }
}

// MARK: - Painel

/// MOTION CAPTURE (BETA) no painel 3D da camada com rig (par de MocapSection.kt).
struct MocapSection: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var flow: MocapFlow
    /// nil = o AutoRig 3D do modelo (AureaModel.startAutoRig), como no Android.
    let onAutoRig: (() -> Void)?
    @State private var picking = false
    @State private var recording = false

    /// Uso: `MocapSection(engine: model.engine, layer: layerId).id(layerId)` no painel 3D.
    init(engine: AureaEngine, layer: Int64, onAutoRig: (() -> Void)? = nil) {
        _flow = StateObject(wrappedValue: MocapFlow(engine: engine, layer: layer))
        self.onAutoRig = onAutoRig
    }

    private static let limbKeys = ["mocap_limb_torso", "mocap_limb_head", "mocap_limb_left_arm",
                                   "mocap_limb_right_arm", "mocap_limb_left_leg", "mocap_limb_right_leg"]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 12)
            title("mocap_title")
            switch flow.step {
            case .needRig:
                // Esqueleto do arquivo não humanoide (1): associar os ossos; nenhum (0): AutoRig.
                if flow.status == 1 {
                    note(AureaText.t("mocap_need_map"))
                } else {
                    note(AureaText.t("mocap_need_rig"))
                    Spacer().frame(height: 8)
                    if model.autoRigState == 1 {
                        ProgressView(value: Double(min(1, max(0, model.autoRigProgress)))).tint(AureaColors.accent)
                    } else {
                        card("mocap_autorig", nil, id: "panel3d.mocap.autorig") {
                            if let onAutoRig { onAutoRig() } else { model.startAutoRig() }
                        }
                    }
                }
            case .choose:
                note(AureaText.t("mocap_intro"))
                Spacer().frame(height: 8)
                card("mocap_import", "mocap_import_hint", id: "panel3d.mocap.import") { picking = true }
                card("mocap_record", "mocap_record_hint", id: "panel3d.mocap.record") {
                    if UIImagePickerController.isSourceTypeAvailable(.camera) { recording = true }
                    else { model.toast = AureaText.t("mocap_camera_denied") }
                }
                toggle("mocap_reduced", on: flow.reduced) { flow.reduced = $0 }
                errorLine
            case .analyzing:
                let pct = Int((flow.progress * 100).rounded())
                Text(AureaText.t("mocap_analyzing", max(0, min(100, pct))))
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                Spacer().frame(height: 8)
                ProgressView(value: Double(min(1, max(0, flow.progress))))
                    .tint(AureaColors.accent)
                    .accessibilityIdentifier("panel3d.mocap.progress")
                    .accessibilityLabel(AureaText.t("mocap_analyze"))
                    .accessibilityValue("\(pct)%")
                Button(AureaText.t("mocap_cancel")) { flow.cancel() }
                    .font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    .frame(minHeight: 44)
                    .accessibilityIdentifier("panel3d.mocap.cancel")
            case .ready:
                if flow.report.count >= 2 {
                    note(AureaText.t("mocap_summary", Int(flow.report[0]), Double(flow.report[1])))
                }
                if !flow.uncertainLimbs.isEmpty {
                    let names = flow.uncertainLimbs.map { AureaText.t(Self.limbKeys[$0]) }.joined(separator: ", ")
                    Text(AureaText.t("mocap_low_conf", names)).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                        .fixedSize(horizontal: false, vertical: true).padding(.top, 6)
                        .accessibilityIdentifier("panel3d.mocap.lowconf")
                }
                toggle("mocap_preview", on: flow.previewOn) { flow.setPreview($0) }
                    .accessibilityIdentifier("panel3d.mocap.preview")
                slider("mocap_intensity", value: flow.intensity * 100, range: 0...150, unit: "%", decimals: 0, id: "intensity") { flow.intensity = $0 / 100 }
                slider("mocap_smoothing", value: flow.smoothing * 100, range: 0...100, unit: "%", decimals: 0, id: "smoothing") { flow.smoothing = $0 / 100 }
                slider("mocap_speed", value: flow.speed, range: 0.25...4, unit: "×", decimals: 2, step: 0.05, id: "speed") { flow.speed = $0 }
                slider("mocap_yaw", value: flow.yaw, range: -180...180, unit: "°", decimals: 0, id: "yaw") { flow.yaw = $0 }
                slider("mocap_floor", value: flow.floor * 100, range: -50...50, unit: "%", decimals: 0, id: "floor") { flow.floor = $0 / 100 }
                toggle("mocap_foot_lock", on: flow.footLock) { flow.footLock = $0; flow.retune() }
                Spacer().frame(height: 8)
                card("mocap_apply", "mocap_apply_hint", id: "panel3d.mocap.apply") { flow.apply() }
                if flow.applied >= 0 {
                    Text(AureaText.t("mocap_applied", flow.applied)).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent)
                        .padding(.bottom, 6)
                }
                errorLine
                card("mocap_new_video", nil, id: "panel3d.mocap.another") { picking = true }
            }
            Spacer().frame(height: 6)
            note(AureaText.t("mocap_limits"))
        }
        .onAppear { flow.onChanged = { [weak model] in model?.refreshModel(force: true) } }
        .onDisappear { flow.dispose() }
        // Depois do AutoRig 3D (o projeto muda): o rig apareceu?
        .onReceive(model.objectWillChange) { _ in DispatchQueue.main.async { flow.recheckRig() } }
        .sheet(isPresented: $picking) {
            MocapVideoPicker(onDismiss: { picking = false }, onPick: { url in picking = false; flow.analyze(url) },
                             onError: { picking = false; flow.error = 2 })
        }
        .fullScreenCover(isPresented: $recording) {
            MocapCameraPicker(onDismiss: { recording = false }, onPick: { url in recording = false; flow.analyze(url) })
                .ignoresSafeArea()
        }
    }

    @ViewBuilder private var errorLine: some View {
        let key: String? = flow.error == 1 ? "mocap_err_no_person" : flow.error == 2 ? "mocap_err_video"
            : flow.error == 3 ? "mocap_err_apply" : nil
        if let key {
            Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                .fixedSize(horizontal: false, vertical: true).padding(.vertical, 6)
        }
    }

    private func title(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold))
            .foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4)
            .accessibilityAddTraits(.isHeader)
    }

    private func note(_ text: String) -> some View {
        Text(text).font(.aurea(size: 12)).lineSpacing(2)
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }

    private func card(_ key: String, _ detail: String?, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                if let detail {
                    Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .padding(.horizontal, 14).padding(.vertical, 11)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .padding(.bottom, 6)
        .accessibilityIdentifier(id)
    }

    private func toggle(_ key: String, on: Bool, change: @escaping (Bool) -> Void) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
            Spacer(minLength: 8)
            AureaToggle(checked: on, onCheckedChange: change)
        }
        .frame(minHeight: 48)
        .contentShape(Rectangle())
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(on ? [.isSelected, .isButton] : [.isButton])
        .accessibilityAction { change(!on) }
    }

    /// Régua do mocap: ao soltar, o motor resolve de novo e a prévia acompanha.
    private func slider(_ key: String, value: Float, range: ClosedRange<Float>, unit: String, decimals: Int,
                        step: Float = 1, id: String, onValue: @escaping (Float) -> Void) -> some View {
        let label = AureaText.t(key)
        let shown = String(format: "%.\(decimals)f", value) + unit
        return HStack(spacing: 10) {
            Text(label).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.text)
                .lineLimit(2).frame(width: 94, alignment: .leading)
            AureaSlider(value: value, min: range.lowerBound, max: range.upperBound, step: step,
                        identifier: "panel3d.mocap.\(id)", accessibilityLabel: label, accessibilityValue: shown,
                        onBegin: {}, onValue: { onValue(min(range.upperBound, max(range.lowerBound, $0))) },
                        onEnd: { flow.retune() })
                .frame(height: 44)
            Text(shown).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
                .monospacedDigit().frame(width: 52, alignment: .trailing)
        }
        .frame(minHeight: 52)
    }
}

// MARK: - Seletores

/// Importar vídeo: o seletor do sistema (só vídeos), cópia num temporário do app.
private struct MocapVideoPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL) -> Void
    let onError: () -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var config = PHPickerConfiguration()
        config.filter = .videos
        config.selectionLimit = 1
        config.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: config)
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let parent: MocapVideoPicker
        init(_ parent: MocapVideoPicker) { self.parent = parent }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            guard let item = results.first?.itemProvider, item.hasItemConformingToTypeIdentifier(UTType.movie.identifier) else {
                parent.onDismiss(); return
            }
            item.loadFileRepresentation(forTypeIdentifier: UTType.movie.identifier) { url, _ in
                guard let url, let copy = try? mocapTemporaryCopy(of: url) else {
                    DispatchQueue.main.async { self.parent.onError() }; return
                }
                DispatchQueue.main.async { self.parent.onPick(copy) }
            }
        }
    }
}

/// Gravar movimento: a câmera do sistema em modo vídeo.
private struct MocapCameraPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> UIImagePickerController {
        let picker = UIImagePickerController()
        picker.sourceType = .camera
        picker.mediaTypes = [UTType.movie.identifier]
        picker.cameraCaptureMode = .video
        picker.videoQuality = .typeMedium
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIImagePickerController, context: Context) {}
    final class Coordinator: NSObject, UIImagePickerControllerDelegate, UINavigationControllerDelegate {
        let parent: MocapCameraPicker
        init(_ parent: MocapCameraPicker) { self.parent = parent }
        func imagePickerController(_ picker: UIImagePickerController, didFinishPickingMediaWithInfo info: [UIImagePickerController.InfoKey: Any]) {
            guard let url = info[.mediaURL] as? URL, let copy = try? mocapTemporaryCopy(of: url) else { parent.onDismiss(); return }
            parent.onPick(copy)
        }
        func imagePickerControllerDidCancel(_ picker: UIImagePickerController) { parent.onDismiss() }
    }
}

private func mocapTemporaryCopy(of url: URL) throws -> URL {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("Mocap", isDirectory: true)
    try? FileManager.default.removeItem(at: directory)   // só o vídeo mais recente fica
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let copy = directory.appendingPathComponent(UUID().uuidString + "." + (url.pathExtension.isEmpty ? "mov" : url.pathExtension))
    try FileManager.default.copyItem(at: url, to: copy)
    return copy
}
