package com.aurea.aurea.mocap

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.pose.PoseDetection
import com.google.mlkit.vision.pose.PoseDetector
import com.google.mlkit.vision.pose.accurate.AccuratePoseDetectorOptions
import com.google.mlkit.vision.pose.defaults.PoseDetectorOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Motion Capture (0.3.6 BETA), Android: detecção de pose NO APARELHO com o
 * ML Kit Pose Detection (modelo embutido no AAR — nada é baixado à parte;
 * armeabi-v7a e arm64-v8a). O vídeo é decodificado em quadros reduzidos
 * (lado maior 384 px; 256 px e metade dos quadros no modo reduzido), um por
 * vez, e cada pose vira um quadro de [FRAME_FLOATS] floats no formato do
 * motor (rig/Mocap.hpp): tempo, sem 3D (o ML Kit dá z relativo), e 33 ×
 * (x, y normalizados, z na escala de x, probabilidade de estar no quadro).
 *
 * Roda fora da main (Dispatchers.Default), cancela entre quadros e entrega os
 * quadros ao motor em lotes de [BATCH] (pouca memória em vídeo longo).
 * Par do iOS: MocapDetector.swift (Vision).
 */
class MocapDetector(private val context: Context) {

    companion object {
        const val FRAME_FLOATS = 2 + 33 * 4
        const val BATCH = 32
        private const val DECODE_BUDGET_BYTES = 48L * 1024 * 1024
    }

    /**
     * Analisa [uri] e manda os quadros para [sink] (floats, quantos, largura/altura).
     * [onProgress] 0..1. Devolve quantos quadros foram analisados.
     */
    suspend fun run(
        uri: Uri,
        reduced: Boolean,
        onProgress: suspend (Float) -> Unit,
        sink: (FloatArray, Int, Float) -> Unit,
    ): Int = withContext(Dispatchers.Default) {
        val retriever = MediaMetadataRetriever()
        val detector: PoseDetector = PoseDetection.getClient(
            if (reduced) PoseDetectorOptions.Builder().setDetectorMode(PoseDetectorOptions.STREAM_MODE).build()
            else AccuratePoseDetectorOptions.Builder().setDetectorMode(AccuratePoseDetectorOptions.STREAM_MODE).build(),
        )
        val buffer = FloatArray(BATCH * FRAME_FLOATS)
        var pending = 0
        var analyzed = 0
        var aspect = 1f
        val maxSide = if (reduced) 256 else 384
        val targetFps = if (reduced) 15.0 else 30.0

        suspend fun detect(source: Bitmap, timeS: Double) {
            coroutineContext.ensureActive()
            val scale = min(1f, maxSide.toFloat() / max(source.width, source.height))
            val frame = if (scale < 1f) {
                Bitmap.createScaledBitmap(source, max(1, (source.width * scale).roundToInt()), max(1, (source.height * scale).roundToInt()), true)
            } else source
            try {
                val w = frame.width.toFloat()
                val h = frame.height.toFloat()
                aspect = w / h
                val pose = Tasks.await(detector.process(InputImage.fromBitmap(frame, 0)))
                val base = pending * FRAME_FLOATS
                buffer[base] = timeS.toFloat()
                buffer[base + 1] = 0f
                // Sem pessoa: o quadro vai com confiança 0 (o motor interpola a lacuna).
                java.util.Arrays.fill(buffer, base + 2, base + FRAME_FLOATS, 0f)
                // O tipo do ponto do ML Kit É o índice do modelo de 33 pontos (nariz 0 … pé 32).
                for (lm in pose.allPoseLandmarks) {
                    val j = lm.landmarkType
                    if (j !in 0 until 33) continue
                    val o = base + 2 + j * 4
                    val p = lm.position3D
                    buffer[o] = p.x / w
                    buffer[o + 1] = p.y / h
                    buffer[o + 2] = p.z / w
                    buffer[o + 3] = lm.inFrameLikelihood.coerceIn(0f, 1f)
                }
                pending++
                analyzed++
                if (pending == BATCH) {
                    sink(buffer, pending, aspect)
                    pending = 0
                }
            } finally {
                if (frame !== source) frame.recycle()
            }
        }

        try {
            retriever.setDataSource(context, uri)
            val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            if (durationMs <= 0L) return@withContext 0
            val frameCount = if (Build.VERSION.SDK_INT >= 28)
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toIntOrNull() ?: 0 else 0
            if (Build.VERSION.SDK_INT >= 28 && frameCount > 1) {
                // Decodificação em sequência, em lotes que cabem em ~48 MB de bitmaps.
                val vw = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toLongOrNull() ?: 1920L
                val vh = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toLongOrNull() ?: 1080L
                val perFrame = max(1L, vw * vh * 4L)
                val chunk = (DECODE_BUDGET_BYTES / perFrame).toInt().coerceIn(1, 8)
                val sourceFps = frameCount * 1000.0 / durationMs
                val step = max(1, (sourceFps / targetFps).roundToInt())
                var i = 0
                while (i < frameCount) {
                    coroutineContext.ensureActive()
                    val count = min(chunk, frameCount - i)
                    val frames = runCatching { retriever.getFramesAtIndex(i, count) }.getOrNull() ?: break
                    try {
                        for ((k, bitmap) in frames.withIndex()) {
                            val index = i + k
                            if (index % step == 0) detect(bitmap, index * durationMs / 1000.0 / frameCount)
                        }
                    } finally {
                        frames.forEach { it.recycle() }
                    }
                    i += count
                    onProgress(i.toFloat() / frameCount)
                }
            } else {
                // Android 8/8.1 (ou sem contagem de quadros): um quadro por instante.
                val stepUs = (1_000_000.0 / targetFps).toLong()
                var t = 0L
                val endUs = durationMs * 1000L
                while (t < endUs) {
                    coroutineContext.ensureActive()
                    val bitmap = if (Build.VERSION.SDK_INT >= 27)
                        retriever.getScaledFrameAtTime(t, MediaMetadataRetriever.OPTION_CLOSEST, maxSide, maxSide)
                    else retriever.getFrameAtTime(t, MediaMetadataRetriever.OPTION_CLOSEST)
                    if (bitmap != null) {
                        try { detect(bitmap, t / 1_000_000.0) } finally { bitmap.recycle() }
                    }
                    t += stepUs
                    onProgress(t.toFloat() / endUs)
                }
            }
            if (pending > 0) sink(buffer, pending, aspect)
            onProgress(1f)
            analyzed
        } finally {
            detector.close()
            runCatching { retriever.release() }
        }
    }
}
