// =============================================================================
//  Aurea / platform / ios / app / VoiceRecorder.swift
//
//  GRAVAR VOZ (Adicionar → Áudio → "Gravar voz"). Par do Android:
//  editor/VoiceRecorder.kt + editor/VoiceTake.kt.
//
//  A narração é gravada pelo microfone em AAC/.m4a direto em Media/ e entra na
//  timeline como camada de áudio pelo mesmo `importAudio` de um arquivo
//  escolhido (`AureaModel.importVoiceRecording`) — desfazer, waveform e export
//  não sabem que ela veio do microfone. Gravar é recurso da plataforma
//  (AVAudioRecorder); o motor só recebe o arquivo pronto.
//
//  SESSÃO DE ÁUDIO: o motor toca com a categoria Playback (IOSAudio.mm). Só
//  enquanto grava ela vira PlayAndRecord (alto-falante + bluetooth); ao parar,
//  volta exatamente à anterior — o AVAudioEngine do motor se refaz sozinho na
//  troca de configuração.
//
//  Interrupções: ligação (interrupção da sessão) ou app em segundo plano param
//  a gravação e GUARDAM o que foi gravado; em segundo plano o motor está
//  suspenso, então a camada entra quando o app volta. Cancelar descarta.
// =============================================================================
import SwiftUI
import AVFoundation
import UIKit

/// As contas puras do "Gravar voz" — as MESMAS do `VoiceTake` do Android.
enum VoiceTake {
    /// Abaixo disso a tomada é um toque acidental: não vira camada.
    static let minDuration: TimeInterval = 0.3
    /// Faixa do medidor: −50 dBFS (silêncio de sala) até 0 dBFS (estouro).
    private static let floorDb: Float = -50

    /// Próximo nome livre "Voz N": 1 + o maior N já usado por "<prefixo> N".
    static func nextName(prefix: String, existing: [String]) -> String {
        let head = prefix + " "
        var highest = 0
        for name in existing where name.hasPrefix(head) {
            let rest = name.dropFirst(head.count).trimmingCharacters(in: .whitespaces)
            if let number = Int(rest), number > highest { highest = number }
        }
        return "\(prefix) \(highest + 1)"
    }

    /// "mm:ss" do tempo gravado (minutos passam de 59 sem virar hora).
    static func elapsedLabel(_ seconds: TimeInterval) -> String {
        let total = seconds.isFinite ? max(0, Int(seconds)) : 0
        return String(format: "%02d:%02d", total / 60, total % 60)
    }

    /// Nível 0...1 do medidor a partir de dBFS (`averagePower`).
    static func level(db: Float) -> Float {
        guard db.isFinite else { return 0 }
        return min(1, max(0, (db - floorDb) / -floorDb))
    }

    /// Sobe na hora e desce devagar (o pico não pisca entre sílabas).
    static func smooth(_ previous: Float, _ next: Float) -> Float { max(next, previous * 0.82) }
}

/// Uma sessão do painel: permissão, a tomada em curso, relógio, medidor e a
/// tomada que espera o app voltar para entrar na timeline.
@MainActor
final class VoiceRecorderController: ObservableObject {
    enum Phase: Equatable { case idle, countdown, recording }
    enum Permission: Equatable { case granted, undetermined, denied }

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var countdown = 3
    @Published private(set) var elapsed: TimeInterval = 0
    @Published private(set) var level: Float = 0
    @Published private(set) var permission: Permission = VoiceRecorderController.currentPermission()

    private weak var model: AureaModel?
    private var close: () -> Void = {}
    private var recorder: AVAudioRecorder?
    private var fileURL: URL?
    private var startedAt: TimeInterval = 0
    private var startFrame: Int64 = 0
    private var playAlong = true
    private var meterTimer: Timer?
    private var countdownTask: Task<Void, Never>?
    private var interruptionObserver: NSObjectProtocol?
    private var previousSession: (category: AVAudioSession.Category, mode: AVAudioSession.Mode, options: AVAudioSession.CategoryOptions)?
    /// Tomada parada com o app fora da tela: entra quando ele volta.
    private var pending: (url: URL, frame: Int64, interrupted: Bool)?

    /// Só lê a sessão compartilhada (segura fora da main): serve de valor inicial.
    nonisolated static func currentPermission() -> Permission {
        switch AVAudioSession.sharedInstance().recordPermission {
        case .granted: return .granted
        case .denied: return .denied
        default: return .undetermined
        }
    }

    func attach(model: AureaModel, close: @escaping () -> Void) {
        self.model = model
        self.close = close
    }

    func refreshPermission() { permission = Self.currentPermission() }

    /// O pedido do sistema, depois da explicação do painel.
    func requestPermission() {
        AVAudioSession.sharedInstance().requestRecordPermission { [weak self] granted in
            Task { @MainActor in
                guard let self else { return }
                self.permission = granted ? .granted : .denied
            }
        }
    }

    func openSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else { return }
        UIApplication.shared.open(url)
    }

    /// Gravar: com a contagem, 3-2-1 antes; o quadro de início é o cabeçote de agora.
    func arm(withCountdown: Bool, playVideo: Bool) {
        guard phase == .idle, pending == nil, let model else { return }
        model.pause()
        startFrame = model.status.playhead
        playAlong = playVideo
        guard withCountdown else { begin(); return }
        countdown = 3
        phase = .countdown
        countdownTask = Task { @MainActor [weak self] in
            for number in stride(from: 3, through: 1, by: -1) {
                self?.countdown = number
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                if Task.isCancelled { return }
            }
            guard let self, self.phase == .countdown else { return }
            self.countdownTask = nil
            self.begin()
        }
    }

    private func begin() {
        guard let model else { phase = .idle; return }
        let session = AVAudioSession.sharedInstance()
        previousSession = (session.category, session.mode, session.categoryOptions)
        do {
            try session.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker, .allowBluetooth])
            try session.setActive(true)
        } catch {
            NSLog("Aurea voz: sessao de gravacao recusada: %@", error.localizedDescription)
            restoreSession()
            failToStart()
            return
        }
        try? FileManager.default.createDirectory(at: AureaPaths.media, withIntermediateDirectories: true)
        let url = AureaPaths.mediaDestination(for: "voz-\(Int(Date().timeIntervalSince1970)).m4a")
        // AAC em MPEG-4 (.m4a), mono, 48 kHz (a taxa do mixer do motor), 128 kbps.
        let settings: [String: Any] = [
            AVFormatIDKey: Int(kAudioFormatMPEG4AAC),
            AVSampleRateKey: 48_000.0,
            AVNumberOfChannelsKey: 1,
            AVEncoderBitRateKey: 128_000,
            AVEncoderAudioQualityKey: AVAudioQuality.high.rawValue,
        ]
        guard let take = try? AVAudioRecorder(url: url, settings: settings) else {
            restoreSession()
            failToStart()
            return
        }
        take.isMeteringEnabled = true
        guard take.prepareToRecord(), take.record() else {
            take.stop()
            try? FileManager.default.removeItem(at: url)
            restoreSession()
            failToStart()
            return
        }
        recorder = take
        fileURL = url
        startedAt = ProcessInfo.processInfo.systemUptime
        elapsed = 0
        level = 0
        phase = .recording
        observeInterruptions()
        meterTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.tick()
            }
        }
        // Narrar olhando o vídeo: a timeline toca a partir do cabeçote.
        if playAlong { model.play() }
    }

    private func failToStart() {
        phase = .idle
        model?.toast = AureaText.t("voice_start_failed")
    }

    private func tick() {
        guard phase == .recording, let take = recorder else { return }
        guard take.isRecording else {
            // O sistema parou o gravador (interrupção, rota perdida): guarda o que foi gravado.
            finish(keep: true, importNow: UIApplication.shared.applicationState != .background, interrupted: true)
            return
        }
        take.updateMeters()
        elapsed = take.currentTime
        level = VoiceTake.smooth(level, VoiceTake.level(db: take.averagePower(forChannel: 0)))
    }

    /// Fim da tomada. `keep` = salvar; `importNow` = pôr na timeline já (falso
    /// com o app em segundo plano: espera `resumed`). A contagem só volta ao início.
    func finish(keep: Bool, importNow: Bool, interrupted: Bool = false) {
        if phase == .countdown {
            countdownTask?.cancel()
            countdownTask = nil
            phase = .idle
            return
        }
        guard let take = recorder, let url = fileURL else { return }
        recorder = nil
        fileURL = nil
        meterTimer?.invalidate()
        meterTimer = nil
        let duration = ProcessInfo.processInfo.systemUptime - startedAt
        take.stop()
        removeInterruptionObserver()
        if playAlong { model?.pause() }
        restoreSession()
        phase = .idle
        level = 0
        guard keep else {
            try? FileManager.default.removeItem(at: url)
            model?.toast = AureaText.t("voice_discarded")
            return
        }
        let attributes = try? FileManager.default.attributesOfItem(atPath: url.path)
        let size = (attributes?[.size] as? NSNumber)?.int64Value ?? 0
        guard duration >= VoiceTake.minDuration, size > 0 else {
            try? FileManager.default.removeItem(at: url)
            model?.toast = AureaText.t("voice_too_short")
            return
        }
        if importNow {
            importTake(url, frame: startFrame, interrupted: interrupted)
        } else {
            pending = (url, startFrame, interrupted)
        }
    }

    /// O app voltou: a tomada guardada na saída entra na timeline.
    func resumed() {
        guard let take = pending else { return }
        pending = nil
        importTake(take.url, frame: take.frame, interrupted: take.interrupted)
    }

    /// O painel saiu da tela: o que foi gravado não se perde.
    func dispose() {
        countdownTask?.cancel()
        countdownTask = nil
        if recorder != nil { finish(keep: true, importNow: false) }
        guard let take = pending else { return }
        pending = nil
        if let model { model.importVoiceRecording(take.url, atFrame: take.frame, interrupted: take.interrupted) }
        else { try? FileManager.default.removeItem(at: take.url) }
    }

    private func importTake(_ url: URL, frame: Int64, interrupted: Bool) {
        guard let model else {
            try? FileManager.default.removeItem(at: url)
            return
        }
        model.importVoiceRecording(url, atFrame: frame, interrupted: interrupted)
        close()
    }

    /// A categoria do motor de volta (Playback), com o modo e as opções de antes.
    private func restoreSession() {
        guard let previous = previousSession else { return }
        previousSession = nil
        do {
            try AVAudioSession.sharedInstance().setCategory(previous.category, mode: previous.mode, options: previous.options)
        } catch {
            NSLog("Aurea voz: categoria de audio nao voltou: %@", error.localizedDescription)
        }
    }

    /// Ligação ou outro app tomando o áudio: para e guarda.
    private func observeInterruptions() {
        guard interruptionObserver == nil else { return }
        interruptionObserver = NotificationCenter.default.addObserver(
            forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] note in
            let raw = (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? NSNumber)?.uintValue
            Task { @MainActor in
                guard let self, raw == AVAudioSession.InterruptionType.began.rawValue else { return }
                self.finish(keep: true, importNow: UIApplication.shared.applicationState != .background, interrupted: true)
            }
        }
    }

    private func removeInterruptionObserver() {
        if let observer = interruptionObserver { NotificationCenter.default.removeObserver(observer) }
        interruptionObserver = nil
    }
}

/// O painel de gravação, por cima da timeline (o palco fica à vista). Um véu
/// leve segura os toques no resto do editor enquanto ele está aberto. Par do
/// Android: `VoiceRecorderOverlay` (VoiceRecorder.kt).
@MainActor struct VoiceRecorderOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var recorder = VoiceRecorderController()
    @State private var withCountdown = false
    @State private var playVideo = true

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .bottom) {
                Color.black.opacity(0.22).ignoresSafeArea()
                    .contentShape(Rectangle())
                    .onTapGesture {}
                    .accessibilityHidden(true)
                ViewThatFits(in: .vertical) {
                    panel
                    ScrollView { panel }.frame(maxHeight: geometry.size.height * 0.8)
                }
                // No iPad/paisagem o painel não estica a tela toda.
                .frame(maxWidth: 560)
                .background(
                    RoundedRectangle(cornerRadius: 20).fill(AureaColors.editorPanelHigh)
                        .overlay(RoundedRectangle(cornerRadius: 20).stroke(AureaColors.hairline, lineWidth: 1))
                        .ignoresSafeArea(edges: .bottom)
                )
                .accessibilityElement(children: .contain)
                .accessibilityIdentifier("voice.panel")
            }
        }
        .onAppear {
            let shell = self.shell
            recorder.attach(model: model) { shell.voiceRecorder = false }
            recorder.refreshPermission()
        }
        .onDisappear { recorder.dispose() }
        .onChange(of: scenePhase) { phase in
            switch phase {
            // Segundo plano: para e guarda (o motor fica suspenso até voltar).
            case .background: recorder.finish(keep: true, importNow: false, interrupted: true)
            case .active:
                recorder.refreshPermission()
                recorder.resumed()
            default: break
            }
        }
    }

    private func closePanel() { shell.voiceRecorder = false }

    private var panel: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                CupertinoGlyph.text(CupertinoGlyph.MicFill, size: 18, color: AureaColors.danger)
                    .frame(width: 18, height: 18)
                    .accessibilityHidden(true)
                Text(AureaText.t("voice_title")).font(.aurea(size: 16, weight: .semibold))
                    .foregroundStyle(AureaColors.text)
                    .accessibilityAddTraits(.isHeader)
                Spacer(minLength: 0)
            }
            .frame(minHeight: 36)
            if recorder.permission == .granted { controls } else { permissionBlock }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .frame(maxWidth: .infinity)
    }

    @ViewBuilder private var controls: some View {
        let phase = recorder.phase
        // Relógio (ou o número da contagem) bem grande: dá para ler com o aparelho longe.
        if phase == .countdown {
            Text("\(recorder.countdown)")
                .font(.aurea(size: 44, weight: .bold))
                .foregroundStyle(AureaColors.danger)
                .padding(.top, 4)
                .accessibilityLabel(AureaText.t("voice_countdown_value", recorder.countdown))
                .accessibilityIdentifier("voice.countdownValue")
        } else {
            let time = VoiceTake.elapsedLabel(recorder.elapsed)
            Text(time)
                .font(.aurea(size: 44, weight: .semibold))
                .monospacedDigit()
                .foregroundStyle(phase == .recording ? AureaColors.text : AureaColors.muted)
                .padding(.top, 4)
                .accessibilityLabel(AureaText.t("voice_elapsed") + " " + time)
                .accessibilityIdentifier("voice.timer")
        }
        Text(AureaText.t(phase == .recording ? "voice_recording" : "voice_hint"))
            .font(.aurea(size: 12, weight: phase == .recording ? .semibold : .regular))
            .foregroundStyle(phase == .recording ? AureaColors.danger : AureaColors.muted)
            .multilineTextAlignment(.center)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.horizontal, 8)
        meter.padding(.top, 10)
        HStack(spacing: 0) {
            cancelButton.frame(maxWidth: .infinity, alignment: .leading)
            recordButton
            Color.clear.frame(maxWidth: .infinity, maxHeight: 1)
        }
        .padding(.top, 14)
        if phase == .idle {
            toggleRow("voice_countdown", isOn: withCountdown, id: "voice.countdown") { withCountdown = $0 }
                .padding(.top, 10)
            toggleRow("voice_play_along", isOn: playVideo, id: "voice.playAlong") { playVideo = $0 }
            if playVideo {
                Text(AureaText.t("voice_headphones"))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.top, 2)
            }
        }
    }

    private var cancelButton: some View {
        let idle = recorder.phase == .idle
        return Button {
            if idle { closePanel() } else { recorder.finish(keep: false, importNow: false) }
        } label: {
            Text(AureaText.t("common_cancel"))
                .font(.aurea(size: 14, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 16)
                .frame(minWidth: 96, minHeight: 48)
                .background(AureaColors.chip, in: Capsule())
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t(idle ? "common_cancel" : "voice_cancel_take"))
        .accessibilityIdentifier("voice.cancel")
    }

    /// O botão grande: círculo vermelho para gravar, quadrado vermelho para parar.
    private var recordButton: some View {
        let recording = recorder.phase == .recording
        let enabled = recorder.phase != .idle || !model.importingMedia
        return Button {
            switch recorder.phase {
            case .idle: recorder.arm(withCountdown: withCountdown, playVideo: playVideo)
            case .countdown: recorder.finish(keep: false, importNow: false)
            case .recording: recorder.finish(keep: true, importNow: true)
            }
        } label: {
            ZStack {
                Circle().strokeBorder(enabled ? AureaColors.text : AureaColors.muted, lineWidth: 4)
                if recording {
                    RoundedRectangle(cornerRadius: 7).fill(AureaColors.danger).frame(width: 30, height: 30)
                } else {
                    Circle().fill(enabled ? AureaColors.danger : AureaColors.muted).frame(width: 60, height: 60)
                }
            }
            .frame(width: 80, height: 80)
            .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .accessibilityLabel(AureaText.t(recording ? "voice_stop" : "voice_record"))
        .accessibilityIdentifier(recording ? "voice.stop" : "voice.record")
    }

    /// Medidor de entrada: verde, amarelo perto do topo, vermelho no estouro.
    private var meter: some View {
        let level = CGFloat(min(1, max(0, recorder.level)))
        let color = level > 0.92 ? AureaColors.danger : (level > 0.75 ? AureaColors.warning : AureaColors.success)
        return GeometryReader { geometry in
            ZStack(alignment: .leading) {
                Capsule().fill(AureaColors.chip)
                Capsule().fill(color).frame(width: geometry.size.width * level)
            }
        }
        .frame(height: 10)
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("voice_level"))
        .accessibilityValue("\(Int(level * 100))%")
        .accessibilityIdentifier("voice.meter")
    }

    /// Linha inteira tocável com o interruptor (o leitor de tela lê nome + estado).
    private func toggleRow(_ key: String, isOn: Bool, id: String, set: @escaping (Bool) -> Void) -> some View {
        HStack(spacing: 8) {
            Text(AureaText.t(key)).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 8)
            AureaToggle(checked: isOn) { set($0) }.accessibilityHidden(true)
        }
        .frame(minHeight: 44)
        .contentShape(Rectangle())
        .onTapGesture { set(!isOn) }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityAddTraits(isOn ? [.isButton, .isSelected] : [.isButton])
        .accessibilityAction { set(!isOn) }
        .accessibilityIdentifier(id)
    }

    /// Antes do pedido do sistema: por que o microfone; negado, o caminho dos Ajustes.
    private var permissionBlock: some View {
        let denied = recorder.permission == .denied
        return VStack(spacing: 6) {
            CupertinoGlyph.text(CupertinoGlyph.Mic, size: 36, color: AureaColors.accent)
                .frame(width: 36, height: 36)
                .accessibilityHidden(true)
            Text(AureaText.t("voice_permission_title")).font(.aurea(size: 15, weight: .semibold))
                .foregroundStyle(AureaColors.text)
            Text(AureaText.t(denied ? "voice_permission_blocked" : "voice_permission_body"))
                .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 8)
                .accessibilityIdentifier("voice.permission.text")
            HStack(spacing: 10) {
                pill("common_cancel", fill: AureaColors.chip, id: "voice.cancel") { closePanel() }
                if denied {
                    pill("voice_open_settings", fill: AureaColors.action, id: "voice.permission.settings") { recorder.openSettings() }
                } else {
                    pill("voice_permission_allow", fill: AureaColors.action, id: "voice.permission.allow") { recorder.requestPermission() }
                }
            }
            .padding(.top, 8)
        }
        .padding(.vertical, 8)
    }

    private func pill(_ key: String, fill: Color, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key))
                .font(.aurea(size: 14, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .multilineTextAlignment(.center)
                .lineLimit(2)
                .padding(.horizontal, 14)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(fill, in: Capsule())
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityIdentifier(id)
    }
}
