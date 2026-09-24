// =============================================================================
//  Aurea iOS — anúncios (AdMob). Mesma arquitetura do Android
//  (android/app/src/main/java/com/aurea/aurea/ads/): política e frequência
//  aqui, o SDK só no GoogleAdsBackend. REGRA: anúncio nunca quebra nem segura
//  função do Aurea — todo "mostrar" chama `continuar` exatamente uma vez.
//
//  NÃO COMPILADO AQUI (sem Mac). Ligação pendente — ver Ads/README.md.
// =============================================================================
import Foundation

enum AdKind { case appOpen, exportInterstitial }

/// Os IDs, por configuração de build. DEBUG = SÓ os de teste oficiais do Google.
struct AdsIds {
    let appOpen: String
    let interstitial: String

    static let testPublisher = "ca-app-pub-3940256099942544"

    static var current: AdsIds {
        #if DEBUG
        // IDs de TESTE do Google para iOS.
        return AdsIds(appOpen: "ca-app-pub-3940256099942544/5575463023",
                      interstitial: "ca-app-pub-3940256099942544/4411468910")
        #else
        // Release: IDs reais vêm do Info.plist (AureaAdAppOpenUnit / AureaAdInterstitialUnit).
        // Vazio = aquele formato não pede anúncio.
        let info = Bundle.main.infoDictionary ?? [:]
        return AdsIds(appOpen: (info["AureaAdAppOpenUnit"] as? String) ?? "",
                      interstitial: (info["AureaAdInterstitialUnit"] as? String) ?? "")
        #endif
    }
}

/// Todos os números de frequência num lugar só (os mesmos do Android).
struct AdsPolicy {
    var appOpenFromLaunch = 2
    var minBackgroundForAppOpen: TimeInterval = 30
    var appOpenCooldown: TimeInterval = 30 * 60
    var fullscreenGap: TimeInterval = 3 * 60
    var exportMaxPerWindow = 1
    var exportWindow: TimeInterval = 30 * 60
    var appOpenMaxAge: TimeInterval = 4 * 3600
    var interstitialMaxAge: TimeInterval = 3600
    var showStartTimeout: TimeInterval = 5
    var showHardCap: TimeInterval = 180
}

/// Persistente entre aberturas (UserDefaults, domínio próprio).
final class AdsFrequencyController {
    private let d: UserDefaults
    let policy: AdsPolicy
    init(defaults: UserDefaults = UserDefaults(suiteName: "aurea.ads") ?? .standard, policy: AdsPolicy = AdsPolicy()) {
        d = defaults; self.policy = policy
    }
    var launchCount: Int { d.integer(forKey: "launches") }
    func registerLaunch() { d.set(launchCount + 1, forKey: "launches") }
    var isFirstUse: Bool { launchCount < policy.appOpenFromLaunch }

    private func time(_ k: String) -> TimeInterval { d.object(forKey: k) as? TimeInterval ?? -.greatestFiniteMagnitude / 2 }

    func appOpenBlock(now: TimeInterval, working: Bool) -> String? {
        if isFirstUse { return "primeira utilização" }
        if working { return "usuário trabalhando" }
        if now - time("last_app_open") < policy.appOpenCooldown { return "App Open recente" }
        if now - time("last_fullscreen") < policy.fullscreenGap { return "outro anúncio acabou de aparecer" }
        return nil
    }

    func exportBlock(now: TimeInterval) -> String? {
        if now - time("last_fullscreen") < policy.fullscreenGap { return "outro anúncio acabou de aparecer" }
        let shows = (d.array(forKey: "export_at") as? [TimeInterval]) ?? []
        return shows.filter { now - $0 < policy.exportWindow }.count >= policy.exportMaxPerWindow ? "limite de exportação na janela" : nil
    }

    func recordShown(_ kind: AdKind, now: TimeInterval) {
        d.set(now, forKey: "last_fullscreen")
        switch kind {
        case .appOpen: d.set(now, forKey: "last_app_open")
        case .exportInterstitial:
            let shows = ((d.array(forKey: "export_at") as? [TimeInterval]) ?? []) + [now]
            d.set(Array(shows.suffix(8)), forKey: "export_at")
        }
    }
}

/// O que o manager precisa de um provedor (o GoogleAdsBackend; falso nos testes).
protocol AdsBackend: AnyObject {
    func initialize(from host: AnyObject, done: @escaping (_ canRequestAds: Bool) -> Void)
    func load(_ kind: AdKind, unitId: String, loaded: @escaping () -> Void, failed: @escaping (String) -> Void)
    func show(_ kind: AdKind, from host: AnyObject, opened: @escaping () -> Void,
              dismissed: @escaping () -> Void, failed: @escaping (String) -> Void) -> Bool
    func release(_ kind: AdKind)
}

/// Nenhuma tela conhece o SDK: elas falam com este objeto. Tudo na main thread.
final class AureaAdsManager {
    static let shared = AureaAdsManager()

    private var backend: AdsBackend?
    private var frequency: AdsFrequencyController?
    private var ids = AdsIds(appOpen: "", interstitial: "")
    private var canRequest = false
    private var appOpenAt: TimeInterval = 0
    private var interstitialAt: TimeInterval = 0
    private var loading: Set<String> = []
    private var fullscreenOpen = false
    private var coldStartResolved = false
    private var backgroundAt: TimeInterval = 0
    private weak var host: AnyObject?
    var now: () -> TimeInterval = { Date().timeIntervalSince1970 }

    private func log(_ m: String) {
        #if DEBUG
        print(m)
        #endif
    }

    func initialize(from host: AnyObject, backend: AdsBackend, frequency: AdsFrequencyController, ids: AdsIds) {
        guard self.backend == nil else { return }
        self.backend = backend; self.frequency = frequency; self.ids = ids
        frequency.registerLaunch()
        backend.initialize(from: host) { [weak self] ok in
            guard let self else { return }
            self.canRequest = ok
            self.log("[AUREA ADS] SDK initialized")
            if ok {
                if !frequency.isFirstUse { self.preloadAppOpen() }
                self.preloadExportInterstitial()
            }
        }
    }

    func attach(_ h: AnyObject) { host = h }
    func detach(_ h: AnyObject) { if host === h { host = nil } }

    func preloadAppOpen() { preload(.appOpen, unit: ids.appOpen) }
    func preloadExportInterstitial() { preload(.exportInterstitial, unit: ids.interstitial) }

    private func preload(_ kind: AdKind, unit: String) {
        guard let b = backend, canRequest, !unit.isEmpty, !isValid(kind) else { return }
        let key = kind == .appOpen ? "ao" : "in"
        guard !loading.contains(key) else { return }
        loading.insert(key)
        b.load(kind, unitId: unit, loaded: { [weak self] in
            guard let self else { return }
            self.loading.remove(key)
            if kind == .appOpen { self.appOpenAt = self.now(); self.log("[AUREA ADS] AppOpen loaded") }
            else { self.interstitialAt = self.now(); self.log("[AUREA ADS] Interstitial loaded") }
        }, failed: { [weak self] e in
            self?.loading.remove(key); self?.onAdFailed(kind, e)
        })
    }

    private func isValid(_ kind: AdKind) -> Bool {
        guard let f = frequency else { return false }
        let at = kind == .appOpen ? appOpenAt : interstitialAt
        let maxAge = kind == .appOpen ? f.policy.appOpenMaxAge : f.policy.interstitialMaxAge
        if at == 0 { return false }
        if now() - at > maxAge { consume(kind); return false }
        return true
    }

    private func consume(_ kind: AdKind) {
        if kind == .appOpen { appOpenAt = 0 } else { interstitialAt = 0 }
        backend?.release(kind)
    }

    /// Abertura fria: uma vez, quando o Aurea termina de carregar. Sem anúncio pronto, a Home segue.
    func onAppLoaded(host h: AnyObject, working: Bool) {
        guard !coldStartResolved else { return }
        coldStartResolved = true
        showAppOpenIfAvailable(from: h, working: working) {}
    }

    func onBackground() { backgroundAt = now(); preloadAppOpen() }

    func onForeground(host h: AnyObject, working: Bool) {
        guard coldStartResolved, backgroundAt > 0, let f = frequency else { return }
        let away = now() - backgroundAt
        backgroundAt = 0
        guard away >= f.policy.minBackgroundForAppOpen else { return }
        showAppOpenIfAvailable(from: h, working: working) {}
    }

    func showAppOpenIfAvailable(from h: AnyObject?, working: Bool, continue next: @escaping () -> Void) {
        guard let b = backend, let f = frequency, let h else { unavailable("AppOpen", "anúncios não iniciados"); next(); return }
        let block: String? = !canRequest ? "sem consentimento/SDK" : fullscreenOpen ? "outro anúncio na tela"
            : !isValid(.appOpen) ? "App Open não carregado" : f.appOpenBlock(now: now(), working: working)
        if let block { unavailable("AppOpen", block); next(); return }
        present(.appOpen, b, f, h, next)
    }

    /// Ponto seguro da exportação (vídeo já salvo). `next` sempre é chamado, uma vez.
    func showExportInterstitialIfAvailable(continue next: @escaping () -> Void) {
        guard let b = backend, let f = frequency, let h = host else { unavailable("Interstitial", "anúncios não iniciados/tela"); next(); return }
        let block: String? = !canRequest ? "sem consentimento/SDK" : fullscreenOpen ? "outro anúncio na tela"
            : !isValid(.exportInterstitial) ? "interstitial não carregado" : f.exportBlock(now: now())
        if let block { unavailable("Interstitial", block); next(); return }
        present(.exportInterstitial, b, f, h, next)
    }

    private func present(_ kind: AdKind, _ b: AdsBackend, _ f: AdsFrequencyController, _ h: AnyObject, _ next: @escaping () -> Void) {
        var opened = false, finished = false
        let name = kind == .appOpen ? "AppOpen" : "Interstitial"
        let finish: (String?) -> Void = { [weak self] err in
            guard !finished else { return }
            finished = true
            self?.fullscreenOpen = false
            if let err { self?.onAdFailed(kind, err) } else { self?.log("[AUREA ADS] \(name) dismissed") }
            next()
        }
        fullscreenOpen = true
        let tried = b.show(kind, from: h, opened: { [weak self] in
            opened = true
            f.recordShown(kind, now: self?.now() ?? 0)
            self?.log("[AUREA ADS] \(name) shown")
        }, dismissed: { finish(nil) }, failed: { finish($0) })
        consume(kind)
        if !tried { finish("sem anúncio para mostrar"); return }
        DispatchQueue.main.asyncAfter(deadline: .now() + f.policy.showStartTimeout) { if !opened { finish("não abriu") } }
        DispatchQueue.main.asyncAfter(deadline: .now() + f.policy.showHardCap) { finish("sem aviso de fechamento") }
    }

    private func unavailable(_ what: String, _ why: String) {
        log("[AUREA ADS] Ad unavailable - continuing normally (\(what): \(why))")
    }

    func onAdFailed(_ kind: AdKind, _ error: String) {
        log("[AUREA ADS] Ad error: \(kind == .appOpen ? "AppOpen" : "Interstitial"): \(error)")
    }
}
