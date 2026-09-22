package com.aurea.aurea.engine

import android.app.ActivityManager
import android.content.Context
import android.content.SharedPreferences
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.os.Build
import androidx.core.content.edit

/**
 * A SONDAGEM DO APARELHO — feita UMA vez, na primeira abertura, e guardada.
 *
 * Por que guardar em vez de sondar sempre: enumerar o `MediaCodecList` custa
 * dezenas de ms e cria instâncias de codec. Fazer isso a cada abertura atrasa a
 * primeira tela em troca de nada — a tabela de codecs de um aparelho não muda
 * entre duas aberturas do mesmo app. Muda quando o SO sobe de versão ou o
 * aparelho é outro, e é para isso que serve a [fingerprint]: se ela mudar, a
 * sondagem é refeita.
 *
 * A MEMÓRIA NÃO É GUARDADA. `availMem` de hoje não vale para amanhã — o valor
 * guardado só enganaria o orçamento. Só os fatos ESTÁVEIS (memória total,
 * codecs) entram na preferência; a memória disponível é lida viva a cada
 * abertura, e é barata.
 *
 * Quem DECIDE o que fazer com isto é o motor, não esta classe: aqui só se mede.
 * A decisão fica em C++, onde o iOS também a executa e onde ela é testada sem
 * aparelho na mão.
 */
object DeviceProfile {

    private const val PREFS = "aurea.device"
    private const val KEY_FINGERPRINT = "sondagem.impressao"
    private const val KEY_TOTAL_MB = "sondagem.memoria_total_mb"
    private const val KEY_CODECS = "sondagem.codecs"

    /** Códigos de tag dos codecs que o motor conhece ('avc1', 'hvc1'…). */
    private const val TAG_AVC1 = 0x61766331
    private const val TAG_HVC1 = 0x68766331
    private const val TAG_AV01 = 0x61763031
    private const val TAG_VP09 = 0x76703039

    /** Ints por linha da tabela: tag, é_encoder, hardware, largura, altura, bits, instâncias. */
    private const val CODEC_STRIDE = 7

    private fun prefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /**
     * O que muda a sondagem. Não é o nome do modelo: é o que muda a RESPOSTA —
     * versão do SO (a tabela de codecs vem dela) e o hardware.
     */
    private fun fingerprint(): String =
        "${Build.FINGERPRINT}|${Build.VERSION.SDK_INT}|${Runtime.getRuntime().availableProcessors()}|$SURVEY_VERSION"

    /** Sobe quando a REGRA da sondagem muda: a tabela guardada é refeita uma vez. */
    private const val SURVEY_VERSION = 2

    /**
     * A sondagem para passar ao motor: memória viva + codecs (guardados).
     *
     * Devolve `probe` (memória) e `codecs` (tabela) — os dois arrays que
     * `AureaEngine.initialize` repassa ao motor.
     */
    class Probe(val memory: LongArray, val codecs: IntArray)

    fun probe(context: Context): Probe {
        val app = context.applicationContext
        val p = prefs(app)
        val prints = fingerprint()

        if (p.getString(KEY_FINGERPRINT, null) != prints) {
            // Aparelho novo, ou o SO subiu de versão: a tabela de codecs antiga
            // não vale mais.
            val table = readCodecs()
            p.edit {
                putString(KEY_FINGERPRINT, prints)
                putInt(KEY_TOTAL_MB, totalMemoryMb(app))
                putString(KEY_CODECS, table.joinToString(","))
            }
        }

        val total = p.getInt(KEY_TOTAL_MB, 0).takeIf { it > 0 } ?: totalMemoryMb(app)
        val avail = availableMemoryMb(app)
        val table = p.getString(KEY_CODECS, null)
            ?.split(',')
            ?.mapNotNull { it.toIntOrNull() }
            ?.toIntArray()
            ?: IntArray(0)

        // [memória total, memória disponível] em bytes. A disponível é de AGORA.
        val memory = longArrayOf(total.toLong() * 1024 * 1024, avail.toLong() * 1024 * 1024)
        return Probe(memory, table)
    }

    /** Esquece a sondagem: a próxima abertura mede o aparelho de novo. */
    fun forget(context: Context) {
        prefs(context).edit {
            remove(KEY_FINGERPRINT)
            remove(KEY_TOTAL_MB)
            remove(KEY_CODECS)
        }
    }

    /** A sondagem já está guardada neste aparelho? */
    fun surveyed(context: Context): Boolean =
        prefs(context).getString(KEY_FINGERPRINT, null) == fingerprint()

    // -------------------------------------------------------------------------
    // Medição
    // -------------------------------------------------------------------------

    private fun memoryInfo(app: Context): ActivityManager.MemoryInfo? = try {
        val am = app.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
        am?.let { ActivityManager.MemoryInfo().also(it::getMemoryInfo) }
    } catch (_: Throwable) {
        null
    }

    private fun totalMemoryMb(app: Context): Int {
        val bytes = memoryInfo(app)?.totalMem ?: 0L
        return if (bytes > 0) (bytes / (1024 * 1024)).toInt() else 0
    }

    private fun availableMemoryMb(app: Context): Int {
        val bytes = memoryInfo(app)?.availMem ?: 0L
        return (bytes / (1024 * 1024)).toInt()
    }

    /**
     * A tabela de codecs do aparelho, no formato que o motor lê.
     *
     * Só os quatro codecs que o motor conhece entram: uma linha de um codec que
     * ele não sabe usar não vira nada além de bytes na preferência.
     *
     * O decoder ESCOLHIDO é o melhor de cada tag — hardware antes de software, e
     * o de maior resolução entre iguais. O MediaCodecList lista vários por tag
     * (o do Qualcomm, o do Google, o de software); mandar todos faria o motor
     * ligar o tag ao último da lista, que costuma ser o de software.
     */
    private fun readCodecs(): IntArray {
        val rows = ArrayList<Int>(CODEC_STRIDE * 8)
        try {
            val list = MediaCodecList(MediaCodecList.REGULAR_CODECS)
            val best = HashMap<Long, IntArray>()   // (tag<<1|encoder) → linha

            for (info in list.codecInfos) {
                for (type in info.supportedTypes) {
                    val tag = tagFor(type) ?: continue
                    val caps = try {
                        info.getCapabilitiesForType(type)
                    } catch (_: Throwable) {
                        continue
                    }
                    val video = caps.videoCapabilities ?: continue
                    // Antes do Android 10 não há `isHardwareAccelerated`: o nome
                    // separa os de software do AOSP (antes, TODOS contavam como
                    // hardware — e o export 4K por software passava por hardware).
                    val hw = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) info.isHardwareAccelerated
                             else !softwareName(info.name)
                    val line = intArrayOf(
                        tag,
                        if (info.isEncoder) 1 else 0,
                        if (hw) 1 else 0,
                        video.supportedWidths.upper,
                        video.supportedHeights.upper,
                        maxBitDepth(tag, caps),
                        0,
                    )
                    val key = (tag.toLong() shl 1) or (if (info.isEncoder) 1L else 0L)
                    val current = best[key]
                    if (current == null || better(line, current)) best[key] = line
                }
            }
            best.values.forEach { rows.addAll(it.toList()) }
        } catch (_: Throwable) {
            // Sem a tabela o motor fica no conservador — que é o comportamento
            // correto quando não se sabe, não um erro.
            return IntArray(0)
        }
        return rows.toIntArray()
    }

    /**
     * 10 bits se o codec anuncia algum perfil de 10 bits (HEVC Main10, VP9
     * Profile 2/3, AV1 Main10, os HDR); senão 8. O Android não expõe a
     * profundidade direto: ela é implícita no perfil.
     */
    private fun maxBitDepth(tag: Int, caps: MediaCodecInfo.CodecCapabilities): Int {
        val p = MediaCodecInfo.CodecProfileLevel::class.java
        fun c(name: String): Int = try { p.getField(name).getInt(null) } catch (_: Throwable) { -1 }
        val ten = when (tag) {
            TAG_HVC1 -> setOf(c("HEVCProfileMain10"), c("HEVCProfileMain10HDR10"), c("HEVCProfileMain10HDR10Plus"))
            TAG_VP09 -> setOf(c("VP9Profile2"), c("VP9Profile3"), c("VP9Profile2HDR"), c("VP9Profile3HDR"))
            TAG_AV01 -> setOf(c("AV1ProfileMain10"), c("AV1ProfileMain10HDR10"), c("AV1ProfileMain10HDR10Plus"))
            else -> emptySet()
        } - (-1)
        return if (caps.profileLevels.any { it.profile in ten }) 10 else 8
    }

    /** Codecs de software do AOSP (a mesma regra do MediaCodecExport no motor). */
    private fun softwareName(name: String): Boolean =
        name.startsWith("OMX.google.") || name.startsWith("c2.android.") || name.startsWith("c2.google.") ||
            name.startsWith("OMX.ffmpeg.") || name.contains(".sw.")

    /** Uma linha é melhor que outra se é de hardware, ou se é maior. */
    private fun better(a: IntArray, b: IntArray): Boolean {
        if (a[2] != b[2]) return a[2] > b[2]
        return a[3].toLong() * a[4] > b[3].toLong() * b[4]
    }

    private fun tagFor(mime: String): Int? = when (mime.lowercase()) {
        "video/avc" -> TAG_AVC1
        "video/hevc" -> TAG_HVC1
        "video/av01" -> TAG_AV01
        "video/x-vnd.on2.vp9" -> TAG_VP09
        else -> null
    }

    /** Quantos codecs de hardware o aparelho tem, para o resumo dos Ajustes. */
    fun hardwareDecoderCount(context: Context): Int {
        val table = prefs(context).getString(KEY_CODECS, null) ?: return 0
        val ints = table.split(',').mapNotNull { it.toIntOrNull() }
        var n = 0
        var at = 0
        while (at + CODEC_STRIDE <= ints.size) {
            if (ints[at] != 0 && ints[at + 1] == 0 && ints[at + 2] == 1) ++n
            at += CODEC_STRIDE
        }
        return n
    }
}
