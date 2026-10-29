import Foundation

// =============================================================================
//  0.4.1: legendas automáticas na NUVEM — Whisper large-v3-turbo no Workers AI,
//  pelo servidor do Aurea (discovery/transcribe.js, POST /api/transcribe).
//
//  O motor corta o áudio da camada nas pausas (Engine::export_speech_chunks:
//  WAV 16 kHz mono, trechos de 25–45 s, silêncio fica de fora); cada trecho vai
//  com a sessão da conta e volta como palavras com tempo, que somam o início do
//  trecho. O áudio não é guardado no servidor. Sem conta ou sem rede: lança
//  `Failure` e quem chama usa o Whisper local. Par do Android: CloudCaptions.kt.
// =============================================================================

enum CloudCaptions {
    struct Failure: LocalizedError {
        let message: String
        var auth = false
        var errorDescription: String? { message }
    }

    /// 0.4.1: o token da sessão da conta (o mesmo Keychain de Conta.swift).
    private static func token() -> String { ContaKeychain.ler("token") ?? "" }

    /// Pode usar a nuvem agora (há sessão de conta guardada).
    static var available: Bool { !token().isEmpty }

    /// Transcreve a camada `layer`. `progress` recebe 0..100 (corte do áudio até 30,
    /// envio dos trechos até 100). Cancelar a Task cancela o corte e o envio.
    static func transcribe(engine: AureaEngine, layer: Int64, language: String,
                           progress: @escaping @Sendable (Int) -> Void) async throws -> [CaptionWord] {
        let session = token()
        guard !session.isEmpty else { throw Failure(message: "sem sessao", auth: true) }
        let files = FileManager.default
        let dir = files.temporaryDirectory.appendingPathComponent("cloud-captions", isDirectory: true)
        try? files.removeItem(at: dir)
        try files.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        progress(2)
        let path = dir.path
        let listing: String = try await withTaskCancellationHandler {
            try await Task.detached(priority: .utility) { try engine.exportSpeechChunks(layer, dir: path) }.value
        } onCancel: { _ = engine.captionProgress(true) }
        try Task.checkCancellation()
        progress(30)

        // Uma linha por trecho: "início\tfim\tcaminho" (segundos da mídia).
        let chunks: [(start: Double, path: String)] = listing.split(whereSeparator: \.isNewline).compactMap { (line) -> (start: Double, path: String)? in
            let parts = line.split(separator: "\t", maxSplits: 2, omittingEmptySubsequences: false)
            guard parts.count == 3, let start = Double(parts[0]), start.isFinite, !parts[2].isEmpty else { return nil }
            return (start, String(parts[2]))
        }
        let trimmed = language.trimmingCharacters(in: .whitespacesAndNewlines)
        let lang = trimmed.isEmpty || trimmed == "auto" ? "auto" : trimmed.lowercased()
        var out: [CaptionWord] = []
        for (index, chunk) in chunks.enumerated() {
            try Task.checkCancellation()
            let file = URL(fileURLWithPath: chunk.path)
            for word in try await send(file, lang: lang, session: session) {
                out.append(CaptionWord(word: word.word, start: word.start + chunk.start, end: word.end + chunk.start))
            }
            try? files.removeItem(at: file)
            progress(30 + 70 * (index + 1) / max(chunks.count, 1))
        }
        return out
    }

    private static func send(_ file: URL, lang: String, session: String) async throws -> [CaptionWord] {
        let bytes = try Data(contentsOf: file)
        var components = URLComponents(string: ContaAPI.base + "/api/transcribe")
        components?.queryItems = [URLQueryItem(name: "lang", value: lang)]
        guard let url = components?.url else { throw Failure(message: "url") }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 120)
        request.httpMethod = "POST"
        request.setValue("Bearer " + session, forHTTPHeaderField: "Authorization")
        request.setValue("audio/wav", forHTTPHeaderField: "Content-Type")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        // 0.4.1: a sessão de ContaAPI não segue redirecionamento (o token não vaza).
        let (data, response) = try await ContaAPI.session.upload(for: request, from: bytes)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        if status == 401 { throw Failure(message: "sessao invalida", auth: true) }
        guard status == 200 else { throw Failure(message: "servidor \(status)") }
        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any] else { throw Failure(message: "resposta") }
        let words = object["words"] as? [[String: Any]] ?? []
        return words.compactMap { (row) -> CaptionWord? in
            let text = (row["w"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            guard !text.isEmpty,
                  let s = (row["s"] as? NSNumber)?.doubleValue, let e = (row["e"] as? NSNumber)?.doubleValue,
                  s.isFinite, e.isFinite, s >= 0 else { return nil }
            // 0.4.1: o iOS exige fim > início para criar a legenda; palavra de duração zero ganha 10 ms.
            return CaptionWord(word: text, start: s, end: max(e, s + 0.01))
        }
    }
}
