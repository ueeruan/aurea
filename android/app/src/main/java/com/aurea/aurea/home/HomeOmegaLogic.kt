package com.aurea.aurea.home

import com.aurea.aurea.state.ProjectEntry
import java.text.DecimalFormat
import java.text.DecimalFormatSymbols
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.util.Locale
import kotlin.math.min
import kotlin.math.roundToLong

// =============================================================================
//  Omega home (área 01): as contas da Home, puras (testáveis na JVM). Par das
//  funções `omegaHome*` do ProjectCards.swift — mesmas regras e mesmos textos.
// =============================================================================

/**
 * A ficha 2 × 2 da linha do projeto: proporção · resolução · tamanho do arquivo
 * · fps. Sem medida gravada (sidecar antigo) proporção e resolução ficam "—".
 */
internal data class ProjectFacts(val aspect: String, val resolution: String, val size: String, val fps: String)

internal fun projectFacts(p: ProjectEntry, locale: Locale = Locale.getDefault()): ProjectFacts {
    val known = p.width > 0 && p.height > 0
    return ProjectFacts(
        aspect = if (known) aspectLabel(p.width, p.height) else "—",
        resolution = if (known) "${min(p.width, p.height)}p" else "—",
        size = formatFileSize(p.sizeBytes, locale),
        fps = "${formatFps(p.fps)} fps",
    )
}

/**
 * Tamanho do arquivo como a Home mostra (base 1000, como os gerenciadores de
 * arquivo): "860 B", "12,3 MB", "1,2 GB"; uma casa abaixo de 100, sem ",0".
 */
internal fun formatFileSize(bytes: Long, locale: Locale = Locale.getDefault()): String {
    if (bytes <= 0L) return "0 B"
    if (bytes < 1000L) return "$bytes B"
    val units = listOf("KB", "MB", "GB", "TB")
    var value = bytes / 1000.0
    var unit = 0
    while (value >= 1000.0 && unit < units.size - 1) {
        value /= 1000.0
        unit++
    }
    val pattern = if (value < 100.0) "0.#" else "0"
    val text = DecimalFormat(pattern, DecimalFormatSymbols.getInstance(locale)).format(value)
    return "$text ${units[unit]}"
}

/** Duração do projeto: "m:ss" abaixo de 1 h, "h:mm:ss" acima. Nula = sem duração gravada. */
internal fun formatProjectDuration(durationFrames: Int, fps: Float): String? {
    if (durationFrames <= 0 || !fps.isFinite() || fps <= 0f) return null
    val total = (durationFrames / fps.toDouble()).roundToLong().coerceAtLeast(0L)
    val h = total / 3600
    val m = (total % 3600) / 60
    val s = total % 60
    return if (h > 0) String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s)
    else String.format(Locale.ROOT, "%d:%02d", m, s)
}

/** Um item da lista da aba Projetos: cabeçalho de data ou uma fileira de linhas (colunas no tablet). */
internal sealed interface ProjectListItem {
    val key: String

    data class DateHeader(val day: LocalDate) : ProjectListItem {
        override val key: String get() = "dia-$day"
    }

    data class Cards(val entries: List<ProjectEntry>) : ProjectListItem {
        override val key: String get() = "linha-" + entries.joinToString("|") { it.path }
    }
}

/**
 * A lista arrumada em itens: com "Mais recentes" um cabeçalho de data entra
 * antes do primeiro projeto de cada dia (referência: `P.B8K`); [columns] > 1
 * agrupa as linhas em fileiras (tablet), sem atravessar um cabeçalho.
 */
internal fun projectListItems(
    arranged: List<ProjectEntry>,
    sort: ProjectSort,
    columns: Int,
    zone: ZoneId = ZoneId.systemDefault(),
): List<ProjectListItem> {
    val cols = columns.coerceAtLeast(1)
    val out = ArrayList<ProjectListItem>()
    val run = ArrayList<ProjectEntry>()
    fun flush() {
        run.chunked(cols).forEach { out += ProjectListItem.Cards(it) }
        run.clear()
    }
    var lastDay: LocalDate? = null
    for (e in arranged) {
        if (sort == ProjectSort.Recent) {
            val day = Instant.ofEpochMilli(e.modifiedMs).atZone(zone).toLocalDate()
            if (day != lastDay) {
                flush()
                out += ProjectListItem.DateHeader(day)
                lastDay = day
            }
        }
        run += e
    }
    flush()
    return out
}

/** Quantas colunas de linhas cabem (largura mínima de 360 dp por linha, 12 dp entre elas). */
internal fun projectColumns(widthDp: Float): Int =
    (((widthDp - 32f) + 12f) / (360f + 12f)).toInt().coerceIn(1, 4)

/**
 * Onde o cartão do menu rápido abre (referência `P.euv`: altura `104 + 48 n`;
 * se passar do fim da área útil, abre ACIMA do item). Devolve o topo do cartão
 * (dp, mesma origem de [rowTop]).
 */
internal fun quickMenuTop(rowTop: Float, rowHeight: Float, options: Int, areaBottom: Float, gap: Float = 8f, optionHeight: Float = 48f): Float {
    val menu = optionHeight * options
    val below = rowTop + rowHeight + gap
    return if (below + menu <= areaBottom) below else rowTop - gap - menu
}
