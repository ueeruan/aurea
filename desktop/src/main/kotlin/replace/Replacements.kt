// Substitutos de PC dos arquivos do Android presos a SDKs móveis (ver a lista
// `androidOnly` em desktop/build.gradle.kts). Mesma API pública; o
// comportamento é o de um computador.
@file:Suppress("unused", "UNUSED_PARAMETER")

package com.aurea.aurea.ads

import android.app.Activity
import android.content.Context

/** O Aurea PC não tem anúncios: nenhum SDK de anúncio existe para Windows. */
class GoogleAdsBackend(context: Context) : AdsBackend {
    override fun initialize(host: Any, aoTerminar: (podePedir: Boolean) -> Unit) = aoTerminar(false)
    override fun loadAppOpen(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) = aoFalhar("pc")
    override fun loadInterstitial(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) = aoFalhar("pc")
    override fun loadRewarded(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) = aoFalhar("pc")
    override fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit, aoRecompensar: () -> Unit): Boolean = false
    override fun release(kind: AdKind) {}
    fun privacyOptionsRequired(): Boolean = false
    fun showPrivacyOptions(activity: Activity, aoTerminar: () -> Unit = {}) = aoTerminar()
}

class LevelPlayAdsBackend(context: Context, appKey: String, debug: Boolean, abrirTestSuite: Boolean = false) : AdsBackend by GoogleAdsBackend(context)

object UmpConsent {
    fun pedir(activity: Activity, aoDecidir: (podePedir: Boolean) -> Unit) = aoDecidir(false)
    fun opcoesDePrivacidadeExigidas(context: Context): Boolean = false
    fun mostrarOpcoesDePrivacidade(activity: Activity, aoTerminar: () -> Unit = {}) = aoTerminar()
}
