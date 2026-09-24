// =============================================================================
//  Aurea iOS — anúncios. Mesma arquitetura do Android
//  (android/app/src/main/java/com/aurea/aurea/ads/): política e frequência
//  aqui, o SDK só no backend (LevelPlayAdsBackend). REGRA: anúncio nunca quebra
//  nem segura função do Aurea — todo "mostrar" chama `continuar` exatamente uma vez.
// =============================================================================
import Foundation

enum AdKind { case appOpen, exportInterstitial, aiRewarded }

/// Os IDs do iOS. NUNCA os do Android (cada plataforma tem app e unidades próprias).
struct AdsIds {
    let appOpen: String
    let interstitial: String
    let aiRewarded: String

    /// Unity LevelPlay (iOS). App Open não existe no LevelPlay.
    static let levelPlayAppKey = "284ec147d"
    static let current = AdsIds(appOpen: "",
                                interstitial: "a26boa5s3ws0el0v",
                                aiRewarded: "mgykirb9g392nodz")
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
    var rewardedMaxAge: TimeInterval = 3600
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
        case .aiRewarded: break   // pedido pelo usuário: só conta como "tela cheia recente"
        case .exportInterstitial:
            let shows = ((d.array(forKey: "export_at") as? [TimeInterval]) ?? []) + [now]
            d.set(Array(shows.suffix(8)), forKey: "export_at")
        }
    }
}

/// O que o manager precisa de um provedor (o LevelPlayAdsBackend; falso nos testes).
protocol AdsBackend: AnyObject {
    func initialize(from host: AnyObject, done: @escaping (_ canRequestAds: Bool) -> Void)
    func load(_ kind: AdKind, unitId: String, loaded: @escaping () -> Void, failed: @escaping (String) -> Void)
    /// `rewarded` só vem do callback OFICIAL de recompensa do SDK; `dismissed` nunca recompensa.
    func show(_ kind: AdKind, from host: AnyObject, opened: @escaping () -> Void,
              dismissed: @escaping () -> Void, failed: @escaping (String) -> Void,
              rewarded: @escaping () -> Void) -> Bool
    func release(_ kind: AdKind)
    /// Quem será recompensado pelo PRÓXIMO Rewarded (verificação server-side:
    /// no LevelPlay, o Dynamic User ID que volta no callback assinado).
    func setRewardUserId(_ id: String)
}

extension AdsBackend {
    func setRewardUserId(_ id: String) {}
}

/// Nenhuma tela conhece o SDK: elas falam com este objeto. Tudo na main thread.
final class AureaAdsManager {
    static let shared = AureaAdsManager()

    private var backend: AdsBackend?
    private var frequency: AdsFrequencyController?
    private var ids = AdsIds(appOpen: "", interstitial: "", aiRewarded: "")
    private var canRequest = false
    private var appOpenAt: TimeInterval = 0
    private var interstitialAt: TimeInterval = 0
    private var rewardedAt: TimeInterval = 0
    private var loading: Set<String> = []
    private var rewardedWaiting: [(ok: () -> Void, fail: (String) -> Void)] = []
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
        self.host = host
        frequency.registerLaunch()
        backend.initialize(from: host) { [weak self] ok in
            guard let self else { return }
            self.canRequest = ok
            self.log("[AUREA ADS] SDK initialized (consentimento permite anúncios: \(ok))")
            if ok {
                if !frequency.isFirstUse { self.preloadAppOpen() }
                self.preloadExportInterstitial()
                self.preloadRewarded()
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
        let at: TimeInterval, maxAge: TimeInterval
        switch kind {
        case .appOpen: at = appOpenAt; maxAge = f.policy.appOpenMaxAge
        case .exportInterstitial: at = interstitialAt; maxAge = f.policy.interstitialMaxAge
        case .aiRewarded: at = rewardedAt; maxAge = f.policy.rewardedMaxAge
        }
        if at == 0 { return false }
        if now() - at > maxAge { consume(kind); return false }
        return true
    }

    private func consume(_ kind: AdKind) {
        switch kind {
        case .appOpen: appOpenAt = 0
        case .exportInterstitial: interstitialAt = 0
        case .aiRewarded: rewardedAt = 0
        }
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
        // Consumido: já pede o próximo (o load é assíncrono e não segura nada).
        preloadExportInterstitial()
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
        }, dismissed: { finish(nil) }, failed: { finish($0) }, rewarded: {})
        consume(kind)
        if !tried { finish("sem anúncio para mostrar"); return }
        DispatchQueue.main.asyncAfter(deadline: .now() + f.policy.showStartTimeout) { if !opened { finish("não abriu") } }
        DispatchQueue.main.asyncAfter(deadline: .now() + f.policy.showHardCap) { finish("sem aviso de fechamento") }
    }

    // MARK: Rewarded da geração por IA (AI Video)

    /// Há um Rewarded carregado e dentro da validade, pronto para aparecer agora.
    func rewardedReady() -> Bool { canRequest && isValid(.aiRewarded) }

    /// Amarra o próximo Rewarded ao ticket da geração: o callback server-to-server
    /// do provedor devolve esse id ASSINADO, e é ele que libera a geração paga.
    func setRewardUserId(_ id: String) { backend?.setRewardUserId(id) }

    /// Carrega o Rewarded (na abertura e depois de consumido). `loaded`/`failed`:
    /// no máximo um dos dois, uma vez.
    func preloadRewarded(loaded: (() -> Void)? = nil, failed: ((String) -> Void)? = nil) {
        var answered = false
        let ok = { if !answered { answered = true; loaded?() } }
        let fail = { (e: String) in if !answered { answered = true; failed?(e) } }
        if isValid(.aiRewarded) { ok(); return }
        guard let b = backend else { fail("anúncios não iniciados"); return }
        guard canRequest else { fail("sem consentimento/SDK"); return }
        guard !ids.aiRewarded.isEmpty else { fail("sem ID de rewarded"); return }
        rewardedWaiting.append((ok, fail))
        guard !loading.contains("rw") else { return }
        loading.insert("rw")
        b.load(.aiRewarded, unitId: ids.aiRewarded, loaded: { [weak self] in
            guard let self else { return }
            self.loading.remove("rw")
            self.rewardedAt = self.now()
            self.log("[AUREA ADS] Rewarded loaded")
            self.notifyRewardedWaiting(nil)
        }, failed: { [weak self] e in
            guard let self else { return }
            self.loading.remove("rw")
            self.onAdFailed(.aiRewarded, e)
            self.notifyRewardedWaiting(e)
        })
    }

    private func notifyRewardedWaiting(_ error: String?) {
        let list = rewardedWaiting
        rewardedWaiting.removeAll()
        list.forEach { error == nil ? $0.ok() : $0.fail(error!) }
    }

    /// Mostra o Rewarded. A RECOMPENSA só existe por `reward`, que vem do callback
    /// oficial do SDK (didRewardAd). `closed(earned)` é só o aviso de que a tela
    /// fechou — nunca concede nada. Devolve `false` (e chama `failed`) se não havia
    /// anúncio para mostrar.
    @discardableResult
    func showRewarded(opened: @escaping () -> Void, reward: @escaping () -> Void,
                      closed: @escaping (_ earned: Bool) -> Void, failed: @escaping (String) -> Void) -> Bool {
        let reason: String? = backend == nil || frequency == nil ? "anúncios não iniciados"
            : host == nil ? "tela não visível" : !canRequest ? "sem consentimento/SDK"
            : fullscreenOpen ? "outro anúncio na tela" : !isValid(.aiRewarded) ? "rewarded não carregado" : nil
        guard reason == nil, let b = backend, let f = frequency, let h = host else {
            unavailable("Rewarded", reason ?? "?"); failed(reason ?? "anúncio indisponível"); return false
        }
        var didOpen = false, earned = false, finished = false
        let finish: (String?) -> Void = { [weak self] err in
            guard !finished else { return }
            finished = true
            self?.fullscreenOpen = false
            if let err {
                self?.onAdFailed(.aiRewarded, err)
                if didOpen { closed(earned) } else { failed(err) }
            } else {
                self?.log("[AUREA ADS] Rewarded dismissed")
                closed(earned)
            }
            self?.preloadRewarded()   // consumido: já pede o próximo
        }
        fullscreenOpen = true
        let tried = b.show(.aiRewarded, from: h, opened: { [weak self] in
            guard !didOpen else { return }
            didOpen = true
            f.recordShown(.aiRewarded, now: self?.now() ?? 0)
            self?.log("[AUREA ADS] Rewarded shown")
            opened()
        }, dismissed: { finish(nil) }, failed: { finish($0) }, rewarded: { [weak self] in
            guard !earned else { return }
            earned = true
            self?.log("[AUREA ADS] Reward earned")
            reward()
        })
        consume(.aiRewarded)
        if !tried {
            fullscreenOpen = false
            unavailable("Rewarded", "sem anúncio para mostrar"); failed("sem anúncio para mostrar")
            preloadRewarded()
            return false
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + f.policy.showStartTimeout) { if !didOpen { finish("não abriu") } }
        return true
    }

    private func unavailable(_ what: String, _ why: String) {
        log("[AUREA ADS] Ad unavailable - continuing normally (\(what): \(why))")
    }

    func onAdFailed(_ kind: AdKind, _ error: String) {
        let name: String
        switch kind {
        case .appOpen: name = "AppOpen"
        case .exportInterstitial: name = "Interstitial"
        case .aiRewarded: name = "Rewarded"
        }
        log("[AUREA ADS] Ad error: \(name): \(error)")
    }
}
