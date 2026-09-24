package com.aurea.aurea.ads

import android.content.Context

/** Onde a frequência fica guardada entre aberturas do app. */
interface AdsStore {
    fun getLong(key: String, def: Long): Long
    fun putLong(key: String, value: Long)
}

class PrefsAdsStore(context: Context) : AdsStore {
    private val prefs = context.applicationContext.getSharedPreferences("aurea.ads", Context.MODE_PRIVATE)
    override fun getLong(key: String, def: Long): Long = prefs.getLong(key, def)
    override fun putLong(key: String, value: Long) { prefs.edit().putLong(key, value).apply() }
}

class MemoryAdsStore : AdsStore {
    private val m = HashMap<String, Long>()
    override fun getLong(key: String, def: Long): Long = m[key] ?: def
    override fun putLong(key: String, value: Long) { m[key] = value }
}

enum class AdKind { AppOpen, ExportInterstitial, AiRewarded }

/**
 * Quem decide SE um anúncio pode aparecer — nunca COMO. Persistente: o
 * limite vale entre aberturas do app. Sem SDK nem Android aqui: testável na JVM.
 */
class AdsFrequencyController(
    private val store: AdsStore,
    val policy: AdsPolicy = AdsPolicy(),
) {
    /** Aberturas do app (frias). 1 = primeira utilização. */
    val launchCount: Long get() = store.getLong(K_LAUNCHES, 0L)

    /** Uma abertura fria do app. Chamar uma vez por processo. */
    fun registerLaunch() = store.putLong(K_LAUNCHES, launchCount + 1)

    val isFirstUse: Boolean get() = launchCount < policy.appOpenFromLaunch

    /** Motivo pelo qual um App Open não pode aparecer agora (null = pode). */
    fun appOpenBlock(now: Long, inEditorOrBusy: Boolean): String? = when {
        isFirstUse -> "primeira utilização"
        inEditorOrBusy -> "usuário trabalhando"
        now - store.getLong(K_LAST_APP_OPEN, Long.MIN_VALUE / 2) < policy.appOpenCooldownMs -> "App Open recente"
        now - lastFullscreen() < policy.fullscreenGapMs -> "outro anúncio acabou de aparecer"
        else -> null
    }

    /** Motivo pelo qual o interstitial da exportação não pode aparecer (null = pode). */
    fun exportBlock(now: Long): String? {
        if (now - lastFullscreen() < policy.fullscreenGapMs) return "outro anúncio acabou de aparecer"
        val janela = exportShows().count { now - it < policy.exportWindowMs }
        return if (janela >= policy.exportMaxPerWindow) "limite de exportação na janela" else null
    }

    /** Um anúncio de tela cheia apareceu. */
    fun recordShown(kind: AdKind, now: Long) {
        store.putLong(K_LAST_FULLSCREEN, now)
        when (kind) {
            AdKind.AppOpen -> store.putLong(K_LAST_APP_OPEN, now)
            AdKind.AiRewarded -> Unit   // pedido pelo usuário: só conta como "tela cheia recente"
            AdKind.ExportInterstitial -> {
                // Guarda os últimos instantes (até 8) para a janela deslizante.
                val lista = (exportShows() + now).takeLast(8)
                store.putLong(K_EXPORT_N, lista.size.toLong())
                lista.forEachIndexed { i, t -> store.putLong("$K_EXPORT_AT$i", t) }
            }
        }
    }

    private fun lastFullscreen(): Long = store.getLong(K_LAST_FULLSCREEN, Long.MIN_VALUE / 2)

    private fun exportShows(): List<Long> {
        val n = store.getLong(K_EXPORT_N, 0L).toInt().coerceIn(0, 8)
        return List(n) { store.getLong("$K_EXPORT_AT$it", 0L) }
    }

    private companion object {
        const val K_LAUNCHES = "launches"
        const val K_LAST_FULLSCREEN = "last_fullscreen"
        const val K_LAST_APP_OPEN = "last_app_open"
        const val K_EXPORT_N = "export_n"
        const val K_EXPORT_AT = "export_at_"
    }
}
