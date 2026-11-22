// =============================================================================
//  Aurea / platform / ios / app / AureaMcpView.swift
//
//  AureaMCP em tela cheia (par do AureaMcpScreen.kt): mascote, ligar, status,
//  conectar agente (código do Claude.ai/ChatGPT), token do Claude Code/Codex,
//  agentes com acesso, atividade, parar/retomar e permissões. Mais a raiz
//  (AureaMcpRoot) que conecta, segue o primeiro plano e mostra as confirmações.
// =============================================================================
import Photos
import SwiftUI
import UIKit

private enum McpInk {
    static let ink = Color(red: 0.055, green: 0.043, blue: 0.086)
    static let card = Color(red: 0.102, green: 0.078, blue: 0.157)
    static let line = Color.white.opacity(0.12)
    static let violet = Color(red: 0.659, green: 0.333, blue: 0.969)
    static let glow = LinearGradient(colors: [Color(red: 0.427, green: 0.157, blue: 0.851), Color(red: 0.659, green: 0.333, blue: 0.969)],
                                     startPoint: .leading, endPoint: .trailing)
    static let online = Color(red: 0.30, green: 0.85, blue: 0.39)
    static let warn = Color(red: 1, green: 0.69, blue: 0.125)
}

struct AureaMcpView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var mcp = AureaMcpClient.shared
    let close: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                mascot(30)
                Text(AureaText.t("mcp_title")).font(.aurea(size: 17, weight: .bold)).accessibilityAddTraits(.isHeader)
                Spacer()
                Button(action: close) { CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 18, color: AureaColors.muted).frame(width: 40, height: 40) }
                    .accessibilityLabel(AureaText.t("mcp_close")).accessibilityIdentifier("mcp.close")
            }
            .padding(.horizontal, 12).frame(height: 56)
            Rectangle().fill(McpInk.line).frame(height: 1)
            ScrollView {
                VStack(spacing: 14) {
                    hero
                    if mcp.enabled && mcp.status != .noAccount {
                        McpConnectCard(mcp: mcp)
                        McpBackgroundCard(mcp: mcp)
                        McpAureaosCard(mcp: mcp)
                        McpTokenCard(mcp: mcp)
                        McpAgentsCard(mcp: mcp)
                        activity
                        permissions
                        Button { mcp.changeEnabled(false) } label: {
                            Text(AureaText.t("mcp_turn_off")).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.danger)
                                .frame(maxWidth: .infinity).padding(.vertical, 12)
                        }.accessibilityIdentifier("mcp.turnOff")
                    }
                }
                .padding(16)
            }
        }
        .background(McpInk.ink.ignoresSafeArea())
        .foregroundStyle(.white)
        .onAppear { mcp.attach(model); if mcp.enabled { mcp.start(); Task { await mcp.refreshGrants(); await mcp.refreshAureaos() } } }
        .accessibilityIdentifier("mcp.screen")
        .modifier(McpConfirmAlert(mcp: mcp))
    }

    private func mascot(_ size: CGFloat) -> some View {
        Image("AureaMCPMascot").resizable().scaledToFit().frame(width: size, height: size)
            .clipShape(RoundedRectangle(cornerRadius: size * 0.23, style: .continuous)).accessibilityHidden(true)
    }

    private var hero: some View {
        VStack(spacing: 12) {
            mascot(104)
            Text(AureaText.t("mcp_tagline")).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted)
                .multilineTextAlignment(.center).frame(maxWidth: 420)
            if !mcp.enabled {
                McpPrimaryButton(title: AureaText.t("mcp_enable"), id: "mcp.enable") { mcp.attach(model); mcp.changeEnabled(true) }
            } else {
                HStack(spacing: 8) {
                    Circle().fill(statusColor).frame(width: 8, height: 8)
                    Text(statusLabel).font(.aurea(size: 13))
                }
                .padding(.horizontal, 12).padding(.vertical, 7).background(McpInk.card, in: Capsule())
                .accessibilityElement(children: .combine).accessibilityIdentifier("mcp.status")
                if mcp.paused {
                    Text(AureaText.t("mcp_paused")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(McpInk.warn)
                    McpPrimaryButton(title: AureaText.t("mcp_resume"), id: "mcp.resume") { mcp.resume() }
                } else if mcp.status == .online {
                    McpSecondaryButton(title: AureaText.t("mcp_stop"), id: "mcp.stop", color: AureaColors.danger) { mcp.pause() }
                }
            }
        }
        .frame(maxWidth: .infinity)
    }

    private var statusLabel: String {
        switch mcp.status {
        case .online: return AureaText.t("mcp_status_online")
        case .connecting: return AureaText.t("mcp_status_connecting")
        case .offline: return AureaText.t("mcp_status_offline")
        case .noAccount: return AureaText.t("mcp_status_no_account")
        case .off: return AureaText.t("mcp_status_off")
        }
    }

    private var statusColor: Color {
        switch mcp.status {
        case .online: return McpInk.online
        case .connecting: return McpInk.violet
        case .offline: return McpInk.warn
        case .noAccount: return AureaColors.danger
        case .off: return AureaColors.muted
        }
    }

    private var activity: some View {
        McpSection(title: AureaText.t("mcp_activity_title")) {
            if mcp.log.isEmpty { Text(AureaText.t("mcp_activity_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
            ForEach(Array(mcp.log.suffix(40).reversed())) { line in
                HStack(spacing: 8) {
                    CupertinoGlyph.text(line.ok ? CupertinoGlyph.CheckmarkCircle : CupertinoGlyph.ExclamationmarkBubble, size: 13,
                                        color: line.ok ? McpInk.violet : AureaColors.danger)
                    Text(Date(timeIntervalSince1970: line.at / 1000), style: .time).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).frame(width: 64, alignment: .leading)
                    Text(line.tool.map { "\(line.agent) · \($0)" } ?? AureaText.t("mcp_activity_connected", line.agent)).font(.aurea(size: 12)).lineLimit(1)
                }
            }
        }
    }

    private var permissions: some View {
        McpSection(title: AureaText.t("mcp_perm_title")) {
            ForEach([("read", "mcp_perm_read"), ("edit", "mcp_perm_edit"), ("projects", "mcp_perm_projects"),
                     ("delete", "mcp_perm_delete"), ("media", "mcp_perm_media"), ("export", "mcp_perm_export")], id: \.0) { item in
                toggleRow(AureaText.t(item.1), mcp.permissions[item.0] == true, "mcp.perm.\(item.0)") { mcp.setPermission(item.0, $0) }
            }
            McpDeviceMediaToggle(mcp: mcp)
            toggleRow(AureaText.t("mcp_ask_every"), mcp.askEveryEdit, "mcp.askEvery") { mcp.changeAskEveryEdit($0) }
        }
    }

    private func toggleRow(_ label: String, _ on: Bool, _ id: String, _ change: @escaping (Bool) -> Void) -> some View {
        HStack {
            Text(label).font(.aurea(size: 14))
            Spacer(minLength: 10)
            AureaToggle(checked: on, onCheckedChange: change).accessibilityIdentifier(id)
        }
        .accessibilityElement(children: .combine).accessibilityLabel(label)
    }
}

/// "Fora do app" (par do BackgroundCard do Android): o que o iOS deixa o agente
/// fazer com o app fora da tela, o interruptor e, no iOS 26+, a tarefa contínua.
private struct McpBackgroundCard: View {
    @ObservedObject var mcp: AureaMcpClient
    var body: some View {
        McpSection(title: AureaText.t("mcp_bg_title")) {
            Text(AureaText.t("mcp_bg_body_ios")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            HStack {
                Text(AureaText.t("mcp_bg_toggle")).font(.aurea(size: 14))
                Spacer(minLength: 10)
                AureaToggle(checked: mcp.keepWorking, onCheckedChange: { mcp.changeKeepWorking($0) }).accessibilityIdentifier("mcp.keepWorking")
            }
            .accessibilityElement(children: .combine).accessibilityLabel(AureaText.t("mcp_bg_toggle"))
            if mcp.keepWorking {
                if !mcp.notificationsAllowed {
                    Text(AureaText.t("mcp_bg_notif_denied_ios")).font(.aurea(size: 12)).foregroundStyle(McpInk.warn)
                        .accessibilityIdentifier("mcp.keepWorking.notify")
                } else if mcp.pushToken != nil {
                    HStack(spacing: 8) {
                        Circle().fill(McpInk.online).frame(width: 8, height: 8)
                        Text(AureaText.t("mcp_bg_push_ready_ios")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(McpInk.online)
                    }
                    .accessibilityElement(children: .combine).accessibilityIdentifier("mcp.keepWorking.status")
                }
                if mcp.continuedSupported {
                    if mcp.continuedRunning {
                        HStack(spacing: 8) {
                            Circle().fill(McpInk.online).frame(width: 8, height: 8)
                            Text(AureaText.t("mcp_bg_session_running_ios")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(McpInk.online)
                        }
                        .accessibilityElement(children: .combine).accessibilityIdentifier("mcp.continued.status")
                    } else if mcp.status == .online {
                        McpSecondaryButton(title: AureaText.t("mcp_bg_session_ios"), id: "mcp.continued.start") { mcp.startContinuedSession() }
                    }
                }
                Text(AureaText.t("mcp_bg_limits_ios")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
        }
    }
}

private struct McpSection<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.aurea(size: 15, weight: .bold)).accessibilityAddTraits(.isHeader)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(McpInk.card, in: RoundedRectangle(cornerRadius: 16))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(McpInk.line, lineWidth: 1))
    }
}

private struct McpPrimaryButton: View {
    let title: String
    let id: String
    var enabled = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: 15, weight: .bold)).foregroundStyle(enabled ? .white : AureaColors.muted)
                .frame(maxWidth: .infinity).frame(height: 46)
                .background(enabled ? AnyShapeStyle(McpInk.glow) : AnyShapeStyle(Color(white: 0.17)), in: RoundedRectangle(cornerRadius: 12))
        }
        .disabled(!enabled).accessibilityIdentifier(id)
    }
}

private struct McpSecondaryButton: View {
    let title: String
    let id: String
    var color: Color = McpInk.violet
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(color)
                .padding(.horizontal, 12).padding(.vertical, 8)
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(McpInk.line, lineWidth: 1))
        }
        .accessibilityIdentifier(id)
    }
}

private struct McpConnectCard: View {
    @ObservedObject var mcp: AureaMcpClient
    @State private var code = ""
    @State private var error: String?
    @State private var done: String?
    @State private var request: AureaMcpClient.PairRequest?
    @State private var copied = false

    var body: some View {
        McpSection(title: AureaText.t("mcp_connect_title")) {
            content
        }
        // Link aurea://mcp/pair?code=…: o código entra sozinho e já é conferido.
        .onAppear { takeLinkedCode() }
        .onChange(of: mcp.pendingPairCode) { _ in takeLinkedCode() }
    }

    private func takeLinkedCode() {
        guard let linked = mcp.pendingPairCode else { return }
        mcp.pendingPairCode = nil
        code = linked; error = nil; request = nil
        Task { if let r = await mcp.lookupPair(linked) { request = r } else { error = AureaText.t("mcp_code_invalid") } }
    }

    @ViewBuilder private var content: some View {
        Group {
            Text(AureaText.t("mcp_connect_body")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            HStack {
                Text(AureaMcpClient.serverURL).font(.system(size: 12, design: .monospaced)).lineLimit(2)
                Spacer(minLength: 8)
                McpSecondaryButton(title: AureaText.t(copied ? "mcp_copied" : "mcp_copy_url"), id: "mcp.copyUrl") {
                    UIPasteboard.general.string = AureaMcpClient.serverURL; copied = true
                }
            }
            if let pending = request {
                VStack(alignment: .leading, spacing: 10) {
                    Text(AureaText.t("mcp_approve_title")).font(.aurea(size: 15, weight: .bold))
                    Text(AureaText.t("mcp_approve_body", pending.client, pending.redirectHost)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    HStack(spacing: 10) {
                        McpPrimaryButton(title: AureaText.t("mcp_approve"), id: "mcp.approve", enabled: !mcp.busy) {
                            Task {
                                let ok = await mcp.approvePair(pending.code, allow: true)
                                request = nil; code = ""
                                if ok { done = AureaText.t("mcp_approved", pending.client) } else { error = AureaText.t("mcp_code_invalid") }
                            }
                        }
                        McpSecondaryButton(title: AureaText.t("mcp_deny"), id: "mcp.deny", color: AureaColors.danger) {
                            Task { _ = await mcp.approvePair(pending.code, allow: false); request = nil; code = "" }
                        }
                    }
                }
                .padding(12).background(McpInk.ink, in: RoundedRectangle(cornerRadius: 12))
            } else {
                TextField(AureaText.t("mcp_code_hint"), text: $code)
                    .textInputAutocapitalization(.characters).autocorrectionDisabled()
                    .font(.system(size: 18, weight: .bold, design: .monospaced))
                    .padding(12).background(McpInk.ink, in: RoundedRectangle(cornerRadius: 12))
                    .overlay(RoundedRectangle(cornerRadius: 12).stroke(McpInk.line, lineWidth: 1))
                    .onChange(of: code) { v in let u = String(v.uppercased().prefix(12)); if u != v { code = u }; error = nil }
                    .accessibilityIdentifier("mcp.code")
                if let error { Text(error).font(.aurea(size: 13)).foregroundStyle(AureaColors.danger) }
                if let done { Text(done).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(McpInk.online) }
                McpPrimaryButton(title: AureaText.t("mcp_code_check"), id: "mcp.codeCheck",
                                 enabled: code.filter { $0.isLetter || $0.isNumber }.count == 8 && !mcp.busy) {
                    done = nil
                    Task { if let r = await mcp.lookupPair(code) { request = r } else { error = AureaText.t("mcp_code_invalid") } }
                }
            }
            if mcp.busy { ProgressView().tint(McpInk.violet).frame(maxWidth: .infinity) }
        }
    }
}

/// "Galeria do aparelho": ligar pede o acesso ao Fotos. Negado, volta a desligado;
/// limitado, avisa que o agente só vê o escolhido (e deixa mudar a seleção).
private struct McpDeviceMediaToggle: View {
    @ObservedObject var mcp: AureaMcpClient
    @State private var status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
    @Environment(\.scenePhase) private var phase

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(AureaText.t("mcp_perm_device_media")).font(.aurea(size: 14))
                Spacer(minLength: 10)
                AureaToggle(checked: mcp.permissions["device_media"] == true, onCheckedChange: change).accessibilityIdentifier("mcp.perm.device_media")
            }
            .accessibilityElement(children: .combine).accessibilityLabel(AureaText.t("mcp_perm_device_media"))
            if mcp.permissions["device_media"] == true && status == .limited {
                HStack {
                    Text(AureaText.t("mcp_device_media_limited")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    Spacer(minLength: 8)
                    McpSecondaryButton(title: AureaText.t("mcp_device_media_manage"), id: "mcp.gallery.manage") { manage() }
                }
            } else if status == .denied || status == .restricted {
                Text(AureaText.t("mcp_device_media_denied")).font(.aurea(size: 12)).foregroundStyle(AureaColors.danger)
            }
        }
        .onChange(of: phase) { p in if p == .active { status = PHPhotoLibrary.authorizationStatus(for: .readWrite) } }
    }

    private func change(_ on: Bool) {
        guard on else { mcp.setPermission("device_media", false); return }
        let now = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        if now == .authorized || now == .limited { status = now; mcp.setPermission("device_media", true); return }
        if now == .notDetermined {
            PHPhotoLibrary.requestAuthorization(for: .readWrite) { result in
                DispatchQueue.main.async {
                    status = result
                    mcp.setPermission("device_media", result == .authorized || result == .limited)
                }
            }
        } else {
            status = now
            mcp.setPermission("device_media", false)
            if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
        }
    }

    private func manage() {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        guard var presenter = scenes.flatMap(\.windows).first(where: \.isKeyWindow)?.rootViewController else { return }
        while let next = presenter.presentedViewController { presenter = next }
        PHPhotoLibrary.shared().presentLimitedLibraryPicker(from: presenter)
    }
}

/// AureaOS: o método medido do dono, pago (aurea_aureaos, compra única). O editor continua livre.
private struct McpAureaosCard: View {
    @ObservedObject var mcp: AureaMcpClient
    var body: some View {
        McpSection(title: AureaText.t("mcp_aureaos_title")) {
            Text(AureaText.t("mcp_aureaos_body")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            HStack(spacing: 8) {
                Circle().fill(mcp.aureaosActive == true ? McpInk.online : Color(white: 0.25)).frame(width: 8, height: 8)
                Text(AureaText.t(mcp.aureaosActive == true ? "mcp_aureaos_active" : "mcp_aureaos_inactive"))
                    .font(.aurea(size: 13, weight: .semibold)).foregroundStyle(mcp.aureaosActive == true ? McpInk.online : AureaColors.muted)
            }
            .accessibilityElement(children: .combine).accessibilityIdentifier("mcp.aureaos.status")
            if mcp.aureaosActive == false {
                if !mcp.aureaosPurchasable {
                    Text(AureaText.t("mcp_aureaos_unavailable")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                } else {
                    McpPrimaryButton(title: mcp.aureaosProduct.map { AureaText.t("mcp_aureaos_activate_price", $0.displayPrice) } ?? AureaText.t("mcp_aureaos_activate"),
                                     id: "mcp.aureaos.activate", enabled: !mcp.aureaosBusy) { Task { await mcp.buyAureaos(restore: false) } }
                    McpSecondaryButton(title: AureaText.t("mcp_aureaos_restore"), id: "mcp.aureaos.restore") { Task { await mcp.buyAureaos(restore: true) } }
                }
            }
            if let notice = mcp.aureaosNotice {
                Text(AureaText.t(notice)).font(.aurea(size: 12, weight: .semibold))
                    .foregroundStyle(notice == "mcp_aureaos_thanks" ? McpInk.online : McpInk.warn).accessibilityIdentifier("mcp.aureaos.notice")
            }
            if mcp.aureaosBusy { ProgressView().tint(McpInk.violet).frame(maxWidth: .infinity) }
        }
    }
}

private struct McpTokenCard: View {
    @ObservedObject var mcp: AureaMcpClient
    @State private var token: String?
    var body: some View {
        McpSection(title: AureaText.t("mcp_token_title")) {
            Text(AureaText.t("mcp_token_body")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            if let token {
                Text(AureaText.t("mcp_token_once")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(McpInk.warn)
                Text(token).font(.system(size: 12, design: .monospaced)).textSelection(.enabled)
                    .padding(10).frame(maxWidth: .infinity, alignment: .leading).background(McpInk.ink, in: RoundedRectangle(cornerRadius: 10))
                    .accessibilityIdentifier("mcp.token")
                HStack(spacing: 8) {
                    McpSecondaryButton(title: AureaText.t("mcp_token_copy_claude"), id: "mcp.copyClaude") { UIPasteboard.general.string = AureaMcpClient.claudeCodeCommand(token) }
                    McpSecondaryButton(title: AureaText.t("mcp_token_copy_codex"), id: "mcp.copyCodex") { UIPasteboard.general.string = AureaMcpClient.codexConfig(token) }
                }
            } else {
                McpSecondaryButton(title: AureaText.t("mcp_token_create"), id: "mcp.tokenCreate") {
                    Task { token = await mcp.createToken(AureaText.t("mcp_token_title")) }
                }
            }
        }
    }
}

private struct McpAgentsCard: View {
    @ObservedObject var mcp: AureaMcpClient
    @State private var confirmAll = false
    var body: some View {
        McpSection(title: AureaText.t("mcp_agents_title")) {
            if mcp.grants.isEmpty { Text(AureaText.t("mcp_agents_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
            ForEach(mcp.grants) { g in
                let live = mcp.agents.contains { $0.tokenId == g.id && Date().timeIntervalSince1970 * 1000 - $0.seen < 300_000 }
                HStack(spacing: 10) {
                    Circle().fill(live ? McpInk.online : Color(white: 0.25)).frame(width: 8, height: 8)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(g.name).font(.aurea(size: 14, weight: .semibold))
                        Group {
                            if live { Text(AureaText.t("mcp_agent_online")) }
                            else if let last = g.lastUsedAt {
                                Text(AureaText.t("mcp_agent_last", DateFormatter.localizedString(from: Date(timeIntervalSince1970: last / 1000), dateStyle: .short, timeStyle: .short)))
                            } else { Text(AureaText.t("mcp_agent_never")) }
                        }
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    }
                    Spacer()
                    McpSecondaryButton(title: AureaText.t("mcp_revoke"), id: "mcp.revoke.\(g.id.prefix(8))", color: AureaColors.danger) { Task { await mcp.revoke(g.id) } }
                }
            }
            if mcp.grants.count > 1 {
                McpSecondaryButton(title: AureaText.t("mcp_revoke_all"), id: "mcp.revokeAll", color: AureaColors.danger) { confirmAll = true }
            }
        }
        .alert(AureaText.t("mcp_revoke_all"), isPresented: $confirmAll) {
            Button(AureaText.t("mcp_revoke_all"), role: .destructive) { Task { await mcp.revoke(nil) } }
            Button(AureaText.t("mcp_deny"), role: .cancel) {}
        } message: { Text(AureaText.t("mcp_revoke_all_confirm")) }
    }
}

/// Raiz do AureaMCP (ContentView): com ele ligado, conecta, avisa o relay quando
/// o app volta à frente e quando o projeto muda, e mostra as confirmações.
struct AureaMcpRoot: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var mcp = AureaMcpClient.shared
    @Environment(\.scenePhase) private var phase

    var body: some View {
        Color.clear.frame(width: 0, height: 0)
            .onAppear { mcp.attach(model); if mcp.enabled { mcp.start() } }
            // Fora do app (18/11/2026): o relay sabe se o app está na frente; voltar drena a fila.
            .onChange(of: phase) { p in
                if p == .active { mcp.appBecameActive() }
                else if p == .background, mcp.enabled { mcp.appDidEnterBackground() }
            }
            .onChange(of: model.projectName) { _ in if mcp.status == .online { mcp.projectChanged() } }
            .onChange(of: model.screen) { _ in if mcp.status == .online { mcp.projectChanged() } }
            .modifier(McpConfirmAlert(mcp: mcp))
            // O link aurea://mcp/pair abre o AureaMCP em qualquer tela (mesmo desligado).
            .fullScreenCover(isPresented: $mcp.deepLinkOpen) {
                AureaMcpView(close: { mcp.deepLinkOpen = false }).environmentObject(model)
            }
    }
}

/// O pedido de confirmação de um agente. Fica na raiz e também na tela do
/// AureaMCP: um alerta da raiz não aparece por cima de uma tela cheia.
struct McpConfirmAlert: ViewModifier {
    @ObservedObject var mcp: AureaMcpClient
    /// 1.0.6: o pedido NA TELA fica parado enquanto o alerta aparece. Antes título,
    /// papel do botão e texto liam `mcp.confirm` ao vivo: com "perguntar a cada edição"
    /// o agente troca o pedido (nil → novo) com o alerta aberto, e o UIKit abortava com
    /// "A view controller not containing an alert controller was asked for its contained
    /// alert controller" (NSInternalInconsistencyException, 2224, no editor). Agora o
    /// velho fecha e o novo entra depois; cada botão responde só ao pedido que mostrou.
    @State private var shown: AureaMcpClient.Confirm?
    @State private var shownTitle = ""

    func body(content: Content) -> some View {
        content
            .alert(shownTitle, isPresented: Binding(get: { shown != nil }, set: { presented in
                guard !presented else { return }
                if let c = shown, mcp.confirm?.id == c.id { mcp.answerConfirm(false, id: c.id.uuidString) }
                shown = nil
            }), presenting: shown) { c in
                Button(AureaText.t("mcp_approve"), role: c.tool == "delete_project" ? .destructive : nil) { mcp.answerConfirm(true, id: c.id.uuidString) }
                Button(AureaText.t("mcp_deny"), role: .cancel) { mcp.answerConfirm(false, id: c.id.uuidString) }
            } message: { c in Text(Self.message(c)) }
            .onAppear { if shown == nil, let c = mcp.confirm { show(c) } }
            .onReceive(mcp.$confirm) { next in
                guard next?.id != shown?.id else { return }
                if shown == nil {
                    if let next { show(next) }
                } else {
                    // Fecha o que está na tela; o próximo sobe quando ele tiver saído.
                    shown = nil
                    if next != nil {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                            if shown == nil, let c = mcp.confirm { show(c) }
                        }
                    }
                }
            }
    }

    private func show(_ c: AureaMcpClient.Confirm) {
        shownTitle = AureaText.t("mcp_confirm_title", c.agent)
        shown = c
    }

    private static func message(_ c: AureaMcpClient.Confirm) -> String {
        if c.tool == "delete_project" { return AureaText.t("mcp_confirm_delete", c.detail) }
        return AureaText.t("mcp_confirm_edit", c.tool) + (c.detail.isEmpty ? "" : "\n\n" + String(c.detail.prefix(300)))
    }
}
