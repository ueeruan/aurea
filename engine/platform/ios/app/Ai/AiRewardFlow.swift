// =============================================================================
//  Aurea iOS — a geração PAGA e o Rewarded. Porte fiel de
//  android/.../ai/Recompensa.kt (AiRewardFlow). Não sabe nada de SDK nem de
//  provedor.
//
//  Ordem:  ticket no servidor → Rewarded (ticket como Dynamic User ID)
//          → recompensa → geração real → vídeo.
//   - nada é gerado antes da recompensa (o provedor cobra por geração);
//   - a recompensa só vem de `reward` (callback oficial do SDK); o servidor
//     ainda confere o callback ASSINADO do LevelPlay antes de gerar;
//   - uma recompensa inicia UMA geração;
//   - erro técnico depois da recompensa não pede outro anúncio: o mesmo ticket
//     gera de novo quando o usuário tocar em "Tentar de novo";
//   - fechar o anúncio cedo não gera nada e não perde o pedido.
//  Tudo na main thread.
// =============================================================================
import Foundation

/// O que a geração precisa de um Rewarded. `show` devolve `false` (e não chama
/// nada) se não havia anúncio para mostrar.
@MainActor
protocol RewardedAds {
    func ready() -> Bool
    func load(loaded: @escaping () -> Void, failed: @escaping (String) -> Void)
    func show(opened: @escaping () -> Void, reward: @escaping () -> Void,
              closed: @escaping () -> Void, failed: @escaping (String) -> Void) -> Bool
}

/// `String`: a sessão é gravada em disco pelo nome do caso (GuardaDaSessao).
enum AiSessionStatus: String {
    /// Pedindo o ticket e/ou esperando o Rewarded carregar. Nada foi gerado.
    case preparing
    /// Não deu para ter anúncio. Nada foi gerado; dá para tentar de novo.
    case adUnavailable
    /// Anúncio na tela. A geração ainda NÃO começou.
    case adShowing
    /// O anúncio fechou sem recompensa. Nada foi gerado; o pedido espera outro anúncio.
    case noReward
    /// Recompensa confirmada: a geração real está rodando no servidor.
    case generating
    /// Vídeo pronto, baixado e validado.
    case unlocked
    /// Não há vídeo. `canRetryWithoutAd` diz se tentar de novo pede anúncio.
    case failed
}

/// UMA geração, com o próprio ticket: a recompensa de um anúncio vale só para ela.
struct AiGenerationSession: Equatable {
    let generationId: String
    let request: AiRequest
    var ticket: String?
    /// O job aceito pelo servidor. Existindo, retomar é só ACOMPANHAR.
    var jobId: String?
    var rewardEarned = false
    var generationCompleted = false
    var result: URL?
    var status: AiSessionStatus = .preparing
    var generationStarted = false
    var adClosedEarly = false
    /// Código curto do erro (ver `explainVideoFailure`).
    var error: String?
    var adError: String?
    /// Falha técnica com a recompensa ainda valendo: o mesmo ticket, sem anúncio.
    var canRetryWithoutAd = false

    var unlocked: Bool { generationCompleted && rewardEarned && result != nil && error == nil }
}

@MainActor
final class AiRewardFlow {
    typealias Finish = (_ file: URL?, _ error: String?, _ retryWithoutAd: Bool) -> Void

    private let ads: RewardedAds
    private let requestTicket: (AiGenerationSession, @escaping (String) -> Void, @escaping (String) -> Void) -> Void
    private let bindAd: (String) -> Void
    private let startGeneration: (AiGenerationSession, @escaping (String) -> Void, @escaping Finish) -> Void
    private let onChange: (AiGenerationSession) -> Void
    private(set) var sessions: [String: AiGenerationSession] = [:]

    init(ads: RewardedAds,
         requestTicket: @escaping (AiGenerationSession, _ onTicket: @escaping (String) -> Void,
                                   _ onFail: @escaping (String) -> Void) -> Void,
         bindAd: @escaping (String) -> Void,
         startGeneration: @escaping (AiGenerationSession, _ onJob: @escaping (String) -> Void,
                                     _ onFinish: @escaping Finish) -> Void,
         onChange: @escaping (AiGenerationSession) -> Void) {
        self.ads = ads; self.requestTicket = requestTicket; self.bindAd = bindAd
        self.startGeneration = startGeneration; self.onChange = onChange
    }

    @discardableResult
    private func put(_ s: AiGenerationSession) -> AiGenerationSession {
        sessions[s.generationId] = s
        onChange(s)
        return s
    }

    /// Toque em "Gerar": ticket → anúncio. A geração só começa na recompensa.
    @discardableResult
    func generate(_ request: AiRequest, id: String = UUID().uuidString) -> AiGenerationSession? {
        guard sessions[id] == nil else { return nil }
        put(AiGenerationSession(generationId: id, request: request))
        requestTicket(sessions[id]!,
            { [weak self] ticket in
                guard let self, var s = self.sessions[id], s.ticket == nil else { return }
                s.ticket = ticket
                self.put(s)
                self.bindAd(ticket)
                self.prepareAndPresent(id)
            },
            { [weak self] code in
                // Sem ticket (limite, IA desligada, rede): nenhum anúncio, nada gerado.
                guard let self, var s = self.sessions[id] else { return }
                s.status = .failed; s.error = code; s.canRetryWithoutAd = false
                self.put(s)
            })
        return sessions[id]
    }

    /// Põe de volta uma sessão gravada. Não pede ticket, não mostra anúncio e não gera.
    @discardableResult
    func resume(_ s: AiGenerationSession) -> AiGenerationSession {
        sessions[s.generationId] = s
        return evaluate(s)
    }

    /// "Assistir de novo": o anúncio não veio ou fechou cedo. Mesmo ticket, mesmo pedido.
    func watchAgain(_ id: String) {
        guard var s = sessions[id], !s.generationStarted, !s.rewardEarned,
              s.status == .adUnavailable || s.status == .noReward, let ticket = s.ticket else { return }
        s.status = .preparing; s.adError = nil; s.adClosedEarly = false
        put(s)
        bindAd(ticket)
        prepareAndPresent(id)
    }

    /// "Tentar de novo" depois de erro técnico: mesmo ticket, SEM anúncio.
    func retryWithoutAd(_ id: String) {
        guard var s = sessions[id], s.status == .failed, s.canRetryWithoutAd, s.rewardEarned, s.ticket != nil else { return }
        s.jobId = nil; s.error = nil; s.canRetryWithoutAd = false; s.generationStarted = false
        start(s)
    }

    private func prepareAndPresent(_ id: String) {
        if ads.ready() { present(id); return }
        ads.load(loaded: { [weak self] in self?.present(id) },
                 failed: { [weak self] e in self?.noAd(id, e) })
    }

    private func noAd(_ id: String, _ e: String) {
        guard var s = sessions[id], !s.rewardEarned, !s.generationStarted else { return }
        s.status = .adUnavailable; s.adError = e
        put(s)
    }

    private func present(_ id: String) {
        guard sessions[id] != nil else { return }
        let shown = ads.show(
            opened: { [weak self] in
                guard let self, var s = self.sessions[id], !s.rewardEarned else { return }
                s.status = .adShowing; s.adError = nil
                self.put(s)
            },
            reward: { [weak self] in
                // Um anúncio, uma geração: recompensa repetida não gera de novo.
                guard let self, var s = self.sessions[id], !s.rewardEarned, !s.generationStarted else { return }
                s.rewardEarned = true
                self.start(s)
            },
            closed: { [weak self] in
                guard let self, var s = self.sessions[id], !s.rewardEarned else { return }
                s.status = .noReward; s.adClosedEarly = true
                self.put(s)
            },
            failed: { [weak self] e in self?.noAd(id, e) })
        if !shown { noAd(id, "anúncio indisponível") }
    }

    private func start(_ base: AiGenerationSession) {
        var s = base
        s.generationStarted = true; s.status = .generating; s.error = nil
        put(s)
        let id = s.generationId
        startGeneration(s,
            { [weak self] job in
                guard let self, var c = self.sessions[id] else { return }
                c.jobId = job; self.put(c)
            },
            { [weak self] file, error, retry in self?.finished(id, file, error, retry) })
    }

    private func finished(_ id: String, _ file: URL?, _ error: String?, _ retry: Bool) {
        guard var s = sessions[id] else { return }
        guard error == nil, let file else {
            s.generationCompleted = false; s.result = nil; s.error = error ?? "geracao_falhou"
            s.status = .failed; s.canRetryWithoutAd = retry && s.rewardEarned
            put(s)
            return
        }
        s.generationCompleted = true; s.result = file
        evaluate(s)
    }

    @discardableResult
    private func evaluate(_ s: AiGenerationSession) -> AiGenerationSession {
        var n = s
        if n.error != nil { n.status = .failed }
        else if n.unlocked { n.status = .unlocked }
        else if n.generationStarted { n.status = .generating }
        else if n.adClosedEarly { n.status = .noReward }
        return put(n)
    }
}
