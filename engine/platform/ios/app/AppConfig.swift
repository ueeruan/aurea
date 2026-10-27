// Configuração remota ASSINADA (0.3.2; servidor: discovery/appconfig.js). Par
// do Android: config/AppConfig.kt e config/AppConfigScreens.kt (as mesmas
// regras e os mesmos accessibilityIdentifier).
//
// GET /api/app-config?platform=ios → {payload, signature}: o app confere a
// assinatura ECDSA P-256/SHA-256 (r||s cru, base64) sobre os bytes do
// `payload` com a chave PÚBLICA embutida e só então lê. Recusa: sem assinatura,
// assinatura errada, outra plataforma, v ≠ 1, vencida, emitida no futuro ou
// mais antiga que a guardada.
//   · ATUALIZAÇÃO OBRIGATÓRIA: CFBundleVersion < minVersionCode da última
//     config verificada (a guardada vale mesmo vencida). Sem config: não bloqueia.
//   · AureaHyper: "free" libera só com config emitida há ≤ 7 dias e no prazo;
//     "entitled" = o portão por conta; "off" = bloqueado com o aviso.
//   · `message` vira um banner na Home.
// accessibilityIdentifier: atualizacao.obrigatoria · atualizacao.abrir · aviso.config · aviso.config.fechar
import Combine
import CryptoKit
import Foundation
import SwiftUI
import UIKit

enum AppConfigKeys {
    /// A chave PÚBLICA (SPKI, base64) de discovery/scripts/gen-config-key.mjs. A privada nunca sai do Worker.
    static let publicKeySPKI =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE8cKccQcdlWE04+nNFF7OYcWJqbpQQOFJpG9vRRc4NGUWyGnsPUphLfnDZmg3Z2kblZ0e6W/MTZPhFR6vp+kFhg=="
    static let platform = "ios"
}

enum HyperMode: String { case free, entitled, off }

struct AppConfig: Equatable {
    var platform: String
    var minVersionCode: Int64
    var latestVersionCode: Int64
    var updateUrl: String
    var hyperMode: HyperMode
    var message: [String: String]
    var issuedAt: Int64
    var expiresAt: Int64
    var payload: String
    var signature: String

    /// O aviso no idioma do app: o idioma, depois inglês, depois português.
    func message(for language: AureaLanguage) -> String? {
        let tag: String
        switch language.resolved {
        case .pt, .system: tag = "pt-BR"
        default: tag = language.resolved.rawValue
        }
        return message[tag] ?? message["en"] ?? message["pt-BR"]
    }
}

enum AppConfigVerifier {
    static let maxLifetime: Int64 = 31 * 86400
    static let clockSkew: Int64 = 86400

    static func publicKey(_ spki: String) -> P256.Signing.PublicKey? {
        guard let der = Data(base64Encoded: spki) else { return nil }
        return try? P256.Signing.PublicKey(derRepresentation: der)
    }

    /// Confere e lê. `allowExpired` só para o cache (a versão mínima vale offline).
    static func parse(payload: String, signature: String, key: P256.Signing.PublicKey?, platform: String, now: Int64,
                      allowExpired: Bool = false) -> AppConfig? {
        guard let key, !payload.isEmpty, payload.utf8.count <= 16 * 1024,
              let raw = Data(base64Encoded: signature), raw.count == 64,
              let sig = try? P256.Signing.ECDSASignature(rawRepresentation: raw),
              key.isValidSignature(sig, for: Data(payload.utf8)) else { return nil }
        guard let object = try? JSONSerialization.jsonObject(with: Data(payload.utf8)) as? [String: Any],
              (object["v"] as? NSNumber)?.intValue == 1,
              let p = object["platform"] as? String, p == platform,
              let issued = (object["issuedAt"] as? NSNumber)?.int64Value,
              let expires = (object["expiresAt"] as? NSNumber)?.int64Value,
              let minVersion = (object["minVersionCode"] as? NSNumber)?.int64Value,
              let modeText = (object["hyper"] as? [String: Any])?["mode"] as? String,
              let mode = HyperMode(rawValue: modeText) else { return nil }
        guard expires > issued, expires - issued <= maxLifetime, issued - now <= clockSkew else { return nil }
        if !allowExpired && now >= expires { return nil }
        var messages: [String: String] = [:]
        for (k, v) in (object["message"] as? [String: Any]) ?? [:] {
            if let text = v as? String, !text.trimmingCharacters(in: .whitespaces).isEmpty { messages[k] = text }
        }
        return AppConfig(platform: p, minVersionCode: minVersion,
                         latestVersionCode: (object["latestVersionCode"] as? NSNumber)?.int64Value ?? 0,
                         updateUrl: object["updateUrl"] as? String ?? "", hyperMode: mode, message: messages,
                         issuedAt: issued, expiresAt: expires, payload: payload, signature: signature)
    }
}

enum AppConfigPolicy {
    static let freeWindow: Int64 = 7 * 86400

    static func updateRequired(_ config: AppConfig?, versionCode: Int64) -> Bool {
        guard let config else { return false }
        return versionCode < config.minVersionCode
    }

    static func fresh(_ config: AppConfig?, now: Int64) -> Bool {
        guard let c = config else { return false }
        return now < c.expiresAt && now - c.issuedAt <= freeWindow && c.issuedAt - now <= AppConfigVerifier.clockSkew
    }

    static func accept(_ candidate: AppConfig, over cached: AppConfig?) -> Bool {
        guard let cached else { return true }
        return candidate.issuedAt >= cached.issuedAt
    }

    static func hyperAccess(_ config: AppConfig?, entitlement: HyperAccess, now: Int64, fetching: Bool) -> HyperAccess {
        guard fresh(config, now: now), let c = config else { return fetching ? .checking : .needsConfig }
        switch c.hyperMode {
        case .free: return .unlocked
        case .entitled: return entitlement
        case .off: return .off
        }
    }
}

@MainActor
final class AppConfigStore: ObservableObject {
    static let shared = AppConfigStore()
    static let minInterval: TimeInterval = 30 * 60
    private static let payloadKey = "aurea.appconfig.payload"
    private static let signatureKey = "aurea.appconfig.signature"
    private static let dismissedKey = "aurea.appconfig.dismissed"

    @Published private(set) var config: AppConfig?
    @Published private(set) var fetching = false
    @Published private(set) var dismissedMessage: String
    private var lastAttempt: Date?
    private let key = AppConfigVerifier.publicKey(AppConfigKeys.publicKeySPKI)

    static var versionCode: Int64 { Int64(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "") ?? 0 }
    static func now() -> Int64 { Int64(Date().timeIntervalSince1970) }

    var updateRequired: Bool { AppConfigPolicy.updateRequired(config, versionCode: Self.versionCode) }

    private init() {
        let d = UserDefaults.standard
        dismissedMessage = d.string(forKey: Self.dismissedKey) ?? ""
        if let p = d.string(forKey: Self.payloadKey), let s = d.string(forKey: Self.signatureKey) {
            // O cache é reverificado: adulterar o arquivo só faz a config sumir.
            config = AppConfigVerifier.parse(payload: p, signature: s, key: key, platform: AppConfigKeys.platform,
                                             now: Self.now(), allowExpired: true)
        }
    }

    /// Abertura (`force`) e volta ao app (no máximo a cada 30 min).
    func refresh(force: Bool = false) {
        if !force, let last = lastAttempt, Date().timeIntervalSince(last) < Self.minInterval { return }
        Task { @MainActor in await fetch() }
    }

    /// Antes de um job Hyper: consulta agora e devolve se a config está em dia.
    func refreshNow() async -> Bool {
        await fetch()
        return AppConfigPolicy.fresh(config, now: Self.now())
    }

    private func fetch() async {
        guard !fetching else { return }
        lastAttempt = Date()
        fetching = true
        HyperEntitlements.shared.recheck()
        defer {
            fetching = false
            HyperEntitlements.shared.recheck()
        }
        let r = await ContaAPI.chamar("/api/app-config?platform=\(AppConfigKeys.platform)&version=\(Self.versionCode)",
                                      metodo: "GET", corpo: nil, token: nil)
        guard r.ok, let payload = r.corpo["payload"] as? String, let signature = r.corpo["signature"] as? String,
              let fresh = AppConfigVerifier.parse(payload: payload, signature: signature, key: key,
                                                  platform: AppConfigKeys.platform, now: Self.now()),
              AppConfigPolicy.accept(fresh, over: config) else { return }
        config = fresh
        UserDefaults.standard.set(payload, forKey: Self.payloadKey)
        UserDefaults.standard.set(signature, forKey: Self.signatureKey)
    }

    func dismissMessage(_ text: String) {
        dismissedMessage = text
        UserDefaults.standard.set(text, forKey: Self.dismissedKey)
    }
}

// MARK: - Telas

/// Cobre o app inteiro: Home e editor não aparecem por trás. Os projetos ficam no aparelho.
struct ForcedUpdateView: View {
    @EnvironmentObject private var model: AureaModel
    let config: AppConfig

    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                Spacer().frame(height: 64)
                Image(systemName: "arrow.down.circle").font(.system(size: 30, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.accent)
                    .frame(width: 64, height: 64).background(RoundedRectangle(cornerRadius: 18).fill(AureaColors.accent.opacity(0.18)))
                    .accessibilityHidden(true)
                Spacer().frame(height: 20)
                Text(AureaText.t("app_update_title")).font(.aurea(size: 22, weight: .bold)).multilineTextAlignment(.center)
                    .accessibilityAddTraits(.isHeader)
                Spacer().frame(height: 12)
                Text(AureaText.t("app_update_body")).font(.aurea(size: 15)).multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                if let message = config.message(for: model.language) {
                    Spacer().frame(height: 12)
                    Text(message).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer().frame(height: 10)
                Text(AureaText.t("app_update_projects_safe")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    .multilineTextAlignment(.center)
                Spacer().frame(height: 28)
                if let url = URL(string: config.updateUrl), config.updateUrl.hasPrefix("https://") {
                    HyperButton(title: AureaText.t("app_update_button"), id: "atualizacao.abrir") { UIApplication.shared.open(url) }
                } else {
                    // Sem link na config (IPA instalada à mão): diz onde procurar.
                    Text(AureaText.t("app_update_no_link")).font(.aurea(size: 14, weight: .semibold)).multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer().frame(height: 48)
            }
            .padding(.horizontal, 24)
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("atualizacao.obrigatoria")
    }
}

/// O aviso opcional da config na Home; fechar vale até o texto mudar.
struct AppConfigBanner: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var store = AppConfigStore.shared

    var body: some View {
        if let text = store.config?.message(for: model.language), store.dismissedMessage != text {
            HStack(spacing: 10) {
                Image(systemName: "info.circle").fontWeight(AureaIconWeight.symbol).foregroundStyle(AureaColors.accent).accessibilityHidden(true)
                Text(text).font(.aurea(size: 14)).fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
                Button { store.dismissMessage(text) } label: {
                    Image(systemName: "xmark").font(.system(size: 13, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.muted)
                        .frame(width: 44, height: 44)
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("app_notice_dismiss")).accessibilityIdentifier("aviso.config.fechar")
            }
            .padding(.leading, 14)
            .background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.surface))
            .padding(.horizontal, 16).padding(.vertical, 6)
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("aviso.config")
        }
    }
}
