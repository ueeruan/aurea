// Real Camera: a câmera 3D segue o iPhone. CoreMotion (giroscópio +
// acelerômetro, sem bússola) vira a rotação da camada Câmera relativa à
// postura em que a sessão começou ("Recentrar" zera). "Gravar" toca a
// timeline e escreve keyframes de Rotação X/Y/Z no quadro da prévia a cada
// leitura. Só rotação: o aparelho não mede posição sem AR.
// Par do Android: editor/RealCamera.kt.
import CoreMotion
import SwiftUI

@MainActor final class RealCameraState: ObservableObject {
    static let shared = RealCameraState()

    @Published var layer: Int64 = 0
    @Published var active = false
    @Published var recording = false
    /// Multiplica os graus do aparelho (0,5 = metade, 2 = dobro).
    @Published var sensitivity: Double = 1
    @Published var inverted = false
    @Published var available = true
    @Published var lastFrame: Int64 = -1

    private let motion = CMMotionManager()
    private var reference: CMAttitude?
    private var smooth = (x: 0.0, y: 0.0, z: 0.0)
    private var lastSent = Date.distantPast

    func painting(_ model: AureaModel) -> Bool { active && model.layers.contains { $0.id == layer } }

    func start(_ model: AureaModel, layer id: Int64) {
        layer = id
        recording = false
        lastFrame = -1
        reference = nil
        smooth = (0, 0, 0)
        available = motion.isDeviceMotionAvailable
        active = true
        guard available else { return }
        motion.deviceMotionUpdateInterval = 1.0 / 30.0
        motion.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: .main) { [weak self, weak model] data, _ in
            guard let self, let model, let data else { return }
            self.consume(data.attitude, model)
        }
    }

    func stop(_ model: AureaModel) {
        guard active else { return }
        if recording { recording = false; model.pause() }
        motion.stopDeviceMotionUpdates()
        active = false
    }

    func recenter() { reference = nil; smooth = (0, 0, 0) }

    func toggleRecording(_ model: AureaModel) {
        if recording {
            recording = false
            model.pause()
        } else {
            lastFrame = -1
            recording = true
            model.realCameraBeginRecording(layer)
        }
    }

    private func consume(_ attitude: CMAttitude, _ model: AureaModel) {
        guard active else { return }
        let a = attitude.copy() as! CMAttitude
        if let reference { a.multiply(byInverseOf: reference) } else { reference = attitude.copy() as? CMAttitude; return }
        // Suaviza (filtro simples): o sensor tem ruído de ~0,2°.
        smooth.x += (a.pitch - smooth.x) * 0.45
        smooth.y += (a.yaw - smooth.y) * 0.45
        smooth.z += (a.roll - smooth.z) * 0.45
        let k = sensitivity * (inverted ? -1 : 1) * 180 / .pi
        // yaw = pan (Rotação Y); pitch = tilt (Rotação X); roll = Rotação Z.
        let rx = Float(smooth.x * k), ry = Float(-smooth.y * k), rz = Float(-smooth.z * k)
        guard rx.isFinite, ry.isFinite, rz.isFinite else { return }
        let now = Date()
        guard now.timeIntervalSince(lastSent) >= 0.025 else { return }
        lastSent = now
        if recording && model.status.playing != 0 {
            let frame = model.frame
            if frame != lastFrame {
                lastFrame = frame
                model.realCameraKey(layer, rx: rx, ry: ry, rz: rz)
            }
        } else {
            if recording && model.status.playing == 0 { recording = false }   // chegou ao fim
            model.realCameraApply(layer, rx: rx, ry: ry, rz: rz)
        }
    }
}

/// A faixa no alto do palco enquanto a Real Camera está ligada.
@MainActor struct RealCameraStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var cam = RealCameraState.shared

    private func chip(_ text: String, _ id: String, _ on: Bool, enabled: Bool = true, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 11.5, weight: .semibold))
                .foregroundStyle(on ? AureaColors.onAccent : (enabled ? AureaColors.text : AureaColors.disabled))
                .padding(.horizontal, 10)
                .frame(minHeight: 34)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.accent : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .accessibilityLabel(text)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }

    var body: some View {
        if cam.painting(model) {
            VStack {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 7) {
                        Image(systemName: "camera.fill").font(.system(size: 12)).foregroundStyle(AureaColors.accent)
                        Text(AureaText.t("real_camera_title")).font(.system(size: 12, weight: .semibold)).foregroundStyle(AureaColors.text)
                        Text(!cam.available ? AureaText.t("real_camera_no_sensor")
                             : cam.recording ? String(format: AureaText.t("real_camera_recording"), Int(max(cam.lastFrame, 0)))
                             : AureaText.t("real_camera_hint"))
                            .font(.system(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(2)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    HStack(spacing: 6) {
                        chip(AureaText.t(cam.recording ? "real_camera_stop" : "real_camera_record"), "realcam.gravar", cam.recording,
                             enabled: cam.available) { cam.toggleRecording(model) }
                        chip(AureaText.t("real_camera_recenter"), "realcam.recentrar", false) { cam.recenter() }
                        chip(AureaText.t("real_camera_invert"), "realcam.inverter", cam.inverted) { cam.inverted.toggle() }
                        chip(AureaText.t("real_camera_done"), "realcam.concluir", false) { cam.stop(model) }
                    }
                    HStack(spacing: 8) {
                        Text(AureaText.t("real_camera_sensitivity")).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                        Slider(value: $cam.sensitivity, in: 0.25...3)
                            .accessibilityLabel(AureaText.t("real_camera_sensitivity"))
                            .accessibilityIdentifier("realcam.sens")
                        Text("\(Int(cam.sensitivity * 100))%").font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                    }
                }
                .padding(.horizontal, 10).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.editorPanelHigh))
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.accent.opacity(0.5), lineWidth: 1))
                .padding(.horizontal, 8).padding(.top, 8)
                .accessibilityIdentifier("realcam.bar")
                Spacer()
            }
            .onDisappear { if cam.active { cam.stop(model) } }
        }
    }
}
