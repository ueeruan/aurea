//
//  PluginsV2.swift
//  AUREA
//
//  0.3.4 PLUGINS (par de effects/PluginRuntime.kt, PluginUi.kt e PluginHost.kt):
//  os painéis declarados no plugin.json desenhados com os componentes do AUREA
//  (nada de HTML/webview), o runtime que chama o motor numa thread de pilha
//  grande (o script roda lá), a folha de consentimento, o console e os pontos
//  onde os plugins aparecem: painel no editor (moldura PanelFrame), aba da Home,
//  menu de plugins do editor, ⋮ e barra da camada.
//
//  A regra (permissões, domínios, limites, desfazer) mora no motor
//  (EnginePluginsV2.cpp). Ids iguais aos do Android: `plugin.panel.<plugin>.<painel>`,
//  `plugin.ctl.<controle>`, `plugin.action.<plugin>.<ação>`, `plugins.consent.*`.
//

import SwiftUI
import UIKit
import UniformTypeIdentifiers

// =============================================================================
// O que o plugin.json declara (a lista do motor, `pluginsJSON`)
// =============================================================================
struct PluginV2Control {
    let id: String
    let type: String
    let label: [String: String]
    let style: String
    let min: Double
    let max: Double
    let step: Double
    let unit: String
    let options: [[String: String]]
    let placeholder: [String: String]
    let multiline: Bool
    let maxLength: Int
    let aspect: Double
    let text: [String: String]
}

struct PluginV2Panel {
    let id: String
    let title: [String: String]
    let icon: String
    let placement: Set<String>
    let controls: [PluginV2Control]
}

struct PluginV2Action {
    let id: String
    let title: [String: String]
    let icon: String
    let placement: Set<String>
    let panel: String
    let call: String
    let needsLayer: Bool
}

struct PluginV2Info: Identifiable {
    let id: String
    let version: String
    let author: String
    let names: [String: String]
    let ok: Bool
    let enabled: Bool
    let hasScript: Bool
    let network: [String]
    let wantsClipboard: Bool
    let wantsFiles: Bool
    /// Concedidas: 1 rede, 2 área de transferência, 4 arquivos.
    let granted: UInt32
    let panels: [PluginV2Panel]
    let actions: [PluginV2Action]
    var requested: UInt32 { (network.isEmpty ? 0 : 1) | (wantsClipboard ? 2 : 0) | (wantsFiles ? 4 : 0) }
}

/// O texto no idioma do app: exato, a língua, "*", inglês, pt-BR, o primeiro.
func pv2Pick(_ texts: [String: String], _ tag: String) -> String {
    if texts.isEmpty { return "" }
    if let v = texts[tag] { return v }
    let lang = tag.split(separator: "-").first.map(String.init)?.lowercased() ?? tag
    if let v = texts.first(where: { $0.key.split(separator: "-").first.map(String.init)?.lowercased() == lang })?.value { return v }
    return texts["*"] ?? texts["en"] ?? texts["pt-BR"] ?? texts.values.first ?? ""
}

/// Os nomes de ícone do manifesto → glifos do AUREA (a mesma lista do Android e do motor).
func pv2Glyph(_ name: String) -> Character {
    switch name {
    case "sparkles": return CupertinoGlyph.Sparkles
    case "text": return CupertinoGlyph.Textformat
    case "star": return CupertinoGlyph.Star
    case "bolt": return CupertinoGlyph.Bolt
    case "cloud": return CupertinoGlyph.CloudDownload
    case "music": return CupertinoGlyph.MusicNote
    case "photo": return CupertinoGlyph.Photo
    case "film": return CupertinoGlyph.Film
    case "layers": return CupertinoGlyph.SquareStack3dUp
    case "gear": return CupertinoGlyph.Gear
    case "brush": return CupertinoGlyph.Paintbrush
    case "clock": return CupertinoGlyph.Clock
    case "list": return CupertinoGlyph.LineHorizontal3
    case "cube": return CupertinoGlyph.Cube
    case "lightbulb": return CupertinoGlyph.Lightbulb
    case "wrench": return CupertinoGlyph.Wrench
    case "book": return CupertinoGlyph.Book
    case "flag": return CupertinoGlyph.Flag
    case "grid": return CupertinoGlyph.Grid
    case "camera": return CupertinoGlyph.Camera
    case "chart": return CupertinoGlyph.ChartBarAltFill
    case "sliders": return CupertinoGlyph.SliderHorizontal3
    case "eye": return CupertinoGlyph.Eye
    case "folder": return CupertinoGlyph.Folder
    case "link": return CupertinoGlyph.Link
    case "speed": return CupertinoGlyph.Speedometer
    case "waveform": return CupertinoGlyph.Waveform
    default: return CupertinoGlyph.WandStars
    }
}

private func pv2Texts(_ any: Any?) -> [String: String] {
    guard let d = any as? [String: Any] else { return [:] }
    var out: [String: String] = [:]
    for (k, v) in d { if let s = v as? String { out[k] = s } }
    return out
}

private func pv2Places(_ any: Any?) -> Set<String> { Set((any as? [String]) ?? []) }

private func pv2Parse(_ json: String) -> [PluginV2Info] {
    guard let data = json.data(using: .utf8), let list = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else { return [] }
    return list.map { o in
        let ui = o["ui"] as? [String: Any] ?? [:]
        let perms = o["permissions"] as? [String: Any] ?? [:]
        let granted = o["granted"] as? [String: Any] ?? [:]
        let panels = (ui["panels"] as? [[String: Any]] ?? []).map { p in
            PluginV2Panel(
                id: p["id"] as? String ?? "", title: pv2Texts(p["title"]), icon: p["icon"] as? String ?? "wand",
                placement: pv2Places(p["placement"]),
                controls: (p["controls"] as? [[String: Any]] ?? []).map { c in
                    PluginV2Control(
                        id: c["id"] as? String ?? "", type: c["type"] as? String ?? "", label: pv2Texts(c["label"]),
                        style: c["style"] as? String ?? "primary", min: (c["min"] as? NSNumber)?.doubleValue ?? 0,
                        max: (c["max"] as? NSNumber)?.doubleValue ?? 1, step: (c["step"] as? NSNumber)?.doubleValue ?? 0,
                        unit: c["unit"] as? String ?? "", options: (c["options"] as? [Any] ?? []).map { pv2Texts($0) },
                        placeholder: pv2Texts(c["placeholder"]), multiline: c["multiline"] as? Bool ?? false,
                        maxLength: (c["maxLength"] as? NSNumber)?.intValue ?? 2000, aspect: (c["aspect"] as? NSNumber)?.doubleValue ?? 0,
                        text: pv2Texts(c["text"]))
                })
        }
        let actions = (ui["actions"] as? [[String: Any]] ?? []).map { a in
            PluginV2Action(id: a["id"] as? String ?? "", title: pv2Texts(a["title"]), icon: a["icon"] as? String ?? "wand",
                           placement: pv2Places(a["placement"]), panel: a["panel"] as? String ?? "", call: a["call"] as? String ?? "",
                           needsLayer: a["needsLayer"] as? Bool ?? false)
        }
        return PluginV2Info(
            id: o["id"] as? String ?? "", version: o["version"] as? String ?? "", author: o["author"] as? String ?? "",
            names: pv2Texts(o["name"]), ok: o["ok"] as? Bool ?? false, enabled: o["enabled"] as? Bool ?? true,
            hasScript: o["hasScript"] as? Bool ?? false, network: perms["network"] as? [String] ?? [],
            wantsClipboard: perms["clipboard"] as? Bool ?? false, wantsFiles: perms["files"] as? Bool ?? false,
            granted: ((granted["network"] as? Bool ?? false) ? 1 : 0) | ((granted["clipboard"] as? Bool ?? false) ? 2 : 0)
                | ((granted["files"] as? Bool ?? false) ? 4 : 0),
            panels: panels, actions: actions)
    }
}

private func pv2Object(_ json: String) -> [String: Any]? {
    guard let data = json.data(using: .utf8) else { return nil }
    return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
}

/// Um valor do Swift em JSON (para `plugin_event`).
private func pv2JSON(_ value: Any) -> String {
    if let s = value as? String {
        let data = try? JSONSerialization.data(withJSONObject: [s], options: [.fragmentsAllowed])
        let text = data.flatMap { String(data: $0, encoding: .utf8) } ?? "[\"\"]"
        return String(text.dropFirst().dropLast())
    }
    if let b = value as? Bool { return b ? "true" : "false" }
    if let n = value as? Double { return n.isFinite ? String(n) : "0" }
    if let n = value as? Int { return String(n) }
    return "null"
}

// =============================================================================
// O runtime
// =============================================================================
struct PluginPanelRef: Equatable {
    let plugin: String
    let panel: String
}

struct PluginPendingInstall {
    let path: String
    let info: [String: Any]
}

/// Pedido de arquivo esperando o seletor (a thread do script espera no semáforo).
final class PluginPickRequest {
    let types: [String]
    let done = DispatchSemaphore(value: 0)
    var result: [String]?
    init(types: [String]) { self.types = types }
}

final class PluginRuntime: ObservableObject {
    static let shared = PluginRuntime()

    private weak var model: AureaModel?
    private var hostInstalled = false
    @Published private(set) var plugins: [PluginV2Info] = []
    /// Estado dos controles por plugin (o JSON do motor: values, items, texts...).
    @Published var states: [String: [String: Any]] = [:]
    @Published var busy: Set<String> = []
    @Published var errors: [String: String] = [:]
    @Published var editorPanel: PluginPanelRef?
    @Published var homePanel: PluginPanelRef?
    @Published var alert: String?
    @Published var console: String?
    @Published var pendingInstall: PluginPendingInstall?
    @Published var pickRequest: PluginPickRequest?

    var locale: String { FxPluginTexts.appTag }
    var active: [PluginV2Info] { plugins.filter { $0.ok && $0.enabled } }

    func attach(_ model: AureaModel) {
        self.model = model
        if !hostInstalled {
            hostInstalled = model.engine.installPluginHost()
            model.engine.pluginPickFile = { [weak self] _, types in self?.waitForPick(types) }
        }
        plugins = pv2Parse(model.engine.pluginsJSON)
        if let open = editorPanel, !active.contains(where: { $0.id == open.plugin }) { editorPanel = nil }
    }

    func plugin(_ id: String) -> PluginV2Info? { plugins.first { $0.id == id } }

    // --- chamadas (thread própria de pilha grande: o interpretador é recursivo) ---
    private func run(_ id: String, _ body: @escaping (AureaEngine) -> String) {
        guard let engine = model?.engine else { return }
        busy.insert(id)
        errors[id] = nil
        let poll = Timer.scheduledTimer(withTimeInterval: 0.4, repeats: true) { [weak self] _ in
            guard let self, self.busy.contains(id) else { return }
            if let s = pv2Object(engine.pluginState(id)), s["values"] != nil { self.states[id] = s }
        }
        let thread = Thread { [weak self] in
            let raw = body(engine)
            DispatchQueue.main.async {
                poll.invalidate()
                self?.finish(id, raw)
            }
        }
        thread.stackSize = 8 << 20
        thread.name = "aurea-plugin"
        thread.start()
    }

    @MainActor private func finish(_ id: String, _ raw: String) {
        busy.remove(id)
        guard let result = pv2Object(raw) else { return }
        if let s = result["state"] as? [String: Any] { states[id] = s }
        if (result["ok"] as? Bool) != true {
            let e = result["error"] as? [String: Any]
            errors[id] = (e?["text"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? (e?["message"] as? String ?? "")
        }
        for m in result["messages"] as? [[String: Any]] ?? [] {
            let text = m["text"] as? String ?? ""
            switch m["kind"] as? String {
            case "toast": model?.toast = text
            case "alert": alert = text
            case "select":
                if let data = text.data(using: .utf8), let ids = try? JSONSerialization.jsonObject(with: data) as? [String] {
                    model?.selectFromPlugin(ids.compactMap { UInt64($0).map { Int64(bitPattern: $0) } })
                }
            default: break
            }
        }
        if let panel = result["openPanel"] as? String, !panel.isEmpty { editorPanel = PluginPanelRef(plugin: id, panel: panel) }
        model?.refreshModel(force: true)
    }

    func openPanel(_ id: String, _ panel: String, inEditor: Bool = true) {
        if inEditor { editorPanel = PluginPanelRef(plugin: id, panel: panel) } else { homePanel = PluginPanelRef(plugin: id, panel: panel) }
        let loc = locale
        run(id) { $0.pluginOpenPanel(id, panel: panel, locale: loc) }
    }

    func runAction(_ id: String, _ action: PluginV2Action) {
        if !action.panel.isEmpty { openPanel(id, action.panel) } else { call(id, action.call) }
    }

    func call(_ id: String, _ function: String, args: String = "[]") {
        let loc = locale
        run(id) { $0.pluginCall(id, function: function, args: args, locale: loc) }
    }

    /// A pessoa mexeu num controle: `kind` = click | change | select.
    func event(_ id: String, _ control: String, _ kind: String, _ value: Any? = nil) {
        let json = value.map { pv2JSON($0) } ?? ""
        if kind == "change", let value, var s = states[id], var values = s["values"] as? [String: Any] {
            values[control] = value   // aparece na hora; o motor confirma na resposta
            s["values"] = values
            states[id] = s
        }
        let loc = locale
        run(id) { $0.pluginEvent(id, control: control, kind: kind, value: json, locale: loc) }
    }

    func cancel(_ id: String) { model?.engine.pluginCancel(id) }

    func refreshState(_ id: String) {
        guard let engine = model?.engine else { return }
        if let s = pv2Object(engine.pluginState(id)), s["values"] != nil { states[id] = s }
    }

    // --- seletor de arquivo (chamado na thread do script) ---
    private func waitForPick(_ types: [String]) -> [String]? {
        let request = PluginPickRequest(types: types)
        DispatchQueue.main.async {
            self.pickRequest = request
            var utTypes: [UTType] = types.compactMap { UTType(filenameExtension: $0) }
            if utTypes.isEmpty { utTypes = [.data] }
            DocumentImportPicker.present(types: utTypes) { urls in
                self.pickRequest = nil
                guard let url = urls?.first else { request.done.signal(); return }
                DispatchQueue.global(qos: .userInitiated).async {
                    let scoped = url.startAccessingSecurityScopedResource()
                    defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                    let dir = FileManager.default.temporaryDirectory.appendingPathComponent("plugin-files", isDirectory: true)
                    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                    let name = url.lastPathComponent.replacingOccurrences(of: "[^A-Za-z0-9._-]", with: "_", options: .regularExpression)
                    let target = dir.appendingPathComponent(name)
                    try? FileManager.default.removeItem(at: target)
                    if (try? FileManager.default.copyItem(at: url, to: target)) != nil {
                        let size = (try? FileManager.default.attributesOfItem(atPath: target.path)[.size] as? NSNumber)?.int64Value ?? 0
                        request.result = [url.lastPathComponent, target.path, String(size)]
                    }
                    request.done.signal()
                }
            }
        }
        _ = request.done.wait(timeout: .now() + 300)
        return request.result
    }

    // --- instalação com consentimento ---
    @MainActor func beginInstall(_ url: URL) {
        guard let model else { return }
        model.pluginBusy(AureaText.t("pv2_reading"))
        let engine = model.engine
        DispatchQueue.global(qos: .userInitiated).async {
            let fm = FileManager.default
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            let dir = AureaPaths.caches.appendingPathComponent("plugin-install", isDirectory: true)
            try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
            var name = url.lastPathComponent.replacingOccurrences(of: "[^A-Za-z0-9._-]", with: "_", options: .regularExpression)
            if !["fs", "aureaplugin", "zip"].contains((name as NSString).pathExtension.lowercased()) { name += ".aureaplugin" }
            let temp = dir.appendingPathComponent(name)
            try? fm.removeItem(at: temp)
            let copied = (try? fm.copyItem(at: url, to: temp)) != nil
            let info = copied ? pv2Object(engine.inspectPlugin(temp.path)) : nil
            DispatchQueue.main.async {
                model.pluginBusy(nil)
                if let info, (info["ok"] as? Bool) == true {
                    self.pendingInstall = PluginPendingInstall(path: temp.path, info: info)
                } else {
                    try? fm.removeItem(at: temp)
                    model.toast = AureaText.t("pv2_inspect_failed", (info?["error"] as? String) ?? "")
                }
            }
        }
    }

    @MainActor func confirmInstall(_ mask: UInt32) {
        guard let pending = pendingInstall, let model else { return }
        pendingInstall = nil
        model.pluginBusy(AureaText.t("plug_installing"))
        let engine = model.engine
        DispatchQueue.global(qos: .userInitiated).async {
            let raw = engine.installPlugin(pending.path)
            if raw.hasPrefix("ok:") { _ = engine.setPluginPermissions(String(raw.dropFirst(3)), mask: mask) }
            try? FileManager.default.removeItem(atPath: pending.path)
            DispatchQueue.main.async {
                model.pluginBusy(nil)
                model.afterPluginInstall()
                if raw.hasPrefix("ok:") {
                    let id = String(raw.dropFirst(3))
                    let shown = self.plugin(id).map { pv2Pick($0.names, self.locale) } ?? id
                    model.toast = AureaText.t("plug_installed", shown.isEmpty ? id : shown)
                    self.states[id] = nil
                    self.errors[id] = nil
                } else {
                    model.toast = AureaText.t("plug_failed", raw.hasPrefix("error:") ? String(raw.dropFirst(6)) : raw)
                }
            }
        }
    }

    func cancelInstall() {
        if let path = pendingInstall?.path { try? FileManager.default.removeItem(atPath: path) }
        pendingInstall = nil
    }

    @MainActor func setEnabled(_ id: String, _ on: Bool) {
        guard let model, model.engine.setPluginEnabled(id, on: on) else { return }
        if !on, editorPanel?.plugin == id { editorPanel = nil }
        if !on, homePanel?.plugin == id { homePanel = nil }
        model.refreshPlugins()
    }

    @MainActor func setPermission(_ id: String, _ bit: UInt32, _ on: Bool) {
        guard let model, let p = plugin(id) else { return }
        let mask = on ? (p.granted | bit) : (p.granted & ~bit)
        if model.engine.setPluginPermissions(id, mask: mask) { model.refreshPlugins() }
    }

    func consoleLines(_ id: String) -> [(Int, String)] {
        guard let raw = model?.engine.pluginConsole(id), let data = raw.data(using: .utf8),
              let list = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else { return [] }
        return list.map { (($0["level"] as? NSNumber)?.intValue ?? 0, $0["text"] as? String ?? "") }
    }

    func clearConsole(_ id: String) { model?.engine.pluginClearConsole(id) }

    func reload(_ id: String) {
        model?.engine.pluginReload(id)
        errors[id] = nil
    }

    // --- entradas de menu ---
    func editorMenuEntries() -> [MenuEntry] {
        let tag = locale
        var out: [MenuEntry] = []
        for p in active {
            for panel in p.panels where panel.placement.contains("editor.panel") {
                out.append(.item(pv2Pick(panel.title, tag), glyph: pv2Glyph(panel.icon), sublabel: pv2Pick(p.names, tag),
                                 tag: "plugin.open.\(p.id).\(panel.id)") { self.openPanel(p.id, panel.id) })
            }
            for action in p.actions where action.placement.contains("editor.menu") && action.panel.isEmpty {
                out.append(.item(pv2Pick(action.title, tag), glyph: pv2Glyph(action.icon), sublabel: pv2Pick(p.names, tag),
                                 tag: "plugin.action.\(p.id).\(action.id)") { self.runAction(p.id, action) })
            }
        }
        return out
    }

    func layerMenuEntries() -> [MenuEntry] {
        let tag = locale
        var out: [MenuEntry] = []
        for p in active {
            for action in p.actions where action.placement.contains("layer.menu") {
                out.append(.item(pv2Pick(action.title, tag), glyph: pv2Glyph(action.icon), tag: "plugin.action.\(p.id).\(action.id)") {
                    self.runAction(p.id, action)
                })
            }
        }
        if !out.isEmpty { out.insert(.divider, at: 0) }
        return out
    }

    /// Título e ícone do item do dock (a única aba de plugin dá o nome; várias = "Plugins").
    var homeTabLabel: (String, Character)? {
        let tabs = active.flatMap { $0.panels.filter { $0.placement.contains("home.tab") } }
        if tabs.isEmpty { return nil }
        if tabs.count == 1 { return (pv2Pick(tabs[0].title, locale), pv2Glyph(tabs[0].icon)) }
        return (AureaText.t("pv2_menu"), CupertinoGlyph.Wrench)
    }
}

// =============================================================================
// O painel
// =============================================================================
private func pv2Color(_ hex: String) -> Color {
    var h = hex.hasPrefix("#") ? String(hex.dropFirst()) : hex
    if h.count == 6 { h += "FF" }
    guard h.count == 8, let v = UInt64(h, radix: 16) else { return .white }
    return Color(.sRGB, red: Double((v >> 24) & 0xFF) / 255, green: Double((v >> 16) & 0xFF) / 255,
                 blue: Double((v >> 8) & 0xFF) / 255, opacity: Double(v & 0xFF) / 255)
}

private func pv2Hex(_ color: Color) -> String {
    var r: CGFloat = 1, g: CGFloat = 1, b: CGFloat = 1, a: CGFloat = 1
    UIColor(color).getRed(&r, green: &g, blue: &b, alpha: &a)
    func c(_ x: CGFloat) -> Int { Int((min(max(x, 0), 1) * 255).rounded()) }
    return a < 0.999 ? String(format: "#%02X%02X%02X%02X", c(r), c(g), c(b), c(a)) : String(format: "#%02X%02X%02X", c(r), c(g), c(b))
}

private func pv2Number(_ v: Double, _ step: Double) -> String {
    let decimals = step <= 0 ? 2 : step >= 1 ? 0 : min(4, max(1, Int(ceil(-log10(step)))))
    return String(format: "%.\(decimals)f", v)
}

/// Os controles de um painel do plugin (o estado vem do motor).
struct PluginPanelContent: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    let plugin: PluginV2Info
    let panel: PluginV2Panel

    var body: some View {
        let tag = runtime.locale
        let state = runtime.states[plugin.id] ?? [:]
        let disabled = Set(state["disabled"] as? [String] ?? [])
        let hidden = Set(state["hidden"] as? [String] ?? [])
        let busy = runtime.busy.contains(plugin.id)
        VStack(alignment: .leading, spacing: 10) {
            if busy {
                HStack(spacing: 10) {
                    if let progress = state["progress"] as? NSNumber {
                        ProgressView(value: progress.doubleValue).tint(AureaColors.accent)
                    } else {
                        ProgressView().tint(AureaColors.accent).frame(maxWidth: .infinity, alignment: .leading)
                    }
                    Button { runtime.cancel(plugin.id) } label: {
                        Text(AureaText.t("pv2_cancel_run")).font(.aurea(size: 13)).foregroundStyle(AureaColors.danger).frame(minHeight: 44)
                    }.buttonStyle(.plain).accessibilityIdentifier("plugin.cancel")
                }
                if let text = state["progressText"] as? String, !text.isEmpty {
                    Text(text).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
            }
            if let error = runtime.errors[plugin.id] {
                VStack(alignment: .leading, spacing: 4) {
                    Text(AureaText.t("pv2_error_title")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.danger)
                    Text(error).font(.aurea(size: 12)).fixedSize(horizontal: false, vertical: true)
                    Button { runtime.console = plugin.id } label: {
                        Text(AureaText.t("pv2_see_console")).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent)
                    }.buttonStyle(.plain)
                }
                .padding(10).frame(maxWidth: .infinity, alignment: .leading)
                .background(AureaColors.danger.opacity(0.12), in: RoundedRectangle(cornerRadius: 10))
                .accessibilityIdentifier("plugin.error")
            }
            ForEach(panel.controls.filter { !hidden.contains($0.id) }, id: \.id) { c in
                let enabled = !busy && !disabled.contains(c.id)
                let override = (state["labels"] as? [String: Any])?[c.id] as? String
                PluginControlView(pluginId: plugin.id, control: c, label: (override?.isEmpty == false ? override! : pv2Pick(c.label, tag)),
                                  state: state, enabled: enabled, tag: tag)
                    .opacity(enabled ? 1 : 0.4)
                    .allowsHitTesting(enabled)
                    .accessibilityIdentifier("plugin.ctl.\(c.id)")
            }
        }
        .foregroundStyle(AureaColors.text)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("plugin.panel.\(plugin.id).\(panel.id)")
        .onAppear { if runtime.states[plugin.id] == nil { runtime.refreshState(plugin.id) } }
    }
}

private struct PluginControlView: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    let pluginId: String
    let control: PluginV2Control
    let label: String
    let state: [String: Any]
    let enabled: Bool
    let tag: String
    @State private var live: Double?
    @State private var draft: String?
    @State private var dragBase: Double?
    @FocusState private var focused: Bool

    private var values: [String: Any] { state["values"] as? [String: Any] ?? [:] }

    private func header(_ trailing: String? = nil) -> some View {
        HStack {
            Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            Spacer()
            if let trailing { Text(trailing).font(.aurea(size: 13)) }
        }
    }

    var body: some View {
        let c = control
        switch c.type {
        case "button":
            Button { runtime.event(pluginId, c.id, "click") } label: {
                Text(label).font(.aurea(size: 15, weight: .semibold))
                    .foregroundStyle(c.style == "primary" ? AureaColors.onAccent : c.style == "danger" ? AureaColors.danger : AureaColors.text)
                    .frame(maxWidth: .infinity, minHeight: 44)
                    .background(c.style == "primary" ? AureaColors.accent : AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
            }.buttonStyle(.plain)
        case "slider":
            let stored = (values[c.id] as? NSNumber)?.doubleValue ?? c.min
            let shown = live ?? stored
            VStack(spacing: 4) {
                header(pv2Number(shown, c.step) + (c.unit.isEmpty ? "" : " \(c.unit)"))
                Slider(value: Binding(get: { shown }, set: { live = $0 }), in: c.min...max(c.max, c.min + 0.0001),
                       step: c.step > 0 ? c.step : (c.max - c.min) / 1000,
                       onEditingChanged: { editing in
                           if !editing, let v = live { runtime.event(pluginId, c.id, "change", v); live = nil }
                       })
                .tint(AureaColors.accent)
                .accessibilityLabel(label)
            }
        case "ruler":
            let stored = (values[c.id] as? NSNumber)?.doubleValue ?? 0
            let shown = live ?? stored
            let perPoint = c.step > 0 ? c.step / 4 : 0.05
            VStack(spacing: 4) {
                header(pv2Number(shown, c.step) + (c.unit.isEmpty ? "" : " \(c.unit)"))
                ZStack {
                    RoundedRectangle(cornerRadius: 10).fill(AureaColors.chip)
                    HStack(spacing: 7) {
                        ForEach(0..<40, id: \.self) { i in
                            Rectangle().fill(AureaColors.muted.opacity(i % 5 == 0 ? 0.8 : 0.4)).frame(width: 1, height: i % 5 == 0 ? 16 : 9)
                        }
                    }
                    .offset(x: CGFloat((shown / perPoint).truncatingRemainder(dividingBy: 40)))
                    .clipped()
                    Rectangle().fill(AureaColors.accent).frame(width: 2)
                }
                .frame(height: 44)
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 2).onChanged { g in
                    if dragBase == nil { dragBase = stored }
                    live = min(max((dragBase ?? stored) - Double(g.translation.width) * perPoint, c.min), c.max)
                }.onEnded { _ in
                    if let v = live { runtime.event(pluginId, c.id, "change", v) }
                    live = nil
                    dragBase = nil
                })
                .accessibilityLabel(label)
                .accessibilityValue(pv2Number(shown, c.step))
            }
        case "color":
            let hex = values[c.id] as? String ?? "#FFFFFF"
            ColorPicker(selection: Binding(get: { pv2Color(hex) }, set: { runtime.event(pluginId, c.id, "change", pv2Hex($0)) })) {
                Text(label).font(.aurea(size: 14))
            }
            .frame(minHeight: 44)
        case "choice":
            let selected = (values[c.id] as? NSNumber)?.intValue ?? 0
            VStack(alignment: .leading, spacing: 4) {
                header()
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(Array(c.options.enumerated()), id: \.offset) { i, option in
                            Button { runtime.event(pluginId, c.id, "change", i) } label: {
                                Text(pv2Pick(option, tag)).font(.aurea(size: 14))
                                    .foregroundStyle(i == selected ? AureaColors.onAccent : AureaColors.text)
                                    .padding(.horizontal, 14).frame(minHeight: 36)
                                    .background(i == selected ? AureaColors.accent : AureaColors.chip, in: Capsule())
                            }.buttonStyle(.plain).frame(minHeight: 44)
                        }
                    }
                }
            }
        case "text":
            let stored = values[c.id] as? String ?? ""
            VStack(alignment: .leading, spacing: 4) {
                header()
                TextField(pv2Pick(c.placeholder, tag), text: Binding(get: { draft ?? stored },
                                                                     set: { draft = String($0.prefix(c.maxLength)) }), axis: c.multiline ? .vertical : .horizontal)
                    .font(.aurea(size: 15))
                    .focused($focused)
                    .submitLabel(.done)
                    .onSubmit(send)
                    .onChange(of: focused) { on in if !on { send() } }
                    .padding(.horizontal, 12).frame(minHeight: 44)
                    .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
            }
        case "list":
            let items = (state["items"] as? [String: Any])?[c.id] as? [[String: Any]] ?? []
            let selected = values[c.id] as? String
            VStack(alignment: .leading, spacing: 4) {
                header()
                ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                    let itemId = item["id"] as? String ?? ""
                    let itemLabel = (item["label"] as? String) ?? pv2Pick(pv2Texts(item["label"]), tag)
                    let detail = (item["detail"] as? String) ?? pv2Pick(pv2Texts(item["detail"]), tag)
                    Button { runtime.event(pluginId, c.id, "select", itemId) } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(itemLabel).font(.aurea(size: 14))
                            if !detail.isEmpty { Text(detail).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                        }
                        .padding(.horizontal, 12).padding(.vertical, 8)
                        .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        .background(itemId == selected ? AureaColors.accent.opacity(0.18) : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(itemId == selected ? AureaColors.accent : .clear, lineWidth: 1))
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("plugin.item.\(c.id).\(itemId)")
                }
            }
        case "image":
            let path = (state["images"] as? [String: Any])?[c.id] as? String ?? ""
            VStack(alignment: .leading, spacing: 4) {
                if !label.isEmpty { header() }
                ZStack {
                    RoundedRectangle(cornerRadius: 10).fill(AureaColors.chip)
                    if !path.isEmpty, let image = UIImage(contentsOfFile: path) {
                        Image(uiImage: image).resizable().scaledToFit()
                    } else {
                        CupertinoGlyph.text(CupertinoGlyph.Photo, size: 24, color: AureaColors.muted)
                    }
                }
                .aspectRatio(c.aspect > 0 ? c.aspect : 16.0 / 9.0, contentMode: .fit)
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .accessibilityLabel(label)
            }
        case "label":
            let texts = state["texts"] as? [String: Any]
            VStack(alignment: .leading, spacing: 4) {
                if !label.isEmpty { header() }
                Text((texts?[c.id] as? String) ?? pv2Pick(c.text, tag)).font(.aurea(size: 13)).fixedSize(horizontal: false, vertical: true)
            }
        case "toggle":
            let on = values[c.id] as? Bool ?? false
            Toggle(isOn: Binding(get: { on }, set: { runtime.event(pluginId, c.id, "change", $0) })) {
                Text(label).font(.aurea(size: 14))
            }
            .tint(AureaColors.accent)
            .frame(minHeight: 44)
        default:
            EmptyView()
        }
    }

    private func send() {
        guard let text = draft else { return }
        let stored = values[control.id] as? String ?? ""
        if text != stored { runtime.event(pluginId, control.id, "change", text) }
        draft = nil
    }
}

// =============================================================================
// Onde os painéis aparecem
// =============================================================================

/// O painel aberto no editor, por cima da zona inferior, na moldura comum (EditorView).
struct PluginEditorPanelHost: View {
    @ObservedObject private var runtime = PluginRuntime.shared

    var body: some View {
        ZStack(alignment: .bottom) {
            PluginOverlays()
            if let open = runtime.editorPanel,
               let plugin = runtime.active.first(where: { $0.id == open.plugin }),
               let panel = plugin.panels.first(where: { $0.id == open.panel }) {
                PanelFrame(rail: PanelRail(onBack: { runtime.editorPanel = nil }, supportsAnimation: false)) {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack(spacing: 8) {
                            CupertinoGlyph.text(pv2Glyph(panel.icon), size: 18, color: AureaColors.accent)
                            Text(pv2Pick(panel.title, runtime.locale)).font(.aurea(size: 15, weight: .semibold))
                            Spacer()
                        }
                        PluginPanelContent(plugin: plugin, panel: panel)
                    }
                    .padding(.horizontal, 14).padding(.vertical, 10)
                }
                .frame(height: 300)
                .background(AureaColors.background)
                .transition(.move(edge: .bottom))
            }
        }
    }
}

/// Alertas, console e consentimento pedidos pelos plugins.
struct PluginOverlays: View {
    @ObservedObject private var runtime = PluginRuntime.shared

    var body: some View {
        Color.clear.frame(width: 0, height: 0)
            .alert(runtime.alert ?? "", isPresented: Binding(get: { runtime.alert != nil }, set: { if !$0 { runtime.alert = nil } })) {
                Button(AureaText.t("common_ok"), role: .cancel) { runtime.alert = nil }
            }
            .sheet(isPresented: Binding(get: { runtime.console != nil }, set: { if !$0 { runtime.console = nil } })) {
                if let id = runtime.console { PluginConsoleSheet(pluginId: id).presentationDetents([.medium, .large]) }
            }
            .sheet(isPresented: Binding(get: { runtime.pendingInstall != nil }, set: { if !$0 { runtime.cancelInstall() } })) {
                if let pending = runtime.pendingInstall { PluginConsentSheet(info: pending.info).presentationDetents([.medium, .large]) }
            }
    }
}

/// O botão do menu de plugins no topo do editor (some sem plugin de painel/ação ligado).
struct PluginEditorMenuButton: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    @State private var open = false

    var body: some View {
        if !runtime.editorMenuEntries().isEmpty {
            Button { open = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.Wrench, size: 20, color: AureaColors.text)
                    .frame(width: 44, height: EditorLayout.topBar).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("pv2_menu"))
            .accessibilityIdentifier("plugin.menu")
            .aureaAnchoredMenu(isPresented: $open, alignEnd: true, tag: "plugin.menu.list") { runtime.editorMenuEntries() }
        }
    }
}

/// Botões "layer.toolbar" na barra da camada.
struct PluginLayerBarButtons: View {
    @ObservedObject private var runtime = PluginRuntime.shared

    var body: some View {
        ForEach(runtime.active, id: \.id) { p in
            ForEach(p.actions.filter { $0.placement.contains("layer.toolbar") }, id: \.id) { a in
                Button { runtime.runAction(p.id, a) } label: {
                    CupertinoGlyph.text(pv2Glyph(a.icon), size: 20, color: AureaColors.text)
                        .frame(width: 44, height: EditorLayout.topBar).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .disabled(runtime.busy.contains(p.id))
                .accessibilityLabel(pv2Pick(a.title, runtime.locale))
                .accessibilityIdentifier("plugin.action.\(p.id).\(a.id)")
            }
        }
    }
}

/// A aba de plugins da Home (seletor no topo se houver várias abas).
struct PluginHomeTab: View {
    @ObservedObject private var runtime = PluginRuntime.shared

    var body: some View {
        let tabs = runtime.active.flatMap { p in p.panels.filter { $0.placement.contains("home.tab") }.map { (p, $0) } }
        VStack(spacing: 0) {
            PluginOverlays()
            if tabs.isEmpty {
                Text(AureaText.t("pv2_menu_empty")).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted)
                    .padding(24).frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                let current = tabs.first(where: { runtime.homePanel == PluginPanelRef(plugin: $0.0.id, panel: $0.1.id) }) ?? tabs[0]
                if tabs.count > 1 {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            ForEach(Array(tabs.enumerated()), id: \.offset) { _, entry in
                                let on = entry.0.id == current.0.id && entry.1.id == current.1.id
                                Button { runtime.openPanel(entry.0.id, entry.1.id, inEditor: false) } label: {
                                    Text(pv2Pick(entry.1.title, runtime.locale)).font(.aurea(size: 14))
                                        .foregroundStyle(on ? AureaColors.onAccent : AureaColors.text)
                                        .padding(.horizontal, 14).frame(minHeight: 36)
                                        .background(on ? AureaColors.accent : AureaColors.chip, in: Capsule())
                                }.buttonStyle(.plain).frame(minHeight: 44)
                            }
                        }.padding(.horizontal, 16)
                    }
                }
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        Text(pv2Pick(current.1.title, runtime.locale)).font(.aurea(size: 20, weight: .semibold))
                        PluginPanelContent(plugin: current.0, panel: current.1)
                    }
                    .padding(16)
                }
                .onAppear {
                    if runtime.homePanel != PluginPanelRef(plugin: current.0.id, panel: current.1.id) {
                        runtime.openPanel(current.0.id, current.1.id, inEditor: false)
                    }
                }
            }
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.background)
        .accessibilityIdentifier("home.plugins")
    }
}

// =============================================================================
// Consentimento, console e o cartão do gerenciador
// =============================================================================
struct PluginPermissionRow: View {
    let text: String
    let on: Bool
    let tag: String
    let onChange: (Bool) -> Void
    var body: some View {
        Toggle(isOn: Binding(get: { on }, set: onChange)) { Text(text).font(.aurea(size: 13)) }
            .tint(AureaColors.accent)
            .frame(minHeight: 44)
            .accessibilityIdentifier(tag)
    }
}

struct PluginConsentSheet: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    let info: [String: Any]
    @State private var allowNetwork = true
    @State private var allowClip = true
    @State private var allowFiles = true

    var body: some View {
        let tag = runtime.locale
        let perms = info["permissions"] as? [String: Any] ?? [:]
        let network = perms["network"] as? [String] ?? []
        let clip = perms["clipboard"] as? Bool ?? false
        let files = perms["files"] as? Bool ?? false
        let name = pv2Pick(pv2Texts(info["name"]), tag)
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                Text(AureaText.t("pv2_consent_title", name.isEmpty ? (info["id"] as? String ?? "") : name)).font(.aurea(size: 17, weight: .semibold))
                Text(AureaText.t("plug_by", (info["author"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? (info["id"] as? String ?? ""),
                                 info["version"] as? String ?? ""))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                let description = pv2Pick(pv2Texts(info["description"]), tag)
                if !description.isEmpty { Text(description).font(.aurea(size: 13)).fixedSize(horizontal: false, vertical: true) }
                if let installed = info["installedVersion"] as? String, !installed.isEmpty {
                    Text(AureaText.t("pv2_consent_update", installed)).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                }
                Text(AureaText.t(network.isEmpty && !clip && !files ? "pv2_consent_none" : "pv2_consent_body")).font(.aurea(size: 14, weight: .semibold))
                if !network.isEmpty {
                    PluginPermissionRow(text: AureaText.t("pv2_perm_network", network.joined(separator: ", ")), on: allowNetwork,
                                        tag: "plugins.consent.network") { allowNetwork = $0 }
                }
                if clip { PluginPermissionRow(text: AureaText.t("pv2_perm_clipboard"), on: allowClip, tag: "plugins.consent.clipboard") { allowClip = $0 } }
                if files { PluginPermissionRow(text: AureaText.t("pv2_perm_files"), on: allowFiles, tag: "plugins.consent.files") { allowFiles = $0 } }
                if info["hasScript"] as? Bool == true {
                    Text(AureaText.t("pv2_consent_script")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                }
                HStack(spacing: 10) {
                    Button { runtime.cancelInstall() } label: {
                        Text(AureaText.t("common_cancel")).font(.aurea(size: 15)).frame(maxWidth: .infinity, minHeight: 46)
                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
                    }.buttonStyle(.plain).accessibilityIdentifier("plugins.consent.cancel")
                    Button {
                        let mask: UInt32 = (allowNetwork && !network.isEmpty ? 1 : 0) | (allowClip && clip ? 2 : 0) | (allowFiles && files ? 4 : 0)
                        runtime.confirmInstall(mask)
                    } label: {
                        Text(AureaText.t("pv2_install")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                            .frame(maxWidth: .infinity, minHeight: 46).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                    }.buttonStyle(.plain).accessibilityIdentifier("plugins.consent.install")
                }
            }
            .padding(18)
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.surface.ignoresSafeArea())
        .accessibilityIdentifier("plugins.consent")
    }
}

struct PluginConsoleSheet: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    let pluginId: String
    @State private var version = 0

    var body: some View {
        let lines = runtime.consoleLines(pluginId)
        let _ = version
        let name = runtime.plugin(pluginId).map { pv2Pick($0.names, runtime.locale) } ?? pluginId
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(AureaText.t("pv2_console_title", name)).font(.aurea(size: 17, weight: .semibold))
                Spacer()
                Button { runtime.clearConsole(pluginId); version += 1 } label: {
                    Text(AureaText.t("pv2_console_clear")).font(.aurea(size: 14)).foregroundStyle(AureaColors.accent).frame(minHeight: 44)
                }.buttonStyle(.plain).accessibilityIdentifier("plugins.console.clear")
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    if lines.isEmpty { Text(AureaText.t("pv2_console_empty")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                    ForEach(Array(lines.enumerated()), id: \.offset) { _, line in
                        Text(line.1).font(.system(size: 12, design: .monospaced))
                            .foregroundStyle(line.0 == 3 ? AureaColors.danger : line.0 == 2 ? AureaColors.warning : AureaColors.text)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }.padding(10)
            }
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
            Button { runtime.reload(pluginId); version += 1 } label: {
                Text(AureaText.t("pv2_reload")).font(.aurea(size: 14)).foregroundStyle(AureaColors.accent).frame(minHeight: 44)
            }.buttonStyle(.plain).accessibilityIdentifier("plugins.console.reload")
        }
        .padding(18)
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.surface.ignoresSafeArea())
        .accessibilityIdentifier("plugins.console")
    }
}

/// O que o cartão de um plugin com interface/script ganha no gerenciador.
struct PluginV2Controls: View {
    @ObservedObject private var runtime = PluginRuntime.shared
    let pluginId: String

    var body: some View {
        if let p = runtime.plugin(pluginId), p.ok, p.hasScript || !p.panels.isEmpty || !p.actions.isEmpty {
            VStack(alignment: .leading, spacing: 2) {
                PluginPermissionRow(text: AureaText.t("pv2_enabled"), on: p.enabled, tag: "plugins.enabled.\(p.id)") { runtime.setEnabled(p.id, $0) }
                if p.requested != 0 {
                    Text(AureaText.t("pv2_permissions")).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.muted).padding(.top, 4)
                    if !p.network.isEmpty {
                        PluginPermissionRow(text: AureaText.t("pv2_perm_network", p.network.joined(separator: ", ")), on: p.granted & 1 != 0,
                                            tag: "plugins.perm.network.\(p.id)") { runtime.setPermission(p.id, 1, $0) }
                    }
                    if p.wantsClipboard {
                        PluginPermissionRow(text: AureaText.t("pv2_perm_clipboard"), on: p.granted & 2 != 0,
                                            tag: "plugins.perm.clipboard.\(p.id)") { runtime.setPermission(p.id, 2, $0) }
                    }
                    if p.wantsFiles {
                        PluginPermissionRow(text: AureaText.t("pv2_perm_files"), on: p.granted & 4 != 0,
                                            tag: "plugins.perm.files.\(p.id)") { runtime.setPermission(p.id, 4, $0) }
                    }
                }
                if p.enabled {
                    ForEach(p.panels.filter { $0.placement.contains("editor.panel") }, id: \.id) { panel in
                        Button { runtime.openPanel(p.id, panel.id) } label: {
                            Text(AureaText.t("pv2_open_panel", pv2Pick(panel.title, runtime.locale))).font(.aurea(size: 13))
                                .foregroundStyle(AureaColors.accent).frame(minHeight: 44)
                        }.buttonStyle(.plain).accessibilityIdentifier("plugins.open.\(p.id).\(panel.id)")
                    }
                    if p.hasScript {
                        Button { runtime.console = p.id } label: {
                            Text(AureaText.t("pv2_console")).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).frame(minHeight: 44)
                        }.buttonStyle(.plain).accessibilityIdentifier("plugins.console.\(p.id)")
                    }
                }
            }
            .padding(.top, 6)
        }
    }
}
