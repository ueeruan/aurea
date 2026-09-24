// =============================================================================
//  Aurea iOS — Unity LevelPlay (SDK 9.6.0, API `LPM*`). O provedor ATIVO no
//  iOS, como no Android. Só este arquivo importa o SDK do LevelPlay.
//
//   - consentimento pelo UMP (TCF) antes de tudo; depois `LevelPlay.initWith`,
//     e os anúncios só são criados/carregados depois do init com sucesso;
//   - Rewarded: a recompensa vem SÓ de `didRewardAd`; `didCloseAd` é só fechar;
//   - App Open: não existe no LevelPlay — este backend não carrega App Open;
//   - a rede Unity Ads (Game ID/placements) é configurada no painel do
//     LevelPlay; o app só conhece o App Key e as unidades.
// =============================================================================
import UIKit
import IronSource

final class LevelPlayAdsBackend: NSObject, AdsBackend {
    private let appKey: String
    private let debug: Bool
    /// DEBUG: abre a Integration Test Suite oficial assim que o init terminar.
    private let openTestSuite: Bool

    private var interstitial: LPMInterstitialAd?
    private var rewardedUnit = ""
    private var rewardedAd: LPMRewardedAd?
    private var interLoad: (ok: () -> Void, fail: (String) -> Void)?
    private var rewardLoad: (ok: () -> Void, fail: (String) -> Void)?
    private var interShow: ShowCallbacks?
    private var rewardShow: ShowCallbacks?

    private struct ShowCallbacks {
        let opened: () -> Void, dismissed: () -> Void, failed: (String) -> Void, rewarded: () -> Void
    }

    init(appKey: String, debug: Bool, openTestSuite: Bool = false) {
        self.appKey = appKey; self.debug = debug; self.openTestSuite = openTestSuite
    }

    private func onMain(_ block: @escaping () -> Void) {
        if Thread.isMainThread { block() } else { DispatchQueue.main.async(execute: block) }
    }

    func initialize(from host: AnyObject, done: @escaping (Bool) -> Void) {
        guard let vc = host as? UIViewController else { done(false); return }
        var answered = false
        let finish: (Bool) -> Void = { [weak self] ok in self?.onMain { if !answered { answered = true; done(ok) } } }
        UmpConsent.request(from: vc) { [weak self] canRequest in
            guard let self else { return }
            guard canRequest else { finish(false); return }
            if self.debug {
                // Ferramenta oficial de teste do LevelPlay (Integration Test Suite).
                LevelPlay.setMetaDataWithKey("is_test_suite", value: "enable")
            }
            let request = LPMInitRequestBuilder(appKey: self.appKey).build()
            LevelPlay.initWith(request) { [weak self] _, error in
                guard let self else { return }
                if error != nil { finish(false); return }
                if self.debug && self.openTestSuite {
                    self.onMain { LevelPlay.launchTestSuite(vc) }
                }
                finish(true)
            }
        }
    }

    func load(_ kind: AdKind, unitId: String, loaded: @escaping () -> Void, failed: @escaping (String) -> Void) {
        switch kind {
        case .appOpen:
            onMain { failed("App Open não existe no LevelPlay") }
        case .exportInterstitial:
            if interstitial == nil {
                let a = LPMInterstitialAd(adUnitId: unitId)
                a.setDelegate(self)
                interstitial = a
            }
            guard let ad = interstitial else { return }
            interLoad = (loaded, failed)
            ad.loadAd()
        case .aiRewarded:
            if rewardedAd == nil {
                let a = LPMRewardedAd(adUnitId: unitId)
                rewardedUnit = unitId
                a.setDelegate(self)
                rewardedAd = a
            }
            guard let ad = rewardedAd else { return }
            rewardLoad = (loaded, failed)
            ad.loadAd()
        }
    }

    func show(_ kind: AdKind, from host: AnyObject, opened: @escaping () -> Void,
              dismissed: @escaping () -> Void, failed: @escaping (String) -> Void,
              rewarded: @escaping () -> Void) -> Bool {
        guard var vc = host as? UIViewController else { return false }
        // Por cima de qualquer folha aberta (ex.: a da exportação).
        while let top = vc.presentedViewController, !top.isBeingDismissed { vc = top }
        guard vc.viewIfLoaded?.window != nil else { return false }
        let cb = ShowCallbacks(opened: opened, dismissed: dismissed, failed: failed, rewarded: rewarded)
        switch kind {
        case .exportInterstitial:
            guard let ad = interstitial, ad.isAdReady() else { return false }
            interShow = cb
            ad.showAd(viewController: vc, placementName: nil)
            return true
        case .aiRewarded:
            guard let ad = rewardedAd, ad.isAdReady() else { return false }
            rewardShow = cb
            ad.showAd(viewController: vc, placementName: nil)
            return true
        case .appOpen:
            return false
        }
    }

    /// O objeto do LevelPlay é reaproveitado (carregar de novo depois de mostrar); só os callbacks saem.
    func release(_ kind: AdKind) {
        switch kind {
        case .exportInterstitial: interLoad = nil
        case .aiRewarded: rewardLoad = nil
        case .appOpen: break
        }
    }

    fileprivate static func describe(_ error: Error) -> String {
        let e = error as NSError
        return "levelplay \(e.code): \(e.localizedDescription)"
    }
}

extension LevelPlayAdsBackend: LPMInterstitialAdDelegate {
    func didLoadAd(with adInfo: LPMAdInfo) {
        #if DEBUG
        print("[AUREA ADS] LevelPlay carregou \(adInfo.adUnitId) pela rede \(adInfo.adNetwork)")
        #endif
        onMain { [weak self] in
            // O mesmo delegate atende aos dois formatos: decide pelo ID da unidade.
            guard let self else { return }
            if adInfo.adUnitId == self.rewardedUnit {
                self.rewardLoad?.ok(); self.rewardLoad = nil
            } else {
                self.interLoad?.ok(); self.interLoad = nil
            }
        }
    }
    func didFailToLoadAd(withAdUnitId adUnitId: String, error: Error) {
        let msg = Self.describe(error)
        onMain { [weak self] in
            guard let self else { return }
            if adUnitId == self.rewardedUnit {
                self.rewardLoad?.fail(msg); self.rewardLoad = nil
            } else {
                self.interLoad?.fail(msg); self.interLoad = nil
            }
        }
    }
    func didDisplayAd(with adInfo: LPMAdInfo) {
        onMain { [weak self] in
            guard let self else { return }
            (adInfo.adUnitId == self.rewardedUnit ? self.rewardShow : self.interShow)?.opened()
        }
    }
    func didFailToDisplayAd(with adInfo: LPMAdInfo, error: Error) {
        let msg = Self.describe(error)
        onMain { [weak self] in
            guard let self else { return }
            if adInfo.adUnitId == self.rewardedUnit {
                self.rewardShow?.failed(msg); self.rewardShow = nil
            } else {
                self.interShow?.failed(msg); self.interShow = nil
            }
        }
    }
    /// Fechar NÃO é recompensa.
    func didCloseAd(with adInfo: LPMAdInfo) {
        onMain { [weak self] in
            guard let self else { return }
            if adInfo.adUnitId == self.rewardedUnit {
                self.rewardShow?.dismissed(); self.rewardShow = nil
            } else {
                self.interShow?.dismissed(); self.interShow = nil
            }
        }
    }
}

extension LevelPlayAdsBackend: LPMRewardedAdDelegate {
    /// A ÚNICA recompensa.
    func didRewardAd(with adInfo: LPMAdInfo, reward: LPMReward) {
        onMain { [weak self] in self?.rewardShow?.rewarded() }
    }
}
