import Foundation
import Security

/// Um modelo gratuito do OpenRouter que aceita ferramentas (tool calling).
struct AssistantModel: Identifiable, Equatable, Hashable {
    let id: String
    let name: String
    let context: Int
    let vision: Bool
}

/// 0.4.6 (par do OpenRouter.kt): cliente mínimo do OpenRouter (API da OpenAI).
/// A chave é do usuário (colada no app, guardada no Keychain); nada passa por
/// servidor do Aurea. Só modelos GRATUITOS com ferramentas: a lista vem viva de
/// `/api/v1/models` (os gratuitos mudam toda semana) e os melhores vão primeiro.
enum OpenRouter {
    private static let base = URL(string: "https://openrouter.ai/api/v1")!

    /// Ordem de preferência (os mais capazes para agir no editor); o resto vem depois, pelo contexto.
    static let preferred = [
        "thinkingmachines/inkling:free",
        "nvidia/nemotron-3-ultra-550b-a55b:free",
        "google/gemma-4-31b-it:free",
        "nvidia/nemotron-3.5-super-vl:free",
        "dots-studio/dots-3-note-preview:free",
        "openrouter/free",
    ]

    struct Failure: LocalizedError {
        let code: Int
        let message: String
        var errorDescription: String? { message }
    }

    private static func request(_ method: String, _ path: String, key: String?, body: [String: Any]? = nil,
                                timeout: TimeInterval = 90) async throws -> [String: Any] {
        var r = URLRequest(url: base.appendingPathComponent(path), timeoutInterval: timeout)
        r.httpMethod = method
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        // Identificação do app para o OpenRouter (ranking de apps); não leva dado do usuário.
        r.setValue("https://aurea.app", forHTTPHeaderField: "HTTP-Referer")
        r.setValue("AUREA", forHTTPHeaderField: "X-Title")
        if let key { r.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization") }
        if let body {
            r.setValue("application/json", forHTTPHeaderField: "Content-Type")
            r.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        let (data, response) = try await URLSession.shared.data(for: r)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        guard (200..<300).contains(code) else {
            let message = (json["error"] as? [String: Any])?["message"] as? String
            throw Failure(code: code, message: (message?.isEmpty == false ? message! : "HTTP \(code)"))
        }
        return json
    }

    /// A chave vale? (GET /key responde 401 para chave errada).
    static func validate(_ key: String) async throws -> Bool {
        do { _ = try await request("GET", "key", key: key, timeout: 20); return true }
        catch let f as Failure where f.code == 401 || f.code == 403 { return false }
    }

    /// Os gratuitos com ferramentas, os preferidos primeiro. Modelos "stealth" ficam de fora (guardam os prompts).
    static func freeToolModels() async throws -> [AssistantModel] {
        let json = try await request("GET", "models", key: nil, timeout: 30)
        let data = json["data"] as? [[String: Any]] ?? []
        let models: [AssistantModel] = data.compactMap { m in
            guard let id = m["id"] as? String, !id.hasPrefix("stealth/") else { return nil }
            let pricing = m["pricing"] as? [String: Any]
            let free = id.hasSuffix(":free") || id == "openrouter/free"
                || ((pricing?["prompt"] as? String) == "0" && (pricing?["completion"] as? String) == "0")
            let tools = (m["supported_parameters"] as? [String])?.contains("tools") ?? false
            let arch = m["architecture"] as? [String: Any]
            let textOut = (arch?["output_modalities"] as? [String])?.contains("text") ?? true
            guard free, tools, textOut else { return nil }
            let vision = (arch?["input_modalities"] as? [String])?.contains("image") ?? false
            var name = m["name"] as? String ?? id
            if name.hasSuffix(" (free)") { name.removeLast(7) }
            return AssistantModel(id: id, name: name, context: m["context_length"] as? Int ?? 0, vision: vision)
        }
        func rank(_ m: AssistantModel) -> Int { preferred.firstIndex(of: m.id) ?? Int.max }
        return models.sorted { rank($0) != rank($1) ? rank($0) < rank($1) : $0.context > $1.context }
    }

    /// Uma volta do chat: devolve a mensagem do assistente (texto e/ou tool_calls).
    static func complete(key: String, model: String, messages: [[String: Any]], tools: Any) async throws -> [String: Any] {
        let body: [String: Any] = ["model": model, "messages": messages, "tools": tools, "tool_choice": "auto", "temperature": 0.3]
        let json = try await request("POST", "chat/completions", key: key, body: body)
        if let error = json["error"] as? [String: Any] {
            throw Failure(code: error["code"] as? Int ?? 500, message: error["message"] as? String ?? "erro")
        }
        guard let message = ((json["choices"] as? [[String: Any]])?.first)?["message"] as? [String: Any] else {
            throw Failure(code: 500, message: "resposta vazia")
        }
        return message
    }
}

/// A chave do OpenRouter SÓ no aparelho (Keychain, sem iCloud); nunca vai para log, projeto ou backup.
enum AssistantKeychain {
    private static let service = "aurea.assistant.openrouter"
    static func read() -> String {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                kSecReturnData as String: true, kSecMatchLimit as String: kSecMatchLimitOne]
        var item: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &item) == errSecSuccess, let data = item as? Data else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }
    static func save(_ key: String) {
        let base: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service]
        SecItemDelete(base as CFDictionary)
        var add = base
        add[kSecValueData as String] = Data(key.utf8)
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        SecItemAdd(add as CFDictionary, nil)
    }
    static func remove() {
        SecItemDelete([kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service] as CFDictionary)
    }
}
