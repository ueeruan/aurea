package com.aurea.aurea.engine

/**
 * O que o MOTOR decidiu para este aparelho, em números.
 *
 * Existe para a UI poder mostrar a decisão com lastro em vez de um "otimizado"
 * sem número atrás — e para a folha "Novo projeto" e a tela Exportar não
 * ESCONDEREM o que o aparelho não faz (Fase 8 §109): a opção continua na tela,
 * marcada, com a frase do porquê. Os dois lados leem a MESMA decisão: quem
 * decide é o motor, na subida, a partir da sondagem do aparelho e da GPU real.
 *
 * O layout dos slots é o de `write_device_report` (DeviceCapabilities.hpp):
 * mudou lá, muda aqui — e [SLOTS] tem de bater, senão o JNI recusa.
 */
class DeviceReport(private val v: LongArray) {

    val totalCores: Int get() = v[0].toInt()
    val performanceCores: Int get() = v[1].toInt()
    val efficiencyCores: Int get() = v[2].toInt()

    /** RAM total e disponível do aparelho, em MB. */
    val totalMemoryMb: Int get() = v[3].toInt()
    val availableMemoryMb: Int get() = v[4].toInt()

    /** O teto que o motor se dá para caches, quadros e texturas. */
    val budgetMb: Int get() = v[5].toInt()

    val maxTexture: Int get() = v[6].toInt()
    val maxPreviewWidth: Int get() = v[7].toInt()
    val maxPreviewHeight: Int get() = v[8].toInt()
    /** Teto do export: lado MAIOR × lado MENOR (um 1920×1080 também sai em pé). */
    val maxExportWidth: Int get() = v[9].toInt()
    val maxExportHeight: Int get() = v[10].toInt()

    val decodeParallelism: Int get() = v[11].toInt()
    val workers: Int get() = v[12].toInt()

    /** Escala inicial do preview que o motor escolheu para um 1080p (PreviewScale). */
    val initialScale: Int get() = v[13].toInt()

    /** Classe: 0 entrada (LOW) · 1 intermediário (MID) · 2 avançado (HIGH) · 3 topo (ULTRA). */
    val tier: Int get() = v[14].toInt()
    /** O que segurou a classe (DeviceLimit): 0 nada · 1 RAM · 2 RAM ocupada · 3 GPU · 4 GPU não medida · 5 CPU · 6 codecs. */
    val limit: Int get() = v[15].toInt()
    private val bits: Long get() = v[20]
    val codecsKnown: Boolean get() = (bits and 1L) != 0L
    val hwDecodeH264: Boolean get() = (bits and 2L) != 0L
    val hwDecodeHevc: Boolean get() = (bits and 4L) != 0L
    val hwDecode4K: Boolean get() = (bits and 8L) != 0L
    val encodeHevc: Boolean get() = (bits and 32L) != 0L

    /** Por que o teto de export é esse (ExportLimit): 0 sem tabela · 1 nada · 2 codificador · 3 memória · 4 sem H.264. */
    val exportLimit: Int get() = v[21].toInt()
    val hevcEncodeMaxShort: Int get() = v[23].toInt()

    /** Fração do custo das operações caras do preview AGORA (classe × temperatura), em %. */
    val heavyPercent: Int get() = v[24].toInt()
    /** O preview começa em 1/N. */
    val previewStartDenominator: Int get() = v[25].toInt()
    val effectPreviewSide: Int get() = v[26].toInt()
    val decodeCachePercent: Int get() = v[27].toInt()
    /** 0 normal · 1 morno · 2 quente · 3 crítico. */
    val thermalTier: Int get() = v[28].toInt()
    val shadowSize: Int get() = v[29].toInt()
    /** Fonte com lado menor acima disto prefere proxy (0 = nunca). */
    val proxyAboveShort: Int get() = v[30].toInt()
    val maxFrequencyMhz: Int get() = v[31].toInt()

    /** Maior resolução de export (lado menor) que este aparelho aguenta, entre as oferecidas. */
    fun exportCeiling(resolutions: List<Int>): Int {
        val cap = if (maxExportHeight in 1 until Int.MAX_VALUE) maxExportHeight else return resolutions.max()
        return resolutions.filter { it <= cap }.maxOrNull() ?: resolutions.min()
    }

    /** Um quadro de lado menor [shortSide] cabe no que este aparelho exporta? */
    fun exports(shortSide: Int): Boolean = maxExportHeight <= 0 || shortSide <= maxExportHeight

    /** "Aparelho de entrada" … "Aparelho de topo". */
    fun tierLabel(): String = when (tier) {
        0 -> "Aparelho de entrada"
        1 -> "Aparelho intermediário"
        2 -> "Aparelho avançado"
        else -> "Aparelho de topo"
    }

    /** O porquê da classe, em uma frase de gente. Nulo quando nada segurou. */
    fun tierReason(): String? = when (limit) {
        1 -> "A memória (${gb(totalMemoryMb)}) é o que mais limita este celular."
        2 -> "Pouca memória livre agora (${availableMemoryMb} MB): outros apps estão ocupando."
        3 -> "A placa de vídeo (GPU) é o que mais limita este celular."
        4 -> "A placa de vídeo ainda não foi medida."
        5 -> "O processador é o que mais limita este celular."
        6 -> if (!hwDecodeHevc) "Sem decodificação de HEVC por hardware: vídeos HEVC tocam mais devagar."
             else "A decodificação de vídeo por hardware não chega a 4K."
        else -> null
    }

    /**
     * O porquê do teto de export (§109), ou nulo se o aparelho vai até 4K.
     * Ex.: "Este aparelho exporta até 1080p: o codificador de vídeo não passa de 1920 × 1080."
     */
    fun exportLimitReason(): String? {
        if (maxExportHeight <= 0 || maxExportHeight >= 2160) return null
        val top = shortLabel(maxExportHeight)
        return when (exportLimit) {
            2 -> "Este aparelho exporta até $top: o codificador de vídeo dele não passa de $maxExportWidth × $maxExportHeight."
            3 -> "Este aparelho exporta até $top: a memória não comporta os quadros de uma exportação maior."
            4 -> "Este aparelho exporta até $top: o sistema não informou codificador H.264."
            else -> "Este aparelho exporta até $top."
        }
    }

    /** HEVC dá para escolher no export? Sem tabela de codecs (não medido), não se esconde nada. */
    val hevcExportAvailable: Boolean get() = !codecsKnown || encodeHevc

    /** A frase do HEVC indisponível, ou nulo. */
    fun hevcExportReason(): String? =
        if (hevcExportAvailable) null else "HEVC indisponível neste aparelho: ele não tem codificador HEVC. O vídeo sai em H.264."

    /**
     * Tudo o que este aparelho NÃO faz, ou faz reduzido, em frases (§109). A
     * tela "Este aparelho" lista; nada disto é escondido.
     */
    fun limitations(): List<String> = buildList {
        exportLimitReason()?.let { add(it) }
        hevcExportReason()?.let { add(it) }
        if (codecsKnown && !hwDecodeHevc) add("Vídeos HEVC (padrão de muitas câmeras) decodificam por software neste aparelho: a prévia deles fica mais lenta.")
        if (codecsKnown && hwDecodeHevc && !hwDecode4K) add("Vídeos 4K decodificam por software neste aparelho: a prévia deles fica mais lenta.")
        if (heavyPercent in 1..25) add("Optical flow de alta qualidade indisponível na prévia agora: ela usa a mistura de quadros. O export mantém a qualidade.")
        if (tier == 0) add("Prévia começa em 1/$previewStartDenominator da resolução, com partículas e sombras mais leves. O export sai sempre em qualidade cheia.")
    }

    /** "Normal", "Morno", "Quente", "Muito quente". */
    fun thermalLabel(): String = when (thermalTier) {
        0 -> "Temperatura normal"
        1 -> "Aparelho morno: tarefas de fundo mais devagar"
        2 -> "Aparelho quente: prévia mais leve"
        else -> "Aparelho muito quente: prévia no mínimo"
    }

    /** "8 núcleos (4+4) · 5,8 GB · orçamento 1,2 GB" — a linha dos Ajustes. */
    fun summary(): String {
        val cores = if (efficiencyCores > 0) "$totalCores núcleos ($performanceCores+$efficiencyCores)"
        else "$totalCores núcleos"
        return buildString {
            append(cores)
            if (maxFrequencyMhz > 0) append(" até ").append("%.1f GHz".format(maxFrequencyMhz / 1000f))
            append(" · ")
            append(gb(totalMemoryMb))
            append(" · orçamento ")
            append(gb(budgetMb))
        }
    }

    /** "Prévia começa em 1/4 · efeitos pesados a 50% · cache de decodificação 16%" — o plano, em números. */
    fun planSummary(): String = buildString {
        append(if (previewStartDenominator > 1) "Prévia começa em 1/$previewStartDenominator" else "Prévia começa cheia")
        append(" · efeitos pesados a ").append(heavyPercent).append("%")
        append(" · sombra ").append(shadowSize).append(" px")
        append(" · ").append(decodeCachePercent).append("% da memória para vídeo decodificado")
        if (proxyAboveShort > 0) append(" · versão leve (proxy) para vídeos acima de ${shortLabel(proxyAboveShort)}")
    }

    private fun gb(mb: Int): String = "%.1f GB".format(mb / 1024f)

    companion object {
        const val SLOTS = 32

        /** 720p, 1080p, 1440p, 4K. */
        fun shortLabel(short: Int): String = when {
            short >= 2160 -> "4K"
            short >= 1440 -> "1440p"
            short >= 1080 -> "1080p"
            short >= 720 -> "720p"
            else -> "${short}p"
        }
    }
}
