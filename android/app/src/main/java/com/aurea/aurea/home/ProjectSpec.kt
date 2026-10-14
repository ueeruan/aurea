package com.aurea.aurea.home

import androidx.annotation.StringRes
import com.aurea.aurea.R
import com.aurea.aurea.state.ProjectEntry
import java.util.Locale
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Um formato de quadro oferecido ao criar um projeto.
 *
 * [key] é a IDENTIDADE (vai para as preferências e para o projeto) e nunca
 * muda; [label] e [hint] são ID de recurso, porque é o que o usuário lê. A
 * proporção em si ("16:9") é universal e fica como está.
 */
internal data class AspectOption(val key: String, val label: String, @StringRes val hint: Int, val ratio: Float)

internal object ProjectPresets {
    val aspects = listOf(
        AspectOption("16:9", "16:9", R.string.aspect_hint_tv, 16f / 9f),
        AspectOption("9:16", "9:16", R.string.aspect_hint_reels, 9f / 16f),
        AspectOption("1:1", "1:1", R.string.aspect_hint_feed, 1f),
        AspectOption("4:5", "4:5", R.string.aspect_hint_instagram, 4f / 5f),
        AspectOption("4:3", "4:3", R.string.aspect_hint_classic, 4f / 3f),
    )

    /** "Livre": os dois números na mão; a razão sai deles. */
    val free = AspectOption("livre", "1:1", R.string.aspect_free_hint, 1f)

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
    // NaN/infinito (sidecar do import sem fps) derrubava a Home a cada
    // abertura: `roundToInt` lança. Aqui nunca lança.
    if (!fps.isFinite() || fps <= 0f) return "30"
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

/** O que um chip de metadados da linha do projeto mostra (o ícone sai daqui). */
internal enum class ProjectChipKind { Fps, Aspect, Resolution, Size }

internal data class ProjectChip(val kind: ProjectChipKind, val text: String)

/**
 * Os chips da linha do projeto na lista (spec am-ui §6, P2-8): fps,
 * proporção, resolução e tamanho do quadro, todos do sidecar que a Home já
 * lê (nada de rede). Sem medida gravada (sidecar antigo) só o fps aparece —
 * um "16:9" inventado num chip parece dado. Proporção fora dos presets sai
 * reduzida ("21:9", "2.39:1"), não arredondada para 16:9. Par do
 * `homeProjectChips` do iOS.
 */
internal fun projectChips(p: ProjectEntry): List<ProjectChip> {
    val fps = ProjectChip(ProjectChipKind.Fps, "${formatFps(p.fps)} fps")
    if (p.width <= 0 || p.height <= 0) return listOf(fps)
    return listOf(
        fps,
        ProjectChip(ProjectChipKind.Aspect, aspectLabel(p.width, p.height)),
        ProjectChip(ProjectChipKind.Resolution, ProjectPresets.resolutionLabel(min(p.width, p.height))),
        ProjectChip(ProjectChipKind.Size, "${p.width} × ${p.height}"),
    )
}

/** "16:9" dos presets (< 0,01 de diferença); senão a razão reduzida, ou "x.xx:1". */
internal fun aspectLabel(width: Int, height: Int): String {
    val ratio = width.toFloat() / height
    ProjectPresets.aspects.firstOrNull { abs(it.ratio - ratio) < 0.01f }?.let { return it.label }
    var a = width
    var b = height
    while (b != 0) { val t = a % b; a = b; b = t }
    val w = width / a
    val h = height / a
    if (w <= 99 && h <= 99) return "$w:$h"
    return if (ratio >= 1f) String.format(Locale.ROOT, "%.2f:1", ratio)
    else String.format(Locale.ROOT, "1:%.2f", 1f / ratio)
}

/** Razão para a moldura do placeholder (≤ 0 → 16:9, como na A.01). */
internal fun projectRatio(p: ProjectEntry): Float =
    if (p.width > 0 && p.height > 0) p.width.toFloat() / p.height else 16f / 9f

/**
 * Ordem da lista; o índice é o que vai para as preferências.
 *
 * [label] é ID de recurso, não texto: o rótulo muda de idioma, o ÍNDICE não —
 * é ele que fica gravado, então um idioma novo nunca reordena a preferência de
 * quem já escolheu.
 */
internal enum class ProjectSort(@StringRes val label: Int) {
    Recent(R.string.home_sort_recent),
    Name(R.string.home_sort_name),
    Longest(R.string.home_sort_longest);

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
