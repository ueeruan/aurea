package com.aurea.aurea.editor

import com.aurea.aurea.state.EditorStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega conteúdo (área 06): as contas puras dos editores de conteúdo — busca do
 * navegador de fontes, a lista do mini navegador (recentes + favoritas), a
 * amostra pela escrita, os passos de tamanho e de espessura. Gêmeo dos casos do
 * `verify/check_content_editors.py` (iOS).
 */
class OmegaContentLogicTest {
    private fun font(family: String, weight: Int = 400, scripts: Int = FontScripts.LATIN) =
        EditorStore.FontItem(family, "Regular", weight, false, "/fonts/$family-$weight.ttf", false, 0, scripts)

    @Test fun searchMatchesTheStartOfTheNameOrOfAWord() {
        assertEquals(0, fontMatches("Roboto Mono", "ro"))
        assertEquals(7, fontMatches("Roboto Mono", "mo"))
        assertEquals(-1, fontMatches("Roboto Mono", "bo"))
        assertEquals(0, fontMatches("Roboto Mono", ""))
        assertEquals(7, fontMatches("Roboto Mono", "MONO"))
    }

    @Test fun miniListKeepsRecentsThenFavoritesWithTheCurrentInFront() {
        val a = font("Alpha"); val b = font("Bravo"); val c = font("Charlie"); val d = font("Delta"); val e = font("Echo")
        val all = listOf(a, b, c, d, e)
        // Sem favoritas: todas as recentes, a última usada primeiro.
        FontPrefs.recordUse(a); FontPrefs.recordUse(b); FontPrefs.recordUse(c)
        val plain = FontPrefs.miniList(all, c)
        assertEquals(listOf(c, b, a), plain.map { it.first })
        assertEquals(1, plain.first().second)   // relógio na 1ª recente
        // Com 2 favoritas: max(6 − 2, 3) = 4 recentes, depois as favoritas em A–Z.
        if (FontKeys.has(d)) FontPrefs.toggleFavorite(d)
        FontPrefs.toggleFavorite(e); FontPrefs.toggleFavorite(d)
        val mixed = FontPrefs.miniList(all, null)
        assertEquals(null, mixed.first().first)   // a fonte padrão (atual) na frente
        val names = mixed.drop(1).map { it.first!!.family }
        assertEquals(listOf("Charlie", "Bravo", "Alpha", "Delta", "Echo"), names)
        assertEquals(2, mixed.first { it.first?.family == "Delta" }.second)   // estrela na 1ª favorita
        FontPrefs.toggleFavorite(d); FontPrefs.toggleFavorite(e)
    }

    @Test fun sampleUsesTheOnlyOtherScriptOfTheFace() {
        assertEquals("Abcdefg", FontScripts.sample(FontScripts.LATIN, long = true))
        assertEquals("Abc", FontScripts.sample(FontScripts.LATIN or FontScripts.GREEK or FontScripts.CYRILLIC, long = false))
        // Japonês com ideogramas e grego de brinde: a amostra é japonesa.
        val jp = FontScripts.LATIN or FontScripts.GREEK or FontScripts.JAPANESE or FontScripts.CHINESE
        assertEquals("あいう", FontScripts.sample(jp, long = false))
        assertTrue(FontScripts.sample(FontScripts.LATIN or FontScripts.ARABIC, long = true).endsWith("Abcd"))
    }

    @Test fun sizeAndWidthSteps() {
        assertEquals(36, TextSizeSteps.size)
        assertEquals(72, TextSizeSteps[nearestSizeStep(72f)])
        assertEquals(2000, TextSizeSteps[nearestSizeStep(5000f)])
        assertEquals(5, TextSizeSteps[nearestSizeStep(1f)])
        assertEquals(42, DrawWidthSteps.size)
        for (i in 1 until DrawWidthSteps.size) assertTrue(DrawWidthSteps[i] > DrawWidthSteps[i - 1])
        assertEquals(1, DrawWidthSteps.first())
        assertEquals(300, DrawWidthSteps.last())
    }
}

/** Ajuda do teste: a face já é favorita? (o estado é do objeto, compartilhado entre testes). */
private object FontKeys {
    fun has(f: EditorStore.FontItem) = FontPrefs.key(f) in FontPrefs.favorites
}
