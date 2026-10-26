package com.aurea.aurea.home

import com.aurea.aurea.state.ProjectEntry
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.util.Locale

/** Omega 0.3.2 (área 01): as contas da Home clara (par das funções `omegaHome*` do iOS). */
class HomeOmegaLogicTest {
    private val utc: ZoneId = ZoneOffset.UTC

    private fun entry(path: String, w: Int, h: Int, fps: Float = 30f, frames: Int = 300, size: Long = 0L, modified: Long = 0L) =
        ProjectEntry(path, path, modified, w, h, fps, frames, null, sizeBytes = size)

    @Test fun `ficha 2x2 mostra proporcao resolucao pelo lado menor tamanho e fps`() {
        val pt = Locale.forLanguageTag("pt-BR")
        val wide = projectFacts(entry("/a", 1920, 1080, size = 12_345_678), pt)
        assertEquals(ProjectFacts("16:9", "1080p", "12,3 MB", "30 fps"), wide)
        assertEquals("9:16", projectFacts(entry("/b", 1080, 1920), pt).aspect)
        assertEquals("1080p", projectFacts(entry("/b", 1080, 1920), pt).resolution)
        assertEquals("4:5", projectFacts(entry("/c", 1080, 1350), pt).aspect)
        assertEquals("1080p", projectFacts(entry("/c", 1080, 1350), pt).resolution)
        // Sidecar antigo sem medida: nada inventado.
        assertEquals(ProjectFacts("—", "—", "0 B", "29.97 fps"), projectFacts(entry("/d", 0, 0, fps = 29.97f), pt))
    }

    @Test fun `tamanho do arquivo em base 1000 com uma casa abaixo de 100`() {
        val en = Locale.US
        assertEquals("0 B", formatFileSize(0, en))
        assertEquals("860 B", formatFileSize(860, en))
        assertEquals("1.5 KB", formatFileSize(1_500, en))
        assertEquals("12.3 MB", formatFileSize(12_345_678, en))
        assertEquals("12 MB", formatFileSize(12_000_000, en))
        assertEquals("123 MB", formatFileSize(123_456_789, en))
        assertEquals("1.3 GB", formatFileSize(1_260_000_000, en))
        assertEquals("12,3 MB", formatFileSize(12_345_678, Locale.forLanguageTag("pt-BR")))
    }

    @Test fun `duracao m ss abaixo de uma hora e h mm ss acima`() {
        assertEquals("0:10", formatProjectDuration(300, 30f))
        assertEquals("2:05", formatProjectDuration(125 * 30, 30f))
        assertEquals("1:02:05", formatProjectDuration(3725 * 30, 30f))
        assertNull(formatProjectDuration(0, 30f))
        assertNull(formatProjectDuration(300, Float.NaN))
        assertNull(formatProjectDuration(300, 0f))
    }

    @Test fun `cabecalho de data so na ordem mais recentes e antes do primeiro de cada dia`() {
        val day1 = LocalDate.of(2026, 10, 26)
        val day0 = LocalDate.of(2026, 10, 25)
        val ms = { d: LocalDate, h: Int -> d.atTime(h, 0).toInstant(ZoneOffset.UTC).toEpochMilli() }
        val a = entry("/a", 1920, 1080, modified = ms(day1, 18))
        val b = entry("/b", 1920, 1080, modified = ms(day1, 9))
        val c = entry("/c", 1920, 1080, modified = ms(day0, 22))
        val recent = projectListItems(listOf(a, b, c), ProjectSort.Recent, 1, utc)
        assertEquals(
            listOf(ProjectListItem.DateHeader(day1), ProjectListItem.Cards(listOf(a)), ProjectListItem.Cards(listOf(b)),
                ProjectListItem.DateHeader(day0), ProjectListItem.Cards(listOf(c))),
            recent,
        )
        // Tablet: as linhas do mesmo dia viram uma fileira; o cabeçalho não é atravessado.
        val twoColumns = projectListItems(listOf(a, b, c), ProjectSort.Recent, 2, utc)
        assertEquals(
            listOf(ProjectListItem.DateHeader(day1), ProjectListItem.Cards(listOf(a, b)),
                ProjectListItem.DateHeader(day0), ProjectListItem.Cards(listOf(c))),
            twoColumns,
        )
        val byName = projectListItems(listOf(a, b, c), ProjectSort.Name, 1, utc)
        assertTrue(byName.none { it is ProjectListItem.DateHeader })
        assertEquals(3, byName.size)
        // As chaves da LazyColumn são únicas.
        assertEquals(recent.size, recent.map { it.key }.toSet().size)
    }

    @Test fun `colunas de linhas com no minimo 360 dp`() {
        assertEquals(1, projectColumns(360f))
        assertEquals(1, projectColumns(600f))
        assertEquals(2, projectColumns(800f))
        assertEquals(3, projectColumns(1200f))
    }

    @Test fun `menu rapido abre embaixo se couber e acima do item se nao`() {
        // 5 opções de 48 dp: 240 dp de cartão, 8 dp de folga.
        assertEquals(204f, quickMenuTop(rowTop = 100f, rowHeight = 96f, options = 5, areaBottom = 1000f), 0.01f)
        assertEquals(100f - 8f - 240f, quickMenuTop(rowTop = 100f, rowHeight = 96f, options = 5, areaBottom = 300f), 0.01f)
        // Exatamente no limite ainda cabe embaixo.
        assertEquals(204f, quickMenuTop(100f, 96f, 5, 204f + 240f), 0.01f)
    }

    @Test fun `nome sugerido e o primeiro Projeto N livre`() {
        val name = { n: Int -> "Projeto $n" }
        assertEquals("Projeto 1", suggestedProjectName(emptyList(), name))
        assertEquals("Projeto 2", suggestedProjectName(listOf("Projeto 1", "Projeto 3"), name))
        assertEquals("Projeto 3", suggestedProjectName(listOf("projeto 1", " Projeto 2 "), name))
    }

    @Test fun `fundo lembrado volta como cor ou transparente`() {
        val (green, t1) = parseBackground("0,0.5,0")
        assertArrayEquals(floatArrayOf(0f, 0.5f, 0f), green, 0.0001f)
        assertFalse(t1)
        assertTrue(parseBackground("t").second)
        assertArrayEquals(floatArrayOf(0f, 0f, 0f), parseBackground("lixo").first, 0f)
        assertArrayEquals(floatArrayOf(0f, 0f, 0f), parseBackground(null).first, 0f)
        assertArrayEquals(floatArrayOf(0f, 0f, 0f), parseBackground("2,0,0").first, 0f)
    }

    @Test fun `novidades paginadas de quatro em quatro com a versao no alto`() {
        val (header, pages) = releaseNotePages("Aurea Beta 0.3.2 (2199) • 26/10/2026\n\n• um\n• dois\n• tres\n• quatro\n• cinco")
        assertEquals("Aurea Beta 0.3.2 (2199) • 26/10/2026", header)
        assertEquals(listOf(listOf("um", "dois", "tres", "quatro"), listOf("cinco")), pages)
        assertEquals(1, releaseNotePages("so o titulo").second.size)
    }

    @Test fun `tabela do sobre vira texto para copiar`() {
        assertEquals("Versão: 0.3.2\nAparelho: Moto g52", aboutPlainText(listOf(AboutRow("Versão", "0.3.2"), AboutRow("Aparelho", "Moto g52"))))
    }
}
