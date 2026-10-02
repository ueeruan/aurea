package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EditorLayoutTest {
    @Test fun curvesStayCompactWithoutShrinkingOtherTools() {
        for (height in listOf(568f, 640f, 780f, 960f, 1200f)) {
            val curve = EditorLayout.solve(height, SheetContent.Curve, false)
            val panel = EditorLayout.solve(height, SheetContent.Panel, false)
            assertTrue(curve.sheet <= 280.01f)
            assertTrue(curve.sheet >= 230f)
            assertTrue(curve.timeline >= 110f)
            assertTrue(curve.preview >= panel.preview)
            assertTrue(curve.sheet <= panel.sheet)
            assertEquals(height, curve.topBar + curve.preview + curve.strip + curve.transport + curve.timeline + curve.sheet, 0.01f)
        }
    }
    @Test fun wideProjectPreviewFitsTheCanvasAndGivesTheRestToTheTimeline() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val width = 390f
            val tall = EditorLayout.solve(height, SheetContent.None, false, width, 9f / 16f)
            val plain = EditorLayout.solve(height, SheetContent.None, false)
            // 9:16: o quadro passa da fração — tudo como antes.
            assertEquals(plain.preview, tall.preview, 0.01f)
            assertEquals(plain.timeline, tall.timeline, 0.01f)
            // 16:9: o palco é o quadro + a folga; a sobra vai inteira para a timeline.
            val wide = EditorLayout.solve(height, SheetContent.None, false, width, 16f / 9f)
            assertEquals(width * 9f / 16f + EditorLayout.PREVIEW_FIT_MARGIN, wide.preview, 0.01f)
            assertEquals(plain.timeline + (plain.preview - wide.preview), wide.timeline, 0.01f)
            assertEquals(height, wide.topBar + wide.preview + wide.strip + wide.transport + wide.timeline + wide.sheet, 0.01f)
            // Painel aberto: a folha segue a mesma regra; o que o palco não usa vira timeline.
            val panel = EditorLayout.solve(height, SheetContent.Panel, false, width, 16f / 9f)
            assertEquals(EditorLayout.solve(height, SheetContent.Panel, false).sheet, panel.sheet, 0.01f)
            assertTrue(panel.timeline >= 110f)
        }
    }

    @Test fun draggedPreviewHeightIsClampedAndWins() {
        for (height in listOf(640f, 780f, 960f)) {
            val big = EditorLayout.solve(height, SheetContent.None, false, 390f, 16f / 9f, preferred = 5000f)
            // O maior palco deixa a timeline no piso (120 sem nada escolhido).
            assertEquals(120f, big.timeline, 0.01f)
            assertTrue(big.preview <= EditorLayout.maxPreview(height))
            val small = EditorLayout.solve(height, SheetContent.None, false, 390f, 9f / 16f, preferred = 10f)
            assertEquals(EditorLayout.PREVIEW_MIN, small.preview, 0.01f)
            val chosen = EditorLayout.solve(height, SheetContent.None, false, 390f, 16f / 9f, preferred = 300f)
            assertEquals(300f, chosen.preview, 0.01f)
            assertEquals(height, chosen.topBar + chosen.preview + chosen.strip + chosen.transport + chosen.timeline + chosen.sheet, 0.01f)
        }
    }

    @Test fun editingPanelsKeepUsableSpaceOnPhoneScreens() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val overview = EditorLayout.solve(height, SheetContent.None, false)
            val editing = EditorLayout.solve(height, SheetContent.Panel, false)
            val dock = EditorLayout.solve(height, SheetContent.Dock, false)
            // Painel de 336 sempre que a tela deixa (topo 64 + transporte 60 do
            // redesenho); numa tela baixa ele leva tudo o que sobra sobre os pisos.
            val room = EditorLayout.workspace(height) - EditorLayout.PREVIEW_MIN - EditorLayout.TIMELINE_MIN
            assertTrue("panel at $height", editing.sheet >= minOf(336f, room) - 0.01f)
            // Doca compacta: a altura do conteúdo, o resto fica para a timeline.
            assertEquals("dock at $height", EditorLayout.DOCK, dock.sheet, 0.01f)
            assertTrue(dock.timeline >= 110f)
            assertTrue(editing.preview >= 96f)
            assertTrue(editing.preview < overview.preview)
            assertTrue(editing.timeline >= 110f)
            assertEquals(height, editing.topBar + editing.preview + editing.strip +
                editing.transport + editing.timeline + editing.sheet, 0.01f)
            assertEquals(height * EditorLayout.PREVIEW_NATURAL_FRACTION, overview.preview, 0.01f)
        }
    }
    @Test fun batchSheetFitsTheStaggerRowAtFingerSize() {
        // Puxador 12 + 4 + tempo 52 + 8 + tela 48 + 8 + escalonar 48: nada espremido.
        val content = 12f + 4f + 52f + 8f + 48f + 8f + 48f
        for (height in listOf(568f, 640f, 720f, 780f, 840f, 960f)) {
            val batch = EditorLayout.solve(height, SheetContent.Batch, false)
            assertTrue("batch at $height: ${batch.sheet}", batch.sheet >= content)
            assertTrue(batch.timeline >= 110f)
            assertEquals(height, batch.topBar + batch.preview + batch.strip + batch.transport + batch.timeline + batch.sheet, 0.01f)
        }
    }
    @Test fun oneRowDockGivesTheFreedHeightToTheTimeline() {
        // Doca enxuta (≤ 4 fichas): uma fileira só, sem faixa vazia; o palco não mexe.
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val two = EditorLayout.solve(height, SheetContent.Dock, false)
            val one = EditorLayout.solve(height, SheetContent.Dock, false, dockRows = 1)
            assertEquals("one row at $height", EditorLayout.dock(1), one.sheet, 0.01f)
            assertEquals(EditorLayout.DOCK, EditorLayout.dock(2), 0.01f)
            assertEquals(two.preview, one.preview, 0.01f)
            assertEquals(two.timeline + (two.sheet - one.sheet), one.timeline, 0.01f)
        }
    }
    @Test fun dockRowsPutTheLargerHalfBelow() {
        assertEquals(listOf(4), dockRows(4))
        assertEquals(listOf(2, 3), dockRows(5))
        assertEquals(listOf(3, 3), dockRows(6))
        assertEquals(listOf(3, 4), dockRows(7))
        assertEquals(listOf(4, 4), dockRows(8))
    }
    @Test fun addBarSitsUnderTheTimelineWithoutMovingThePreview() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val overview = EditorLayout.solve(height, SheetContent.None, false)
            val bar = EditorLayout.solve(height, SheetContent.AddBar, false)
            assertEquals("bar at $height", EditorLayout.ADD_BAR, bar.sheet, 0.01f)
            assertEquals(overview.preview, bar.preview, 0.01f)
            assertTrue(bar.timeline >= 110f)
            assertEquals(height, bar.topBar + bar.preview + bar.strip + bar.transport + bar.timeline + bar.sheet, 0.01f)
        }
    }
    // "A UI inteira sumiu" no tablet: em pé (iPad 11"/13", tablets Android de
    // 800–1032 dp) o editor usa o layout do celular; o largo é só paisagem.
    private val portraitTablets = listOf(744f to 1089f, 820f to 1136f, 834f to 1150f, 800f to 1208f,
        1024f to 1322f, 1032f to 1332f, 1280f to 1700f)

    @Test fun portraitTabletsUseThePhoneLayoutAndLandscapeUsesTheWideOne() {
        for ((w, h) in portraitTablets) {
            assertTrue("$w x $h is portrait: phone layout", !EditorLayout.isWide(w, h))
            assertTrue("$h x $w is landscape: wide layout", EditorLayout.isWide(h, w))
        }
        // Celular em pé nunca é largo; deitado (≥ 600) é.
        assertTrue(!EditorLayout.isWide(390f, 780f))
        assertTrue(EditorLayout.isWide(844f, 390f))
        assertTrue(!EditorLayout.isWide(560f, 320f))
        // Janela quadrada (Stage Manager / multitarefa): empilhado.
        assertTrue(!EditorLayout.isWide(1000f, 1000f))
    }

    @Test fun portraitTabletZonesAreAllVisibleAndFillTheScreen() {
        val contents = listOf(SheetContent.None, SheetContent.AddBar, SheetContent.Dock, SheetContent.Panel,
            SheetContent.Curve, SheetContent.Batch, SheetContent.Adding)
        for ((w, h) in portraitTablets) for (content in contents) for (aspect in listOf(0f, 9f / 16f, 16f / 9f, 1f)) {
            val m = EditorLayout.solve(h, content, false, w - 16f, aspect)
            val label = "$w x $h $content aspect $aspect"
            assertEquals(label, EditorLayout.TOP_BAR, m.topBar, 0.01f)
            assertEquals(label, EditorLayout.TRANSPORT, m.transport, 0.01f)
            assertTrue("$label preview ${m.preview}", m.preview >= EditorLayout.PREVIEW_MIN)
            assertTrue("$label timeline ${m.timeline}", m.timeline >= 110f)
            if (content != SheetContent.None) assertTrue("$label sheet ${m.sheet}", m.sheet > 0f)
            assertEquals(label, h, m.topBar + m.preview + m.strip + m.transport + m.timeline + m.sheet, 0.01f)
        }
    }

    @Test fun landscapeTabletWideZonesStayPositive() {
        for ((w, h) in portraitTablets) {
            // Deitado: largura = h, altura = w.
            val timeline = EditorLayout.wideTimeline(w)
            val preview = w - EditorLayout.TOP_BAR - EditorLayout.TRANSPORT - EditorLayout.STRIP - timeline - EditorLayout.ADD_BAR
            assertTrue("timeline $timeline", timeline in 88f..280f)
            assertTrue("preview $preview at $h x $w", preview >= EditorLayout.PREVIEW_MIN)
            val sheet = EditorLayout.wideSheetWidth(h)
            assertTrue("sheet $sheet", sheet in 280f..380f && h - sheet > 400f)
        }
    }
}
