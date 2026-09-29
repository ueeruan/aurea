package com.aurea.aurea.state

import org.json.JSONObject

/**
 * A ficha da Home (`<projeto>.aurea.meta.json`) lida SEM confiar nela.
 *
 * Galaxy A32, build 2125: "importei o arquivo do projeto e o app fechou; limpei
 * o cache e ele não abre mais". O import grava um sidecar só com o título, e
 * `JSONObject.optDouble("fps")` de chave ausente é **NaN**, não o padrão. O NaN
 * ia para `formatFps` → `roundToInt()` → IllegalArgumentException na composição
 * do cartão da Home — a cada abertura, porque o sidecar mora em `files/` (limpar
 * o cache não o apaga). Aqui todo número sai finito e dentro de uma faixa útil;
 * o que não serve vira o padrão.
 */
internal object ProjectMeta {
    const val DEFAULT_FPS = 30f
    private const val MAX_FPS = 1000f
    private const val MAX_SIDE = 65_536
    private const val MAX_TITLE = 200

    /** fps finito em (0, 1000]; o resto é 30. */
    fun sanitizeFps(value: Double?): Float {
        val v = value ?: return DEFAULT_FPS
        if (v.isNaN() || v.isInfinite() || v <= 0.0 || v > MAX_FPS) return DEFAULT_FPS
        return v.toFloat()
    }

    fun sanitizeSide(value: Int): Int = if (value in 1..MAX_SIDE) value else 0

    /**
     * O cartão de um projeto. `json` = conteúdo do sidecar (null = não há ou
     * ilegível); `thumbExists` confere a miniatura sem tocar em disco nos testes.
     */
    fun entry(
        path: String,
        fileName: String,
        modifiedMs: Long,
        json: String?,
        thumbExists: (String) -> Boolean,
    ): ProjectEntry {
        val j = json?.let { runCatching { JSONObject(it) }.getOrNull() }
        val thumb = j?.optString("thumbnail")?.takeIf { it.isNotEmpty() && thumbExists(it) }
        val title = j?.optString("title")?.trim()?.takeIf { it.isNotEmpty() }?.take(MAX_TITLE) ?: fileName
        val fps = if (j != null && j.has("fps")) sanitizeFps(j.optDouble("fps", Double.NaN)) else DEFAULT_FPS
        val duration = j?.optInt("durationFrames", 0)?.takeIf { it >= 0 } ?: 0
        return ProjectEntry(
            path = path,
            title = title,
            modifiedMs = modifiedMs,
            width = sanitizeSide(j?.optInt("width", 0) ?: 0),
            height = sanitizeSide(j?.optInt("height", 0) ?: 0),
            fps = fps,
            durationFrames = duration,
            thumbnailPath = thumb,
        )
    }
}
