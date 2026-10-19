package com.aurea.aurea.state

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ProxyMode
import com.aurea.aurea.engine.ProxySummary
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

/**
 * "Otimizar vídeos pesados" (0.2.5): o lado da UI do proxy de edição.
 *
 * O motor decide o que otimizar, gera em segundo plano (um por vez, parado
 * durante o play) e troca a fonte do preview sozinho quando a versão leve fica
 * pronta. Aqui mora só: o modo escolhido (gravado no aparelho e mandado ao
 * motor na abertura), o resumo da fila para o selo "Otimizando 42%" e o
 * tamanho/limpeza do cache. Par do ProxyOptimizer.swift (iOS).
 *
 * Nada aqui roda JNI na main thread: o motor trava o modelo para trocar o modo
 * e a medição do cache percorre a pasta.
 */
class ProxyState(
    app: Application,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    /** Um vídeo acabou de ficar pronto (o EditorStore mostra o aviso). */
    private val onOptimized: () -> Unit = {},
    /** Um vídeo não pôde ser otimizado: a prévia segue no original. */
    private val onFailed: () -> Unit = {},
) {
    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    var mode by mutableStateOf(ProxyMode.of(prefs.getInt(KEY_MODE, ProxyMode.AUTOMATIC.code)))
        private set
    var summary by mutableStateOf(ProxySummary.EMPTY)
        private set
    /** Bytes da mídia otimizada em disco; -1 = ainda não medido. */
    var cacheBytes by mutableLongStateOf(-1L)
        private set

    private var tracking: Job? = null

    /** Logo depois do `initialize` (thread do motor): o modo gravado vale antes do primeiro projeto. */
    fun pushModeToEngine() = engine.setProxyMode(mode.code)

    /** Motor no ar: acompanha a fila e mede o cache. */
    fun startTracking() {
        if (tracking?.isActive == true) return
        refreshCacheSize()
        tracking = scope.launch {
            var lastReady = -1
            var lastFailed = -1
            var wasBusy = false
            var wasGenerating = false
            while (isActive) {
                val s = withContext(Dispatchers.Default) { engine.proxySummary() } ?: ProxySummary.EMPTY
                if (s != summary) summary = s
                // Um vídeo GERADO ficou pronto (o achado no disco ao reabrir não avisa):
                // a edição já usa a versão leve e o tamanho em disco mudou.
                if (lastReady >= 0 && s.ready > lastReady && wasGenerating) onOptimized()
                if (lastFailed >= 0 && s.failed > lastFailed) onFailed()
                if (wasBusy && !s.busy) refreshCacheSize()
                lastReady = s.ready
                lastFailed = s.failed
                wasBusy = s.busy
                wasGenerating = s.generating > 0
                delay(if (s.busy) BUSY_POLL_MS else IDLE_POLL_MS)
            }
        }
    }

    fun select(newMode: ProxyMode) {
        if (newMode == mode) return
        mode = newMode
        prefs.edit().putInt(KEY_MODE, newMode.code).apply()
        scope.launch(Dispatchers.Default) { engine.setProxyMode(newMode.code) }
    }

    fun refreshCacheSize() {
        scope.launch { cacheBytes = withContext(Dispatchers.IO) { engine.proxyCacheBytes() } }
    }

    /** Apaga a mídia otimizada; [onCleared] recebe o texto "12,3" (MB liberados). */
    fun clearCache(onCleared: (String) -> Unit) {
        scope.launch {
            val freed = withContext(Dispatchers.IO) { engine.clearProxyCache() }
            cacheBytes = withContext(Dispatchers.IO) { engine.proxyCacheBytes() }
            onCleared(megabytes(freed))
        }
    }

    companion object {
        private const val PREFS = "aurea_proxy"
        private const val KEY_MODE = "mode"
        private const val BUSY_POLL_MS = 700L
        private const val IDLE_POLL_MS = 2_000L

        /** "12,3" / "12.3": MB com uma casa, no formato do idioma do aparelho. */
        fun megabytes(bytes: Long): String = String.format(Locale.getDefault(), "%.1f", bytes.coerceAtLeast(0L) / (1024.0 * 1024.0))
    }
}
