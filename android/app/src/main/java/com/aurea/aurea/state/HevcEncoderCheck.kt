package com.aurea.aurea.state

import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import java.util.Locale
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/**
 * HEVC só quando o aparelho tem ENCODER HEVC de hardware que aceita o tamanho
 * e o fps do export. Aparelho com só o DECODER HEVC (ou encoder HEVC ruim)
 * falhava na abertura ou caía no HEVC por software, lento demais no celular.
 * Sem um: H.264 direto, sem erro — o motivo vai para o diagnóstico do export.
 *
 * O motor escolhe o encoder pela MESMA regra (FFmpegExportSink, `android_codec`:
 * isEncoder + isHardwareAccelerated + areSizeAndRateSupported, API 29+); abaixo
 * da API 29 ele só usa encoder de software, então HEVC lá vira H.264 também.
 */
object HevcEncoderCheck {
    data class Encoder(val name: String, val hardware: Boolean, val supportsSize: Boolean)
    data class Choice(val hevc: Boolean, val encoder: String?, val reason: String)

    /** A regra, pura (testada na JVM): o primeiro encoder de hardware que aceita o tamanho/fps. */
    fun choose(sdk: Int, encoders: List<Encoder>, width: Int, height: Int, fps: Double): Choice {
        val size = "${width}x$height@${String.format(Locale.ROOT, "%.3f", fps).trimEnd('0').trimEnd('.')}"
        if (sdk < 29) return Choice(false, null, "API $sdk: sem encoder HEVC de hardware classificado")
        if (encoders.isEmpty()) return Choice(false, null, "sem encoder video/hevc no aparelho")
        encoders.firstOrNull { it.hardware && it.supportsSize }?.let {
            return Choice(true, it.name, "encoder HEVC ${it.name} aceita $size")
        }
        val hardware = encoders.firstOrNull { it.hardware }
        return if (hardware != null) Choice(false, null, "encoder HEVC ${hardware.name} nao aceita $size")
        else Choice(false, null, "so encoder HEVC de software (${encoders.first().name})")
    }

    /**
     * O tamanho do vídeo pela regra do motor (export_frame_size_v2): o lado
     * menor pedido, a proporção da composição, lados pares. 0 = o da composição.
     */
    fun exportSize(compW: Int, compH: Int, shortSide: Int): Pair<Int, Int> {
        if (compW <= 0 || compH <= 0) return 0 to 0
        val nativeShort = min(compW, compH)
        if (shortSide <= 0 || shortSide == nativeShort) return compW to compH
        val scale = shortSide.toDouble() / nativeShort
        fun even(v: Double) = max(2, floor(v / 2.0 + 0.5).toInt() * 2)
        return even(compW * scale) to even(compH * scale)
    }

    /** MediaCodecList(REGULAR_CODECS): os encoders video/hevc do aparelho. Chamar fora da main thread. */
    fun probe(width: Int, height: Int, fps: Double): Choice = runCatching {
        val sdk = Build.VERSION.SDK_INT
        val encoders = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .filter { info -> info.isEncoder && info.supportedTypes.any { it.equals(MediaFormat.MIMETYPE_VIDEO_HEVC, ignoreCase = true) } }
            .map { info ->
                val video = runCatching { info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_HEVC).videoCapabilities }.getOrNull()
                val hardware = sdk >= Build.VERSION_CODES.Q && info.isHardwareAccelerated
                Encoder(info.name, hardware, video?.areSizeAndRateSupported(width, height, fps) == true)
            }
        choose(sdk, encoders, width, height, fps)
    }.getOrElse { Choice(false, null, "MediaCodecList falhou: ${it.javaClass.simpleName}") }
}
