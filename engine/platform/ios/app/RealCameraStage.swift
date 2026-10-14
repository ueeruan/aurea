// Real Camera delivers a display-oriented OpenGL pose to the shared camera
// session. ARKit supplies true position only while world tracking is normal;
// devices without AR/camera access retain CoreMotion rotation without position.
import ARKit
import AVFoundation
import CoreMotion
import simd
import SwiftUI

@MainActor final class RealCameraState: NSObject, ObservableObject, @preconcurrency ARSessionDelegate {
    static let shared = RealCameraState()

    @Published var layer: Int64 = 0
    @Published var active = false
    @Published var recording = false
    @Published var sensitivity: Double = 1
    @Published var inverted = false
    /// Andar (botões de segurar), m/s, e × do frente/trás medido pelo AR.
    @Published var walkSpeed: Double = 1
    @Published var depthScale: Double = 1 { didSet { walk(0) } }
    @Published var available = false
    @Published var trackingValid = false
    @Published var positional = false
    @Published var lastFrame: Int64 = -1
    @Published private var trackingMessageKey = "real_camera_tracking_rotation"

    private let motion = CMMotionManager()
    private let arSession = ARSession()
    private weak var model: AureaModel?
    private var generation: UInt64 = 0
    private var usingAR = false

    var statusText: String {
        let tracking = AureaText.t(available ? trackingMessageKey : "real_camera_no_sensor")
        guard recording else { return tracking }
        return String(format: AureaText.t("real_camera_recording"), Int(max(lastFrame, 0))) + " · " + tracking
    }

    func painting(_ model: AureaModel) -> Bool { active && model.layers.contains { $0.id == layer } }

    func start(_ model: AureaModel, layer id: Int64) {
        stopSensors()
        generation &+= 1
        self.model = model
        layer = id
        recording = false
        lastFrame = -1
        positional = false
        trackingValid = false
        guard model.realCameraStart(id) else { active = false; return }
        synchronize(model)
        if ARWorldTrackingConfiguration.isSupported {
            available = true
            trackingMessageKey = "real_camera_tracking_limited"
            authorizeCamera(generation)
        } else {
            startMotion(reason: "real_camera_tracking_rotation")
        }
    }

    func stop(_ model: AureaModel) {
        generation &+= 1
        stopSensors()
        model.realCameraStop()
        self.model = nil
        active = false
        recording = false
        trackingValid = false
        positional = false
    }

    func recenter() {
        guard active, let model else { return }
        _ = model.realCameraRecenter()
        synchronize(model)
    }

    func toggleRecording(_ model: AureaModel) {
        guard active, recording || trackingValid else { return }
        _ = model.realCameraRecord(!recording)
        synchronize(model)
    }

    private func authorizeCamera(_ token: UInt64) {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            startAR()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                DispatchQueue.main.async {
                    guard let self, self.active, self.generation == token else { return }
                    if granted { self.startAR() }
                    else { self.startMotion(reason: "real_camera_camera_permission") }
                }
            }
        default:
            startMotion(reason: "real_camera_camera_permission")
        }
    }

    private func startAR() {
        guard active else { return }
        motion.stopDeviceMotionUpdates()
        usingAR = true
        available = true
        trackingValid = false
        trackingMessageKey = "real_camera_tracking_limited"
        arSession.delegate = self
        arSession.delegateQueue = .main
        let configuration = ARWorldTrackingConfiguration()
        configuration.worldAlignment = .gravity
        configuration.isLightEstimationEnabled = false
        arSession.run(configuration, options: [.resetTracking, .removeExistingAnchors])
    }

    private var displayOrientation: UIInterfaceOrientation {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        return scenes.first(where: { $0.activationState == .foregroundActive })?.interfaceOrientation ?? .portrait
    }

    func session(_ session: ARSession, didUpdate frame: ARFrame) {
        guard active, usingAR, let model else { return }
        synchronize(model)
        guard active else { return }
        guard case .normal = frame.camera.trackingState else {
            _ = holdTracking(model)
            return
        }
        // viewMatrix is world -> display camera. Its inverse is the required
        // right-handed camera pose; raw camera.transform ignores screen orientation.
        let pose = simd_inverse(frame.camera.viewMatrix(for: displayOrientation))
        let accepted = model.realCameraSample(pose, timestamp: frame.timestamp,
            positional: true, sensitivity: Float(sensitivity), inverted: inverted)
        trackingValid = accepted
        trackingMessageKey = accepted ? "real_camera_tracking_position" : "real_camera_tracking_limited"
        synchronize(model)
    }

    func session(_ session: ARSession, cameraDidChangeTrackingState camera: ARCamera) {
        guard active, usingAR else { return }
        if case .normal = camera.trackingState { return }
        trackingValid = false
        trackingMessageKey = "real_camera_tracking_limited"
    }

    func session(_ session: ARSession, didFailWithError error: Error) {
        guard active, usingAR else { return }
        // The shared pose session rebases a position/rotation mode change at
        // the current virtual pose. Recenter is reserved for the explicit button.
        startMotion(reason: "real_camera_tracking_failed")
    }

    func sessionWasInterrupted(_ session: ARSession) {
        guard active, usingAR else { return }
        trackingValid = false
        trackingMessageKey = "real_camera_tracking_limited"
    }

    func sessionInterruptionEnded(_ session: ARSession) {
        guard active, usingAR else { return }
        // Preserve the session's reference world rather than resetting origin.
        let configuration = ARWorldTrackingConfiguration()
        configuration.worldAlignment = .gravity
        configuration.isLightEstimationEnabled = false
        arSession.run(configuration)
    }

    @discardableResult
    private func holdTracking(_ model: AureaModel) -> Bool {
        // Fill each actual native frame with the last accepted virtual pose;
        // gaps must not interpolate movement through lost tracking.
        let held = positional && model.realCameraHold()
        synchronize(model)
        trackingValid = false
        trackingMessageKey = "real_camera_tracking_limited"
        return held
    }

    private func startMotion(reason: String) {
        arSession.pause()
        arSession.delegate = nil
        usingAR = false
        available = motion.isDeviceMotionAvailable
        trackingValid = false
        positional = false
        trackingMessageKey = reason
        guard available else { return }
        let token = generation
        // 100 Hz: a 60 fps take gets a fresh measured pose for every frame.
        motion.deviceMotionUpdateInterval = 1.0 / 100.0
        motion.startDeviceMotionUpdates(using: .xArbitraryZVertical, to: .main) { [weak self] data, _ in
            guard let self, self.active, !self.usingAR, self.generation == token,
                  let model = self.model, let data else { return }
            let q = data.attitude.quaternion
            let vector = SIMD4<Float>(Float(q.x), Float(q.y), Float(q.z), Float(q.w))
            guard vector.x.isFinite, vector.y.isFinite, vector.z.isFinite, vector.w.isFinite,
                  simd_length_squared(vector) > 0.5 else { return }
            // CoreMotion expresses reference -> device attitude. Invert to
            // device -> reference, then align the camera's axes with the display.
            let orientation = simd_quatf(vector: simd_normalize(vector)).inverse
            let angle: Float
            switch self.displayOrientation {
            case .landscapeLeft: angle = .pi / 2
            case .landscapeRight: angle = -.pi / 2
            case .portraitUpsideDown: angle = .pi
            default: angle = 0
            }
            let display = simd_quatf(angle: angle, axis: SIMD3<Float>(0, 0, 1))
            // CoreMotion's reference is Z-up; the engine's world is the AR one,
            // Y-up (-90° about X): gravity is the vertical it pans about.
            let yUp = simd_quatf(angle: -.pi / 2, axis: SIMD3<Float>(1, 0, 0))
            let pose = simd_float4x4(yUp * orientation * display)
            self.trackingValid = model.realCameraSample(pose, timestamp: data.timestamp,
                positional: false, sensitivity: Float(self.sensitivity), inverted: self.inverted)
            self.synchronize(model)
        }
    }

#if DEBUG && targetEnvironment(simulator)
    // CI proves the production Swift recording controller and native bridge,
    // while explicitly avoiding any claim of physical AR/sensor tracking.
    func parityHoldTracking() -> Bool {
        guard ProcessInfo.processInfo.environment["AUREA_PARITY_EXPORT"] == "1",
              active, let model else { return false }
        return holdTracking(model)
    }

    func paritySubmitPose(_ pose: simd_float4x4, timestamp: Double, positional: Bool) -> Bool {
        guard ProcessInfo.processInfo.environment["AUREA_PARITY_EXPORT"] == "1",
              active, let model else { return false }
        let accepted = model.realCameraSample(pose, timestamp: timestamp,
            positional: positional, sensitivity: Float(sensitivity), inverted: inverted)
        trackingValid = accepted
        synchronize(model)
        return accepted
    }
#endif

    private func synchronize(_ model: AureaModel) {
        let status = model.realCameraStatus()
        guard status.count == 4 else { return }
        active = status[0].boolValue
        recording = status[1].boolValue
        lastFrame = status[2].int64Value
        positional = status[3].boolValue
        if !active { stopSensors() }
    }

    /// +1 frente, −1 trás, 0 parado (o motor soma no rumo da câmera, no plano do chão).
    func walk(_ direction: Int) {
        _ = model?.engine.realCameraWalk(Float(max(-1, min(1, direction))) * Float(walkSpeed), depth: Float(depthScale))
    }

    private func stopSensors() {
        motion.stopDeviceMotionUpdates()
        arSession.pause()
        arSession.delegate = nil
        usingAR = false
    }
}

/// Chip da Real Camera (pílula do palco e painel).
@MainActor fileprivate func realCameraChip(_ text: String, _ id: String, _ on: Bool, enabled: Bool = true,
                                           _ action: @escaping () -> Void) -> some View {
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

/// Na prévia só a pílula (o painel inteiro cobria o palco — retorno de beta):
/// status e Gravar/Parar, sempre à vista, também durante a gravação. Os
/// controles completos ficam na área de painéis (`RealCameraPanelView`).
/// Par do Android (RealCamera.kt, `RealCameraBar`).
@MainActor struct RealCameraStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var cam = RealCameraState.shared

    var body: some View {
        if cam.painting(model) {
            VStack {
                HStack(spacing: 6) {
                    Image(systemName: "camera.fill").font(.system(size: 12))
                        .foregroundStyle(cam.recording ? AureaColors.danger : AureaColors.accent)
                    Text(cam.recording ? cam.statusText : AureaText.t("real_camera_title"))
                        .font(.system(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                    realCameraChip(AureaText.t(cam.recording ? "real_camera_stop" : "real_camera_record"), "realcam.gravar",
                                   cam.recording, enabled: cam.recording || cam.trackingValid) { cam.toggleRecording(model) }
                }
                .padding(.leading, 10).padding(.trailing, 4).padding(.vertical, 4)
                .background(Capsule().fill(AureaColors.editorPanelHigh.opacity(0.92)))
                .overlay(Capsule().stroke(cam.recording ? AureaColors.danger : AureaColors.accent.opacity(0.5), lineWidth: 1))
                .padding(.top, 8)
                .accessibilityIdentifier("realcam.bar")
                Spacer()
            }
            .onDisappear { cam.stop(model) }
            .onReceive(NotificationCenter.default.publisher(for: UIApplication.willResignActiveNotification)) { _ in
                cam.stop(model)
            }
        }
    }
}

/// Os controles da Real Camera na ÁREA DE PAINÉIS, fora da prévia: recentrar,
/// inverter, andar, velocidade, profundidade, sensibilidade e Concluir.
/// Par do Android (`RealCameraPanel`).
@MainActor struct RealCameraPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var cam = RealCameraState.shared

    /// Botão de segurar: true ao apertar, false ao soltar.
    private func holdChip(_ text: String, _ id: String, _ hold: @escaping (Bool) -> Void) -> some View {
        Text(text)
            .font(.system(size: 11.5, weight: .semibold))
            .foregroundStyle(AureaColors.text)
            .padding(.horizontal, 12)
            .frame(minHeight: 34)
            .background(RoundedRectangle(cornerRadius: 8).fill(AureaColors.chip))
            .contentShape(Rectangle())
            .onLongPressGesture(minimumDuration: 60, maximumDistance: 80, pressing: { hold($0) }, perform: {})
            .accessibilityLabel(text)
            .accessibilityAddTraits(.isButton)
            .accessibilityIdentifier(id)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 7) {
                    Image(systemName: "camera.fill").font(.system(size: 12)).foregroundStyle(AureaColors.accent)
                    Text(AureaText.t("real_camera_title")).font(.system(size: 12, weight: .semibold)).foregroundStyle(AureaColors.text)
                    Text(cam.statusText)
                        .font(.system(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(2)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                HStack(spacing: 6) {
                    realCameraChip(AureaText.t("real_camera_recenter"), "realcam.recentrar", false) { cam.recenter() }
                    realCameraChip(AureaText.t("real_camera_invert"), "realcam.inverter", cam.inverted) { cam.inverted.toggle() }
                    realCameraChip(AureaText.t("real_camera_done"), "realcam.concluir", false) { cam.stop(model) }
                }
                HStack(spacing: 6) {
                    Text(AureaText.t("real_camera_walk")).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                    holdChip(AureaText.t("real_camera_forward"), "realcam.frente") { cam.walk($0 ? 1 : 0) }
                    holdChip(AureaText.t("real_camera_back"), "realcam.tras") { cam.walk($0 ? -1 : 0) }
                }
                HStack(spacing: 8) {
                    Text(AureaText.t("real_camera_walk_speed")).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                    Slider(value: $cam.walkSpeed, in: 0.2...3)
                        .accessibilityLabel(AureaText.t("real_camera_walk_speed"))
                        .accessibilityIdentifier("realcam.walkspeed")
                    Text(String(format: "%.1f m/s", cam.walkSpeed)).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                }
                HStack(spacing: 8) {
                    Text(AureaText.t("real_camera_depth")).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                    Slider(value: $cam.depthScale, in: 0.5...5)
                        .accessibilityLabel(AureaText.t("real_camera_depth"))
                        .accessibilityIdentifier("realcam.depth")
                    Text(String(format: "%.1f×", cam.depthScale)).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                }
                HStack(spacing: 8) {
                    Text(AureaText.t("real_camera_sensitivity")).font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                    Slider(value: $cam.sensitivity, in: 0.25...3)
                        .accessibilityLabel(AureaText.t("real_camera_sensitivity"))
                        .accessibilityIdentifier("realcam.sens")
                    Text("\(Int(cam.sensitivity * 100))%").font(.system(size: 11)).foregroundStyle(AureaColors.muted)
                }
            }
            .padding(.horizontal, 14).padding(.vertical, 10)
        }
        .background(AureaColors.editorPanelHigh)
        .accessibilityIdentifier("realcam.panel")
    }
}
