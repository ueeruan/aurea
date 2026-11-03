import SwiftUI

/// Uma linha do chat na tela. `tool` = ação feita no editor (aparece como um chip).
struct ChatLine: Identifiable, Equatable {
    enum Role { case user, assistant, tool, error }
    let id = UUID()
    let role: Role
    let text: String
    var failed = false
}

/// 0.4.6 (par do AssistantChat.kt): o assistente de IA do editor. A conversa vai
/// para o OpenRouter com a chave do usuário; as ferramentas e as regras são do
/// motor (`Engine::assistant_call`). Cada pedido roda até 8 voltas de
/// ferramentas; limite/erro de um modelo gratuito passa para o próximo.
@MainActor
final class AssistantChat: ObservableObject {
    static let shared = AssistantChat()

    @Published private(set) var hasKey = !AssistantKeychain.read().isEmpty
    @Published private(set) var models: [AssistantModel] = []
    @Published private(set) var model = UserDefaults.standard.string(forKey: "aurea.assistant.model") ?? OpenRouter.preferred[0]
    @Published private(set) var busy = false
    @Published var status: String?
    @Published private(set) var keyError: String?
    @Published private(set) var lines: [ChatLine] = []

    private var history: [[String: Any]] = []
    private var job: Task<Void, Never>?
    private var toolsCache: Any?

    private static let maxRounds = 8
    private static let maxToolResult = 12_000

    func loadModels() {
        guard models.isEmpty else { return }
        Task {
            let list = (try? await OpenRouter.freeToolModels()) ?? []
            models = list
            if !list.isEmpty && !list.contains(where: { $0.id == model }) { choose(list[0].id) }
        }
    }

    func choose(_ id: String) {
        model = id
        UserDefaults.standard.set(id, forKey: "aurea.assistant.model")
    }

    /// Cola a chave: valida no OpenRouter antes de guardar (só no aparelho).
    func submitKey(_ raw: String) {
        let key = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty, !busy else { return }
        busy = true; keyError = nil
        Task {
            defer { busy = false }
            do {
                if try await OpenRouter.validate(key) { AssistantKeychain.save(key); hasKey = true; loadModels() }
                else { keyError = AureaText.t("ai_key_invalid") }
            } catch { keyError = AureaText.t("ai_offline") }
        }
    }

    func forgetKey() { AssistantKeychain.remove(); hasKey = false; clear() }

    func clear() { job?.cancel(); lines = []; history = []; busy = false; status = nil }

    func stop() { job?.cancel(); busy = false; status = nil }

    static func toolLabel(_ name: String) -> String {
        switch name {
        case "get_project_state": return AureaText.t("ai_tool_state")
        case "run_ae_script": return AureaText.t("ai_tool_script")
        case "list_ae_effects": return AureaText.t("ai_tool_effects")
        case "select_layers": return AureaText.t("ai_tool_select")
        case "delete_layers": return AureaText.t("ai_tool_delete")
        case "undo": return AureaText.t("ai_tool_undo")
        case "redo": return AureaText.t("ai_tool_redo")
        case "seek": return AureaText.t("ai_tool_seek")
        case "playback": return AureaText.t("ai_tool_playback")
        case "set_view": return AureaText.t("ai_tool_view")
        case "open_panel": return AureaText.t("ai_tool_panel")
        default: return AureaText.t("ai_tool_other")
        }
    }

    /// Um pedido do usuário: conversa + ferramentas até o modelo responder com texto.
    func send(_ raw: String, model editor: AureaModel) {
        let text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty, !busy else { return }
        let key = AssistantKeychain.read()
        guard !key.isEmpty else { hasKey = false; return }
        let engine = editor.engine
        if history.isEmpty {
            let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
            history.append(["role": "system", "content": engine.assistantSystemPrompt(AureaText.language.resolved.rawValue, version: version)])
        }
        if toolsCache == nil, let data = engine.assistantTools().data(using: .utf8) { toolsCache = try? JSONSerialization.jsonObject(with: data) }
        let tools = toolsCache ?? []
        history.append(["role": "user", "content": text])
        lines.append(ChatLine(role: .user, text: text))
        busy = true; status = AureaText.t("ai_thinking")
        job = Task {
            defer { busy = false; status = nil }
            do {
                for _ in 0..<Self.maxRounds {
                    let reply = try await completeWithFallback(key: key, tools: tools)
                    try Task.checkCancellation()
                    history.append(reply)
                    let calls = reply["tool_calls"] as? [[String: Any]] ?? []
                    let content = (reply["content"] as? String).flatMap { $0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? nil : $0 }
                    if calls.isEmpty { lines.append(ChatLine(role: .assistant, text: content ?? AureaText.t("ai_done"))); break }
                    if let content { lines.append(ChatLine(role: .assistant, text: content)) }
                    for call in calls {
                        guard let fn = call["function"] as? [String: Any], let name = fn["name"] as? String else { continue }
                        status = Self.toolLabel(name)
                        let args = (fn["arguments"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "{}"
                        // Um script pode demorar: fora da main thread.
                        let result = await Task.detached(priority: .userInitiated) { engine.assistantCall(name, args: args) }.value
                        try Task.checkCancellation()
                        let parsed = (result.data(using: .utf8)).flatMap { try? JSONSerialization.jsonObject(with: $0) } as? [String: Any]
                        let good = (parsed?["ok"] as? Bool) == true
                        if (parsed?["ui"] as? String) == "open_panel", let panel = parsed?["panel"] as? String { editor.assistantOpenPanel(panel) }
                        if name != "get_project_state" && name != "list_ae_effects" { editor.refreshModel(force: true) }
                        lines.append(ChatLine(role: .tool, text: Self.toolLabel(name), failed: !good))
                        history.append(["role": "tool", "tool_call_id": call["id"] as? String ?? "", "name": name,
                                        "content": String(result.prefix(Self.maxToolResult))])
                    }
                }
            } catch is CancellationError {
            } catch let f as OpenRouter.Failure where f.code == 401 || f.code == 403 {
                AssistantKeychain.remove(); hasKey = false
                lines.append(ChatLine(role: .error, text: AureaText.t("ai_key_rejected")))
            } catch let f as OpenRouter.Failure {
                lines.append(ChatLine(role: .error, text: f.message))
            } catch {
                if !Task.isCancelled { lines.append(ChatLine(role: .error, text: AureaText.t("ai_offline"))) }
            }
        }
    }

    /// Modelo gratuito com limite ou fora do ar: tenta os próximos (até 4), avisando qual ficou.
    private func completeWithFallback(key: String, tools: Any) async throws -> [String: Any] {
        var order = [model] + models.map(\.id) + [OpenRouter.preferred.last!]
        var seen = Set<String>(); order = order.filter { seen.insert($0).inserted }
        var last: Error?
        for (attempt, id) in order.prefix(4).enumerated() {
            do {
                let reply = try await OpenRouter.complete(key: key, model: id, messages: history, tools: tools)
                if attempt > 0 {
                    choose(id)
                    lines.append(ChatLine(role: .tool, text: AureaText.t("ai_switched", models.first { $0.id == id }?.name ?? id)))
                }
                return reply
            } catch let f as OpenRouter.Failure where f.code == 401 || f.code == 403 {
                throw f
            } catch { last = error }
        }
        throw last ?? OpenRouter.Failure(code: 500, message: AureaText.t("ai_offline"))
    }
}

private enum AssistantInk {
    static let ink = Color(red: 0.055, green: 0.059, blue: 0.071)
    static let card = Color(red: 0.102, green: 0.110, blue: 0.129)
    static let line = Color.white.opacity(0.08)
    static let glow = LinearGradient(colors: [Color(red: 0.49, green: 0.36, blue: 1), Color(red: 0.18, green: 0.61, blue: 1),
                                              Color(red: 0.24, green: 0.88, blue: 1)], startPoint: .topLeading, endPoint: .bottomTrailing)
    static let free = Color(red: 0.30, green: 0.85, blue: 0.39)
}

/// 0.4.6 (par do AssistantSheet.kt): o chat do assistente em tela cheia. Sem
/// chave: o cartão que pede a chave do OpenRouter. Com chave: conversa, chips
/// das ações feitas no editor, troca de modelo (só gratuitos) e sugestões.
struct AssistantView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var chat = AssistantChat.shared
    let close: () -> Void
    @State private var draft = ""
    @State private var key = ""

    var body: some View {
        VStack(spacing: 0) {
            header
            Rectangle().fill(AssistantInk.line).frame(height: 1)
            if chat.hasKey { conversation } else { keyGate }
        }
        .background(AssistantInk.ink.ignoresSafeArea())
        .foregroundStyle(.white)
        .onAppear { if chat.hasKey { chat.loadModels() } }
        .accessibilityIdentifier("assistant.sheet")
    }

    private var header: some View {
        HStack(spacing: 10) {
            CupertinoGlyph.text(CupertinoGlyph.Sparkles, size: 17, color: .white)
                .frame(width: 34, height: 34).background(AssistantInk.glow, in: Circle())
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(AureaText.t("ai_title")).font(.aurea(size: 16, weight: .bold))
                    Text(AureaText.t("ai_free")).font(.aurea(size: 10, weight: .bold)).foregroundStyle(AssistantInk.free)
                        .padding(.horizontal, 6).padding(.vertical, 1).background(AssistantInk.free.opacity(0.2), in: RoundedRectangle(cornerRadius: 6))
                }
                if chat.hasKey {
                    Menu {
                        Section(AureaText.t("ai_model")) {
                            ForEach(chat.models) { m in
                                Button { chat.choose(m.id) } label: {
                                    if m.id == chat.model { Label(m.name, systemImage: "checkmark") } else { Text(m.name) }
                                }
                            }
                        }
                    } label: {
                        let current = chat.models.first { $0.id == chat.model }?.name
                            ?? AssistantView.shortName(chat.model)
                        Text(current + "  ▾").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
                    }
                }
            }
            Spacer(minLength: 0)
            if chat.hasKey {
                Menu {
                    Button(AureaText.t("ai_new_chat")) { chat.clear() }
                    Button(AureaText.t("ai_change_key"), role: .destructive) { chat.forgetKey() }
                } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 18, color: AureaColors.muted).frame(width: 40, height: 40)
                }.accessibilityLabel(AureaText.t("ai_model"))
            }
            Button(action: close) {
                CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 18, color: AureaColors.muted).frame(width: 40, height: 40)
            }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("ai_close"))
        }
        .padding(.horizontal, 12).frame(height: 56)
    }

    static func shortName(_ id: String) -> String {
        String(id.split(separator: "/").last ?? "").replacingOccurrences(of: ":free", with: "")
    }

    private var keyGate: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.Sparkles, size: 28, color: .white)
                    .frame(width: 56, height: 56).background(AssistantInk.glow, in: RoundedRectangle(cornerRadius: 16))
                Text(AureaText.t("ai_key_title")).font(.aurea(size: 20, weight: .bold)).padding(.top, 16)
                Text(AureaText.t("ai_key_body")).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted).padding(.top, 8)
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(["ai_key_step1", "ai_key_step2", "ai_key_step3"], id: \.self) { Text(AureaText.t($0)).font(.aurea(size: 14)) }
                }.padding(.top, 16)
                Link(AureaText.t("ai_key_open"), destination: URL(string: "https://openrouter.ai/keys")!)
                    .font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.accent).padding(.vertical, 12)
                SecureField(AureaText.t("ai_key_hint"), text: $key)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .font(.aurea(size: 15)).padding(14)
                    .background(AssistantInk.card, in: RoundedRectangle(cornerRadius: 14))
                    .overlay(RoundedRectangle(cornerRadius: 14).stroke(AssistantInk.line, lineWidth: 1))
                    .accessibilityIdentifier("assistant.key")
                if let error = chat.keyError { Text(error).font(.aurea(size: 13)).foregroundStyle(AureaColors.danger).padding(.top, 8) }
                let enabled = !key.trimmingCharacters(in: .whitespaces).isEmpty && !chat.busy
                Button { chat.submitKey(key) } label: {
                    Text(AureaText.t("ai_key_unlock")).font(.aurea(size: 15, weight: .bold))
                        .foregroundStyle(enabled ? .white : AureaColors.muted)
                        .frame(maxWidth: .infinity, minHeight: 50)
                        .background(enabled ? AnyShapeStyle(AssistantInk.glow) : AnyShapeStyle(AssistantInk.card), in: RoundedRectangle(cornerRadius: 14))
                }.buttonStyle(AureaPressStyle()).disabled(!enabled).padding(.top, 14)
                .accessibilityIdentifier("assistant.unlock")
                if chat.busy { ProgressView().tint(AureaColors.accent).frame(maxWidth: .infinity).padding(.top, 10) }
            }.padding(20)
        }
    }

    private var conversation: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 10) {
                        if chat.lines.isEmpty { welcome }
                        ForEach(chat.lines) { bubble($0).id($0.id) }
                        if let status = chat.status {
                            HStack(spacing: 8) {
                                Circle().fill(AssistantInk.glow).frame(width: 8, height: 8)
                                Text(status).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                            }.id("status")
                        }
                    }.padding(16)
                }
                .onChange(of: chat.lines.count) { _ in withAnimation { proxy.scrollTo(chat.lines.last?.id, anchor: .bottom) } }
            }
            HStack(spacing: 8) {
                ZStack(alignment: .leading) {
                    if draft.isEmpty { Text(AureaText.t("ai_placeholder")).font(.aurea(size: 15)).foregroundStyle(AureaColors.muted) }
                    TextField("", text: $draft, axis: .vertical).lineLimit(1...5).font(.aurea(size: 15))
                        .accessibilityIdentifier("assistant.input")
                }.padding(.vertical, 8)
                let can = chat.busy || !draft.trimmingCharacters(in: .whitespaces).isEmpty
                Button {
                    if chat.busy { chat.stop() } else { chat.send(draft, model: model); draft = "" }
                } label: {
                    CupertinoGlyph.text(chat.busy ? CupertinoGlyph.Xmark : CupertinoGlyph.ArrowUp, size: 18, color: .white)
                        .frame(width: 40, height: 40)
                        .background(can ? AnyShapeStyle(AssistantInk.glow) : AnyShapeStyle(Color(white: 0.17)), in: Circle())
                }.buttonStyle(AureaPressStyle()).disabled(!can)
                .accessibilityLabel(AureaText.t(chat.busy ? "ai_stop" : "ai_send"))
                .accessibilityIdentifier("assistant.send")
            }
            .padding(.leading, 16).padding(.trailing, 6).padding(.vertical, 6)
            .background(AssistantInk.card, in: RoundedRectangle(cornerRadius: 24))
            .overlay(RoundedRectangle(cornerRadius: 24).stroke(AssistantInk.line, lineWidth: 1))
            .padding(12)
        }
    }

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(AureaText.t("ai_welcome")).font(.aurea(size: 15))
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(["ai_suggest_1", "ai_suggest_2", "ai_suggest_3", "ai_suggest_4"], id: \.self) { k in
                        let s = AureaText.t(k)
                        Button { chat.send(s, model: model) } label: {
                            Text(s).font(.aurea(size: 13)).multilineTextAlignment(.leading).frame(maxWidth: 220, alignment: .leading)
                                .padding(.horizontal, 12).padding(.vertical, 10)
                                .background(AssistantInk.card, in: RoundedRectangle(cornerRadius: 14))
                                .overlay(RoundedRectangle(cornerRadius: 14).stroke(AssistantInk.line, lineWidth: 1))
                        }.buttonStyle(AureaPressStyle())
                    }
                }
            }
        }.padding(.top, 12)
    }

    @ViewBuilder private func bubble(_ line: ChatLine) -> some View {
        switch line.role {
        case .user:
            HStack { Spacer(minLength: 40)
                Text(line.text).font(.aurea(size: 15)).padding(.horizontal, 14).padding(.vertical, 10)
                    .background(AssistantInk.glow, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            }
        case .assistant:
            HStack(alignment: .top, spacing: 8) {
                CupertinoGlyph.text(CupertinoGlyph.Sparkles, size: 13, color: .white).frame(width: 26, height: 26).background(AssistantInk.glow, in: Circle())
                Text(line.text).font(.aurea(size: 15)).textSelection(.enabled).padding(.horizontal, 14).padding(.vertical, 10)
                    .background(AssistantInk.card, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                Spacer(minLength: 20)
            }
        case .tool:
            HStack(spacing: 6) {
                CupertinoGlyph.text(line.failed ? CupertinoGlyph.ExclamationmarkBubble : CupertinoGlyph.WandStars, size: 13,
                                    color: line.failed ? AureaColors.danger : AureaColors.accent)
                Text(line.text).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
            .padding(.horizontal, 10).padding(.vertical, 6)
            .background(Color(red: 0.08, green: 0.09, blue: 0.11), in: RoundedRectangle(cornerRadius: 10))
            .padding(.leading, 34)
        case .error:
            Text(line.text).font(.aurea(size: 14)).foregroundStyle(AureaColors.danger)
                .frame(maxWidth: .infinity, alignment: .leading).padding(12)
                .background(AureaColors.danger.opacity(0.13), in: RoundedRectangle(cornerRadius: 12))
        }
    }
}
