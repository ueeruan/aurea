package com.aurea.aurea.ads

import android.content.Context
import com.aurea.aurea.R

/**
 * Os IDs de anúncio, por tipo de build — o ÚNICO lugar que conhece um ID.
 *
 * Vêm de recursos, não de código:
 *  - `src/debug/res/values/ads_config.xml`: SÓ os IDs de TESTE oficiais do Google
 *    (publisher `ca-app-pub-3940256099942544`). Desenvolvimento nunca pede anúncio real.
 *  - `src/main/res/values/ads_config.xml` (release): os IDs reais, a configurar.
 *    Vazios = nenhum anúncio é pedido naquele formato.
 *
 * O App ID (`admob_app_id`) vai no manifesto pelo mesmo recurso.
 */
data class AdsIds(val appOpen: String, val interstitial: String) {
    val appOpenEnabled: Boolean get() = appOpen.isNotBlank()
    val interstitialEnabled: Boolean get() = interstitial.isNotBlank()
}

object AdsConfig {
    /** Publisher dos anúncios de TESTE oficiais do Google. */
    const val TEST_PUBLISHER = "ca-app-pub-3940256099942544"
    const val TEST_APP_ID = "ca-app-pub-3940256099942544~3347511713"
    const val TEST_APP_OPEN = "ca-app-pub-3940256099942544/9257395921"
    const val TEST_INTERSTITIAL = "ca-app-pub-3940256099942544/1033173712"

    fun ids(context: Context): AdsIds = AdsIds(
        appOpen = context.getString(R.string.admob_app_open_unit).trim(),
        interstitial = context.getString(R.string.admob_export_interstitial_unit).trim(),
    )

    /** `true` se o ID é de teste (vale para app, App Open e interstitial). */
    fun isTestId(id: String): Boolean = id.startsWith(TEST_PUBLISHER)
}

/**
 * Tudo o que regula a frequência, num lugar só: ajustar aqui não mexe na
 * arquitetura. Tempos em milissegundos.
 */
data class AdsPolicy(
    /** App Open só a partir desta abertura do app (1 = primeira: nunca). */
    val appOpenFromLaunch: Int = 2,
    /** Tempo mínimo em segundo plano para um App Open na volta (seletor de fotos não conta). */
    val minBackgroundForAppOpenMs: Long = 30_000L,
    /** Entre dois App Open. */
    val appOpenCooldownMs: Long = 30 * 60_000L,
    /** Entre QUALQUER dois anúncios de tela cheia (App Open e interstitial juntos). */
    val fullscreenGapMs: Long = 3 * 60_000L,
    /** Máximo de interstitials de exportação na janela abaixo. */
    val exportMaxPerWindow: Int = 1,
    val exportWindowMs: Long = 30 * 60_000L,
    /** Validade de um App Open carregado (recomendação do Google: 4 h). */
    val appOpenMaxAgeMs: Long = 4 * 60 * 60_000L,
    /** Validade de um interstitial carregado. */
    val interstitialMaxAgeMs: Long = 60 * 60_000L,
    /** Se o SDK não confirmar que o anúncio abriu neste prazo, o fluxo segue. */
    val showStartTimeoutMs: Long = 5_000L,
    /** Teto absoluto de espera por um anúncio aberto: depois disso o fluxo segue de qualquer jeito. */
    val showHardCapMs: Long = 3 * 60_000L,
)
