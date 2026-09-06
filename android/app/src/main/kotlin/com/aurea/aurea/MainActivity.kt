package com.aurea.aurea

import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread

class MainActivity : FlutterActivity() {

    private val encoder = VideoEncoder()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/encoder"
        ).setMethodCallHandler { call, result ->
            // Codificar bloqueia; a thread da interface nao pode parar.
            thread {
                try {
                    val value = handle(call.method, call)
                    runOnUiThread { result.success(value) }
                } catch (e: Exception) {
                    runOnUiThread {
                        result.error("encoder", e.message ?: "$e", null)
                    }
                }
            }
        }
    }

    private fun handle(method: String, call: io.flutter.plugin.common.MethodCall): Any? =
        when (method) {
            "available" -> true

            "start" -> {
                encoder.start(
                    call.argument<String>("path")!!,
                    call.argument<Int>("width")!!,
                    call.argument<Int>("height")!!,
                    call.argument<Int>("fps")!!,
                    call.argument<Int>("bitrate")!!,
                    call.argument<Boolean>("hevc") ?: false
                )
                true
            }

            "frame" -> {
                encoder.encodeFrameFile(call.argument<String>("path")!!)
                true
            }

            /** Codifica uma sequencia inteira sem voltar ao Dart por quadro. */
            "frames" -> {
                val paths = call.argument<List<String>>("paths")!!
                for (p in paths) encoder.encodeFrameFile(p)
                paths.size
            }

            "finish" -> encoder.finish()

            "cancel" -> {
                encoder.stop(discard = true)
                true
            }

            /**
             * REMUX: copia trilhas de um MP4 para outro sem recodificar.
             * Corte puro deixa de custar um render inteiro.
             */
            "remux" -> remux(
                call.argument<String>("source")!!,
                call.argument<String>("target")!!,
                (call.argument<Number>("startUs") ?: 0L).toLong(),
                (call.argument<Number>("endUs") ?: 0L).toLong()
            )

            else -> null
        }

    private fun remux(source: String, target: String, startUs: Long, endUs: Long): Boolean {
        val extractor = MediaExtractor()
        extractor.setDataSource(source)
        File(target).parentFile?.mkdirs()
        val muxer = MediaMuxer(target, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

        val indexMap = HashMap<Int, Int>()
        var maxBuffer = 1 shl 20
        for (i in 0 until extractor.trackCount) {
            val format = extractor.getTrackFormat(i)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: continue
            if (!mime.startsWith("video/") && !mime.startsWith("audio/")) continue
            extractor.selectTrack(i)
            if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) {
                val s = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE)
                if (s > maxBuffer) maxBuffer = s
            }
            indexMap[i] = muxer.addTrack(format)
        }
        if (indexMap.isEmpty()) {
            extractor.release()
            muxer.release()
            return false
        }

        muxer.start()
        val buffer = ByteBuffer.allocate(maxBuffer)
        val info = android.media.MediaCodec.BufferInfo()
        if (startUs > 0) {
            extractor.seekTo(startUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
        }

        while (true) {
            buffer.clear()
            val size = extractor.readSampleData(buffer, 0)
            if (size < 0) break
            val pts = extractor.sampleTime
            if (endUs > 0 && pts > endUs) break

            info.offset = 0
            info.size = size
            info.presentationTimeUs = (pts - startUs).coerceAtLeast(0)
            info.flags = extractor.sampleFlags
            val track = indexMap[extractor.sampleTrackIndex]
            if (track != null) muxer.writeSampleData(track, buffer, info)
            extractor.advance()
        }

        muxer.stop()
        muxer.release()
        extractor.release()
        return true
    }
}
