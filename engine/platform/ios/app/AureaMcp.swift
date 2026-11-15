// =============================================================================
//  Aurea / platform / ios / app / AureaMcp.swift
//
//  AureaMCP (docs/aureamcp/DESIGN.md), par de android/.../mcp/AureaMcp.kt,
//  McpExecutor.kt, McpStoreHost.kt e McpMediaFetch.kt.
//
//  O app conecta no relay do Worker (WebSocket com a sessão da conta) e executa
//  as chamadas dos agentes de IA da pessoa (Claude, ChatGPT, Codex...):
//    · ferramentas do MOTOR → engine.assistantCall (as mesmas regras do Android);
//    · ferramentas de HOST → AureaModel (projetos, quadro, exportação, mídia).
//  Permissões por capacidade e confirmação para o que é destrutivo.
// =============================================================================
import Photos
import StoreKit
import SwiftUI
import UIKit

/// true = o botão do editor abre o chat antigo (0.4.6); false = abre o AureaMCP.
/// Fica true até o Worker com o /mcp estar no ar; depois vira false (o mesmo flag do Android).
enum AureaMcpFlags {
    static var legacyChat = false
}

/// Capacidade de cada ferramenta (o espelho de discovery/mcp_catalog.js e do McpCatalog.kt).
enum McpCatalog {
    static let capabilities = ["read", "edit", "projects", "delete", "media", "device_media", "export"]
    static let defaults: [String: Bool] = ["read": true, "edit": true, "projects": true, "delete": true, "media": true, "device_media": false, "export": true]
    /// O espelho de ENGINE_READ em discovery/mcp_catalog.js (as leituras do motor).
    static let engineRead: Set<String> = ["get_project_state", "list_ae_effects", "seek", "playback", "set_view",
        "get_layer_details", "get_keyframes", "list_effect_types", "get_effect", "list_presets", "list_fonts", "get_markers", "get_work_area"]
    static let host: [String: String] = [
        "list_projects": "read", "open_project": "projects", "create_project": "projects",
        "rename_project": "projects", "duplicate_project": "projects", "delete_project": "delete",
        "save_project": "edit", "close_project": "projects", "render_frame": "read",
        "export_video": "export", "export_status": "export", "cancel_export": "export",
        "import_media_url": "media", "list_device_media": "device_media", "import_device_media": "device_media",
    ]
    static let confirm: Set<String> = ["delete_project"]
    static func capability(_ tool: String) -> String { host[tool] ?? (engineRead.contains(tool) ? "read" : "edit") }
}

/// Resultado de uma ferramenta: texto e, opcional, um JPEG.
struct McpReply {
    var ok: Bool
    var text: String
    var jpeg: Data? = nil
    static func done(_ t: String) -> McpReply { McpReply(ok: true, text: t) }
    static func done(_ o: [String: Any]) -> McpReply { McpReply(ok: true, text: McpJSON.string(o)) }
    static func fail(_ t: String) -> McpReply { McpReply(ok: false, text: t) }
    var content: [[String: Any]] {
        var out: [[String: Any]] = []
        if let jpeg { out.append(["type": "image", "mimeType": "image/jpeg", "data": jpeg.base64EncodedString()]) }
        out.append(["type": "text", "text": text])
        return out
    }
}

enum McpJSON {
    static func string(_ o: Any) -> String {
        guard JSONSerialization.isValidJSONObject(o), let d = try? JSONSerialization.data(withJSONObject: o) else { return "{}" }
        return String(data: d, encoding: .utf8) ?? "{}"
    }
    static func object(_ s: String) -> [String: Any]? {
        s.data(using: .utf8).flatMap { try? JSONSerialization.jsonObject(with: $0) } as? [String: Any]
    }
}

@MainActor
final class AureaMcpClient: ObservableObject {
    static let shared = AureaMcpClient()
    static let serverURL = "https://aurea-ai-discovery.aureaapp.workers.dev/mcp"
    private static let socketURL = "wss://aurea-ai-discovery.aureaapp.workers.dev/api/mcp/app/socket"

    enum Status { case off, noAccount, connecting, online, offline }
    struct Agent: Identifiable { let id = UUID(); let name: String; let version: String; let seen: Double; let tokenId: String? }
    struct LogLine: Identifiable { let id = UUID(); let at: Double; let agent: String; let tool: String?; let ok: Bool }
    struct Grant: Identifiable { let id: String; let kind: String; let name: String; let createdAt: Double; let lastUsedAt: Double?; let expiresAt: Double? }
    struct PairRequest { let code: String; let client: String; let redirectHost: String }
    struct Confirm: Identifiable { let id = UUID(); let tool: String; let agent: String; let detail: String }

    private let defaults = UserDefaults.standard
    @Published private(set) var enabled: Bool
    @Published private(set) var status: Status
    @Published private(set) var paused = false
    @Published private(set) var agents: [Agent] = []
    @Published private(set) var grants: [Grant] = []
    @Published private(set) var log: [LogLine] = []
    @Published private(set) var busy = false
    @Published private(set) var askEveryEdit: Bool
    @Published private(set) var permissions: [String: Bool]
    @Published var confirm: Confirm?
    /// Código que chegou pelo link aurea://mcp/pair (a tela abre e preenche).
    @Published var pendingPairCode: String?
    /// O link pediu a tela do AureaMCP (Home ou editor).
    @Published var deepLinkOpen = false
    // AureaOS (pago: aurea_aureaos). Quem libera o conteúdo aos agentes é o Worker.
    @Published private(set) var aureaosActive: Bool?
    @Published private(set) var aureaosPurchasable = false
    @Published private(set) var aureaosProduct: Product?
    @Published private(set) var aureaosNotice: String?
    @Published private(set) var aureaosBusy = false
    static let aureaosProductId = "aurea_aureaos"
    private var confirmAnswer: CheckedContinuation<Bool, Never>?

    weak var model: AureaModel?
    private var task: URLSessionWebSocketTask?
    private var loop: Task<Void, Never>?
    private var running: [String: Task<Void, Never>] = [:]
    private let deviceId: String

    private init() {
        // Nada de self antes de tudo inicializado: o UserDefaults entra por uma local.
        let d = UserDefaults.standard
        let on = d.bool(forKey: "aurea.mcp.enabled")
        enabled = on
        status = on ? .connecting : .off
        askEveryEdit = d.bool(forKey: "aurea.mcp.ask_every_edit")
        var p: [String: Bool] = [:]
        for c in McpCatalog.capabilities {
            p[c] = d.object(forKey: "aurea.mcp.perm." + c) as? Bool ?? McpCatalog.defaults[c] ?? true
        }
        permissions = p
        if let id = d.string(forKey: "aurea.mcp.device") { deviceId = id } else {
            var bytes = [UInt8](repeating: 0, count: 16)
            _ = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
            let id = Data(bytes).base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
            d.set(id, forKey: "aurea.mcp.device"); deviceId = id
        }
    }

    // MARK: liga / desliga

    func attach(_ model: AureaModel) { self.model = model }

    func changeEnabled(_ on: Bool) {
        enabled = on
        defaults.set(on, forKey: "aurea.mcp.enabled")
        if on { start() } else { stop() }
    }

    /// Abertura e volta ao primeiro plano: conecta, ou avisa o relay que este é o aparelho ativo.
    func start() {
        guard enabled else { status = .off; return }
        if let loop, !loop.isCancelled, task != nil { send(["t": "focus"]); return }
        loop?.cancel()
        loop = Task { [weak self] in await self?.connectLoop() }
    }

    func stop() {
        loop?.cancel(); loop = nil
        task?.cancel(with: .normalClosure, reason: nil); task = nil
        running.values.forEach { $0.cancel() }; running = [:]
        answerConfirm(false)
        status = .off
    }

    func setPermission(_ cap: String, _ on: Bool) {
        permissions[cap] = on
        defaults.set(on, forKey: "aurea.mcp.perm." + cap)
        send(["t": "state", "permissions": permissions])
    }

    func changeAskEveryEdit(_ on: Bool) { askEveryEdit = on; defaults.set(on, forKey: "aurea.mcp.ask_every_edit") }

    func pause() { paused = true; running.values.forEach { $0.cancel() }; send(["t": "pause"]) }
    func resume() { paused = false; send(["t": "resume"]) }

    func projectChanged() {
        guard let model else { return }
        send(["t": "state", "project": ["title": model.projectName, "open": model.screen == .editor] as [String: Any]])
    }

    func answerConfirm(_ approved: Bool) {
        confirm = nil
        confirmAnswer?.resume(returning: approved)
        confirmAnswer = nil
    }

    private func askPerson(tool: String, agent: String, detail: String) async -> Bool {
        answerConfirm(false)
        let timeout = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 85_000_000_000)
            guard !Task.isCancelled else { return }
            self?.answerConfirm(false)
        }
        defer { timeout.cancel() }
        return await withCheckedContinuation { c in
            confirmAnswer = c
            confirm = Confirm(tool: tool, agent: agent, detail: detail)
        }
    }

    // MARK: conexão

    private func connectLoop() async {
        var backoff: UInt64 = 1
        while !Task.isCancelled && enabled {
            guard let token = ContaModel.lerSessao()?.token else {
                status = .noAccount
                try? await Task.sleep(nanoseconds: 5_000_000_000); continue
            }
            status = .connecting
            var request = URLRequest(url: URL(string: Self.socketURL + "?device=" + deviceId)!)
            request.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
            let ws = URLSession.shared.webSocketTask(with: request)
            ws.maximumMessageSize = 4 * 1024 * 1024
            task = ws
            ws.resume()
            let pinger = Task { [weak ws] in
                while !Task.isCancelled {
                    try? await Task.sleep(nanoseconds: 25_000_000_000)
                    try? await ws?.send(.string("ping"))
                }
            }
            do {
                try await ws.send(.string(McpJSON.string(hello())))
                status = .online
                backoff = 1
                while !Task.isCancelled {
                    let message = try await ws.receive()
                    if case .string(let text) = message { onMessage(text) }
                }
            } catch {
                if (ws.response as? HTTPURLResponse)?.statusCode == 401 { status = .noAccount }
            }
            pinger.cancel()
            ws.cancel(with: .goingAway, reason: nil)
            if task === ws { task = nil }
            if !enabled || Task.isCancelled { break }
            if status != .noAccount { status = .offline }
            try? await Task.sleep(nanoseconds: backoff * 1_000_000_000)
            backoff = min(backoff * 2, 60)
        }
    }

    private func hello() -> [String: Any] {
        let tools = (model?.engine.assistantTools()).flatMap { $0.data(using: .utf8) }.flatMap { try? JSONSerialization.jsonObject(with: $0) } ?? [Any]()
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        return [
            "t": "hello",
            "device": ["name": String(UIDevice.current.name.prefix(60)), "platform": "ios", "app": version],
            "engineTools": tools,
            "hostTools": Array(McpCatalog.host.keys),
            "permissions": permissions,
            "project": ["title": model?.projectName ?? "", "open": model?.screen == .editor] as [String: Any],
        ]
    }

    private func send(_ message: [String: Any]) {
        guard let ws = task else { return }
        let text = McpJSON.string(message)
        Task { try? await ws.send(.string(text)) }
    }

    private func onMessage(_ text: String) {
        if text == "pong" { return }
        guard let m = McpJSON.object(text) else { return }
        switch m["t"] as? String {
        case "welcome":
            paused = m["paused"] as? Bool ?? false
            agents = Self.parseAgents(m["agents"])
            log = ((m["log"] as? [[String: Any]]) ?? []).compactMap(Self.parseLog)
        case "agents": agents = Self.parseAgents(m["agents"])
        case "paused": paused = m["paused"] as? Bool ?? false
        case "log":
            if let line = Self.parseLog(m) { log.append(line); if log.count > 200 { log.removeFirst(log.count - 200) } }
        case "cancel": if let id = m["id"] as? String { running.removeValue(forKey: id)?.cancel() }
        case "call": handleCall(m)
        default: break
        }
    }

    private static func parseAgents(_ any: Any?) -> [Agent] {
        ((any as? [[String: Any]]) ?? []).map {
            Agent(name: $0["name"] as? String ?? "", version: $0["version"] as? String ?? "", seen: ($0["seen"] as? NSNumber)?.doubleValue ?? 0, tokenId: $0["tokenId"] as? String)
        }
    }

    private static func parseLog(_ o: [String: Any]) -> LogLine? {
        LogLine(at: (o["at"] as? NSNumber)?.doubleValue ?? 0, agent: o["agent"] as? String ?? "", tool: o["tool"] as? String, ok: o["ok"] as? Bool ?? true)
    }

    private func handleCall(_ m: [String: Any]) {
        guard let id = m["id"] as? String, !id.isEmpty, id.count <= 64 else { return }
        let tool = m["tool"] as? String ?? ""
        let args = m["args"] as? [String: Any] ?? [:]
        let agent = String((m["agent"] as? String ?? "agent").prefix(80))
        running[id] = Task { [weak self] in
            guard let self else { return }
            let reply = self.paused ? McpReply.fail("The person paused AureaMCP in the app.") : await self.run(tool: tool, args: args, agent: agent)
            self.running.removeValue(forKey: id)
            var out: [String: Any] = ["t": "result", "id": id, "ok": reply.ok, "content": reply.content]
            if !reply.ok { out["error"] = String(reply.text.prefix(2000)) }
            self.send(out)
        }
    }

    // MARK: executor (par do McpExecutor.kt)

    func run(tool: String, args: [String: Any], agent: String) async -> McpReply {
        guard let model else { return .fail("Aurea is not ready.") }
        let cap = McpCatalog.capability(tool)
        guard permissions[cap] == true else { return .fail("The person turned off the \"\(cap)\" permission for AureaMCP in the Aurea app.") }
        if McpCatalog.confirm.contains(tool) || (askEveryEdit && cap != "read" && tool != "export_status") {
            let detail: String
            switch tool {
            case "delete_project": detail = AureaMcpHost.projectTitle(args["id"] as? String ?? "") ?? (args["id"] as? String ?? "")
            case "run_ae_script": detail = String((args["code"] as? String ?? "").prefix(400))
            default: detail = String(McpJSON.string(args).prefix(400))
            }
            guard await askPerson(tool: tool, agent: agent, detail: detail) else { return .fail("The person did not approve \"\(tool)\" in the Aurea app.") }
        }
        if McpCatalog.host[tool] != nil { return await AureaMcpHost(model: model).run(tool, args) }
        let engine = model.engine
        let argsText = McpJSON.string(args)
        let result = await Task.detached(priority: .userInitiated) { engine.assistantCall(tool, args: argsText) }.value
        let parsed = McpJSON.object(result)
        let ok = (parsed?["ok"] as? Bool) == true
        if ok, (parsed?["ui"] as? String) == "open_panel", let panel = parsed?["panel"] as? String { model.assistantOpenPanel(panel) }
        if cap != "read" || tool == "seek" || tool == "set_view" { model.refreshModel(force: true) }
        return McpReply(ok: ok, text: result)
    }

    // MARK: link aurea://mcp/pair

    /// `aurea://mcp/pair?code=ABCD-2345` → "ABCD-2345"; outro link ou código fora do alfabeto do pareamento → nil.
    static func pairCode(from url: URL) -> String? {
        guard url.scheme?.lowercased() == "aurea", url.host?.lowercased() == "mcp",
              url.path.trimmingCharacters(in: CharacterSet(charactersIn: "/")) == "pair",
              let raw = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?.first(where: { $0.name == "code" })?.value else { return nil }
        let code = raw.uppercased().filter { $0.isLetter || $0.isNumber }
        let alphabet = Set("ABCDEFGHJKLMNPQRSTUVWXYZ23456789")
        guard code.count == 8, code.allSatisfy({ alphabet.contains($0) }) else { return nil }
        return String(code.prefix(4)) + "-" + String(code.suffix(4))
    }

    /// AureaApp.onOpenURL: o link do pareamento abre o AureaMCP com o código.
    @discardableResult func openPairLink(_ url: URL) -> Bool {
        guard let code = Self.pairCode(from: url) else { return false }
        pendingPairCode = code
        deepLinkOpen = true
        return true
    }

    // MARK: AureaOS (compra única não consumível na App Store; o Worker confere e libera)

    func refreshAureaos() async {
        let r = await call("/api/entitlements?platform=apple", "GET", nil)
        guard r.status == 200 else { return }
        aureaosActive = ((r.corpo["products"] as? [String]) ?? []).contains(Self.aureaosProductId)
        aureaosPurchasable = ((r.corpo["purchasable"] as? [String]) ?? []).contains(Self.aureaosProductId)
        if aureaosPurchasable, aureaosActive == false, aureaosProduct == nil {
            aureaosProduct = try? await Product.products(for: [Self.aureaosProductId]).first
        }
    }

    /// Comprar (ou, com `restore`, só achar a compra que a conta da App Store já tem) e entregar ao servidor.
    func buyAureaos(restore: Bool) async {
        guard !aureaosBusy else { return }
        aureaosBusy = true; aureaosNotice = nil
        defer { aureaosBusy = false }
        if restore { try? await AppStore.sync() }
        var owned: StoreKit.Transaction?
        for await result in StoreKit.Transaction.currentEntitlements {
            if case .verified(let t) = result, t.productID == Self.aureaosProductId, t.revocationDate == nil { owned = t; break }
        }
        if owned == nil, !restore {
            guard let product = aureaosProduct ?? (try? await Product.products(for: [Self.aureaosProductId]).first) else { aureaosNotice = "mcp_aureaos_unavailable"; return }
            switch try? await product.purchase() {
            case .success(let result): if case .verified(let t) = result { owned = t }
            case .pending: aureaosNotice = "mcp_aureaos_pending"; return
            default: return
            }
        }
        guard let transaction = owned else { if restore { aureaosNotice = "mcp_aureaos_nothing_to_restore" }; return }
        let r = await call("/api/entitlements/purchase", "POST", ["platform": "apple", "product": Self.aureaosProductId, "token": String(transaction.id)])
        switch r.status {
        case 200: aureaosActive = true; aureaosNotice = "mcp_aureaos_thanks"; await transaction.finish()
        case 409: aureaosNotice = "mcp_aureaos_other_account"
        case 402: aureaosNotice = "mcp_aureaos_invalid"
        default: aureaosNotice = "mcp_aureaos_unavailable"
        }
        await refreshAureaos()
    }

    // MARK: pareamento e tokens (sessão da conta)

    private func call(_ path: String, _ method: String, _ body: [String: Any]?) async -> ContaResposta {
        guard let token = ContaModel.lerSessao()?.token else { return ContaResposta(status: 401, corpo: [:]) }
        busy = true
        defer { busy = false }
        return await ContaAPI.chamar(path, metodo: method, corpo: body, token: token)
    }

    func lookupPair(_ code: String) async -> PairRequest? {
        let r = await call("/api/mcp/pair/lookup", "POST", ["code": code])
        guard r.status == 200 else { return nil }
        return PairRequest(code: code, client: r.corpo["client"] as? String ?? "", redirectHost: r.corpo["redirectHost"] as? String ?? "")
    }

    func approvePair(_ code: String, allow: Bool) async -> Bool {
        let r = await call("/api/mcp/pair/approve", "POST", ["code": code, "allow": allow])
        await refreshGrants()
        return r.status == 200
    }

    func createToken(_ name: String) async -> String? {
        let r = await call("/api/mcp/tokens", "POST", ["name": name])
        await refreshGrants()
        return r.status == 201 ? r.corpo["token"] as? String : nil
    }

    func revoke(_ id: String?) async {
        _ = await call("/api/mcp/tokens/revoke", "POST", id.map { ["id": $0] } ?? ["all": true])
        await refreshGrants()
    }

    func refreshGrants() async {
        let r = await call("/api/mcp/tokens", "GET", nil)
        guard r.status == 200 else { return }
        grants = ((r.corpo["tokens"] as? [[String: Any]]) ?? []).map {
            Grant(id: $0["id"] as? String ?? "", kind: $0["kind"] as? String ?? "", name: $0["name"] as? String ?? "",
                  createdAt: ($0["createdAt"] as? NSNumber)?.doubleValue ?? 0, lastUsedAt: ($0["lastUsedAt"] as? NSNumber)?.doubleValue,
                  expiresAt: ($0["expiresAt"] as? NSNumber)?.doubleValue)
        }
    }

    static func claudeCodeCommand(_ token: String) -> String {
        "claude mcp add --transport http aurea \(serverURL) --header \"Authorization: Bearer \(token)\""
    }
    static func codexConfig(_ token: String) -> String {
        "[mcp_servers.aurea]\nurl = \"\(serverURL)\"\nbearer_token_env_var = \"AUREA_MCP_TOKEN\"\n\n# AUREA_MCP_TOKEN=\(token)"
    }
}

/// As ferramentas de host no iOS (par do McpStoreHost.kt), sobre o AureaModel.
@MainActor
struct AureaMcpHost {
    let model: AureaModel

    static func projectTitle(_ id: String) -> String? { scanHomeProjects().first { $0.file.url.lastPathComponent == id }?.title }
    private func entry(_ id: String) -> HomeProjectEntry? { scanHomeProjects().first { $0.file.url.lastPathComponent == id || $0.file.url.path == id } }

    /// Espera o estado do modelo (abrir, importar) com prazo; o laço cede a main a cada 100 ms.
    private func wait(_ seconds: Double, _ done: () -> Bool) async -> Bool {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if done() { return true }
            try? await Task.sleep(nanoseconds: 100_000_000)
        }
        return done()
    }

    func run(_ tool: String, _ a: [String: Any]) async -> McpReply {
        let id = (a["id"] as? String).flatMap { $0.isEmpty || $0.count > 300 ? nil : $0 }
        switch tool {
        case "list_projects":
            let q = (a["query"] as? String)?.lowercased()
            let limit = min(max((a["limit"] as? NSNumber)?.intValue ?? 50, 1), 200)
            let openPath = model.screen == .editor ? model.projectURL?.path : nil
            let list = scanHomeProjects().filter { q == nil || q!.isEmpty || $0.title.lowercased().contains(q!) }.prefix(limit)
            return .done(["ok": true, "projects": list.map { p -> [String: Any] in
                ["id": p.file.url.lastPathComponent, "title": p.title, "width": p.width, "height": p.height, "fps": p.fps,
                 "durationSeconds": p.fps > 0 ? (Double(p.durationFrames) / p.fps * 100).rounded() / 100 : 0,
                 "modified": Int64(p.modified.timeIntervalSince1970 * 1000), "bytes": p.fileSize, "open": p.file.url.path == openPath]
            }])
        case "open_project":
            guard let id, let e = entry(id) else { return .fail("No project with that id (see list_projects).") }
            if model.screen == .editor, model.projectURL == e.file.url { return .done("Already open.") }
            if model.screen == .editor { model.closeProject() }
            guard await wait(15, { model.screen == .home }) else { return .fail("Aurea is busy; try again later.") }
            model.open(e.file)
            return await wait(30, { model.screen == .editor && model.projectURL == e.file.url })
                ? .done(["ok": true, "title": e.title, "id": id]) : .fail(model.toast ?? "Could not open the project.")
        case "create_project":
            let title = String(((a["title"] as? String) ?? "").trimmingCharacters(in: .whitespaces).prefix(120))
            let w = (a["width"] as? NSNumber)?.intValue ?? 0, h = (a["height"] as? NSNumber)?.intValue ?? 0
            let fps = (a["fps"] as? NSNumber)?.doubleValue ?? 30
            guard !title.isEmpty else { return .fail("Missing title.") }
            guard (16...8192).contains(w), (16...8192).contains(h) else { return .fail("width and height must be 16..8192") }
            guard fps >= 1, fps <= 120 else { return .fail("fps must be 1..120") }
            if model.screen == .editor { model.closeProject(); _ = await wait(15, { model.screen == .home }) }
            let bg = (a["background"] as? [NSNumber]).flatMap { $0.count == 3 ? $0.map { min(max($0.floatValue, 0), 1) } : nil }
            guard model.newProject(width: UInt32(w), height: UInt32(h), fps: fps, title: title, background: bg) else { return .fail(model.toast ?? "Could not create the project.") }
            return .done(["ok": true, "id": model.projectURL?.lastPathComponent ?? "", "title": title, "width": w, "height": h, "fps": fps])
        case "rename_project":
            let title = String(((a["title"] as? String) ?? "").trimmingCharacters(in: .whitespaces).prefix(120))
            guard !title.isEmpty else { return .fail("Missing title.") }
            guard model.screen == .editor else { return .fail("No project is open.") }
            model.renameCurrentProject(to: title)
            return .done("Renamed to \"\(title)\".")
        case "duplicate_project":
            guard let id, let e = entry(id) else { return .fail("No project with that id.") }
            guard duplicateHomeProject(e) else { return .fail("Could not duplicate the project.") }
            model.refreshProjectList()
            return .done("Duplicated.")
        case "delete_project":
            guard let id, let e = entry(id) else { return .fail("No project with that id.") }
            if model.screen == .editor, model.projectURL == e.file.url { return .fail("That project is open: close_project first.") }
            deleteHomeProjects([e.file.url.path])
            model.refreshProjectList()
            return .done("Deleted.")
        case "save_project":
            guard model.screen == .editor else { return .fail("No project is open.") }
            return model.saveProject(writeThumbnail: true) ? .done("Saved.") : .fail(model.toast ?? "Could not save.")
        case "close_project":
            guard model.screen == .editor else { return .done("No project was open.") }
            model.closeProject()
            return await wait(15, { model.screen == .home }) ? .done("Closed.") : .fail("Aurea is busy; try again later.")
        case "render_frame":
            guard model.screen == .editor else { return .fail("No project is open.") }
            let fps = Double(model.status.compFps > 0 ? model.status.compFps : 30)
            if let s = (a["seconds"] as? NSNumber)?.doubleValue {
                guard s >= 0, s.isFinite else { return .fail("seconds must be >= 0") }
                model.seek(toFrame: Int64((s * fps).rounded()))
                try? await Task.sleep(nanoseconds: 120_000_000)
            }
            let maxSize = UInt32(min(max((a["maxSize"] as? NSNumber)?.intValue ?? 768, 128), 1280))
            guard let frame = await model.capturePreviewFrame(maxSize), let jpeg = Self.jpeg(frame.data, frame.width, frame.height) else { return .fail("Could not render the frame.") }
            let at = Double(model.status.playhead) / fps
            return McpReply(ok: true, text: "Frame at \((at * 1000).rounded() / 1000) s (\(jpeg.count / 1024) KB).", jpeg: jpeg)
        case "export_video":
            guard model.screen == .editor else { return .fail("No project is open.") }
            guard !model.exporting else { return .fail("An export is already running (export_status).") }
            var o = model.exportOptions
            o.kind = (a["format"] as? String) == "gif" ? .gif : (a["format"] as? String) == "png" ? .frame : .video
            if let s = (a["shortSide"] as? NSNumber)?.uint32Value, s > 0 { o.shortSide = s }
            if let f = (a["fps"] as? NSNumber)?.doubleValue, f >= 0 { o.fps = f }
            o.codec = (a["codec"] as? String) == "hevc" ? .hevc : .h264
            switch a["quality"] as? String { case "low": o.quality = 35; case "high": o.quality = 85; default: o.quality = 60 }
            model.exportOptions = o
            model.startExport()
            return .done(["ok": true, "started": true, "shortSide": o.shortSide, "hint": "Call export_status every few seconds."])
        case "export_status":
            let p = model.exportProgress
            let done = (p["framesDone"] as? NSNumber)?.intValue ?? 0, total = (p["framesTotal"] as? NSNumber)?.intValue ?? 0
            let phase = model.exporting ? "running" : model.exportedURL != nil ? "done" : model.exportCancelled ? "cancelled" : model.exportMessage != nil ? "failed" : "idle"
            var out: [String: Any] = ["ok": true, "phase": phase, "framesDone": done, "framesTotal": total,
                                      "progress": total > 0 ? (Double(done) / Double(total) * 1000).rounded() / 1000 : 0, "savedToGallery": model.exportSavedToPhotos]
            if let url = model.exportedURL { out["output"] = url.lastPathComponent }
            if phase == "failed", let m = model.exportMessage { out["message"] = m }
            return .done(out)
        case "cancel_export":
            guard model.exporting else { return .done("No export is running.") }
            model.cancelExport()
            return .done("Cancelled.")
        case "import_media_url":
            guard let url = a["url"] as? String, url.hasPrefix("https://"), url.count <= 2048 else { return .fail("url must be https://") }
            guard model.screen == .editor else { return .fail("Open or create a project first.") }
            let file: URL
            do { file = try await McpMediaFetch.download(url) } catch { return .fail("Download refused: \(error.localizedDescription)") }
            let video = ["mp4", "mov", "m4v", "webm", "mkv"].contains(file.pathExtension.lowercased())
            model.pendingOverlayImport = (a["overlay"] as? Bool) == true
            model.importMediaBatch([(file, video)])
            _ = await wait(1, { model.importingMedia })
            return await wait(300, { !model.importingMedia })
                ? .done(["ok": true, "file": file.lastPathComponent]) : .fail("Import is still running; check with get_project_state later.")
        case "list_device_media":
            let kind = (a["kind"] as? String) ?? "all"
            guard ["all", "video", "image"].contains(kind) else { return .fail("kind must be all, video or image") }
            let limit = min(max((a["limit"] as? NSNumber)?.intValue ?? 30, 1), 100)
            return Self.listDeviceMedia(kind: kind, limit: limit, beforeId: a["beforeId"] as? String)
        case "import_device_media":
            let ids = (a["ids"] as? [String]) ?? []
            guard (1...20).contains(ids.count), ids.allSatisfy({ !$0.isEmpty && $0.count <= 200 }) else { return .fail("ids must be 1..20 ids from list_device_media") }
            guard model.screen == .editor else { return .fail("Open or create a project first.") }
            guard Self.galleryAccess() != nil else { return .fail(Self.noGallery) }
            var unique: [String] = []
            for id in ids where !unique.contains(id) { unique.append(id) }
            let assets = PHAsset.fetchAssets(withLocalIdentifiers: unique, options: nil)
            var byId: [String: PHAsset] = [:]
            assets.enumerateObjects { asset, _, _ in byId[asset.localIdentifier] = asset }
            let ordered = unique.compactMap { byId[$0] }
            guard ordered.count == unique.count else { return .fail("Not in the gallery Aurea can see: \(unique.count - ordered.count) of \(unique.count) (list_device_media again).") }
            var files: [(URL, Bool)] = []
            for asset in ordered {
                guard !Task.isCancelled else { return .fail("Cancelled.") }
                guard let file = await Self.export(asset) else { return .fail("Could not read an item from the gallery (iCloud offline?).") }
                files.append((file, asset.mediaType == .video))
            }
            model.pendingOverlayImport = (a["overlay"] as? Bool) == true
            model.importMediaBatch(files)
            _ = await wait(1, { model.importingMedia })
            return await wait(300, { !model.importingMedia })
                ? .done(["ok": true, "imported": files.count]) : .fail("Import is still running; check with get_project_state later.")
        default:
            return .fail("Unknown tool: \(tool)")
        }
    }

    // MARK: galeria do aparelho (fase D)
    // Só com o interruptor "Galeria do aparelho" (o executor confere) E a permissão do
    // Fotos (pedida ao ligar o interruptor). Acesso limitado: o PhotoKit só devolve o
    // que a pessoa escolheu — é o que o agente vê.

    static let noGallery = "Aurea cannot see the device gallery: ask the person to turn on \"Device gallery\" in Aurea → AureaMCP and allow access."

    /// "full", "limited" ou nil (sem acesso).
    static func galleryAccess() -> String? {
        switch PHPhotoLibrary.authorizationStatus(for: .readWrite) {
        case .authorized: return "full"
        case .limited: return "limited"
        default: return nil
        }
    }

    static func listDeviceMedia(kind: String, limit: Int, beforeId: String?) -> McpReply {
        guard let access = galleryAccess() else { return .fail(noGallery) }
        let options = PHFetchOptions()
        options.predicate = kind == "video" ? NSPredicate(format: "mediaType == %d", PHAssetMediaType.video.rawValue)
            : kind == "image" ? NSPredicate(format: "mediaType == %d", PHAssetMediaType.image.rawValue)
            : NSPredicate(format: "mediaType == %d || mediaType == %d", PHAssetMediaType.image.rawValue, PHAssetMediaType.video.rawValue)
        options.sortDescriptors = [NSSortDescriptor(key: "creationDate", ascending: false)]
        let result = PHAsset.fetchAssets(with: options)
        var start = 0
        if let beforeId {
            for i in 0..<result.count where result.object(at: i).localIdentifier == beforeId { start = i + 1; break }
        }
        var items: [[String: Any]] = []
        var i = start
        while i < result.count && items.count < limit {
            let asset = result.object(at: i)
            let name = PHAssetResource.assetResources(for: asset).first?.originalFilename ?? ""
            var item: [String: Any] = ["id": asset.localIdentifier, "name": name, "kind": asset.mediaType == .video ? "video" : "image",
                                       "added": Int64((asset.creationDate ?? Date(timeIntervalSince1970: 0)).timeIntervalSince1970),
                                       "width": asset.pixelWidth, "height": asset.pixelHeight]
            if asset.mediaType == .video { item["durationSeconds"] = (asset.duration * 10).rounded() / 10 }
            items.append(item)
            i += 1
        }
        var out: [String: Any] = ["ok": true, "access": access, "items": items, "more": i < result.count]
        if access == "limited" { out["hint"] = "Limited access: only the media the person selected for Aurea is visible." }
        return .done(out)
    }

    /// O arquivo original do item (do iCloud se preciso), numa pasta temporária.
    static func export(_ asset: PHAsset) async -> URL? {
        let isVideo = asset.mediaType == .video
        let resources = PHAssetResource.assetResources(for: asset)
        guard let resource = resources.first(where: { $0.type == (isVideo ? .fullSizeVideo : .fullSizePhoto) })
                ?? resources.first(where: { $0.type == (isVideo ? .video : .photo) }) else { return nil }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("mcp_\(UUID().uuidString)", isDirectory: true)
        guard (try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)) != nil else { return nil }
        let destination = folder.appendingPathComponent(resource.originalFilename)
        let options = PHAssetResourceRequestOptions(); options.isNetworkAccessAllowed = true
        return await withCheckedContinuation { done in
            PHAssetResourceManager.default().writeData(for: resource, toFile: destination, options: options) { error in
                if error != nil { try? FileManager.default.removeItem(at: folder); done.resume(returning: nil) } else { done.resume(returning: destination) }
            }
        }
    }

    /// RGBA8 sRGB do motor → JPEG.
    static func jpeg(_ data: Data, _ width: UInt32, _ height: UInt32) -> Data? {
        let w = Int(width), h = Int(height)
        guard w > 0, h > 0, data.count >= w * h * 4, let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: w * 4,
                                  space: CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return nil }
        return UIImage(cgImage: image).jpegData(compressionQuality: 0.85)
    }
}

/// `import_media_url` no iOS (par do McpMediaFetch.kt): só https, só endereço público, tipo e tamanho com teto.
enum McpMediaFetch {
    static let maxBytes: Int64 = 500 * 1024 * 1024

    struct Rejected: LocalizedError { let reason: String; var errorDescription: String? { reason } }

    /// Endereços do host (getaddrinfo) e se TODOS são públicos.
    static func hostIsPublic(_ host: String) -> Bool {
        var hints = addrinfo(ai_flags: 0, ai_family: AF_UNSPEC, ai_socktype: SOCK_STREAM, ai_protocol: 0, ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
        var res: UnsafeMutablePointer<addrinfo>?
        guard getaddrinfo(host, nil, &hints, &res) == 0, let first = res else { return false }
        defer { freeaddrinfo(res) }
        var any = false
        var p: UnsafeMutablePointer<addrinfo>? = first
        while let a = p {
            any = true
            if a.pointee.ai_family == AF_INET {
                let b = a.pointee.ai_addr.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { withUnsafeBytes(of: $0.pointee.sin_addr.s_addr) { Array($0) } }
                if !publicV4(b) { return false }
            } else if a.pointee.ai_family == AF_INET6 {
                let b = a.pointee.ai_addr.withMemoryRebound(to: sockaddr_in6.self, capacity: 1) { withUnsafeBytes(of: $0.pointee.sin6_addr) { Array($0) } }
                if b.prefix(10).allSatisfy({ $0 == 0 }) && b[10] == 0xFF && b[11] == 0xFF { if !publicV4(Array(b[12..<16])) { return false } }
                else if b.allSatisfy({ $0 == 0 }) || b == [UInt8](repeating: 0, count: 15) + [1] || (b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) || b[0] == 0xFF { return false }
            } else { return false }
            p = a.pointee.ai_next
        }
        return any
    }

    static func publicV4(_ b: [UInt8]) -> Bool {
        guard b.count == 4 else { return false }
        let (o0, o1) = (b[0], b[1])
        if o0 == 0 || o0 == 10 || o0 == 127 || o0 >= 224 { return false }
        if o0 == 169 && o1 == 254 { return false }
        if o0 == 172 && (16...31).contains(o1) { return false }
        if o0 == 192 && o1 == 168 { return false }
        if o0 == 100 && (64...127).contains(o1) { return false }
        if o0 == 192 && o1 == 0 && b[2] == 0 { return false }
        if o0 == 198 && (o1 == 18 || o1 == 19) { return false }
        return true
    }

    static func fileExtension(_ contentType: String?, _ path: String) -> String? {
        let t = (contentType ?? "").split(separator: ";").first.map { $0.trimmingCharacters(in: .whitespaces).lowercased() } ?? ""
        let ext = (path as NSString).pathExtension.lowercased()
        if t.hasPrefix("video/") { return t == "video/quicktime" ? "mov" : t == "video/webm" ? "webm" : "mp4" }
        if t.hasPrefix("image/") { return ["image/png": "png", "image/webp": "webp", "image/gif": "gif", "image/heic": "heic"][t] ?? "jpg" }
        if t.hasPrefix("audio/") { return ["audio/mpeg": "mp3", "audio/wav": "wav", "audio/x-wav": "wav", "audio/aac": "aac", "audio/ogg": "ogg"][t] ?? "m4a" }
        if t == "application/octet-stream", ["mp4", "mov", "m4v", "webm", "jpg", "jpeg", "png", "webp", "gif", "heic", "mp3", "wav", "m4a", "aac"].contains(ext) { return ext }
        return nil
    }

    /// Baixa sem seguir redirecionamento às cegas: cada salto é conferido de novo.
    static func download(_ link: String) async throws -> URL {
        var current = URL(string: link)
        for _ in 0...3 {
            guard let url = current, url.scheme == "https", let host = url.host else { throw Rejected(reason: "only https URLs") }
            let isPublic = await Task.detached { hostIsPublic(host) }.value
            guard isPublic else { throw Rejected(reason: "private or local addresses are not allowed") }
            var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 30)
            request.setValue("Aurea-MCP", forHTTPHeaderField: "User-Agent")
            let guardian = NoRedirect()
            let session = URLSession(configuration: .ephemeral, delegate: guardian, delegateQueue: nil)
            defer { session.finishTasksAndInvalidate() }
            let (temp, response) = try await session.download(for: request)
            // DNS-rebinding: o nome passou na resolução de antes, mas a CONEXÃO pode ter
            // ido a outro endereço. As métricas dizem o endereço real; privado = recusa.
            if let remote = guardian.remoteAddress, !addressIsPublic(remote) {
                try? FileManager.default.removeItem(at: temp)
                throw Rejected(reason: "the host resolved to a private or local address")
            }
            guard let http = response as? HTTPURLResponse else { throw Rejected(reason: "no response") }
            if (300...399).contains(http.statusCode) {
                try? FileManager.default.removeItem(at: temp)
                current = (http.value(forHTTPHeaderField: "Location")).flatMap { URL(string: $0, relativeTo: url)?.absoluteURL }
                continue
            }
            guard (200...299).contains(http.statusCode) else { try? FileManager.default.removeItem(at: temp); throw Rejected(reason: "HTTP \(http.statusCode)") }
            guard let ext = fileExtension(http.value(forHTTPHeaderField: "Content-Type"), url.path) else {
                try? FileManager.default.removeItem(at: temp); throw Rejected(reason: "not a video, image or audio file")
            }
            let size = (try? FileManager.default.attributesOfItem(atPath: temp.path)[.size] as? NSNumber)?.int64Value ?? 0
            guard size > 0, size <= maxBytes else { try? FileManager.default.removeItem(at: temp); throw Rejected(reason: size == 0 ? "empty file" : "file larger than 500 MB") }
            let target = FileManager.default.temporaryDirectory.appendingPathComponent("mcp_\(UUID().uuidString).\(ext)")
            try FileManager.default.moveItem(at: temp, to: target)
            return target
        }
        throw Rejected(reason: "too many redirects")
    }

    /// Endereço IP em texto (o das métricas) é público? Mesma regra de `hostIsPublic`.
    static func addressIsPublic(_ text: String) -> Bool {
        var v4 = in_addr()
        if inet_pton(AF_INET, text, &v4) == 1 { return publicV4(withUnsafeBytes(of: v4.s_addr) { Array($0) }) }
        var v6 = in6_addr()
        let bare = text.split(separator: "%").first.map(String.init) ?? text
        guard inet_pton(AF_INET6, bare, &v6) == 1 else { return false }
        let b = withUnsafeBytes(of: v6) { Array($0) }
        if b.prefix(10).allSatisfy({ $0 == 0 }) && b[10] == 0xFF && b[11] == 0xFF { return publicV4(Array(b[12..<16])) }
        if b.allSatisfy({ $0 == 0 }) || b == [UInt8](repeating: 0, count: 15) + [1] || (b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) || b[0] == 0xFF { return false }
        return true
    }

    private final class NoRedirect: NSObject, URLSessionTaskDelegate {
        private let lock = NSLock()
        private var address: String?
        /// O endereço real da conexão (URLSessionTaskMetrics); nil = a pilha não informou.
        var remoteAddress: String? { lock.lock(); defer { lock.unlock() }; return address }
        func urlSession(_ session: URLSession, task: URLSessionTask, didFinishCollecting metrics: URLSessionTaskMetrics) {
            let remote = metrics.transactionMetrics.last(where: { $0.remoteAddress != nil })?.remoteAddress
            lock.lock(); address = remote; lock.unlock()
        }
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
    }
}
