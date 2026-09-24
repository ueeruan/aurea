package com.aurea.aurea.ads

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.unity3d.mediation.LevelPlay
import com.unity3d.mediation.LevelPlayAdError
import com.unity3d.mediation.LevelPlayAdInfo
import com.unity3d.mediation.LevelPlayConfiguration
import com.unity3d.mediation.LevelPlayInitError
import com.unity3d.mediation.LevelPlayInitListener
import com.unity3d.mediation.LevelPlayInitRequest
import com.unity3d.mediation.interstitial.LevelPlayInterstitialAd
import com.unity3d.mediation.interstitial.LevelPlayInterstitialAdListener
import com.unity3d.mediation.rewarded.LevelPlayReward
import com.unity3d.mediation.rewarded.LevelPlayRewardedAd
import com.unity3d.mediation.rewarded.LevelPlayRewardedAdListener
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Unity LevelPlay (API atual, `com.unity3d.mediation`) — o provedor ativo no
 * Android. Só este arquivo importa o SDK do LevelPlay.
 *
 *  - consentimento pelo UMP (TCF) antes de tudo; depois `LevelPlay.init`, e os
 *    anúncios só são criados/carregados depois de `onInitSuccess`;
 *  - Rewarded: a recompensa vem SÓ de `onAdRewarded`; `onAdClosed` é só fechar;
 *  - App Open: não existe no LevelPlay — este backend não carrega App Open;
 *  - os callbacks do SDK voltam para a thread principal (o manager é single-thread).
 */
class LevelPlayAdsBackend(
    context: Context,
    private val appKey: String,
    private val debug: Boolean,
    /** DEBUG: abre a Test Suite oficial do LevelPlay assim que o init terminar. */
    private val abrirTestSuite: Boolean = false,
) : AdsBackend {

    private val app = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private fun naMain(bloco: () -> Unit) = main.post { runCatching(bloco) }

    private var interstitial: LevelPlayInterstitialAd? = null
    private var rewarded: LevelPlayRewardedAd? = null

    // Callbacks do carregamento/exibição em curso (um de cada por vez).
    private var interLoad: Pair<() -> Unit, (String) -> Unit>? = null
    private var rewardLoad: Pair<() -> Unit, (String) -> Unit>? = null
    private var interShow: ShowCallbacks? = null
    private var rewardShow: ShowCallbacks? = null

    private class ShowCallbacks(
        val aoAbrir: () -> Unit, val aoFechar: () -> Unit, val aoFalhar: (String) -> Unit, val aoRecompensar: () -> Unit,
    )

    override fun initialize(host: Any, aoTerminar: (podePedir: Boolean) -> Unit) {
        val activity = host as? Activity ?: run { aoTerminar(false); return }
        val uma = AtomicBoolean(false)
        val terminar = { ok: Boolean -> if (uma.compareAndSet(false, true)) naMain { aoTerminar(ok) } }
        UmpConsent.pedir(activity) { podePedir ->
            if (!podePedir) { terminar(false); return@pedir }
            try {
                if (debug) {
                    LevelPlay.setAdaptersDebug(true)
                    // Ferramenta oficial de teste do LevelPlay (Integration Test Suite).
                    LevelPlay.setMetaData("is_test_suite", "enable")
                }
                val pedido = LevelPlayInitRequest.Builder(appKey).build()
                LevelPlay.init(app, pedido, object : LevelPlayInitListener {
                    override fun onInitSuccess(configuration: LevelPlayConfiguration) {
                        if (debug && abrirTestSuite) naMain { runCatching { LevelPlay.launchTestSuite(app) } }
                        terminar(true)
                    }
                    override fun onInitFailed(error: LevelPlayInitError) {
                        terminar(false)
                    }
                })
            } catch (t: Throwable) {
                terminar(false)
            }
        }
    }

    override fun loadAppOpen(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        naMain { aoFalhar("App Open não existe no LevelPlay") }
    }

    override fun loadInterstitial(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        val ad = interstitial ?: LevelPlayInterstitialAd(unitId).also {
            it.setListener(interListener)
            interstitial = it
        }
        interLoad = aoCarregar to aoFalhar
        ad.loadAd()
    }

    override fun loadRewarded(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
        val ad = rewarded ?: LevelPlayRewardedAd(unitId).also {
            it.setListener(rewardListener)
            rewarded = it
        }
        rewardLoad = aoCarregar to aoFalhar
        ad.loadAd()
    }

    override fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit,
                      aoRecompensar: () -> Unit): Boolean {
        val activity = host as? Activity ?: return false
        if (activity.isFinishing || activity.isDestroyed) return false
        val cb = ShowCallbacks(aoAbrir, aoFechar, aoFalhar, aoRecompensar)
        return when (kind) {
            AdKind.ExportInterstitial -> {
                val ad = interstitial?.takeIf { it.isAdReady() } ?: return false
                interShow = cb
                ad.showAd(activity)
                true
            }
            AdKind.AiRewarded -> {
                val ad = rewarded?.takeIf { it.isAdReady() } ?: return false
                rewardShow = cb
                ad.showAd(activity)
                true
            }
            AdKind.AppOpen -> false
        }
    }

    /** Dynamic User ID: chega no callback S2S como [USER_ID] (precisa vir ANTES do show). */
    override fun setRewardUserId(id: String) {
        runCatching { LevelPlay.setDynamicUserId(id) }
    }

    /** O objeto do LevelPlay é reaproveitado (carregar de novo depois de mostrar); só os callbacks saem. */
    override fun release(kind: AdKind) {
        when (kind) {
            AdKind.ExportInterstitial -> interLoad = null
            AdKind.AiRewarded -> rewardLoad = null
            AdKind.AppOpen -> Unit
        }
    }

    private fun erro(e: LevelPlayAdError) = "levelplay ${e.getErrorCode()}: ${e.getErrorMessage()}"

    private val interListener = object : LevelPlayInterstitialAdListener {
        override fun onAdLoaded(adInfo: LevelPlayAdInfo) {
            naMain { interLoad?.first?.invoke(); interLoad = null }
        }
        override fun onAdLoadFailed(error: LevelPlayAdError) {
            naMain { interLoad?.second?.invoke(erro(error)); interLoad = null }
        }
        override fun onAdDisplayed(adInfo: LevelPlayAdInfo) {
            naMain { interShow?.aoAbrir?.invoke() }
        }
        override fun onAdDisplayFailed(error: LevelPlayAdError, adInfo: LevelPlayAdInfo) {
            naMain { interShow?.aoFalhar?.invoke(erro(error)); interShow = null }
        }
        override fun onAdClosed(adInfo: LevelPlayAdInfo) {
            naMain { interShow?.aoFechar?.invoke(); interShow = null }
        }
    }

    private val rewardListener = object : LevelPlayRewardedAdListener {
        override fun onAdLoaded(adInfo: LevelPlayAdInfo) {
            naMain { rewardLoad?.first?.invoke(); rewardLoad = null }
        }
        override fun onAdLoadFailed(error: LevelPlayAdError) {
            naMain { rewardLoad?.second?.invoke(erro(error)); rewardLoad = null }
        }
        override fun onAdDisplayed(adInfo: LevelPlayAdInfo) {
            naMain { rewardShow?.aoAbrir?.invoke() }
        }
        override fun onAdDisplayFailed(error: LevelPlayAdError, adInfo: LevelPlayAdInfo) {
            naMain { rewardShow?.aoFalhar?.invoke(erro(error)); rewardShow = null }
        }
        /** A ÚNICA recompensa. */
        override fun onAdRewarded(reward: LevelPlayReward, adInfo: LevelPlayAdInfo) {
            naMain { rewardShow?.aoRecompensar?.invoke() }
        }
        /** Fechar NÃO é recompensa. */
        override fun onAdClosed(adInfo: LevelPlayAdInfo) {
            naMain { rewardShow?.aoFechar?.invoke(); rewardShow = null }
        }
    }
}
