// =============================================================================
//  Aurea iOS — Aurea AI: o contrato e o ComfyUI, direto. Porte de
//  android/.../ai/Comfy.kt + Contrato.kt + Cliente.kt (discovery).
//
//  `/system_stats`, `POST /prompt` com o workflow do MiniMax H3, `/history`,
//  `/queue`, `/upload/image`, `/view`, `/interrupt`. Nada de `/generate`/`/v1`.
// =============================================================================
import Foundation

/// A ÚNICA configuração de endereço da Aurea AI (a MESMA do Android).
enum AureaAiConfig {
    static let baseURL = "https://calculators-here-reasons-rice.trycloudflare.com"
    static let discoveryURL = "https://aurea-ai-discovery.aureaapp.workers.dev/server"
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
    var isValid: Bool { online && endpoint.hasPrefix("https://") }

    static func parse(_ data: Data) -> AiDiscovery? {
        guard let o = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return nil }
        var endpoint = (o["endpoint"] as? String ?? "").trimmingCharacters(in: .whitespaces)
        while endpoint.hasSuffix("/") { endpoint.removeLast() }
        return AiDiscovery(endpoint: endpoint, online: o["online"] as? Bool ?? false,
                           model: o["model"] as? String ?? "", gpu: o["gpu"] as? String ?? "",
                           capabilities: (o["capabilities"] as? [String] ?? []).filter { !$0.isEmpty })
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
