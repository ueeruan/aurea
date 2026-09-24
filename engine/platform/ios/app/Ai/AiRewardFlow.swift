// =============================================================================
//  Aurea iOS — o "direito ao vídeo" por cima da geração. Porte fiel de
//  android/.../ai/Recompensa.kt (AiRewardFlow). Não sabe nada de H3 nem de SDK.
//
//   - a geração é enviada ao H3 NO TOQUE em "Gerar" (um job só) e o Rewarded
//     aparece em paralelo: o H3 trabalha enquanto o anúncio roda;
//   - a recompensa só vem de `reward` (callback oficial do SDK) — abrir, fechar
//     ou tempo não contam;
//   - o vídeo só é entregue com `generationCompleted && rewardEarned`;
//   - fechar cedo não cancela, não apaga e não gera de novo: o resultado fica
//     bloqueado até um novo anúncio recompensar.
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

enum AiSessionStatus {
    /// Esperando um Rewarded carregar. A geração NÃO começou.
    case preparing
    /// Não deu para ter anúncio. A geração NÃO começou; dá para tentar de novo.
    case adUnavailable
    /// Anúncio na tela; o H3 já está gerando por baixo.
    case adShowing
    /// H3 gerando; o anúncio já fechou.
    case generating
    /// Vídeo pronto e ainda bloqueado: falta a recompensa.
    case locked
    /// Vídeo pronto e liberado.
    case unlocked
    /// O H3 falhou: não há vídeo para liberar.
    case failed
}

/// UMA geração, com id próprio: a recompensa de uma nunca libera outra.
struct AiGenerationSession: Equatable {
    let generationId: String
    let request: AiRequest
    var promptId: String?
    var rewardEarned = false
    var generationCompleted = false
    var result: URL?
    var status: AiSessionStatus = .preparing
    /// O H3 já foi chamado para esta sessão (uma vez só, nunca de novo).
    var generationStarted = false
    /// O usuário fechou o anúncio antes da recompensa.
    var adClosedEarly = false
    var error: String?
    var adError: String?

    /// A ÚNICA regra de liberação.
    var unlocked: Bool { generationCompleted && rewardEarned && result != nil && error == nil }
}

@MainActor
final class AiRewardFlow {
    private let ads: RewardedAds
    private let startGeneration: (AiGenerationSession, @escaping (String) -> Void, @escaping (URL?, String?) -> Void) -> Void
    private let onChange: (AiGenerationSession) -> Void
    private(set) var sessions: [String: AiGenerationSession] = [:]

    init(ads: RewardedAds,
         startGeneration: @escaping (AiGenerationSession, _ onPromptId: @escaping (String) -> Void,
                                     _ onFinish: @escaping (URL?, String?) -> Void) -> Void,
         onChange: @escaping (AiGenerationSession) -> Void) {
        self.ads = ads; self.startGeneration = startGeneration; self.onChange = onChange
    }

    @discardableResult
    private func put(_ s: AiGenerationSession) -> AiGenerationSession {
        sessions[s.generationId] = s
        onChange(s)
        return s
    }

    /// Toque em "Gerar": o H3 começa JÁ (uma vez só) e o Rewarded vem em paralelo.
    @discardableResult
    func generate(_ request: AiRequest, id: String = UUID().uuidString) -> AiGenerationSession? {
        guard sessions[id] == nil else { return nil }
        var s = AiGenerationSession(generationId: id, request: request)
        s.generationStarted = true; s.status = .generating
        let started = put(s)
        startGeneration(started,
            { [weak self] pid in
                guard let self, var s = self.sessions[id] else { return }
                s.promptId = pid; self.put(s)
            },
            { [weak self] file, error in self?.finished(id, file, error) })
        prepareAndPresent(id, unlock: false)
        return sessions[id]
    }

    /// "Tentar de novo" de uma sessão que ficou sem anúncio (a geração não tinha começado).
    func retry(_ id: String) {
        guard let s = sessions[id], !s.generationStarted, s.status == .adUnavailable else { return }
        var n = s; n.status = .preparing; n.adError = nil
        put(n)
        prepareAndPresent(id, unlock: false)
    }

    /// "Assistir e liberar vídeo": outro Rewarded para a MESMA sessão. Não gera de novo.
    func unlockWithAd(_ id: String) {
        guard let s = sessions[id], !s.rewardEarned, s.error == nil else { return }
        prepareAndPresent(id, unlock: true)
    }

    private func prepareAndPresent(_ id: String, unlock: Bool) {
        if ads.ready() { present(id, unlock: unlock); return }
        if !unlock, var s = sessions[id], !s.generationStarted { s.status = .preparing; put(s) }
        ads.load(loaded: { [weak self] in self?.present(id, unlock: unlock) },
                 failed: { [weak self] e in self?.noAd(id, unlock: unlock, e) })
    }

    private func noAd(_ id: String, unlock: Bool, _ e: String) {
        guard var s = sessions[id] else { return }
        // Sem anúncio: a geração não começa; na liberação, o vídeo segue como estava.
        if !unlock && !s.generationStarted { s.status = .adUnavailable }
        s.adError = e
        put(s)
    }

    private func present(_ id: String, unlock: Bool) {
        guard var current = sessions[id] else { return }
        if current.adError != nil { current.adError = nil; put(current) }
        let shown = ads.show(
            opened: { [weak self] in
                guard let self, var s = self.sessions[id] else { return }
                // O H3 já está gerando por baixo; o anúncio só muda o que a tela diz.
                if !s.generationCompleted && s.error == nil { s.status = .adShowing; self.put(s) }
            },
            reward: { [weak self] in
                guard let self, var s = self.sessions[id] else { return }
                s.rewardEarned = true
                self.evaluate(s)
            },
            closed: { [weak self] in
                guard let self, var s = self.sessions[id] else { return }
                if !s.rewardEarned { s.adClosedEarly = true }
                self.evaluate(s)
            },
            failed: { [weak self] e in self?.noAd(id, unlock: unlock, e) })
        // `false` = não havia anúncio (e nenhum callback é chamado).
        if !shown { noAd(id, unlock: unlock, "anúncio indisponível") }
    }

    private func finished(_ id: String, _ file: URL?, _ error: String?) {
        guard var s = sessions[id] else { return }
        guard error == nil, let file else {
            // Erro do H3: nada é liberado — não existe resultado.
            s.generationCompleted = false; s.result = nil; s.error = error ?? "sem vídeo"; s.status = .failed
            put(s)
            return
        }
        s.generationCompleted = true; s.result = file
        evaluate(s)
    }

    /// Recalcula o status a partir dos DOIS estados independentes.
    private func evaluate(_ s: AiGenerationSession) {
        var n = s
        if n.error != nil { n.status = .failed }
        else if n.unlocked { n.status = .unlocked }
        else if n.generationCompleted { n.status = .locked }
        else if n.status == .adShowing && !n.rewardEarned && !n.adClosedEarly { n.status = .adShowing }
        else { n.status = .generating }
        put(n)
    }
}
