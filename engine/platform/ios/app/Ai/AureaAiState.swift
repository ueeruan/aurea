// =============================================================================
//  Aurea iOS — o estado da Aurea AI no aparelho: fala com o BACKEND do Aurea
//  (que fala com a 8Scale), gera, acompanha, baixa e entrega. Porte de
//  android/.../ai/AureaAi.kt (AureaAiState).
//
//  O app não conhece a 8Scale nem a chave dela: conhece só
//  `VideoGenerationProvider` — hoje o `AureaBackendVideoProvider`.
//
//  Um só por app (`shared`): fechar o painel, girar ou ir para o fundo não perde
//  a geração.
// =============================================================================
import Foundation
import AVFoundation
import Photos

/// Quanto esperar o callback ASSINADO do anúncio chegar ao servidor.
private let rewardWaitSeconds: TimeInterval = 90
/// Uma geração que não termina neste tempo é dada como perdida (a 8Scale leva ~30 s).
private let jobMaxSeconds: TimeInterval = 15 * 60

@MainActor
final class AureaAiState: ObservableObject {
    static let shared = AureaAiState()

    // -- o que a tela observa ---------------------------------------------
    @Published private(set) var status: AureaAiStatus = .checking
    @Published private(set) var capabilities: AiCapabilities = .empty
    @Published private(set) var gpu = ""
    @Published private(set) var modelName = ""
    @Published private(set) var message = ""
    @Published private(set) var promptMax = 800
    /// "Gerações de hoje: 3/5" — o número é do servidor. Nulo = ainda não leu.
    @Published private(set) var quota: AiVideoQuota?

    /// Relê a cota no servidor (depois de abrir, de gerar e de falhar).
    func refreshQuota() {
        Task { [weak self] in
            guard let self else { return }
            if let q = try? await self.provider.quota() { self.quota = q }
        }
    }
    @Published private(set) var job: AiJob?
    @Published private(set) var history: [AiJob] = []
    @Published private(set) var downloading = false
    @Published private(set) var error = ""
    @Published private(set) var showingAd = false
    @Published private(set) var session: AiGenerationSession?
    @Published private(set) var lastFile: URL?
    @Published private(set) var lastTitle = ""

    private let provider: VideoGenerationProvider = AureaBackendVideoProvider()
    private var currentSessionId: String?
    private var watchTask: Task<Void, Never>?
    private var generationTask: Task<Void, Never>?
    private var attempt = 0
    private var resumed = false

    private lazy var flow = AiRewardFlow(
        ads: ManagerRewardedAds(state: self),
        requestTicket: { [weak self] s, onTicket, onFail in
            guard let self else { return }
            Task { @MainActor in
                do {
                    let t = try await self.provider.ticket(s.request)
                    Self.log("ticket = ok")
                    onTicket(t)
                } catch let e as VideoFailure {
                    Self.log("ticket recusado: \(e.code)")
                    onFail(e.code)
                } catch {
                    onFail("sem_conexao")
                }
            }
        },
        bindAd: { ticket in AureaAdsManager.shared.setRewardUserId(ticket) },
        startGeneration: { [weak self] s, onJob, onFinish in
            guard let self else { return }
            self.generationTask?.cancel()
            self.generationTask = Task { @MainActor [weak self] in
                await self?.generateAndFollow(s, onJob: onJob, finish: onFinish)
            }
        },
        onChange: { [weak self] s in
            // Grava SEMPRE, e antes de tudo: o anúncio costuma matar o processo.
            GuardaDaSessao.shared.write(s)
            guard let self, s.generationId == self.currentSessionId else { return }
            self.session = s
            if s.status == .unlocked || s.status == .failed || s.status == .generating { self.refreshQuota() }
            switch s.status {
            case .unlocked: self.unlock(s)
            case .failed:
                self.error = explainVideoFailure(s.error)
                if var j = self.job { j.status = "failed"; j.stage = "Falhou"; self.job = j }
            default: break
            }
        })

    /// Uma geração em andamento: o "Gerar" fica travado (um toque = uma geração).
    var sessionBusy: Bool {
        guard let st = session?.status else { return false }
        return st == .preparing || st == .adShowing || st == .generating
    }

    fileprivate static let log = { (m: String) in
        #if DEBUG
        print("[AUREA AI] " + m)
        #endif
    }

    // -- conexão -------------------------------------------------------------

    /// Lê a configuração do backend (o que ele deixa pedir) e fica de olho.
    func connect() {
        watchTask?.cancel()
        attempt = 0
        watchTask = Task { [weak self] in await self?.search() }
    }

    private func search() async {
        while !Task.isCancelled {
            if attempt == 0 && !status.canGenerate { status = .checking }
            do {
                let cfg = try await provider.config()
                attempt = 0
                modelName = cfg.model
                promptMax = cfg.promptMax
                capabilities = AiCapabilities(modes: cfg.modes, durations: cfg.durations, aspects: cfg.aspects,
                                              resolutions: cfg.resolutions, fps: [16], audio: false, queue: 0)
                if cfg.enabled {
                    if message == explainVideoFailure("sem_conexao") || message == explainVideoFailure("ia_desligada") { message = "" }
                    status = session?.status == .generating ? .generating : .connected
                    if let q = try? await provider.quota() { quota = q }
                    resumeIfNeeded()
                } else {
                    status = .disconnected
                    message = explainVideoFailure("ia_desligada")
                }
                try? await Task.sleep(nanoseconds: 60_000_000_000)
            } catch {
                status = attempt == 0 ? .reconnecting : .disconnected
                message = explainVideoFailure("sem_conexao")
                let wait = aiBackoff(attempt)
                attempt += 1
                try? await Task.sleep(nanoseconds: wait * 1_000_000_000)
            }
        }
    }

    // -- retomar depois do anúncio ------------------------------------------

    /// A sessão gravada volta: com job, o acompanhamento continua (GET, nunca
    /// outro POST pago); sem job mas com recompensa, o ticket — idempotente no
    /// servidor — devolve o mesmo job ou gera o que ainda não tinha sido gerado.
    private func resumeIfNeeded() {
        guard !resumed else { return }
        resumed = true
        guard let saved = GuardaDaSessao.shared.read(), generationTask == nil else { return }
        currentSessionId = saved.generationId
        if saved.generationCompleted, saved.result != nil {
            flow.resume(saved)
        } else if saved.generationStarted, saved.ticket != nil {
            flow.resume(saved)
            generationTask = Task { @MainActor [weak self] in
                await self?.generateAndFollow(saved, onJob: { _ in }, finish: { [weak self] file, e, retry in
                    guard let self else { return }
                    var s = self.session ?? saved
                    if let file, e == nil { s.generationCompleted = true; s.result = file }
                    else { s.error = e ?? "geracao_falhou"; s.status = .failed; s.canRetryWithoutAd = retry && s.rewardEarned }
                    self.flow.resume(s)
                })
            }
        } else if !saved.generationStarted && (saved.status == .preparing || saved.status == .adShowing) {
            // O processo morreu com o anúncio na tela: nada foi gerado.
            var s = saved; s.status = .noReward; s.adClosedEarly = true
            flow.resume(s)
        } else {
            flow.resume(saved)
        }
    }

    // -- gerar -------------------------------------------------------------

    func prepareAd() { AureaAdsManager.shared.preloadRewarded() }

    /// "Gerar": ticket → anúncio → (recompensa) → geração real.
    func generateWithReward(_ request: AiRequest) {
        guard status.canGenerate, !sessionBusy else { return }
        // O servidor recusaria de qualquer jeito; aqui só evita o anúncio à toa.
        if quota?.exhausted == true { error = explainVideoFailure("limite_diario"); return }
        error = ""; message = ""; lastFile = nil; job = nil
        let id = UUID().uuidString
        currentSessionId = id
        flow.generate(request, id: id)
    }

    /// "Assistir de novo": o anúncio não veio ou fechou cedo. Nada gerado ainda.
    func unlockWithAd() { if let id = currentSessionId { flow.watchAgain(id) } }
    func retryGeneration() { unlockWithAd() }

    /// Depois de uma falha: técnica com recompensa valendo → mesmo ticket, sem
    /// anúncio; senão uma geração nova com o MESMO pedido (o prompt não se perde).
    func retryAfterFailure() {
        guard let s = session, s.status == .failed else { return }
        error = ""
        if s.canRetryWithoutAd { flow.retryWithoutAd(s.generationId); return }
        session = nil
        currentSessionId = nil
        GuardaDaSessao.shared.clear()
        job = nil
        generateWithReward(s.request)
    }

    fileprivate func setShowingAd(_ on: Bool) { showingAd = on }

    /// A geração real: pede o job (esperando o callback assinado do anúncio
    /// chegar), acompanha o estado REAL e baixa. Sem porcentagem inventada.
    private func generateAndFollow(_ s: AiGenerationSession, onJob: @escaping (String) -> Void,
                                   finish: @escaping AiRewardFlow.Finish) async {
        defer { generationTask = nil }
        guard let ticket = s.ticket else { finish(nil, "ticket_invalido", false); return }
        let start = Date()
        func step(_ st: String, _ text: String, id: String = "", result: AiResult? = nil) -> AiJob {
            AiJob(id: id, status: st, progress: 0, stage: text, queuePosition: 0,
                  seconds: Date().timeIntervalSince(start), error: nil, result: result)
        }
        status = .generating
        do {
            var jobId = s.jobId
            if jobId == nil {
                job = step("sending", "Enviando…")
                while jobId == nil {
                    do {
                        jobId = try await provider.generate(ticket: ticket)
                    } catch let e as VideoFailure where (e.code == "recompensa_pendente" || e.code == "em_andamento")
                        && Date().timeIntervalSince(start) < rewardWaitSeconds {
                        // O callback do LevelPlay ainda não chegou ao servidor.
                        try await Task.sleep(nanoseconds: 2_000_000_000)
                    }
                }
                Self.log("job = \(jobId!)")
                onJob(jobId!)
            }
            let id = jobId!
            var failures = 0
            while true {
                try Task.checkCancellation()
                let j: AiVideoJob
                do {
                    j = try await provider.status(id); failures = 0
                } catch let e as VideoFailure where e.transient {
                    failures += 1
                    if failures >= 30 { throw e }
                    status = .reconnecting
                    try await Task.sleep(nanoseconds: 3_000_000_000)
                    continue
                }
                status = .generating
                let stage: String
                switch j.status {
                case "queued": stage = "Enviando…"
                case "generating": stage = "Gerando vídeo…"
                case "completed": stage = "Finalizando…"
                default: stage = j.stage
                }
                job = AiJob(id: j.id, status: j.status, progress: 0, stage: stage, queuePosition: 0,
                            seconds: j.seconds, error: j.error, result: nil)
                if j.finished {
                    guard j.status == "completed", j.readyToDownload else {
                        status = .connected
                        finish(nil, j.error ?? "geracao_falhou", j.retryWithoutAd)
                        return
                    }
                    break
                }
                if Date().timeIntervalSince(start) > jobMaxSeconds { throw VideoFailure("tempo_esgotado") }
                try await Task.sleep(nanoseconds: 2_000_000_000)
            }
            downloading = true
            defer { downloading = false }
            let file = try await provider.download(id, to: Self.folder.appendingPathComponent("\(id).mp4"))
            guard let meta = await Self.metadata(file) else { throw VideoFailure("resultado_nao_e_video") }
            job = step("completed", "Concluído", id: id, result: meta)
            status = .connected
            finish(file, nil, false)
        } catch is CancellationError {
            return
        } catch let e as VideoFailure {
            Self.log("falha = \(e.code)")
            status = .connected
            // `recompensa_pendente`: o anúncio FOI assistido; só a confirmação do
            // provedor ainda não chegou — repetir não pede outro anúncio.
            finish(nil, e.code, e.transient || e.code.hasPrefix("download") || e.code == "tempo_esgotado"
                   || e.code == "recompensa_pendente")
        } catch {
            status = .connected
            finish(nil, "geracao_falhou", true)
        }
    }

    /// Duração, tamanho e fps REAIS do arquivo baixado. Nulo = não é vídeo legível.
    private static func metadata(_ url: URL) async -> AiResult? {
        let asset = AVURLAsset(url: url)
        guard let track = try? await asset.loadTracks(withMediaType: .video).first,
              let duration = try? await asset.load(.duration),
              let size = try? await track.load(.naturalSize),
              let fps = try? await track.load(.nominalFrameRate) else { return nil }
        let seconds = CMTimeGetSeconds(duration)
        guard seconds > 0, size.width > 0, size.height > 0 else { return nil }
        let audio = ((try? await asset.loadTracks(withMediaType: .audio)) ?? []).isEmpty == false
        return AiResult(videoURL: "", durationSeconds: seconds, width: Int(abs(size.width)),
                        height: Int(abs(size.height)), fps: Int(fps.rounded()), hasAudio: audio)
    }

    /// Só enquanto está na fila: depois que começa, a geração vai até o fim.
    func cancel() {
        guard let id = session?.jobId else { return }
        Task { [weak self] in
            do { try await self?.provider.cancel(id) }
            catch is CancellationError { return }
            catch let failure as VideoFailure {
                if self?.session?.jobId == id { self?.error = explainVideoFailure(failure.code) }
            }
            catch {
                if self?.session?.jobId == id { self?.error = explainVideoFailure("sem_conexao") }
            }
        }
    }

    private func unlock(_ s: AiGenerationSession) {
        guard let file = s.result else { return }
        Self.log("vídeo entregue")
        GuardaDaSessao.shared.clear()
        if let j = job { history = [j] + history.filter { $0.id != j.id } }
        lastFile = file
        lastTitle = "Aurea AI"
    }

    /// Pelo importador de sempre (copia para a mídia do projeto, sonda, cria a
    /// camada e salva), no cabeçote.
    func addToTimeline(_ model: AureaModel) {
        guard let file = lastFile else { return }
        let size = (try? FileManager.default.attributesOfItem(atPath: file.path)[.size] as? NSNumber)?.intValue ?? 0
        guard size > 0 else { error = "O arquivo baixado está vazio"; return }
        model.importMedia(url: file, kind: .video, atPlayhead: true)
    }

    /// Copia o vídeo para a galeria (Fotos).
    func saveToGallery() {
        guard let file = lastFile else { return }
        PHPhotoLibrary.requestAuthorization(for: .addOnly) { [weak self] st in
            guard st == .authorized || st == .limited else {
                Task { @MainActor in self?.error = "Não consegui salvar na galeria: permissão negada" }
                return
            }
            PHPhotoLibrary.shared().performChanges({
                PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: file)
            }) { saved, e in
                Task { @MainActor in
                    if saved { self?.message = "Salvo na galeria" }
                    else { self?.error = "Não consegui salvar na galeria: \(e?.localizedDescription ?? "?")" }
                }
            }
        }
    }

    /// Reabre um vídeo desta sessão.
    func play(_ item: AiJob) {
        job = item
        if item.status == "completed" {
            let f = Self.folder.appendingPathComponent("\(item.id).mp4")
            if FileManager.default.fileExists(atPath: f.path) { lastFile = f; lastTitle = "Aurea AI" }
        }
    }

    /// Sobe a imagem de partida (I2V) para o backend; devolve o id que o pedido cita.
    func uploadImage(_ bytes: Data, type: String) async -> String? {
        do {
            return try await provider.uploadImage(bytes, type: type)
        } catch let e as VideoFailure {
            error = explainVideoFailure(e.code); return nil
        } catch {
            self.error = explainVideoFailure("sem_conexao"); return nil
        }
    }

    private static var folder: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("aurea-ai", isDirectory: true)
    }
}

/// O Rewarded da IA pelo AureaAdsManager (a tela nunca fala com o SDK).
@MainActor
private final class ManagerRewardedAds: RewardedAds {
    private weak var state: AureaAiState?
    init(state: AureaAiState) { self.state = state }

    func ready() -> Bool { AureaAdsManager.shared.rewardedReady() }

    func load(loaded: @escaping () -> Void, failed: @escaping (String) -> Void) {
        AureaAdsManager.shared.preloadRewarded(loaded: loaded, failed: failed)
    }

    func show(opened: @escaping () -> Void, reward: @escaping () -> Void,
              closed: @escaping () -> Void, failed: @escaping (String) -> Void) -> Bool {
        var returned = false
        let shown = AureaAdsManager.shared.showRewarded(
            opened: { [weak self] in self?.state?.setShowingAd(true); opened() },
            // A recompensa vem SÓ daqui (callback oficial do SDK).
            reward: { AureaAiState.log("reward = recebida"); reward() },
            closed: { [weak self] _ in
                self?.state?.setShowingAd(false); closed()
                AureaAdsManager.shared.preloadRewarded()
            },
            failed: { [weak self] e in
                AureaAiState.log("anúncio: \(e)")
                self?.state?.setShowingAd(false); if returned { failed(e) }
            })
        returned = true
        return shown
    }
}
