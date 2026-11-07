package com.aurea.aurea.diagnostics

import android.net.Uri
import android.os.SystemClock
import android.view.Choreographer
import androidx.compose.runtime.snapshotFlow
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale

/**
 * Bancada de desempenho do editor, só no build de debug e com a prévia na tela
 * (o `PerfPOD` só é escrito pelo laço ao vivo). Abre com `--es aureaBench <rótulo>`
 * e grava `bench-<rótulo>.txt` na pasta externa do app, uma linha por cenário.
 * Precisa de `demo/pf.mp4` nessa pasta. Os mesmos cenários rodam antes e depois de
 * cada otimização, para o número comparar a mesma coisa.
 */
class PlaybackBench(private val store: EditorStore, private val label: String) {
    private val engine get() = store.engineForStress
    private val folder = store.getApplication<android.app.Application>().getExternalFilesDir(null)!!
    private val video = File(folder, "demo/pf.mp4")
    private val report = File(folder, "bench-$label.txt")
    private val perfBuffer = ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())

    private fun perf(): PerfStats {
        if (!engine.readPerf(perfBuffer)) return PerfStats()
        perfBuffer.rewind()
        return PerfStats.read(perfBuffer)
    }

    private val statusBuffer = ByteBuffer.allocateDirect(1024).order(ByteOrder.nativeOrder())

    /** O que a thread da interface paga por vsync para ler o estado do motor. */
    private fun timedStatus(into: MutableList<Float>) {
        val t0 = SystemClock.elapsedRealtimeNanos()
        engine.readStatus(statusBuffer)
        into += (SystemClock.elapsedRealtimeNanos() - t0) / 1_000_000f
    }

    /** Intervalos entre quadros da UI: é aqui que a timeline e os painéis engasgam. */
    private inner class UiFrames : Choreographer.FrameCallback {
        val gaps = ArrayList<Float>(2048)
        private var last = 0L
        private var running = false
        fun start() { running = true; last = 0L; gaps.clear(); Choreographer.getInstance().postFrameCallback(this) }
        fun stop() { running = false }
        override fun doFrame(now: Long) {
            if (!running) return
            if (last != 0L) gaps += (now - last) / 1_000_000f
            last = now
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    private fun pct(values: List<Float>, p: Int): Float {
        if (values.isEmpty()) return 0f
        val sorted = values.sorted()
        return sorted[((p / 100f) * (sorted.size - 1)).toInt().coerceIn(0, sorted.size - 1)]
    }

    private fun write(line: String) {
        report.appendText(line + "\n")
        android.util.Log.i("AureaBench", line)
    }

    private fun f(v: Float) = String.format(Locale.ROOT, "%.1f", v)

    private fun summary(name: String, samples: List<PerfStats>, ui: List<Float>, extra: String = "") {
        val fps = samples.map { it.previewFps }.filter { it > 0f }
        val first = samples.firstOrNull() ?: PerfStats()
        val last = samples.lastOrNull() ?: PerfStats()
        write(
            "$name fps=${f(if (fps.isEmpty()) 0f else fps.average().toFloat())}" +
                " perdidos=${last.droppedFrames - first.droppedFrames}" +
                " pacing95=${f(pct(samples.map { it.pacingP95Ms }.filter { it > 0f }, 50))}" +
                " pacing99=${f(pct(samples.map { it.pacingP99Ms }.filter { it > 0f }, 50))}" +
                " cpu50=${f(pct(samples.map { it.cpuFrameMs }, 50))} cpu99=${f(pct(samples.map { it.cpuFrameMs }, 99))}" +
                " gpu50=${f(pct(samples.map { it.gpuFrameMs }, 50))}" +
                " decode95=${f(pct(samples.map { it.decodeMs }, 95))}" +
                " escala=${last.renderScaleNum}/${last.renderScaleDen}" +
                " ui50=${f(pct(ui, 50))} ui95=${f(pct(ui, 95))} ui99=${f(pct(ui, 99))} uiMax=${f(ui.maxOrNull() ?: 0f)}" +
                " ui>20=${ui.count { it > 20f }} ui>33=${ui.count { it > 33f }} uiN=${ui.size}" +
                " audioFalhas=${last.audioUnderruns - first.audioUnderruns}" +
                " zeroCopy=${last.zeroCopy} hw=${last.hardwareDecoder}" +
                (if (extra.isEmpty()) "" else " $extra"),
        )
    }

    private suspend fun playFor(name: String, seconds: Int) {
        store.seek(0)
        delay(400)
        val ui = UiFrames()
        val samples = ArrayList<PerfStats>()
        store.play()
        delay(500)     // o primeiro meio segundo é aquecimento do decoder
        ui.start()
        val end = SystemClock.uptimeMillis() + seconds * 1000L
        val status = ArrayList<Float>(1024)
        var tick = 0
        while (SystemClock.uptimeMillis() < end) {
            delay(16)
            timedStatus(status)
            if (++tick % 6 == 0) samples += perf()
        }
        ui.stop()
        store.pause()
        delay(300)
        summary(name, samples, ui.gaps.toList(),
            "status50=${String.format(Locale.ROOT, "%.3f", pct(status, 50))} status99=${f(pct(status, 99))} statusMax=${f(status.maxOrNull() ?: 0f)}")
    }

    private suspend fun fresh(title: String) {
        store.newProject(1080, 1920, 30f, title)
        delay(1200)
    }

    private fun effect(vararg names: String): List<Int> = names.mapNotNull { n ->
        store.catalog.firstOrNull { it.name.lowercase(Locale.ROOT).contains(n) }?.typeId
    }

    /** Quadros prontos na prévia guardada, pelo campo que a UI já lê. */
    private fun cachedFrames(): Int {
        engine.readStatus(statusBuffer)
        return statusBuffer.getInt(108) and 0xFF
    }

    /** Parado o app enche o cache; o Play precisa aproveitar o que ficou pronto. */
    private suspend fun cacheKeptOnPlay() {
        store.seek(0)
        delay(9000)
        val before = cachedFrames()
        store.play()
        delay(250)
        val after = cachedFrames()
        val samples = ArrayList<PerfStats>()
        val ui = UiFrames()
        ui.start()
        val end = SystemClock.uptimeMillis() + 3000L
        while (SystemClock.uptimeMillis() < end) { delay(100); samples += perf() }
        ui.stop()
        store.pause()
        delay(300)
        summary("cache60", samples, ui.gaps.toList(), "prontos=$before noPlay=$after")
    }

    /** Busca: um quadro pedido a cada 33 ms, como o dedo arrastando a régua. */
    private suspend fun scrub() {
        store.seek(0)
        delay(400)
        val ui = UiFrames()
        val samples = ArrayList<PerfStats>()
        ui.start()
        val total = 90
        var worst = 0f
        val seeks = ArrayList<Float>(total)
        repeat(total) { i ->
            store.seek((i * 37) % 240)
            delay(33)
            val p = perf()
            samples += p
            seeks += p.lastSeekMs
            worst = maxOf(worst, p.lastSeekMs)
        }
        ui.stop()
        delay(300)
        summary("scrub", samples, ui.gaps.toList(),
            "seek50=${f(pct(seeks, 50))} seek95=${f(pct(seeks, 95))} seekMax=${f(worst)} velhos=${samples.last().staleFrames - samples.first().staleFrames}")
    }

    /** Edição contínua parada no quadro: o dedo na régua de um parâmetro. */
    private suspend fun drag() {
        store.layers.firstOrNull()?.let { store.select(it.id) }
        delay(300)
        val ui = UiFrames()
        val samples = ArrayList<PerfStats>()
        val spent = ArrayList<Float>(120)
        ui.start()
        store.beginGesture("bench")
        repeat(120) { i ->
            val t0 = SystemClock.elapsedRealtimeNanos()
            store.setTextSize(60f + (i % 40))
            spent += (SystemClock.elapsedRealtimeNanos() - t0) / 1_000_000f
            delay(16)
            samples += perf()
        }
        store.endGesture()
        ui.stop()
        delay(300)
        summary("arrasto", samples, ui.gaps.toList(), "chamada50=${f(pct(spent, 50))} chamada99=${f(pct(spent, 99))}")
    }

    suspend fun run() {
        report.delete()
        snapshotFlow { store.engineReady }.first { it }
        write("# ${android.os.Build.MODEL} ${label}")
        fresh("Bench")
        snapshotFlow { store.screen }.first { it == Screen.Editor }
        delay(1000)
        store.importVideo(Uri.fromFile(video))
        delay(1500)
        playFor("video1", 8)
        scrub()
        store.layers.firstOrNull()?.let { store.select(it.id) }
        delay(200)
        effect("gaussian", "desfoque gauss", "brilho").forEach { store.addEffect(it) }
        delay(600)
        playFor("video1+fx", 8)

        store.newProject(1080, 1920, 60f, "BenchCache")
        delay(1200)
        store.importVideo(Uri.fromFile(video))
        delay(1500)
        store.layers.firstOrNull()?.let { store.select(it.id) }
        delay(200)
        effect("gaussian", "desfoque gauss", "brilho").forEach { store.addEffect(it) }
        delay(600)
        cacheKeptOnPlay()

        fresh("Bench3")
        repeat(3) { store.importVideo(Uri.fromFile(video)); delay(1200) }
        playFor("video3", 8)

        fresh("BenchTexto")
        repeat(12) { store.addText(openEditor = false); store.addTextAnimator(1); delay(120) }
        delay(600)
        playFor("texto12", 8)
        drag()
        write("# fim")
    }
}
