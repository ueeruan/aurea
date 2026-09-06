package com.aurea.aurea

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.media.MediaMuxer
import java.io.File
import java.nio.ByteBuffer

/**
 * CODIFICADOR DE VIDEO DA PLATAFORMA.
 *
 * Substitui o x264 do FFmpeg por MediaCodec + MediaMuxer. Tres motivos,
 * na ordem em que importam:
 *
 *  1. LICENCA — x264 e GPL. Num app comercial isso obriga a abrir o
 *     codigo inteiro. O codificador do sistema nao tem esse problema, e
 *     a exposicao de patente passa a ser do fabricante do aparelho.
 *  2. VELOCIDADE — e hardware. x264 e software.
 *  3. MANUTENCAO — vem com o Android; nao e dependencia aposentada.
 *
 * Os quadros chegam como arquivos PNG ja desenhados pelo Flutter. A
 * conversao RGBA -> YUV420 acontece aqui, porque e o formato que o
 * codificador aceita em modo de buffer (o modo de Surface exigiria EGL,
 * e complicaria sem ganhar nada nesse fluxo).
 */
class VideoEncoder {

    private var codec: MediaCodec? = null
    private var muxer: MediaMuxer? = null
    private var trackIndex = -1
    private var muxerStarted = false
    private var frameIndex = 0L

    private var width = 0
    private var height = 0
    private var fps = 30
    private var yuv: ByteArray? = null

    /** Buffer de bitmap reaproveitado: alocar por quadro derruba a taxa. */
    private var argb: IntArray? = null

    /** Se o aparelho sabe codificar este formato. */
    private fun hasEncoder(mime: String): Boolean = try {
        MediaCodecList(MediaCodecList.REGULAR_CODECS)
            .codecInfos
            .any { it.isEncoder && it.supportedTypes.any { t -> t.equals(mime, true) } }
    } catch (e: Exception) {
        false
    }

    fun start(
        path: String,
        w: Int,
        h: Int,
        frameRate: Int,
        bitRate: Int,
        hevc: Boolean = false
    ) {
        stop(discard = true)

        // O H.264 exige dimensao par.
        width = if (w % 2 == 0) w else w + 1
        height = if (h % 2 == 0) h else h + 1
        fps = if (frameRate < 1) 30 else frameRate

        // HEVC quando pedido E quando o aparelho tem: sem codificador de
        // H.265 a exportacao nao pode simplesmente falhar — cai no H.264,
        // que todo aparelho tem.
        val mime = if (hevc && hasEncoder(MediaFormat.MIMETYPE_VIDEO_HEVC)) {
            MediaFormat.MIMETYPE_VIDEO_HEVC
        } else {
            MediaFormat.MIMETYPE_VIDEO_AVC
        }

        val format = MediaFormat.createVideoFormat(mime, width, height).apply {
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible
            )
            setInteger(MediaFormat.KEY_BIT_RATE, bitRate)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            // Um quadro-chave por segundo: sem isso, buscar no video
            // exportado fica lento.
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }

        val c = MediaCodec.createEncoderByType(mime)
        c.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        c.start()
        codec = c

        File(path).parentFile?.mkdirs()
        muxer = MediaMuxer(path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
        trackIndex = -1
        muxerStarted = false
        frameIndex = 0
        yuv = ByteArray(width * height * 3 / 2)
        argb = IntArray(width * height)
    }

    /** Codifica um quadro vindo de um PNG no disco. */
    fun encodeFrameFile(framePath: String) {
        val opts = BitmapFactory.Options().apply {
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        val bmp = BitmapFactory.decodeFile(framePath, opts)
            ?: throw IllegalStateException("Quadro ilegivel: $framePath")
        try {
            encodeBitmap(bmp)
        } finally {
            bmp.recycle()
        }
    }

    private fun encodeBitmap(source: Bitmap) {
        val c = codec ?: throw IllegalStateException("Codificador nao iniciado")

        val bmp = if (source.width == width && source.height == height) {
            source
        } else {
            Bitmap.createScaledBitmap(source, width, height, true)
        }

        val pixels = argb!!
        bmp.getPixels(pixels, 0, width, 0, 0, width, height)
        if (bmp !== source) bmp.recycle()

        argbToNv12(pixels, yuv!!, width, height)

        // Enfileira o quadro.
        var queued = false
        while (!queued) {
            val inIndex = c.dequeueInputBuffer(10_000)
            if (inIndex >= 0) {
                val buf: ByteBuffer = c.getInputBuffer(inIndex)!!
                buf.clear()
                buf.put(yuv!!)
                val ptsUs = frameIndex * 1_000_000L / fps
                c.queueInputBuffer(inIndex, 0, yuv!!.size, ptsUs, 0)
                frameIndex++
                queued = true
            }
            drain(false)
        }
    }

    /** Fecha o fluxo e devolve o caminho do arquivo escrito. */
    fun finish(): Boolean {
        val c = codec ?: return false
        val inIndex = c.dequeueInputBuffer(10_000)
        if (inIndex >= 0) {
            c.queueInputBuffer(
                inIndex, 0, 0,
                frameIndex * 1_000_000L / fps,
                MediaCodec.BUFFER_FLAG_END_OF_STREAM
            )
        }
        drain(true)
        stop(discard = false)
        return true
    }

    private fun drain(endOfStream: Boolean) {
        val c = codec ?: return
        val info = MediaCodec.BufferInfo()
        while (true) {
            val outIndex = c.dequeueOutputBuffer(info, if (endOfStream) 10_000 else 0)
            when {
                outIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    if (!endOfStream) return
                }

                outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    if (muxerStarted) throw IllegalStateException("formato mudou duas vezes")
                    trackIndex = muxer!!.addTrack(c.outputFormat)
                    muxer!!.start()
                    muxerStarted = true
                }

                outIndex >= 0 -> {
                    val buf = c.getOutputBuffer(outIndex)!!
                    // Config do codec vai no formato, nao como amostra.
                    if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) {
                        info.size = 0
                    }
                    if (info.size > 0 && muxerStarted) {
                        buf.position(info.offset)
                        buf.limit(info.offset + info.size)
                        muxer!!.writeSampleData(trackIndex, buf, info)
                    }
                    c.releaseOutputBuffer(outIndex, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) return
                }
            }
        }
    }

    fun stop(discard: Boolean) {
        try {
            codec?.stop()
        } catch (_: Exception) {
        }
        try {
            codec?.release()
        } catch (_: Exception) {
        }
        codec = null
        try {
            if (muxerStarted) muxer?.stop()
        } catch (_: Exception) {
        }
        try {
            muxer?.release()
        } catch (_: Exception) {
        }
        muxer = null
        muxerStarted = false
        yuv = null
        argb = null
        if (discard) frameIndex = 0
    }

    companion object {
        /**
         * ARGB -> NV12 (Y plano, depois UV intercalado).
         *
         * A conversao roda por quadro, entao e escrita para nao alocar
         * nada e percorrer a imagem uma vez so. Coeficientes BT.601, que
         * e o que o codificador espera em video de faixa limitada.
         */
        fun argbToNv12(argb: IntArray, out: ByteArray, w: Int, h: Int) {
            val frameSize = w * h
            var yIndex = 0
            var uvIndex = frameSize

            var index = 0
            for (j in 0 until h) {
                for (i in 0 until w) {
                    val c = argb[index]
                    val r = (c shr 16) and 0xff
                    val g = (c shr 8) and 0xff
                    val b = c and 0xff

                    val y = ((66 * r + 129 * g + 25 * b + 128) shr 8) + 16
                    out[yIndex++] = clamp(y)

                    // Croma em 2x2: so nas linhas e colunas pares.
                    if (j and 1 == 0 && i and 1 == 0) {
                        val u = ((-38 * r - 74 * g + 112 * b + 128) shr 8) + 128
                        val v = ((112 * r - 94 * g - 18 * b + 128) shr 8) + 128
                        out[uvIndex++] = clamp(u)
                        out[uvIndex++] = clamp(v)
                    }
                    index++
                }
            }
        }

        private fun clamp(v: Int): Byte =
            when {
                v < 0 -> 0
                v > 255 -> 255.toByte()
                else -> v.toByte()
            }
    }
}
