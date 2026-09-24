// =============================================================================
//  Aurea iOS — a geração da IA gravada no aparelho.
//
//  Porte de android/.../ai/GuardaDaSessao.kt. Existe pelo mesmo motivo: o
//  Rewarded traz o app para trás e o sistema pode matar o processo. Sem isto, o
//  job ia embora com o processo e a tela ficava em "Gerando..." para sempre.
//
//  Regra que não muda: retomar NUNCA pede outra geração. Com job, só se
//  ACOMPANHA; sem job, o ticket (idempotente no servidor) devolve o mesmo job —
//  gerar duas vezes seria pagar duas vezes pelo mesmo vídeo.
// =============================================================================
import Foundation

final class GuardaDaSessao {

    static let shared = GuardaDaSessao()

    private let chave = "aurea_ai_sessao"
    private let defaults = UserDefaults.standard

    func write(_ s: AiGenerationSession) {
        var d: [String: Any] = [
            "generationId": s.generationId,
            "generationCompleted": s.generationCompleted,
            "rewardEarned": s.rewardEarned,
            "generationStarted": s.generationStarted,
            "adClosedEarly": s.adClosedEarly,
            "canRetryWithoutAd": s.canRetryWithoutAd,
            "status": s.status.rawValue,
            "mode": s.request.mode,
            "prompt": s.request.prompt,
            "negativePrompt": s.request.negativePrompt,
            "duration": s.request.duration,
            "aspect": s.request.aspect,
            "resolution": s.request.resolution,
        ]
        if let v = s.ticket { d["ticket"] = v }
        if let v = s.jobId { d["jobId"] = v }
        if let v = s.error { d["error"] = v }
        if let v = s.adError { d["adError"] = v }
        if let v = s.result { d["file"] = v.path }
        if let v = s.request.imageRef { d["imageRef"] = v }
        defaults.set(d, forKey: chave)
    }

    func clear() { defaults.removeObject(forKey: chave) }

    /// A sessão gravada, ou nulo quando não há nenhuma (ou a gravada está ilegível).
    func read() -> AiGenerationSession? {
        guard let d = defaults.dictionary(forKey: chave) else { return nil }
        guard let id = d["generationId"] as? String else { clear(); return nil }

        let status = AiSessionStatus(rawValue: d["status"] as? String ?? "") ?? .generating
        let retryable = d["canRetryWithoutAd"] as? Bool ?? false
        if status == .unlocked || (status == .failed && !retryable) { clear(); return nil }

        var request = AiRequest(mode: d["mode"] as? String ?? "text_to_video",
                                prompt: d["prompt"] as? String ?? "",
                                duration: d["duration"] as? Int ?? 5,
                                aspect: d["aspect"] as? String ?? "16:9",
                                resolution: d["resolution"] as? String ?? "480p")
        request.negativePrompt = d["negativePrompt"] as? String ?? ""
        request.imageRef = d["imageRef"] as? String

        var s = AiGenerationSession(generationId: id, request: request)
        s.ticket = d["ticket"] as? String
        s.jobId = d["jobId"] as? String
        s.generationCompleted = d["generationCompleted"] as? Bool ?? false
        s.rewardEarned = d["rewardEarned"] as? Bool ?? false
        s.generationStarted = d["generationStarted"] as? Bool ?? false
        s.adClosedEarly = d["adClosedEarly"] as? Bool ?? false
        s.canRetryWithoutAd = retryable
        s.error = d["error"] as? String
        s.adError = d["adError"] as? String
        s.status = status
        if let path = d["file"] as? String, FileManager.default.fileExists(atPath: path) {
            s.result = URL(fileURLWithPath: path)
        }
        return s
    }
}
