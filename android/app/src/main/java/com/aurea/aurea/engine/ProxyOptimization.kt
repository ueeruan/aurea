package com.aurea.aurea.engine

/**
 * "Otimizar vídeos pesados" (0.2.5): o proxy de edição como o motor informa
 * (media/PreviewProxy.hpp). QUANDO otimizar é decisão do motor — o custo de
 * decodificar a fonte (4K, acima de 60 fps, HEVC/10 bits, bitrate alto), em
 * qualquer aparelho. Aqui só o modo escolhido e a leitura do progresso.
 *
 * Os números batem com o C++ e com o AureaEngine.mm (iOS): o mesmo código tem
 * o mesmo significado nos dois sistemas.
 */
enum class ProxyMode(val code: Int) {
    /** Só o que pesa para decodificar. */
    AUTOMATIC(0),
    /** Também tudo acima de 720p. */
    ALWAYS(1),
    /** Nunca gera nem usa a versão leve. */
    OFF(2);

    companion object {
        fun of(code: Int): ProxyMode = entries.firstOrNull { it.code == code } ?: AUTOMATIC
    }
}

/** `ProxyState` do C++, na mesma ordem. */
enum class ProxyClipState { NONE, QUEUED, GENERATING, READY, FAILED }

/** Proxy do vídeo de uma camada. [progress] 0..1, só em [ProxyClipState.GENERATING]. */
data class ProxyClipStatus(val state: ProxyClipState, val progress: Float) {
    companion object {
        val NONE = ProxyClipStatus(ProxyClipState.NONE, 0f)

        /** Estado nos 8 bits baixos, progresso em milésimos acima (nativeProxyStatus). */
        fun decode(packed: Int): ProxyClipStatus = ProxyClipStatus(
            ProxyClipState.entries.getOrElse(packed and 0xFF) { ProxyClipState.NONE },
            ((packed ushr 8) / 1000f).coerceIn(0f, 1f),
        )
    }
}

/** Os pedidos desta sessão de projeto (nativeProxySummary). */
data class ProxySummary(
    val queued: Int,
    val generating: Int,
    val ready: Int,
    val failed: Int,
    /** Do vídeo sendo otimizado agora, 0..1. */
    val progress: Float,
) {
    /** Algo esperando ou sendo otimizado. */
    val busy: Boolean get() = queued > 0 || generating > 0

    /** "42" — o número do selo "Otimizando 42%". */
    val percent: Int get() = (progress * 100f).toInt().coerceIn(0, 99)

    companion object {
        const val SLOTS = 5
        val EMPTY = ProxySummary(0, 0, 0, 0, 0f)

        fun decode(v: LongArray): ProxySummary = ProxySummary(
            v[0].toInt(), v[1].toInt(), v[2].toInt(), v[3].toInt(), (v[4] / 1000f).coerceIn(0f, 1f),
        )
    }
}
