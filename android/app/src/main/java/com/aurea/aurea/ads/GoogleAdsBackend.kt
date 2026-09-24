package com.aurea.aurea.ads

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.google.android.gms.ads.AdError
import com.google.android.gms.ads.AdRequest
import com.google.android.gms.ads.FullScreenContentCallback
import com.google.android.gms.ads.LoadAdError
import com.google.android.gms.ads.MobileAds
import com.google.android.gms.ads.appopen.AppOpenAd
import com.google.android.gms.ads.interstitial.InterstitialAd
import com.google.android.gms.ads.interstitial.InterstitialAdLoadCallback
import com.google.android.gms.ads.rewarded.RewardedAd
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform
import java.util.concurrent.atomic.AtomicBoolean

/**
 * O AdMob (Google Mobile Ads) + o consentimento oficial do Google (UMP).
 *
 * Só este arquivo importa o SDK. Regras:
 *  - carregar SEMPRE com o Context da aplicação (nenhuma Activity fica presa num
 *    anúncio carregado);
 *  - a Activity só entra no `show` e não é guardada;
 *  - a inicialização do SDK roda fora da thread principal (e o manifesto liga
 *    OPTIMIZE_INITIALIZATION / OPTIMIZE_AD_LOADING);
 *  - consentimento: nada é pedido antes de `canRequestAds()`. Nada de
 *    consentimento presumido.
 */
class GoogleAdsBackend(context: Context) : AdsBackend {

    private val app = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private var appOpen: AppOpenAd? = null
    private var interstitial: InterstitialAd? = null
    private var rewarded: RewardedAd? = null

    override fun initialize(host: Any, aoTerminar: (podePedir: Boolean) -> Unit) {
        val activity = host as? Activity ?: run { aoTerminar(false); return }
        val uma = AtomicBoolean(false)
        val terminar = { ok: Boolean -> if (uma.compareAndSet(false, true)) main.post { aoTerminar(ok) } }
        val consent: ConsentInformation = UserMessagingPlatform.getConsentInformation(app)
        val sdkIniciado = AtomicBoolean(false)
        val iniciarSdk = {
            if (!consent.canRequestAds()) {
                terminar(false)
            } else if (sdkIniciado.compareAndSet(false, true)) {
                // O initialize do SDK é pesado: fora da main (o app abre sem esperar por ele).
                Thread({
                    try {
                        MobileAds.initialize(app) { terminar(true) }
                    } catch (t: Throwable) {
                        terminar(false)
                    }
                }, "aurea-ads-init").start()
            }
        }
        consent.requestConsentInfoUpdate(activity, ConsentRequestParameters.Builder().build(), {
            // Mostra o formulário oficial SÓ se a região/usuário exigir; senão volta direto.
            UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) { _ -> iniciarSdk() }
        }, { _ ->
            // Falhou atualizar (offline): segue com o que já estava decidido antes.
            iniciarSdk()
        })
        // Sessões anteriores já decidiram: pode inicializar em paralelo (recomendação do Google).
        if (consent.canRequestAds()) iniciarSdk()
    }

    override fun loadAppOpen(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        AppOpenAd.load(app, unitId, AdRequest.Builder().build(), object : AppOpenAd.AppOpenAdLoadCallback() {
            override fun onAdLoaded(ad: AppOpenAd) { appOpen = ad; aoCarregar() }
            override fun onAdFailedToLoad(e: LoadAdError) { appOpen = null; aoFalhar("load ${e.code}: ${e.message}") }
        })
    }

    override fun loadInterstitial(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        InterstitialAd.load(app, unitId, AdRequest.Builder().build(), object : InterstitialAdLoadCallback() {
            override fun onAdLoaded(ad: InterstitialAd) { interstitial = ad; aoCarregar() }
            override fun onAdFailedToLoad(e: LoadAdError) { interstitial = null; aoFalhar("load ${e.code}: ${e.message}") }
        })
    }

    override fun loadRewarded(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        RewardedAd.load(app, unitId, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
            override fun onAdLoaded(ad: RewardedAd) { rewarded = ad; aoCarregar() }
            override fun onAdFailedToLoad(e: LoadAdError) { rewarded = null; aoFalhar("load ${e.code}: ${e.message}") }
        })
    }

    override fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit,
                      aoRecompensar: () -> Unit): Boolean {
        val activity = host as? Activity ?: return false
        if (activity.isFinishing || activity.isDestroyed) return false
        val cb = object : FullScreenContentCallback() {
            override fun onAdShowedFullScreenContent() = aoAbrir()
            override fun onAdDismissedFullScreenContent() = aoFechar()
            override fun onAdFailedToShowFullScreenContent(e: AdError) = aoFalhar("show ${e.code}: ${e.message}")
        }
        return when (kind) {
            AdKind.AppOpen -> appOpen?.let { it.fullScreenContentCallback = cb; it.show(activity); true } ?: false
            AdKind.ExportInterstitial -> interstitial?.let { it.fullScreenContentCallback = cb; it.show(activity); true } ?: false
            // A recompensa vem SÓ do OnUserEarnedRewardListener (onUserEarnedReward) do SDK.
            AdKind.AiRewarded -> rewarded?.let { ad ->
                ad.fullScreenContentCallback = cb
                ad.show(activity) { _ -> aoRecompensar() }
                true
            } ?: false
        }
    }

    override fun release(kind: AdKind) {
        when (kind) {
            AdKind.AppOpen -> appOpen = null
            AdKind.ExportInterstitial -> interstitial = null
            AdKind.AiRewarded -> rewarded = null
        }
    }

    /** O formulário de privacidade tem de ficar acessível quando a região exige (para um item de Ajustes). */
    fun privacyOptionsRequired(): Boolean = runCatching {
        UserMessagingPlatform.getConsentInformation(app).privacyOptionsRequirementStatus ==
            ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED
    }.getOrDefault(false)

    fun showPrivacyOptions(activity: Activity, aoTerminar: () -> Unit = {}) {
        runCatching { UserMessagingPlatform.showPrivacyOptionsForm(activity) { aoTerminar() } }
            .onFailure { aoTerminar() }
    }
}
