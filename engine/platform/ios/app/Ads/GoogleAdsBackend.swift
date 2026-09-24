// =============================================================================
//  Aurea iOS — o AdMob (Google Mobile Ads SDK 12.x) + consentimento oficial (UMP).
//  Só este arquivo importa o SDK. DESLIGADO: o provedor ativo no iOS é o
//  LevelPlay (fica fora do alvo do Xcode; volta depois como rede mediada).
// =============================================================================
import UIKit
import GoogleMobileAds
import UserMessagingPlatform

final class GoogleAdsBackend: NSObject, AdsBackend, FullScreenContentDelegate {
    private var appOpen: AppOpenAd?
    private var interstitial: InterstitialAd?
    private var callbacks: [ObjectIdentifier: (opened: () -> Void, dismissed: () -> Void, failed: (String) -> Void)] = [:]
    private var started = false

    func initialize(from host: AnyObject, done: @escaping (Bool) -> Void) {
        guard let vc = host as? UIViewController else { done(false); return }
        var answered = false
        let finish: (Bool) -> Void = { ok in if !answered { answered = true; DispatchQueue.main.async { done(ok) } } }
        let startSDK = { [weak self] in
            guard let self else { return }
            guard ConsentInformation.shared.canRequestAds else { finish(false); return }
            if self.started { return }
            self.started = true
            MobileAds.shared.start { _ in finish(true) }
        }
        // Fluxo oficial: atualiza o consentimento e mostra o formulário SÓ se exigido.
        ConsentInformation.shared.requestConsentInfoUpdate(with: RequestParameters()) { error in
            if error != nil { startSDK(); return }          // offline: vale o que já estava decidido
            ConsentForm.loadAndPresentIfRequired(from: vc) { _ in startSDK() }
        }
        if ConsentInformation.shared.canRequestAds { startSDK() }
    }

    func load(_ kind: AdKind, unitId: String, loaded: @escaping () -> Void, failed: @escaping (String) -> Void) {
        switch kind {
        case .appOpen:
            AppOpenAd.load(with: unitId, request: Request()) { [weak self] ad, error in
                if let ad { self?.appOpen = ad; loaded() } else { failed("load: \(error?.localizedDescription ?? "?")") }
            }
        case .exportInterstitial:
            InterstitialAd.load(with: unitId, request: Request()) { [weak self] ad, error in
                if let ad { self?.interstitial = ad; loaded() } else { failed("load: \(error?.localizedDescription ?? "?")") }
            }
        case .aiRewarded:
            failed("rewarded só pelo LevelPlay")
        }
    }

    func show(_ kind: AdKind, from host: AnyObject, opened: @escaping () -> Void,
              dismissed: @escaping () -> Void, failed: @escaping (String) -> Void,
              rewarded: @escaping () -> Void) -> Bool {
        guard let vc = host as? UIViewController else { return false }
        switch kind {
        case .appOpen:
            guard let ad = appOpen else { return false }
            callbacks[ObjectIdentifier(ad)] = (opened, dismissed, failed)
            ad.fullScreenContentDelegate = self
            ad.present(from: vc)
        case .exportInterstitial:
            guard let ad = interstitial else { return false }
            callbacks[ObjectIdentifier(ad)] = (opened, dismissed, failed)
            ad.fullScreenContentDelegate = self
            ad.present(from: vc)
        case .aiRewarded:
            return false
        }
        return true
    }

    func release(_ kind: AdKind) {
        switch kind {
        case .appOpen: appOpen = nil
        case .exportInterstitial: interstitial = nil
        case .aiRewarded: break
        }
    }

    // MARK: FullScreenContentDelegate
    func adWillPresentFullScreenContent(_ ad: FullScreenPresentingAd) {
        callbacks[ObjectIdentifier(ad)]?.opened()
    }
    func adDidDismissFullScreenContent(_ ad: FullScreenPresentingAd) {
        callbacks.removeValue(forKey: ObjectIdentifier(ad))?.dismissed()
    }
    func ad(_ ad: FullScreenPresentingAd, didFailToPresentFullScreenContentWithError error: Error) {
        callbacks.removeValue(forKey: ObjectIdentifier(ad))?.failed("show: \(error.localizedDescription)")
    }
}
