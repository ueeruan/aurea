package com.aurea.aurea.ads

import android.app.Activity
import android.content.pm.ApplicationInfo
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * A ligação do [AureaAdsManager] com o Android: o backend AdMob, o agendador na
 * thread principal e o log (só em build DEBUG; nunca com identificador do
 * usuário). Chamado pela MainActivity — nenhuma tela chama o SDK.
 */
object AureaAds {
    private val main = Handler(Looper.getMainLooper())

    fun initialize(activity: Activity) {
        try {
            val debug = (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
            AureaAdsManager.log = if (debug) { m -> Log.i("AureaAds", m) } else { _ -> }
            AureaAdsManager.agendar = { ms, bloco -> main.postDelayed({ bloco() }, ms) }
            AureaAdsManager.initialize(
                host = activity,
                backend = GoogleAdsBackend(activity),
                frequency = AdsFrequencyController(PrefsAdsStore(activity)),
                ids = AdsConfig.ids(activity),
            )
        } catch (t: Throwable) {
            // SDK ausente/quebrado: o Aurea segue sem anúncio.
            runCatching { Log.w("AureaAds", "[AUREA ADS] Ad error: ${t.javaClass.simpleName}: ${t.message}") }
        }
    }
}
