package com.aurea.aurea.home

import com.aurea.aurea.state.ProjectEntry
import java.util.Locale
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

/** Um formato de quadro oferecido ao criar um projeto (`project_presets.dart`). */
internal data class AspectOption(val key: String, val label: String, val hint: String, val ratio: Float)

internal object ProjectPresets {
    val aspects = listOf(
        AspectOption("16:9", "16:9", "YouTube / TV", 16f / 9f),
        AspectOption("9:16", "9:16", "Reels / TikTok", 9f / 16f),
        AspectOption("1:1", "1:1", "Feed", 1f),
        AspectOption("4:5", "4:5", "Instagram", 4f / 5f),
        AspectOption("4:3", "4:3", "Clássico", 4f / 3f),
    )

    /** "Livre": os dois números na mão; a razão sai deles. */
    val free = AspectOption("livre", "Livre", "Você escolhe", 1f)

    val resolutions = listOf(720, 1080, 1440, 2160)
    val fpsOptions = listOf(24, 30, 60)

    fun aspectByKey(key: String): AspectOption = aspects.firstOrNull { it.key == key } ?: aspects.first()

    fun resolutionLabel(height: Int): String = when (height) {
        720 -> "HD 720p"
        1080 -> "Full HD 1080p"
        1440 -> "QHD 1440p"
        2160 -> "4K 2160p"
        else -> "${height}p"
    }
}

/** Quadro em px. */
internal data class Frame(val width: Int, val height: Int)

/**
 * O quadro de uma proporção e uma resolução, pela regra do projeto da A.01:
 * a resolução é o LADO MENOR (1080p em 9:16 = 1080 × 1920, não 608 × 1080).
 */
internal fun frameFor(ratio: Float, resolution: Int): Frame =
    if (ratio >= 1f) Frame((resolution * ratio).roundToInt(), resolution)
    else Frame(resolution, (resolution / ratio).roundToInt())

/** fps como o app mostra: inteiro quando é inteiro (30), senão uma casa (29,97 → "29.97"). */
internal fun formatFps(fps: Float): String {
    val whole = fps.roundToInt()
    return if (abs(fps - whole) < 0.01f) whole.toString()
    else String.format(Locale.ROOT, "%.2f", fps).trimEnd('0').trimEnd('.')
}

/**
 * A ficha de um projeto: "4:5 · Full HD 1080p · 30 fps" (`fichaDoProjeto`).
 * Proporção = o preset cuja razão difere < 0,01 (senão o primeiro, "16:9",
 * como na A.01); resolução = rótulo do lado menor. Sem medida gravada (sidecar
 * antigo) a resolução fica de fora em vez de mostrar "0p".
 */
internal fun projectSpec(p: ProjectEntry): String {
    val ratio = if (p.width > 0 && p.height > 0) p.width.toFloat() / p.height else 0f
    val aspect = ProjectPresets.aspects.firstOrNull { abs(it.ratio - ratio) < 0.01f } ?: ProjectPresets.aspects.first()
    val side = min(p.width, p.height)
    val fps = formatFps(p.fps)
    return if (side > 0) "${aspect.label} · ${ProjectPresets.resolutionLabel(side)} · $fps fps"
    else "${aspect.label} · $fps fps"
}

/** Razão para a moldura do placeholder (≤ 0 → 16:9, como na A.01). */
internal fun projectRatio(p: ProjectEntry): Float =
    if (p.width > 0 && p.height > 0) p.width.toFloat() / p.height else 16f / 9f

/** Ordem da lista (`OrdemDosProjetos`); o índice é o que vai para as preferências. */
internal enum class ProjectSort(val label: String) {
    Recent("Mais recentes"),
    Name("Nome (A-Z)"),
    Longest("Mais longos");

    companion object {
        fun fromIndex(i: Int): ProjectSort = entries.getOrElse(i) { Recent }
    }
}

/**
 * A lista arrumada (`projetosArrumados`): filtra pelo nome (contém, sem
 * diferenciar maiúsculas) e ordena. "Mais recentes" = a ordem do store (o
 * mais novo primeiro).
 */
internal fun arrangeProjects(all: List<ProjectEntry>, sort: ProjectSort, query: String): List<ProjectEntry> {
    val q = query.trim()
    val filtered = if (q.isEmpty()) all else all.filter { it.title.contains(q, ignoreCase = true) }
    return when (sort) {
        ProjectSort.Recent -> filtered
        ProjectSort.Name -> filtered.sortedBy { it.title.lowercase() }
        ProjectSort.Longest -> filtered.sortedByDescending { if (it.fps > 0f) it.durationFrames / it.fps else 0f }
    }
}
