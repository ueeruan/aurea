// =============================================================================
//  Aurea iOS — o estado da Aurea AI no aparelho: acha o ComfyUI, gera,
//  acompanha, baixa e só entrega com a recompensa. Porte de
//  android/.../ai/AureaAi.kt (AureaAiState).
//
//  Um só por app (`shared`): fechar o painel, girar ou ir para o fundo não perde
//  a geração — o mesmo papel do estado no EditorStore do Android.
// =============================================================================
import Foundation
import Photos

@MainActor
final class AureaAiState: ObservableObject {
    static let shared = AureaAiState()

    // -- o que a tela observa ---------------------------------------------
    @Published private(set) var status: AureaAiStatus = .checking
    @Published private(set) var capabilities: AiCapabilities = .empty
    @Published private(set) var gpu = ""
    @Published private(set) var modelName = ""
    @Published private(set) var message = ""
    @Published private(set) var job: AiJob?
    @Published private(set) var history: [AiJob] = []
    @Published private(set) var downloading = false
    @Published private(set) var error = ""
    /// `true` enquanto o Rewarded está na tela.
    @Published private(set) var showingAd = false
    /// A geração DESTA tela (a última pedida): anúncio, H3 e liberação juntos.
    @Published private(set) var session: AiGenerationSession?
    /// Último arquivo liberado: pronto para tocar, salvar e ir para a timeline.
    @Published private(set) var lastFile: URL?
    @Published private(set) var lastTitle = ""

    private var currentSessionId: String?
    private var comfy: ComfyClient?
    private var watchTask: Task<Void, Never>?
    private var generationTask: Task<Void, Never>?
    private var finishGeneration: ((URL?, String?) -> Void)?
    private var attempt = 0
    private lazy var flow = AiRewardFlow(
        ads: ManagerRewardedAds(state: self),
        startGeneration: { [weak self] s, onPromptId, onFinish in
            guard let self else { return }
            self.finishGeneration = onFinish
            self.generate(s.request, onPromptId: onPromptId) { [weak self] file, e in
                self?.finishGeneration = nil
                onFinish(file, e)
            }
        },
        onChange: { [weak self] s in
            // Grava SEMPRE, e antes de tudo: o processo pode morrer no instante
            // seguinte (é o que o anúncio costuma provocar).
            GuardaDaSessao.shared.write(s)
            guard let self, s.generationId == self.currentSessionId else { return }
            self.session = s
            if s.status == .unlocked { self.unlock(s) }
        })

    /// Uma geração em andamento (preparando anúncio, anúncio na tela ou H3 gerando).
    var sessionBusy: Bool {
        guard let st = session?.status else { return false }
        return st == .preparing || st == .adShowing || st == .generating
    }

    /// `fileprivate` e não `private`: o bridge do Rewarded, no mesmo arquivo,
    /// também registra (é onde a recompensa nasce).
    fileprivate static let log = { (m: String) in
        #if DEBUG
        print("[AUREA AI] " + m)
        #endif
    }

    // -- conexão -------------------------------------------------------------

    /// Procura o servidor na hora e continua de olho. "Tentar de novo" começa do zero.
    func connect() {
        watchTask?.cancel()
        attempt = 0
        comfy = nil
        watchTask = Task { [weak self] in await self?.search() }
    }

    private func search() async {
        while !Task.isCancelled {
            if attempt == 0 && comfy == nil { status = .checking; message = "Procurando o servidor" }
            let final = await tryConnect()
            // Durante a geração o selo fica "Online"; o laço só não pode derrubar o job.
            status = final == .connected && job?.running == true ? .generating : final
            let wait: UInt64
            if final == .connected { attempt = 0; wait = aiWatchSeconds } else { wait = aiBackoff(attempt); attempt += 1 }
            try? await Task.sleep(nanoseconds: wait * 1_000_000_000)
        }
    }

    /// Online só com `GET {endpoint}/system_stats` → 200 + JSON válido.
    ///
    /// A ordem importa: o **discovery** manda. Ele é um endereço fixo que nunca
    /// muda; o que muda é o `endpoint` que ele publica. Depois que o Colab
    /// publica, trocar de túnel não pede IPA nem APK novo.
    private func tryConnect() async -> AureaAiStatus {
        Self.log("discovery = \(AureaAiConfig.discoveryURL)")
        let read = await ComfyClient.readDiscovery()

        var candidates: [String] = []
        if let d = read.doc {
            Self.log("discovery = HTTP \(read.http), online=\(d.online) endpoint=\(d.endpoint) updatedAt=\(d.updatedAt)")
            if d.isValid { candidates.append(d.endpoint) }
        } else {
            Self.log("discovery = HTTP \(read.http), sem documento")
        }
        for base in candidates {
            Self.log("endpoint = \(base)")
            let c = ComfyClient(base: base)
            let (http, ok) = await c.isOnline()
            Self.log("system_stats = HTTP \(http), json \(ok ? "ok" : "inválido")")
            if ok {
                if comfy?.base != base { comfy = c }
                modelName = read.doc.flatMap { $0.model.isEmpty ? nil : $0.model } ?? "MiniMax H3"
                gpu = read.doc?.gpu ?? ""
                let modes = read.doc?.capabilities ?? []
                capabilities = .fromDiscovery(modes.isEmpty ? ["text_to_video", "image_to_video"] : modes)
                message = ""
                resumeIfNeeded()
                return .connected
            }
        }
        comfy = nil
        // Discovery dizendo offline e nenhum endereço de pé: OFFLINE. Senão, ainda subindo.
        return read.doc?.online == false ? .disconnected : .reconnecting
    }

    // -- retomar depois do anúncio ------------------------------------------

    /// Retoma a geração gravada, se houver uma.
    ///
    /// É o caminho de volta do Rewarded: o anúncio traz a Activity para trás (e
    /// às vezes o sistema mata o processo). A sessão gravada diz qual
    /// `prompt_id` acompanhar e por qual endereço — e retomar NUNCA manda outro
    /// `POST /prompt`, senão o mesmo vídeo seria gerado duas vezes.
    private func resumeIfNeeded() {
        guard !sessionBusy, let saved = GuardaDaSessao.shared.read() else { return }
        currentSessionId = saved.generationId
        session = flow.resume(saved)

        if saved.generationCompleted, let file = saved.result {
            Self.log("release = retomando sessão já concluída (recompensa=\(saved.rewardEarned))")
            if saved.rewardEarned { unlock(saved) } else { lastFile = file }
            return
        }
        guard let pid = saved.promptId, !pid.isEmpty else {
            if !saved.generationStarted { GuardaDaSessao.shared.clear() }
            return
        }
        guard let base = saved.endpointUsado ?? comfy?.base else { return }
        Self.log("history = retomando prompt_id \(pid) em \(base)")
        follow(ComfyClient(base: base), promptId: pid, request: saved.request, w: 0, h: 0)
    }

    /// Acompanha um `prompt_id` já existente até o fim (retomada, sem novo POST).
    private func follow(_ c: ComfyClient, promptId: String, request: AiRequest, w: Int, h: Int) {
        let (wd, ht) = H3Workflow.dimensions(aspect: request.aspect, resolution: request.resolution)
        let ww = w > 0 ? w : wd
        let hh = h > 0 ? h : ht
        let start = Date()
        func etapa(_ st: String, _ text: String, queue: Int = 0, result: AiResult? = nil) -> AiJob {
            AiJob(id: promptId, status: st, progress: 0, stage: text, queuePosition: queue,
                  seconds: Date().timeIntervalSince(start), error: nil, result: result)
        }
        generationTask = Task { [weak self] in
            guard let self else { return }
            var failures = 0
            do {
                while !Task.isCancelled {
                    try await Task.sleep(nanoseconds: 2_000_000_000)
                    let record: [String: Any]?
                    do {
                        record = try await c.history(promptId); failures = 0
                    } catch let e as ComfyError {
                        failures += 1
                        if failures >= 15 { throw e }
                        self.status = .reconnecting
                        continue
                    }
                    self.status = .generating
                    Self.log("history = \(promptId) → \(record == nil ? "ainda não terminou" : "terminou")")
                    guard let record else { continue }
                    if let e = ComfyClient.error(inHistory: record) {
                        throw ComfyError(http: 200, code: "execution_error", detail: e)
                    }
                    guard let video = ComfyClient.videos(inHistory: record).first else {
                        Self.log("output = /history sem saída de vídeo ainda")
                        failures += 1
                        if failures >= 5 {
                            throw ComfyError(http: 200, code: "sem_video",
                                             detail: "o /history terminou sem saída de vídeo")
                        }
                        continue
                    }
                    Self.log("output = \(video.subfolder)/\(video.name)")
                    let res = AiResult(videoURL: "\(c.base)/view?filename=\(video.name)",
                                       durationSeconds: Double(H3Workflow.frames(request.duration)) / Double(request.fps),
                                       width: ww, height: hh, fps: request.fps, hasAudio: true)
                    self.job = etapa("finishing", "Finalizando", result: res)
                    self.downloading = true
                    defer { self.downloading = false }
                    Self.log("download = \(c.base)/view?filename=\(video.name)")
                    let file = try await c.download(video, to: Self.folder.appendingPathComponent("\(promptId).mp4"))
                    let bytes = (try? FileManager.default.attributesOfItem(atPath: file.path)[.size] as? NSNumber)?.intValue ?? 0
                    Self.log("download_bytes = \(bytes)")
                    self.job = etapa("completed", "Concluído", result: res)
                    self.status = .connected
                    if let f = self.finishGeneration { self.finishGeneration = nil; f(file, nil) }
                    return
                }
            } catch is CancellationError {
                return
            } catch let e as ComfyError {
                Self.log("error = \(e.forScreen)")
                self.fail(e.forScreen)
                if let f = self.finishGeneration { self.finishGeneration = nil; f(nil, e.forScreen) }
            } catch {
                let text = error.localizedDescription
                Self.log("error = \(text)")
                self.fail(text)
                if let f = self.finishGeneration { self.finishGeneration = nil; f(nil, text) }
            }
        }
    }

    // -- gerar -------------------------------------------------------------

    /// Ao entrar na tela AI Video: o Rewarded já começa a carregar.
    func prepareAd() { AureaAdsManager.shared.preloadRewarded() }

    /// "Gerar": o H3 é enviado na hora (um job só) e o Rewarded aparece em
    /// paralelo; o vídeo só é entregue com a recompensa (AiRewardFlow).
    func generateWithReward(_ request: AiRequest) {
        guard comfy != nil else { error = "Sem conexão com o servidor"; return }
        if job?.running == true || sessionBusy { return }   // um clique = uma geração
        error = ""; message = ""; lastFile = nil
        let id = UUID().uuidString
        currentSessionId = id
        // O endereço que aceitar o POST fica CONGELADO nesta sessão: se o túnel
        // cair no meio, esta geração continua sendo acompanhada por ele.
        Self.log("POST /prompt = enviando (endpoint \(comfy?.base ?? "?"))")
        flow.generate(request, id: id, endpoint: comfy?.base)
    }

    /// "Assistir e liberar vídeo": outro anúncio para a MESMA geração — não gera de novo.
    func unlockWithAd() { if let id = currentSessionId { flow.unlockWithAd(id) } }

    /// "Tentar de novo" depois de o H3 falhar.
    ///
    /// A sessão que falhou não serve mais — não existe prompt_id para retomar. O
    /// que se aproveita é o pedido: começa uma geração NOVA, com um POST novo e
    /// um prompt_id novo. É o único caso em que repetir o POST é certo.
    func retryAfterFailure() {
        guard let s = session, s.status == .failed else { return }
        session = nil
        currentSessionId = nil
        GuardaDaSessao.shared.clear()
        job = nil
        error = ""
        generateWithReward(s.request)
    }

    /// "Tentar de novo" quando não houve anúncio (a geração não tinha começado).
    func retryGeneration() { if let id = currentSessionId { flow.retry(id) } }

    fileprivate func setShowingAd(_ on: Bool) { showingAd = on }

    private func unlock(_ s: AiGenerationSession) {
        guard let file = s.result else { return }
        Self.log("release = geração concluída=\(s.generationCompleted) recompensa=\(s.rewardEarned) → vídeo liberado")
        GuardaDaSessao.shared.clear()
        if let j = job, j.status == "completed" { history = [j] + history.filter { $0.id != j.id } }
        lastFile = file
        let (w, h) = H3Workflow.dimensions(aspect: s.request.aspect, resolution: s.request.resolution)
        lastTitle = "AI \(w)×\(h)"
    }

    /// A geração REAL no H3: Enviando → Na fila → Gerando → Finalizando →
    /// Concluído. O vídeo NÃO é entregue aqui: vai para `finish`, e quem decide a
    /// entrega é a recompensa. O anúncio na frente não pausa nada.
    private func generate(_ request: AiRequest, onPromptId: @escaping (String) -> Void,
                          finish: @escaping (URL?, String?) -> Void) {
        guard let c = comfy else { error = "Sem conexão com o servidor"; finish(nil, error); return }
        error = ""; message = ""; lastFile = nil
        status = .generating
        let (w, h) = H3Workflow.dimensions(aspect: request.aspect, resolution: request.resolution)
        let start = Date()
        func step(_ st: String, _ text: String, id: String = "", queue: Int = 0, result: AiResult? = nil) -> AiJob {
            AiJob(id: id, status: st, progress: 0, stage: text, queuePosition: queue,
                  seconds: Date().timeIntervalSince(start), error: nil, result: result)
        }
        job = step("sending", "Enviando")
        generationTask = Task { [weak self] in
            guard let self else { return }
            do {
                let workflow = try H3Workflow.build(request, image: request.mode == "image_to_video" ? request.imageRef : nil)
                let promptId = try await c.submitPrompt(workflow)
                Self.log("POST /prompt → prompt_id \(promptId) (\(w)x\(h), \(H3Workflow.frames(request.duration)) quadros)")
                onPromptId(promptId)
                self.job = step("queued", "Na fila", id: promptId)
                var failures = 0
                while !Task.isCancelled {
                    try await Task.sleep(nanoseconds: 2_000_000_000)
                    let record: [String: Any]?
                    do {
                        record = try await c.history(promptId); failures = 0
                    } catch let e as ComfyError {
                        failures += 1
                        if failures >= 15 { throw e }
                        self.status = .reconnecting
                        continue
                    }
                    self.status = .generating
                    guard let record else {
                        // Ainda não terminou: a fila diz se está rodando ou esperando.
                        // Os rótulos têm que estar também no valor de reserva: sem
                        // eles o `??` achata o tipo para (Bool, Int) e o `q.running`
                        // deixa de existir.
                        let q = (try? await c.queueSituation(promptId)) ?? (running: false, position: 0)
                        self.job = q.running ? step("running", "Gerando", id: promptId)
                                             : step("queued", "Na fila", id: promptId, queue: q.position)
                        continue
                    }
                    if let e = ComfyClient.error(inHistory: record) { throw ComfyError(http: 200, code: "execution_error", detail: e) }
                    guard let video = ComfyClient.videos(inHistory: record).first else {
                        throw ComfyError(http: 200, code: "sem_video", detail: "o /history terminou sem saída de vídeo")
                    }
                    let res = AiResult(videoURL: "\(c.base)/view?filename=\(video.name)",
                                       durationSeconds: Double(H3Workflow.frames(request.duration)) / Double(request.fps),
                                       width: w, height: h, fps: request.fps, hasAudio: true)
                    self.job = step("finishing", "Finalizando", id: promptId, result: res)
                    self.downloading = true
                    defer { self.downloading = false }
                    let file = try await c.download(video, to: Self.folder.appendingPathComponent("\(promptId).mp4"))
                    Self.log("/view → \(file.path)")
                    self.job = step("completed", "Concluído", id: promptId, result: res)
                    self.status = .connected
                    finish(file, nil)
                    return
                }
            } catch is CancellationError {
                return
            } catch let e as ComfyError {
                Self.log("erro: \(e.forScreen)")
                self.fail(e.forScreen); finish(nil, e.forScreen)
            } catch {
                let text = error.localizedDescription
                self.fail(text); finish(nil, text)
            }
        }
    }

    func cancel() {
        guard let c = comfy else { return }
        let id = job?.id ?? ""
        generationTask?.cancel()
        Task { [weak self] in
            if !id.isEmpty { await c.cancel(id) }
            guard let self else { return }
            if var j = self.job { j.status = "cancelled"; j.stage = "Cancelado"; self.job = j }
            self.status = .connected
            self.message = "Cancelado"
            // A sessão termina sem vídeo (nada a liberar).
            if let f = self.finishGeneration { self.finishGeneration = nil; f(nil, "Cancelado") }
        }
    }

    /// Só depois de o arquivo existir: a camada aponta para ele.
    func addToTimeline(_ model: AureaModel) {
        guard let file = lastFile else { return }
        let size = (try? FileManager.default.attributesOfItem(atPath: file.path)[.size] as? NSNumber)?.intValue ?? 0
        guard size > 0 else { error = "O arquivo baixado está vazio"; return }
        model.importMedia(url: file, kind: .video)
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
            if FileManager.default.fileExists(atPath: f.path) { lastFile = f; lastTitle = "AI" }
        }
    }

    /// Sobe a imagem de partida (`/upload/image`) e devolve a referência do LoadImage.
    func uploadImage(_ bytes: Data, type: String) async -> String? {
        guard let c = comfy else { return nil }
        do {
            let ref = try await c.uploadImage(bytes, type: type)
            Self.log("/upload/image → \(ref)")
            return ref
        } catch let e as ComfyError {
            error = e.forScreen; return nil
        } catch {
            self.error = error.localizedDescription; return nil
        }
    }

    private func fail(_ text: String) {
        error = text
        if var j = job { j.status = "failed"; j.stage = "Falhou"; j.error = text; job = j }
        status = comfy != nil ? .connected : .reconnecting
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
        // Contrato do RewardedAds: `false` = nenhum callback. O manager avisa a
        // falha antes de devolver `false`; essa primeira não é repassada.
        var returned = false
        let shown = AureaAdsManager.shared.showRewarded(
            opened: { [weak self] in self?.state?.setShowingAd(true); opened() },
            // A recompensa vem SÓ daqui (onUserEarnedReward do SDK).
            reward: { AureaAiState.log("reward = recebida"); reward() },
            closed: { [weak self] _ in
                AureaAiState.log("reward = anúncio fechado")
                self?.state?.setShowingAd(false); closed()
            },
            failed: { [weak self] e in
                AureaAiState.log("error = anúncio: \(e)")
                self?.state?.setShowingAd(false); if returned { failed(e) }
            })
        returned = true
        return shown
    }
}
