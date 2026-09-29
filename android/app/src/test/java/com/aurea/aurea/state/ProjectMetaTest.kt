package com.aurea.aurea.state

import com.aurea.aurea.home.formatFps
import com.aurea.aurea.home.projectRatio
import com.aurea.aurea.home.projectSpec
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.roundToInt

/**
 * Galaxy A32: "importei o arquivo do projeto e o app fechou; limpei o cache e
 * não abre mais". O import grava o sidecar só com o título; `optDouble("fps")`
 * de chave ausente é NaN, e o NaN chegava a `roundToInt()` na composição do
 * cartão da Home — a cada abertura.
 */
class ProjectMetaTest {
    private val importedSidecar = JSONObject().put("title", "Viagem").toString()

    @Test fun rootCauseMissingFpsIsNaNAndRoundingNaNThrows() {
        // O que o readMeta antigo fazia: (optDouble("fps") ?: 30.0) — o Elvis nunca entra.
        val old = (JSONObject(importedSidecar).optDouble("fps")).toFloat()
        assertTrue(old.isNaN())
        assertThrows(IllegalArgumentException::class.java) { old.roundToInt() }
    }

    @Test fun importedSidecarBecomesAUsableCardAndTheHomeTextNeverThrows() {
        val e = ProjectMeta.entry("/p/Viagem.aurea", "Viagem", 1L, importedSidecar) { false }
        assertEquals("Viagem", e.title)
        assertEquals(30f, e.fps)
        assertEquals(0, e.width)
        assertEquals(0, e.height)
        assertNull(e.thumbnailPath)
        assertEquals("16:9 · 30 fps", projectSpec(e))
        assertEquals(16f / 9f, projectRatio(e), 1e-6f)
    }

    @Test fun everyBadNumberFallsBackAndNothingThrows() {
        val bad = listOf(
            """{"fps":"NaN","width":-5,"height":999999,"durationFrames":-1}""",
            """{"fps":0,"width":"x"}""",
            """{"fps":1e308}""",
            """{"fps":-30}""",
            """{"fps":null,"title":""}""",
            "não é json",
            "",
            """[1,2,3]""",
        )
        for (json in bad) {
            val e = ProjectMeta.entry("/p/a.aurea", "a", 0L, json) { true }
            assertTrue(json, e.fps.isFinite() && e.fps > 0f)
            assertTrue(json, e.width >= 0 && e.height >= 0 && e.durationFrames >= 0)
            assertEquals(json, "a", e.title)
            projectSpec(e)   // não lança
        }
        assertEquals(ProjectMeta.entry("/p/a.aurea", "a", 0L, null) { true }.fps, 30f)
    }

    @Test fun goodSidecarIsKept() {
        val json = JSONObject().put("title", "Clipe").put("width", 1080).put("height", 1920)
            .put("fps", 29.97).put("durationFrames", 300).put("thumbnail", "/t/c.jpg").toString()
        val e = ProjectMeta.entry("/p/c.aurea", "c", 5L, json) { it == "/t/c.jpg" }
        assertEquals("Clipe", e.title)
        assertEquals(1080, e.width)
        assertEquals(1920, e.height)
        assertEquals(29.97f, e.fps, 1e-4f)
        assertEquals(300, e.durationFrames)
        assertEquals("/t/c.jpg", e.thumbnailPath)
        assertEquals("9:16 · Full HD 1080p · 29.97 fps", projectSpec(e))
    }

    @Test fun formatFpsNeverThrows() {
        assertEquals("30", formatFps(Float.NaN))
        assertEquals("30", formatFps(Float.POSITIVE_INFINITY))
        assertEquals("30", formatFps(-1f))
        assertEquals("24", formatFps(24f))
        assertEquals("29.97", formatFps(29.97f))
    }
}
