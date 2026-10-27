package com.aurea.aurea.mocap

import android.content.Context
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.state.EditorStore
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Motion Capture (0.3.6 BETA): o fluxo do painel 3D — escolher/gravar o vídeo,
 * analisar (detecção no aparelho + solver do motor, progresso real e
 * cancelar), pré-visualizar no preview sem gravar keys, ajustar intensidade,
 * suavização, velocidade e alinhamento, e aplicar à timeline (uma ação de
 * desfazer). Todas as regras estão no motor (Engine::mocap_*); isto só
 * conduz. Par do iOS: MocapFlow em MocapPanel.swift.
 */
class MocapFlow(
    private val store: EditorStore,
    private val layer: Long,
    private val scope: CoroutineScope,
    context: Context,
) {
    enum class Step { NeedRig, Choose, Analyzing, Ready }

    private val engine get() = store.mocapEngine
    private val detector = MocapDetector(context.applicationContext)

    var status by mutableIntStateOf(engine.mocapBegin(layer))
        private set
    var step by mutableStateOf(if (status >= 2) Step.Choose else Step.NeedRig)
        private set
    var progress by mutableFloatStateOf(0f)
        private set
    /** 0 nenhum · 1 sem pessoa no vídeo · 2 vídeo ilegível · 3 falha ao gravar as keys */
    var error by mutableIntStateOf(0)
    var report by mutableStateOf<FloatArray?>(null)
        private set
    var previewOn by mutableStateOf(false)
        private set
    var applied by mutableIntStateOf(-1)
        private set

    var intensity by mutableFloatStateOf(1f)
    var smoothing by mutableFloatStateOf(0.5f)
    var speed by mutableFloatStateOf(1f)
    var yaw by mutableFloatStateOf(0f)
    var floor by mutableFloatStateOf(0f)
    var footLock by mutableStateOf(true)
    /** Modo reduzido (celulares intermediários): 256 px e metade dos quadros. */
    var reduced by mutableStateOf(false)

    private var job: Job? = null
    private var tuneJob: Job? = null

    private fun params() = floatArrayOf(intensity, smoothing, speed, yaw, floor, if (footLock) 1f else 0f, 1f, 1f)

    /** Depois do AutoRig 3D: o rig apareceu? */
    fun recheckRig() {
        if (step != Step.NeedRig) return
        status = engine.mocapBegin(layer)
        if (status >= 2) step = Step.Choose
    }

    fun analyze(uri: Uri) {
        if (status < 2) return
        cancel()
        error = 0
        applied = -1
        report = null
        previewOn = false
        step = Step.Analyzing
        progress = 0f
        job = scope.launch {
            try {
                // Sessão nova (os quadros de antes saem; a prévia também).
                status = engine.mocapBegin(layer)
                val frames = detector.run(uri, reduced,
                    onProgress = { p -> withContext(Dispatchers.Main) { progress = 0.85f * p } },
                    sink = { data, n, aspect -> engine.mocapPushFrames(data, n, aspect) })
                if (frames < 2) { error = 1; step = Step.Choose; return@launch }
                val poll = launch {
                    while (isActive) {
                        progress = 0.85f + 0.15f * engine.mocapProgress()
                        delay(100)
                    }
                }
                val ok = withContext(Dispatchers.Default) { engine.mocapAnalyze(params()) }
                poll.cancel()
                if (!ok) { error = 1; step = Step.Choose; return@launch }
                report = engine.mocapReport()
                progress = 1f
                step = Step.Ready
                setPreview(true)
            } catch (e: CancellationException) {
                throw e   // cancel() já voltou a tela e avisou o motor
            } catch (e: Exception) {
                error = 2
                step = Step.Choose
            }
        }
    }

    fun cancel() {
        job?.cancel()
        job = null
        engine.mocapCancel()
        if (step == Step.Analyzing) step = Step.Choose
    }

    fun setPreview(on: Boolean) {
        previewOn = engine.mocapPreview(on) && on
        store.afterMocap()
    }

    /** Ajuste mudou: resolve de novo (rápido) e a prévia acompanha. */
    fun retune() {
        if (step != Step.Ready) return
        tuneJob?.cancel()
        tuneJob = scope.launch {
            delay(120)
            val ok = withContext(Dispatchers.Default) { engine.mocapAnalyze(params()) }
            if (ok) {
                report = engine.mocapReport()
                store.afterMocap()
            }
        }
    }

    fun apply() {
        if (step != Step.Ready) return
        tuneJob?.cancel()
        scope.launch {
            val n = withContext(Dispatchers.Default) { engine.mocapApply() }
            previewOn = false
            applied = n
            if (n < 0) error = 3
            store.afterMocap()
        }
    }

    /** Saiu do painel / trocou de camada: cancela e tira a prévia. */
    fun dispose() {
        job?.cancel()
        tuneJob?.cancel()
        engine.mocapCancel()
        engine.mocapEnd()
        store.afterMocap()
    }

    /** Membros incertos (índices 0 tronco … 5 perna dir.) acima de 25% dos quadros. */
    fun uncertainLimbs(): List<Int> {
        val r = report ?: return emptyList()
        return (0 until 6).filter { r[4 + it] > 0.25f }
    }
}
