// =============================================================================
//  Aurea iOS — Aurea AI: o contrato e o ComfyUI, direto. Porte de
//  android/.../ai/Comfy.kt + Contrato.kt + Cliente.kt (discovery).
//
//  `/system_stats`, `POST /prompt` com o workflow do MiniMax H3, `/history`,
//  `/queue`, `/upload/image`, `/view`, `/interrupt`. Nada de `/generate`/`/v1`.
// =============================================================================
import Foundation

/// A ÚNICA configuração de endereço da Aurea AI (a MESMA do Android).
///
/// Não há endereço de servidor compilado. Nenhum. O app conhece só o endereço
/// FIXO do discovery (`discoveryURL`) e de lá recebe o `endpoint` do momento.
///
/// É isso que faz o túnel do Colab poder mudar de nome sem IPA novo e sem APK
/// novo: quem conta o endereço novo é o discovery, não o binário.
enum AureaAiConfig {
    static let discoveryURL = "https://aurea-ai-discovery.aureaapp.workers.dev/server"

    /// O mesmo User-Agent em tudo que fala com o Worker. Sem ele a Cloudflare
    /// responde 403 (erro 1010) — o UA padrao e tratado como bot.
    static let agent = "Aurea/2.0 (iOS)"
    /// Workflow do MiniMax H3 no formato de API do ComfyUI (cópia de android/.../assets/ai/).
    static let workflowResource = "minimax_h3_api"
}

// MARK: - Contrato

enum AureaAiStatus {
    case checking, connected, generating, reconnecting, disconnected, error

    /// "Online"/"Offline": o app não tem botão de conectar.
    var label: String {
        switch self {
        case .checking: return "Procurando"
        case .connected, .generating: return "Online"
        case .reconnecting: return "Reconectando"
        case .disconnected, .error: return "Offline"
        }
    }
    var canGenerate: Bool { self == .connected || self == .generating }
}

/// 1ª tentativa ao abrir; a 2ª, 2 s depois; daí em diante, a cada 5 s.
func aiBackoff(_ attempt: Int) -> UInt64 { attempt == 0 ? 2 : 5 }
/// Releitura do discovery depois de online (a batida do servidor é de 20 s).
let aiWatchSeconds: UInt64 = 20

struct AiDiscovery {
    let endpoint: String
    let online: Bool
    let model: String
    let gpu: String
    let capabilities: [String]
    /// Quando o Colab publicou isto, em segundos. 0 = o documento não trouxe.
    let updatedAt: Int
    var isValid: Bool { online && endpoint.hasPrefix("https://") }

    static func parse(_ data: Data) -> AiDiscovery? {
        guard let o = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return nil }
        var endpoint = (o["endpoint"] as? String ?? "").trimmingCharacters(in: .whitespaces)
        while endpoint.hasSuffix("/") { endpoint.removeLast() }
        return AiDiscovery(endpoint: endpoint, online: o["online"] as? Bool ?? false,
                           model: o["model"] as? String ?? "", gpu: o["gpu"] as? String ?? "",
                           capabilities: (o["capabilities"] as? [String] ?? []).filter { !$0.isEmpty },
                           updatedAt: o["updatedAt"] as? Int ?? 0)
    }
}

struct AiCapabilities: Equatable {
    var modes: [String]
    var durations: [Int]
    var aspects: [String]
    var resolutions: [String]
    var fps: [Int]
    var audio: Bool
    var queue: Int
    var hasImageToVideo: Bool { modes.contains("image_to_video") }

    static let empty = AiCapabilities(modes: [], durations: [], aspects: [], resolutions: [], fps: [24], audio: false, queue: 0)

    /// Os modos vêm do discovery; o resto é o valor do contrato (docs/ai/CONTRATO.md).
    static func fromDiscovery(_ modes: [String]) -> AiCapabilities {
        AiCapabilities(modes: modes.filter { $0 == "text_to_video" || $0 == "image_to_video" },
                       durations: [5, 10, 15], aspects: ["9:16", "16:9", "1:1", "4:5"],
                       resolutions: ["preview", "standard", "high"], fps: [24], audio: true, queue: 0)
    }
}

struct AiResult: Equatable {
    let videoURL: String
    let durationSeconds: Double
    let width: Int
    let height: Int
    let fps: Int
    let hasAudio: Bool
}

struct AiJob: Equatable {
    var id: String
    var status: String
    var progress: Double
    var stage: String
    var queuePosition: Int
    var seconds: Double
    var error: String?
    var result: AiResult?
    var finished: Bool { status == "completed" || status == "failed" || status == "cancelled" }
    var running: Bool { !finished }
}

/// Só o que está na tabela do contrato (o espelho do `Pedido` do Android).
struct AiRequest: Equatable {
    var mode: String
    var prompt: String
    var negativePrompt = ""
    var duration: Int
    var aspect: String
    var resolution: String
    var fps = 24
    var audio = true
    var seed = -1
    var turbo = true
    var imageRef: String?
}

func formatAiDuration(_ seconds: Double) -> String {
    let total = Int(seconds)
    return String(format: "%d:%02d", total / 60, total % 60)
}

// MARK: - ComfyUI

/// Erro REAL do ComfyUI, para a tela mostrar o que aconteceu.
struct ComfyError: Error {
    let http: Int
    let code: String
    let detail: String

    var forScreen: String {
        if code == "timeout" { return "timeout: o servidor não respondeu" }
        if code == "sem_conexao" { return "sem conexão: \(detail)" }
        if http == 530 { return "530: Cloudflare Tunnel indisponível" }
        if http == 405 { return "405: método/endpoint errado (\(detail))" }
        if code == "node_errors" { return "node_errors: \(detail)" }
        if http > 0 { return "\(http) \(code): \(detail)" }
        return "\(code): \(detail)"
    }
}

struct ComfyFile: Equatable {
    let name: String
    let subfolder: String
    let type: String
}

final class ComfyClient {
    let base: String
    let clientId = UUID().uuidString
    init(base: String) { self.base = base }

    /// `GET /system_stats`: 200 + JSON com "system" = servidor de pé.
    func isOnline() async -> (http: Int, ok: Bool) {
        do {
            let (http, body) = try await request("GET", "/system_stats", timeout: 10)
            let json = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any]
            return (http, http == 200 && json?["system"] != nil)
        } catch let e as ComfyError {
            return (e.http, false)
        } catch {
            return (0, false)
        }
    }

    /// `POST /prompt` com `{"prompt": WORKFLOW, "client_id"}`. Devolve o prompt_id.
    func submitPrompt(_ workflow: [String: Any]) async throws -> String {
        let body = try JSONSerialization.data(withJSONObject: ["prompt": workflow, "client_id": clientId])
        let (_, raw) = try await request("POST", "/prompt", body: body, timeout: 60)
        let o = (try? JSONSerialization.jsonObject(with: raw)) as? [String: Any] ?? [:]
        if let errors = o["node_errors"] as? [String: Any], !errors.isEmpty {
            throw ComfyError(http: 200, code: "node_errors", detail: Self.summarize(errors))
        }
        guard let id = o["prompt_id"] as? String, !id.isEmpty else {
            throw ComfyError(http: 200, code: "sem_prompt_id", detail: String(decoding: raw.prefix(300), as: UTF8.self))
        }
        return id
    }

    /// `GET /history/{id}`: o registro do job, ou nulo enquanto não terminou.
    func history(_ promptId: String) async throws -> [String: Any]? {
        let (_, raw) = try await request("GET", "/history/" + Self.escape(promptId))
        let o = (try? JSONSerialization.jsonObject(with: raw)) as? [String: Any]
        return o?[promptId] as? [String: Any]
    }

    /// `GET /queue`: (rodando agora?, posição na fila de espera; 0 = não está esperando).
    func queueSituation(_ promptId: String) async throws -> (running: Bool, position: Int) {
        let (_, raw) = try await request("GET", "/queue")
        let o = (try? JSONSerialization.jsonObject(with: raw)) as? [String: Any] ?? [:]
        let running = o["queue_running"] as? [[Any]] ?? []
        if running.contains(where: { $0.count > 1 && ($0[1] as? String) == promptId }) { return (true, 0) }
        // A fila de espera vem na ordem de chegada pelo número (índice 0 do item).
        let pending = (o["queue_pending"] as? [[Any]] ?? [])
            .sorted { (($0.first as? NSNumber)?.doubleValue ?? 0) < (($1.first as? NSNumber)?.doubleValue ?? 0) }
        if let i = pending.firstIndex(where: { $0.count > 1 && ($0[1] as? String) == promptId }) { return (false, i + 1) }
        return (false, 0)
    }

    /// `POST /upload/image` (multipart, campo "image"). Devolve o valor para o LoadImage.
    func uploadImage(_ bytes: Data, type: String) async throws -> String {
        let boundary = "----aurea" + UUID().uuidString.replacingOccurrences(of: "-", with: "")
        let ext = type == "image/png" ? "png" : type == "image/webp" ? "webp" : "jpg"
        let name = "aurea_\(UUID().uuidString.prefix(8).lowercased()).\(ext)"
        var body = Data("--\(boundary)\r\nContent-Disposition: form-data; name=\"image\"; filename=\"\(name)\"\r\nContent-Type: \(type)\r\n\r\n".utf8)
        body.append(bytes)
        body.append(Data(("\r\n--\(boundary)\r\nContent-Disposition: form-data; name=\"type\"\r\n\r\ninput"
            + "\r\n--\(boundary)\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--\(boundary)--\r\n").utf8))
        let (_, raw) = try await request("POST", "/upload/image", body: body,
                                         contentType: "multipart/form-data; boundary=\(boundary)", timeout: 120)
        let o = (try? JSONSerialization.jsonObject(with: raw)) as? [String: Any] ?? [:]
        guard let n = o["name"] as? String, !n.isEmpty else {
            throw ComfyError(http: 200, code: "upload", detail: String(decoding: raw.prefix(300), as: UTF8.self))
        }
        let sub = o["subfolder"] as? String ?? ""
        return sub.isEmpty ? n : "\(sub)/\(n)"
    }

    /// `GET /view?filename&subfolder&type` → arquivo local.
    func download(_ file: ComfyFile, to destination: URL) async throws -> URL {
        let q = "filename=\(Self.escape(file.name))&subfolder=\(Self.escape(file.subfolder))&type=\(Self.escape(file.type))"
        let (_, raw) = try await request("GET", "/view?" + q, timeout: 300)
        if raw.isEmpty { throw ComfyError(http: 200, code: "view_vazio", detail: "o /view devolveu 0 bytes") }
        try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        try raw.write(to: destination, options: .atomic)
        return destination
    }

    /// Cancela: tira da fila de espera e, se já estiver rodando, interrompe.
    func cancel(_ promptId: String) async {
        if let body = try? JSONSerialization.data(withJSONObject: ["delete": [promptId]]) {
            _ = try? await request("POST", "/queue", body: body)
        }
        _ = try? await request("POST", "/interrupt", body: Data("{}".utf8))
    }

    // MARK: transporte

    private func request(_ method: String, _ path: String, body: Data? = nil,
                         contentType: String = "application/json", timeout: TimeInterval = 30) async throws -> (Int, Data) {
        guard let url = URL(string: base + path) else { throw ComfyError(http: 0, code: "sem_conexao", detail: "URL inválida") }
        var r = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: timeout)
        r.httpMethod = method
        r.setValue("*/*", forHTTPHeaderField: "Accept")
        // A Cloudflare do Worker recusa o UA padrao com 403 (1010).
        r.setValue(AureaAiConfig.agent, forHTTPHeaderField: "User-Agent")
        if let body {
            r.httpBody = body
            r.setValue(contentType, forHTTPHeaderField: "Content-Type")
        }
        let data: Data, response: URLResponse
        do {
            (data, response) = try await URLSession.shared.data(for: r)
        } catch let e as URLError where e.code == .timedOut {
            throw ComfyError(http: 0, code: "timeout", detail: "\(method) \(path)")
        } catch {
            throw ComfyError(http: 0, code: "sem_conexao", detail: error.localizedDescription)
        }
        let http = (response as? HTTPURLResponse)?.statusCode ?? 0
        if !(200...299).contains(http) { throw Self.translate(http, data) }
        return (http, data)
    }

    /// O corpo de erro do ComfyUI: `{"error": {"type", "message", "details"}, "node_errors": {...}}`.
    private static func translate(_ http: Int, _ raw: Data) -> ComfyError {
        let text = String(decoding: raw, as: UTF8.self)
        guard let o = (try? JSONSerialization.jsonObject(with: raw)) as? [String: Any] else {
            return ComfyError(http: http, code: "erro", detail: text.isEmpty ? "HTTP \(http)" : String(text.prefix(200)))
        }
        if let errors = o["node_errors"] as? [String: Any], !errors.isEmpty {
            return ComfyError(http: http, code: "node_errors", detail: summarize(errors))
        }
        if let e = o["error"] as? [String: Any] {
            let msg = [e["message"] as? String, e["details"] as? String].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: " — ")
            let type = (e["type"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "erro"
            return ComfyError(http: http, code: type, detail: msg.isEmpty ? String(text.prefix(200)) : msg)
        }
        return ComfyError(http: http, code: "erro", detail: String(text.prefix(200)))
    }

    private static func summarize(_ errors: [String: Any]) -> String {
        errors.keys.sorted().prefix(3).map { id in
            let n = errors[id] as? [String: Any]
            let cls = n?["class_type"] as? String ?? ""
            let first = (n?["errors"] as? [[String: Any]])?.first
            let msg = [first?["message"] as? String, first?["details"] as? String].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: " — ")
            return "nó \(id) (\(cls)): \(msg)"
        }.joined(separator: "; ")
    }

    private static func escape(_ s: String) -> String {
        s.addingPercentEncoding(withAllowedCharacters: .alphanumerics.union(CharacterSet(charactersIn: "-._~"))) ?? s
    }

    /// Todas as saídas de vídeo do registro do `/history`.
    static func videos(inHistory record: [String: Any]) -> [ComfyFile] {
        guard let outputs = record["outputs"] as? [String: Any] else { return [] }
        var found: [ComfyFile] = []
        for node in outputs.keys.sorted() {
            guard let o = outputs[node] as? [String: Any] else { continue }
            for key in o.keys.sorted() {
                for item in o[key] as? [[String: Any]] ?? [] {
                    let name = item["filename"] as? String ?? ""
                    let lower = name.lowercased()
                    if [".mp4", ".webm", ".mkv", ".mov"].contains(where: { lower.hasSuffix($0) }) {
                        found.append(ComfyFile(name: name, subfolder: item["subfolder"] as? String ?? "",
                                               type: item["type"] as? String ?? "output"))
                    }
                }
            }
        }
        return found
    }

    /// Mensagem de erro do `/history` (status_str == "error").
    static func error(inHistory record: [String: Any]) -> String? {
        guard let st = record["status"] as? [String: Any], st["status_str"] as? String == "error" else { return nil }
        for m in st["messages"] as? [[Any]] ?? [] where m.first as? String == "execution_error" {
            let d = m.count > 1 ? m[1] as? [String: Any] : nil
            let msg = (d?["exception_message"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines).prefix(300)
            return "execution_error no nó \(d?["node_id"] ?? "") (\(d?["node_type"] ?? "")): \(d?["exception_type"] ?? ""): \(msg)"
        }
        return "execution_error"
    }

    /// `GET DISCOVERY_URL`, sem token e sem cache. (HTTP 0 = sem rede.)
    static func readDiscovery(_ url: String = AureaAiConfig.discoveryURL) async -> (http: Int, doc: AiDiscovery?) {
        guard let u = URL(string: url) else { return (0, nil) }
        var r = URLRequest(url: u, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 10)
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        r.setValue("no-cache", forHTTPHeaderField: "Cache-Control")
        r.setValue(AureaAiConfig.agent, forHTTPHeaderField: "User-Agent")
        guard let (data, response) = try? await URLSession.shared.data(for: r) else { return (0, nil) }
        let http = (response as? HTTPURLResponse)?.statusCode ?? 0
        return (http, (200...299).contains(http) && !data.isEmpty ? AiDiscovery.parse(data) : nil)
    }
}

// MARK: - Workflow do MiniMax H3

/// O workflow (Resources/ai/minimax_h3_api.json, o MESMO arquivo do Android),
/// montado para UM pedido. A tela só muda VALORES: o mapa `_aurea.inputs` diz
/// em que nó e em que entrada cada um entra.
enum H3Workflow {
    /// Megapixels por resolução (0,4 = padrão do template; 0,98 = 768p oficial).
    static func megapixels(_ resolution: String) -> Double {
        switch resolution {
        case "preview": return 0.2
        case "high": return 0.98
        default: return 0.4
        }
    }

    /// ResolutionSelector: área em MP e proporção, arredondado para CIMA ao múltiplo de 32.
    static func dimensions(aspect: String, resolution: String) -> (Int, Int) {
        let parts = aspect.split(separator: ":").compactMap { Double($0.trimmingCharacters(in: .whitespaces)) }
        let a = parts.count == 2 ? parts[0] : 16, b = parts.count == 2 ? parts[1] : 9
        let area = megapixels(resolution) * 1_000_000
        return (Int(ceil((area * a / b).squareRoot() / 32)) * 32, Int(ceil((area * b / a).squareRoot() / 32)) * 32)
    }

    /// `max(5, round(a*24)) + (5 - (max(5, round(a*24)) % 17)) % 17` — a grade 17k+5 do H3.
    static func frames(_ seconds: Int) -> Int {
        let f = max(5, Int((Double(seconds) * 24).rounded()))
        return f + (((5 - f % 17) % 17) + 17) % 17
    }

    static func build(_ request: AiRequest, image: String?) throws -> [String: Any] {
        guard let url = Bundle.main.url(forResource: AureaAiConfig.workflowResource, withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let file = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
            throw ComfyError(http: 0, code: "workflow", detail: "minimax_h3_api.json ausente no app")
        }
        return try build(file, request, image: image)
    }

    static func build(_ file: [String: Any], _ request: AiRequest, image: String?) throws -> [String: Any] {
        var g = file
        guard let meta = g.removeValue(forKey: "_aurea") as? [String: Any],
              let map = meta["inputs"] as? [String: Any] else {
            throw ComfyError(http: 0, code: "workflow", detail: "workflow sem _aurea.inputs")
        }
        func put(_ key: String, _ value: Any) throws {
            guard let target = map[key] as? String else { throw ComfyError(http: 0, code: "workflow", detail: "sem entrada \(key)") }
            let parts = target.split(separator: ".", maxSplits: 1).map(String.init)
            guard parts.count == 2, var node = g[parts[0]] as? [String: Any], var inputs = node["inputs"] as? [String: Any] else {
                throw ComfyError(http: 0, code: "workflow", detail: "nó \(target) não existe")
            }
            inputs[parts[1]] = value
            node["inputs"] = inputs
            g[parts[0]] = node
        }
        let (w, h) = dimensions(aspect: request.aspect, resolution: request.resolution)
        try put("prompt", request.prompt)
        try put("largura", w)
        try put("altura", h)
        try put("quadros", frames(request.duration))
        try put("semente", request.seed >= 0 ? Int64(request.seed) : Int64.random(in: 0..<1_000_000_000_000_000))
        try put("turbo", request.turbo)
        try put("fps", Double(request.fps))
        try put("prefixo", "aurea/h3")
        if request.mode == "image_to_video", let image, let img = meta["imagem"] as? [String: Any],
           let nodeId = img["no"] as? String, let entry = img["entrada"] as? String {
            g[nodeId] = ["class_type": "LoadImage", "inputs": ["image": image]]
            let parts = entry.split(separator: ".", maxSplits: 1).map(String.init)
            if parts.count == 2, var node = g[parts[0]] as? [String: Any], var inputs = node["inputs"] as? [String: Any] {
                inputs[parts[1]] = [nodeId, 0]
                node["inputs"] = inputs
                g[parts[0]] = node
            }
        }
        return g
    }
}

// MARK: - Backend do Aurea (8Scale por trás)

/// A geração de vídeo pelo BACKEND do Aurea (Cloudflare Worker), que fala com a
/// 8Scale. O app nunca fala com a 8Scale e nunca vê a chave dela.
enum AureaVideoBackend {
    /// `https://aurea-ai-discovery.aureaapp.workers.dev/api/ai/video`
    static let base: String = {
        let d = AureaAiConfig.discoveryURL
        let raiz = d.hasSuffix("/server") ? String(d.dropLast("/server".count)) : d
        return raiz + "/api/ai/video"
    }()
}

/// O que o backend oferece hoje (monta a tela).
struct AiVideoConfig {
    let enabled: Bool
    let model: String
    let modes: [String]
    let durations: [Int]
    let aspects: [String]
    let resolutions: [String]
    let promptMax: Int
}

/// Estado de um job. `status` ∈ queued | generating | completed | failed | cancelled.
struct AiVideoJob {
    let id: String
    let status: String
    let stage: String
    let seconds: Double
    let error: String?
    let retryWithoutAd: Bool
    let readyToDownload: Bool
    var finished: Bool { status == "completed" || status == "failed" || status == "cancelled" }
}

/// Falha com o código curto do backend.
struct VideoFailure: Error {
    let code: String
    let http: Int
    init(_ code: String, http: Int = 0) { self.code = code; self.http = http }
    var transient: Bool {
        http == 0 || http >= 500 || ["provedor_ocupado", "provedor_indisponivel", "provedor_timeout",
                                     "sem_conexao", "tempo_esgotado"].contains(code)
    }
}

/// O provedor do ponto de vista do APP (a tela só conhece isto).
protocol VideoGenerationProvider {
    func config() async throws -> AiVideoConfig
    func uploadImage(_ data: Data, type: String) async throws -> String
    func ticket(_ r: AiRequest) async throws -> String
    func generate(ticket: String) async throws -> String
    func status(_ jobId: String) async throws -> AiVideoJob
    func cancel(_ jobId: String) async throws
    func download(_ jobId: String, to destination: URL) async throws -> URL
}

/// Identidade aleatória do aparelho, só para os limites do servidor.
enum AiDeviceId {
    static var value: String {
        let k = "aurea_ai_video_aparelho"
        if let v = UserDefaults.standard.string(forKey: k) { return v }
        let n = UUID().uuidString
        UserDefaults.standard.set(n, forKey: k)
        return n
    }
}

final class AureaBackendVideoProvider: VideoGenerationProvider {
    private let base: String
    private let device: String
    private let session: URLSession

    init(base: String = AureaVideoBackend.base, device: String = AiDeviceId.value) {
        self.base = base
        self.device = device
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 30
        c.requestCachePolicy = .reloadIgnoringLocalCacheData
        session = URLSession(configuration: c)
    }

    func config() async throws -> AiVideoConfig {
        let o = try await request("GET", "/config")
        func list(_ k: String) -> [String] { (o[k] as? [Any] ?? []).map { "\($0)" } }
        return AiVideoConfig(enabled: o["enabled"] as? Bool ?? false,
                             model: o["model"] as? String ?? "",
                             modes: list("modes"),
                             durations: (o["durations"] as? [Any] ?? []).compactMap { ($0 as? NSNumber)?.intValue },
                             aspects: list("aspectRatios"),
                             resolutions: list("resolutions"),
                             promptMax: (o["promptMaxChars"] as? NSNumber)?.intValue ?? 800)
    }

    func uploadImage(_ data: Data, type: String) async throws -> String {
        let o = try await request("POST", "/images", body: data, contentType: type)
        guard let id = o["imageId"] as? String, !id.isEmpty else { throw VideoFailure("resposta_invalida") }
        return id
    }

    func ticket(_ r: AiRequest) async throws -> String {
        var b: [String: Any] = ["mode": r.mode, "prompt": r.prompt, "negativePrompt": r.negativePrompt,
                                "duration": r.duration, "aspectRatio": r.aspect, "resolution": r.resolution]
        if let img = r.imageRef { b["imageId"] = img }
        let o = try await request("POST", "/tickets", body: try JSONSerialization.data(withJSONObject: b))
        guard let t = o["ticket"] as? String, !t.isEmpty else { throw VideoFailure("resposta_invalida") }
        return t
    }

    func generate(ticket: String) async throws -> String {
        let body = try JSONSerialization.data(withJSONObject: ["ticket": ticket])
        let o = try await request("POST", "/generate", body: body)
        guard let j = o["jobId"] as? String, !j.isEmpty else { throw VideoFailure("resposta_invalida") }
        return j
    }

    func status(_ jobId: String) async throws -> AiVideoJob {
        let o = try await request("GET", "/jobs/\(enc(jobId))")
        return AiVideoJob(id: o["jobId"] as? String ?? jobId,
                          status: o["status"] as? String ?? "queued",
                          stage: o["stage"] as? String ?? "",
                          seconds: (o["elapsedSeconds"] as? NSNumber)?.doubleValue ?? 0,
                          error: o["error"] as? String,
                          retryWithoutAd: o["retryWithoutAd"] as? Bool ?? false,
                          readyToDownload: o["result"] is [String: Any])
    }

    func cancel(_ jobId: String) async throws {
        _ = try await request("POST", "/jobs/\(enc(jobId))/cancel", body: Data())
    }

    /// Baixa para um temporário e só troca de nome se for MP4 de verdade (`ftyp`).
    func download(_ jobId: String, to destination: URL) async throws -> URL {
        var req = URLRequest(url: URL(string: base + "/jobs/\(enc(jobId))/video")!)
        req.timeoutInterval = 300
        headers(&req)
        let tmp: URL
        let resp: URLResponse
        do {
            (tmp, resp) = try await session.download(for: req)
        } catch {
            throw VideoFailure((error as? URLError)?.code == .timedOut ? "tempo_esgotado" : "download_interrompido")
        }
        let http = (resp as? HTTPURLResponse)?.statusCode ?? 0
        guard (200..<300).contains(http) else {
            let data = (try? Data(contentsOf: tmp)) ?? Data()
            throw failure(http, data)
        }
        let expected = resp.expectedContentLength
        let size = (try? FileManager.default.attributesOfItem(atPath: tmp.path)[.size] as? NSNumber)?.int64Value ?? 0
        if expected > 0 && size != expected { throw VideoFailure("download_interrompido") }
        guard Self.isMp4(tmp) else { throw VideoFailure("resultado_nao_e_video") }
        try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? FileManager.default.removeItem(at: destination)
        try FileManager.default.moveItem(at: tmp, to: destination)
        return destination
    }

    static func isMp4(_ url: URL) -> Bool {
        guard let h = try? FileHandle(forReadingFrom: url) else { return false }
        defer { try? h.close() }
        guard let d = try? h.read(upToCount: 8), d.count == 8 else { return false }
        return d[4] == 0x66 && d[5] == 0x74 && d[6] == 0x79 && d[7] == 0x70   // "ftyp"
    }

    // -- transporte --------------------------------------------------------

    private func enc(_ s: String) -> String { s.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? s }

    private func headers(_ r: inout URLRequest) {
        r.setValue("application/json, video/mp4", forHTTPHeaderField: "Accept")
        // Sem UA próprio a Cloudflare do Worker devolve 403 (1010).
        r.setValue(AureaAiConfig.agent, forHTTPHeaderField: "User-Agent")
        r.setValue(device, forHTTPHeaderField: "x-aurea-device")
    }

    private func request(_ method: String, _ path: String, body: Data? = nil,
                         contentType: String = "application/json") async throws -> [String: Any] {
        var req = URLRequest(url: URL(string: base + path)!)
        req.httpMethod = method
        headers(&req)
        if let body { req.httpBody = body; req.setValue(contentType, forHTTPHeaderField: "Content-Type") }
        let data: Data
        let resp: URLResponse
        do {
            (data, resp) = try await session.data(for: req)
        } catch {
            let code = (error as? URLError)?.code
            throw VideoFailure(code == .timedOut ? "tempo_esgotado" : "sem_conexao")
        }
        let http = (resp as? HTTPURLResponse)?.statusCode ?? 0
        guard (200..<300).contains(http) else { throw failure(http, data) }
        if data.isEmpty { return [:] }
        guard let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw VideoFailure("resposta_invalida")
        }
        return o
    }

    private func failure(_ http: Int, _ data: Data) -> VideoFailure {
        let o = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
        let code = (o?["error"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "erro_\(http)"
        return VideoFailure(code, http: http)
    }
}

/// Código do backend → frase para quem usa. O prompt nunca se perde por causa disso.
func explainVideoFailure(_ code: String?) -> String {
    guard let code else { return "Não foi possível gerar o vídeo." }
    switch code {
    case "sem_conexao": return "Sem internet. Confira a conexão e tente de novo."
    case "tempo_esgotado", "provedor_timeout": return "O servidor demorou para responder. Tente de novo."
    case "provedor_indisponivel", "provedor_erro", "resposta_invalida":
        return "O serviço de geração está indisponível agora. Tente de novo em instantes."
    case "provedor_ocupado", "servidor_ocupado": return "Muita gente gerando agora. Tente de novo em alguns minutos."
    case "provedor_auth", "provedor_nao_configurado", "saldo_insuficiente", "modelo_indisponivel",
         "ia_nao_configurada", "recompensa_nao_configurada":
        return "A geração por IA está em manutenção. Tente mais tarde."
    case "ia_desligada": return "A geração por IA está pausada no momento."
    case "orcamento_diario", "limite_global_diario": return "O limite de gerações de hoje foi atingido. Volte amanhã."
    case "limite_diario": return "Você atingiu o limite de gerações de hoje. Volte amanhã."
    case "muitos_pedidos": return "Muitos pedidos seguidos. Espere um pouco e tente de novo."
    case "job_em_andamento", "em_andamento": return "Já existe uma geração sua em andamento."
    case "conteudo_bloqueado": return "Esse pedido foi bloqueado pela política de conteúdo. Mude o texto e tente de novo."
    case "pedido_recusado": return "O pedido foi recusado pelo modelo. Mude o texto e tente de novo."
    case "prompt_vazio": return "Escreva o que você quer ver no vídeo."
    case "prompt_longo": return "O texto está longo demais."
    case "geracao_falhou": return "A geração falhou do lado do servidor."
    case "resultado_invalido", "resultado_nao_e_video": return "O servidor devolveu um arquivo que não é vídeo."
    case "resultado_expirado": return "O vídeo expirou no servidor (fica guardado 24 h)."
    case "download_interrompido", "download_falhou": return "O download foi interrompido. Toque para baixar de novo."
    case "recompensa_pendente": return "O anúncio ainda não foi confirmado. Tente de novo em instantes."
    case "ticket_expirado", "ticket_invalido", "ticket_usado": return "Este pedido expirou. Toque em gerar de novo."
    case "imagem_expirada", "imagem_invalida", "imagem_grande": return "Escolha a imagem de novo (PNG, JPEG ou WebP, até 8 MB)."
    case "cancelado": return "Geração cancelada."
    default: return "Não foi possível gerar o vídeo (\(code))."
    }
}
