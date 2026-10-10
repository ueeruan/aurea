package com.aurea.aurea.state

import android.view.Choreographer

/**
 * O laço de frame da UI.
 *
 * POR QUE `Choreographer` E NÃO UMA CORROTINA COM `delay`:
 *
 * `delay(16)` produz um laço a ~62 Hz que não tem relação com o display. Num
 * painel de 120 Hz, metade dos frames chegariam tarde e a outra metade cedo, e o
 * resultado é micro-engasgo que nenhum perfil mostra como pico. Pior: a cada
 * mudança de taxa (o usuário troca o modo de exibição, o sistema reduz para
 * economizar bateria) o laço continua no ritmo antigo.
 *
 * O `Choreographer` entrega o `frameTimeNanos` do vsync REAL e reagenda na
 * cadência atual. É a mesma fonte que o Compose usa — então o frame do motor e
 * o frame da interface caem no mesmo vsync, em vez de competirem.
 *
 * E o mais importante: o timestamp que ele dá é o do DISPLAY, não o do
 * relógio do sistema. É esse valor que o motor recebe como tempo de áudio
 * quando não há áudio tocando, e é o que mantém o preview estável.
 */
class RenderLoop internal constructor(
    private val onFrame: (frameTimeNanos: Long) -> Unit,
    private val scheduler: Scheduler,
) {
    constructor(onFrame: (frameTimeNanos: Long) -> Unit) : this(onFrame, VsyncScheduler())

    internal interface Scheduler {
        fun post(callback: Choreographer.FrameCallback, delayMs: Long)
        fun remove(callback: Choreographer.FrameCallback)
    }

    private class VsyncScheduler : Scheduler {
        override fun post(callback: Choreographer.FrameCallback, delayMs: Long) {
            val choreographer = Choreographer.getInstance()
            if (delayMs > 0L) choreographer.postFrameCallbackDelayed(callback, delayMs)
            else choreographer.postFrameCallback(callback)
        }
        override fun remove(callback: Choreographer.FrameCallback) {
            Choreographer.getInstance().removeFrameCallback(callback)
        }
    }

    private var running = false
    private var dispatching = false
    private var lastFrameNanos = 0L

    /**
     * Ocioso (fase 8D, §38/§40): > 0 = o próximo callback vem depois de tantos
     * ms, não no próximo vsync. Com o app parado o laço não acorda a CPU 60–120
     * vezes por segundo à toa; [wake] volta ao vsync na hora (comando, gesto).
     */
    var idleDelayMs: Long = 0L

    /**
     * Estatística de cadência. Serve para a telemetria distinguir "a UI está
     * lenta" de "o motor está lento": se o intervalo entre callbacks já é
     * maior que o esperado, o problema está acima do motor.
     */
    var averageIntervalMs: Float = 0f
        private set

    private val callback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!running) return

            if (lastFrameNanos != 0L) {
                val deltaMs = (frameTimeNanos - lastFrameNanos) / 1_000_000f
                // Média móvel: um pico isolado (o primeiro frame depois de
                // abrir o app) não deve distorcer a leitura.
                averageIntervalMs = if (averageIntervalMs == 0f) deltaMs
                else averageIntervalMs * 0.9f + deltaMs * 0.1f
            }
            lastFrameNanos = frameTimeNanos

            dispatching = true
            try {
                onFrame(frameTimeNanos)
            } finally {
                dispatching = false
            }

            // Reagenda SEMPRE, mesmo que o frame tenha demorado. Deixar de
            // reagendar por causa de um frame lento transformaria um engasgo
            // momentâneo numa parada até a próxima interação.
            if (running) {
                if (idleDelayMs > 0L) {
                    lastFrameNanos = 0L   // o intervalo ocioso não entra na média de cadência
                    scheduler.post(this, idleDelayMs)
                } else {
                    scheduler.post(this, 0L)
                }
            }
        }
    }

    fun start() {
        if (running) return
        running = true
        lastFrameNanos = 0L
        if (!dispatching) scheduler.post(callback, 0L)
    }

    /** Sai do modo ocioso já (o callback agendado com atraso é trocado pelo do próximo vsync). */
    fun wake() {
        if (!running || idleDelayMs == 0L) return
        idleDelayMs = 0L
        // updateIdle may wake us from inside onFrame. Its trailing post already
        // targets the next vsync; posting here too permanently doubled polling.
        if (dispatching) return
        scheduler.remove(callback)
        scheduler.post(callback, 0L)
    }

    fun stop() {
        if (!running) return
        running = false
        scheduler.remove(callback)
    }

    val isRunning: Boolean get() = running
}
