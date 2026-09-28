package com.aurea.aurea.effects

import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.EffectCatalogEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A lógica do escolhedor de efeitos (aba "Adicionar"): abas, áudio, categorias,
 * recentes, favoritos e a busca sem acento/caixa em vários idiomas.
 */
class EffectPickerLogicTest {

    private fun fx(key: String, name: String, category: String) =
        EffectCatalogEntry(typeId = effectTypeId(key), effectClass = 0, paramCount = 1, name = name, category = category)

    private val gaussian = fx("aurea.blur.gaussian", "Desfoque gaussiano", "Desfoque")
    private val lens = fx("aurea.blur.lens", "Lente", "Desfoque")
    private val glow = fx("aurea.light.glow", "Brilho", "Luz")
    private val vhs = fx("aurea.glitch.vhs", "VHS", "Glitch")
    private val reverb = fx("aurea.audio.reverb", "Reverb", "Áudio")
    private val toneByKey = fx("aurea.audio.tone", "Gerador", "Áudio")
    private val toneByName = fx("aurea.audio.qualquer", "Tom", "Áudio")
    private val catalog = listOf(gaussian, lens, glow, vhs, reverb, toneByKey, toneByName)

    @Test
    fun opensOnTheStackOnlyWhenTheLayerHasEffects() {
        assertEquals(EffectsTab.Add, initialEffectsTab(0))
        assertEquals(EffectsTab.Applied, initialEffectsTab(1))
        assertEquals(EffectsTab.Applied, initialEffectsTab(7))
    }

    @Test
    fun categoryIdsAreStableSlugsWithoutAccents() {
        assertEquals("controles_de_expressao", effectCategoryId("Controles de expressão"))
        assertEquals("audio", effectCategoryId("Áudio"))
        assertEquals("glitch", effectCategoryId("Glitch"))
        assertEquals("transicao", effectCategoryId("Transição"))
        assertTrue(effectCategoryId("???").isNotEmpty())
    }

    @Test
    fun cardIdIsTheUnsignedTypeId() {
        assertEquals("4294967295", effectCardId(-1))
        assertEquals("7", effectCardId(7))
    }

    @Test
    fun audioEffectsOnlyForLayersWithSoundButToneAlwaysShows() {
        val silent = pickableEffects(catalog, layerHasAudio = false)
        assertFalse(reverb in silent)
        assertTrue("o Tom gera som: serve até para camada muda", toneByKey in silent)
        assertTrue(toneByName in silent)
        assertTrue(gaussian in silent && glow in silent)
        assertEquals(catalog, pickableEffects(catalog, layerHasAudio = true))
        assertTrue(isAudioCategory("Áudio") && isAudioCategory("audio") && !isAudioCategory("Desfoque"))
    }

    @Test
    fun browsingFiltersByCategoryInCatalogOrder() {
        assertEquals(catalog, browseEffects(catalog, null))
        assertEquals(listOf(gaussian, lens), browseEffects(catalog, "Desfoque"))
        assertTrue(browseEffects(catalog, "Inexistente").isEmpty())
    }

    @Test
    fun recentsKeepUseOrderAndDropWhatIsNotOffered() {
        val recents = listOf(glow.typeId, 12345, gaussian.typeId, glow.typeId, reverb.typeId)
        val silent = pickableEffects(catalog, layerHasAudio = false)
        assertEquals(listOf(glow, gaussian), recentEffects(recents, silent))
    }

    @Test
    fun favoritesFollowCatalogOrder() {
        val favorites = setOf(vhs.typeId, gaussian.typeId)
        assertEquals(listOf(gaussian, vhs), favoriteEffects(favorites, catalog))
        assertTrue(favoriteEffects(emptySet(), catalog).isEmpty())
    }

    @Test
    fun pushRecentMovesToFrontWithoutDuplicatesAndCaps() {
        assertEquals(listOf(3, 1, 2), pushRecent(listOf(1, 2, 3), 3, 12))
        assertEquals(listOf(9, 1, 2), pushRecent(listOf(1, 2, 3), 9, 3))
        assertEquals(listOf(5), pushRecent(emptyList(), 5, 12))
    }

    @Test
    fun toggleFavoriteAddsAndRemoves() {
        val on = toggleFavorite(emptySet(), 4)
        assertEquals(setOf(4), on)
        assertEquals(emptySet<Int>(), toggleFavorite(on, 4))
    }

    @Test
    fun recentsSurviveTheRoundTripAndIgnoreGarbage() {
        val list = listOf(-5, 12, 700)
        assertEquals(list, parseRecents(formatRecents(list)))
        assertEquals(listOf(1, 2), parseRecents("1,,x, 2,1"))
        assertTrue(parseRecents(null).isEmpty())
        assertTrue(parseRecents("").isEmpty())
    }

    private val docs = mapOf(
        // Nome em inglês (exibido) + nomes traduzidos; o resto é categoria/descrição/sinônimo.
        gaussian.typeId to effectSearchDoc(listOf("Gaussian Blur", "Desfoque gaussiano", "Размытие по Гауссу"), listOf("Desfoque", categorySynonyms("Desfoque"))),
        lens.typeId to effectSearchDoc(listOf("Lens", "Lente"), listOf("Desfoque", "simula a lente da câmera", categorySynonyms("Desfoque"))),
        glow.typeId to effectSearchDoc(listOf("Glow", "Brilho"), listOf("Luz", "um halo de luz em volta do que é claro", categorySynonyms("Luz"))),
        vhs.typeId to effectSearchDoc(listOf("VHS"), listOf("Glitch", "fita analógica gasta, com desfoque de cor", categorySynonyms("Glitch"))),
    )
    private val searchable = listOf(gaussian, lens, glow, vhs)

    @Test
    fun searchIgnoresAccentsAndCase() {
        assertEquals(gaussian, searchEffects(searchable, docs, "GAUSSIANO").first())
        assertEquals(gaussian, searchEffects(searchable, docs, "desfoque gaussiano").first())
        assertEquals(listOf(vhs), searchEffects(searchable, docs, "analogica"))
    }

    @Test
    fun searchFindsNamesInOtherLanguagesAndCategorySynonyms() {
        assertEquals(listOf(gaussian), searchEffects(searchable, docs, "размытие по"))
        // "blur" é sinônimo da categoria Desfoque: a Lente não tem "blur" em nome nenhum.
        val blur = searchEffects(searchable, docs, "blur")
        assertTrue(gaussian in blur && lens in blur)
        assertFalse(glow in blur)
        // Achado no nome vem antes do achado só no sinônimo, qualquer que seja a ordem do catálogo.
        assertEquals(listOf(gaussian, lens), searchEffects(listOf(lens, gaussian), docs, "blur"))
    }

    @Test
    fun searchPutsNameHitsBeforeDescriptionHits() {
        // "desfoque" está no NOME pt do Gaussiano, e só na descrição/categoria dos outros.
        val r = searchEffects(searchable, docs, "desfoque")
        assertEquals(gaussian, r.first())
        assertTrue(vhs in r && lens in r)
        assertTrue(r.indexOf(gaussian) < r.indexOf(vhs))
    }

    @Test
    fun searchRanksPrefixThenWordThenInsideThenElsewhere() {
        val inside = fx("t.inside", "Excelente", "Cor")
        val prefix = fx("t.prefix", "Lente", "Cor")
        val elsewhere = fx("t.elsewhere", "Azul", "Cor")
        val word = fx("t.word", "Olho de lente", "Cor")
        val exact = fx("t.exact", "Len", "Cor")
        val d = mapOf(
            inside.typeId to effectSearchDoc(listOf("Excelente"), emptyList()),
            prefix.typeId to effectSearchDoc(listOf("Lente"), emptyList()),
            elsewhere.typeId to effectSearchDoc(listOf("Azul"), listOf("um lenço azul")),
            word.typeId to effectSearchDoc(listOf("Olho de lente"), emptyList()),
            exact.typeId to effectSearchDoc(listOf("Len"), emptyList()),
        )
        assertEquals(
            listOf(exact, prefix, word, inside, elsewhere),
            searchEffects(listOf(elsewhere, inside, word, prefix, exact), d, "LEN"),
        )
    }

    @Test
    fun everyTermMustMatchAndBlankFindsNothing() {
        assertTrue(searchEffects(searchable, docs, "vhs brilho").isEmpty())
        assertTrue(searchEffects(searchable, docs, "   ").isEmpty())
        assertEquals(listOf(glow), searchEffects(searchable, docs, "  halo   claro "))
    }

    @Test
    fun categorySynonymsCoverKnownCategoriesOnly() {
        assertTrue(categorySynonyms("Desfoque").contains("blur"))
        assertTrue(categorySynonyms("Áudio").contains("sound"))
        assertEquals("", categorySynonyms("Categoria que o motor inventou"))
    }
}
