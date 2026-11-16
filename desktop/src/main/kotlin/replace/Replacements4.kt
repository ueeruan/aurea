package com.aurea.aurea.mocap

import android.content.Context
import android.content.toFile
import android.net.Uri
import com.aurea.aurea.desktop.DesktopMocap
import com.aurea.pc.PcPaths
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException
import java.util.concurrent.Executors

/**
 * Motion Capture no Aurea PC: o mesmo contrato do MocapDetector.kt do Android
 * (ML Kit), com a pose do motor — o vídeo é decodificado pelo Media Foundation
 * e o BlazePose do MediaPipe roda no ncnn (Vulkan quando há GPU, senão CPU):
 * engine/platform/desktop/DesktopMocap.cpp e engine/src/ai/PoseEstimator.cpp.
 * Os quadros chegam ao motor no formato de sempre ([FRAME_FLOATS]: tempo, 0,
 * 33 × (x, y normalizados, z na escala de x, probabilidade de estar no
 * quadro)), cada um seguido do quadro COMPANHEIRO de mundo (segundo campo 2:
 * 33 × metros do BlazePose GHUM 3D e visibilidade), que o solver do motor usa
 * para as direções dos ossos. O MocapFlow/MocapSection funcionam sem mudança.
 *
 * Qualidade máxima (padrão): 30 quadros/s na resolução do vídeo, modelo heavy
 * com refino pelo heatmap; o modo reduzido usa o full, 15 quadros/s e 960 px.
 * Uma thread só (a sessão nativa não troca de thread), com cancelamento entre
 * os lotes.
 */
class MocapDetector(@Suppress("unused") private val context: Context) {

    companion object {
        const val FRAME_FLOATS = 2 + 33 * 4
        const val BATCH = 32
        /** Quadros por pedido ao motor (imagem + mundo = 2 por quadro analisado): progresso e cancelamento a cada ~0,3 s. */
        private const val STEP = 16
    }

    /**
     * Analisa [uri] (arquivo local) e manda os quadros para [sink] (floats,
     * quantos, largura/altura). [onProgress] 0..1. Devolve quantos quadros.
     */
    suspend fun run(
        uri: Uri,
        reduced: Boolean,
        onProgress: suspend (Float) -> Unit,
        sink: (FloatArray, Int, Float) -> Unit,
    ): Int {
        val video = uri.toFile()?.takeIf { it.isFile } ?: throw FileNotFoundException(uri.toString())
        val models = File(PcPaths.engineAssets, "mocap")
        val executor = Executors.newSingleThreadExecutor { r ->
            Thread(r, "aurea-mocap").apply { isDaemon = true; priority = Thread.NORM_PRIORITY - 1 }
        }
        val dispatcher = executor.asCoroutineDispatcher()
        try {
            return withContext(dispatcher) {
                val info = FloatArray(6)
                val session = DesktopMocap.nativeOpen(models.absolutePath, video.absolutePath, reduced, info)
                if (session == 0L) {
                    val why = if (info[0] == 1f) "modelos de pose ausentes em $models" else "vídeo ilegível: $video"
                    android.util.Log.w("Mocap", why)
                    throw IOException(why)
                }
                android.util.Log.i("Mocap", "analisando ${video.name}: ${info[3].toInt()}x${info[4].toInt()}, " +
                    "${"%.1f".format(info[1])} s, pose em ${if (info[2] > 0.5f) "Vulkan" else "CPU"}")
                try {
                    val buffer = FloatArray(BATCH * FRAME_FLOATS)
                    val state = FloatArray(6)
                    var total = 0
                    while (true) {
                        coroutineContext.ensureActive()
                        val n = DesktopMocap.nativeNext(session, buffer, STEP, state)
                        if (n < 0) throw IOException("decoder")
                        if (n > 0) {
                            sink(buffer, n, state[1])
                            total += n
                        }
                        onProgress(state[0].coerceIn(0f, 1f))
                        if (state[2] > 0.5f) break
                    }
                    android.util.Log.i("Mocap", "$total quadros, ${state[5].toInt()} com pessoa, " +
                        "${"%.1f".format(state[3])} ms de rede por quadro")
                    onProgress(1f)
                    total
                } finally {
                    DesktopMocap.nativeClose(session)
                }
            }
        } finally {
            dispatcher.close()
        }
    }
}
