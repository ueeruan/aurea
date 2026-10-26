package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega fundação (F6): a geometria da referência no telefone em retrato — topo
 * 42, transporte 46, zona inferior fixa de 280 (timeline 210 + barra de
 * adicionar 70, ou a timeline com o painel de 200 POR CIMA) e a prévia, que é
 * o resto, igual em todos os estados.
 */
class EditorLayoutTest {
    private val phones = listOf(640f, 700f, 760f, 780f, 844f, 900f, 960f)
    private val allContents = SheetContent.entries

    @Test fun barsHaveTheReferenceHeights() {
        assertEquals(42f, EditorLayout.TOP_BAR, 0f)
        assertEquals(46f, EditorLayout.TRANSPORT, 0f)
        assertEquals(280f, EditorLayout.bottomZone(), 0f)
        assertEquals(200f, EditorLayout.layerPanel(), 0f)
        assertEquals(EditorLayout.BOTTOM_ZONE, EditorLayout.LAYER_PANEL + EditorLayout.TIMELINE_FOCUS, 0f)
    }

    @Test fun previewIsTheRestAndNeverMovesWhenPanelsOpenOrClose() {
        for (h in phones) {
            val previews = allContents.map { EditorLayout.solve(h, it, false, 375f, 9f / 16f).preview }
            // P0-1 da área 02: prévia = H − 368 em todos os estados do retrato.
            for (p in previews) assertEquals("preview at $h", h - 368f, p, 0.01f)
            // O aspecto do projeto e a altura arrastada antiga não mexem mais na prévia (D2).
            assertEquals(h - 368f, EditorLayout.solve(h, SheetContent.None, false, 375f, 16f / 9f, preferred = 300f).preview, 0.01f)
        }
    }

    @Test fun withoutLayerTheTimelineAndTheAddBarShareTheZone() {
        for (h in phones) {
            val bar = EditorLayout.solve(h, SheetContent.AddBar, false)
            assertFalse(bar.overlay)
            assertEquals(210f, bar.timeline, 0.01f)
            assertEquals(EditorLayout.ADD_BAR, bar.sheet, 0.01f)
            assertEquals(280f, bar.zone, 0.01f)
            assertEquals(h, bar.total, 0.01f)
            val none = EditorLayout.solve(h, SheetContent.None, false)
            assertEquals(280f, none.timeline, 0.01f)
            assertEquals(0f, none.sheet, 0.01f)
        }
    }

    @Test fun withLayerThePanelCoversTheZoneAndLeavesEightyOfTimeline() {
        for (h in phones) for (content in listOf(SheetContent.Dock, SheetContent.Panel, SheetContent.Curve)) {
            val m = EditorLayout.solve(h, content, false)
            assertTrue("$content overlay", m.overlay)
            assertEquals("$content timeline", 280f, m.timeline, 0.01f)
            assertEquals("$content sheet", 200f, m.sheet, 0.01f)
            assertEquals("$content visible", 80f, m.visibleTimeline, 0.01f)
            assertEquals("$content obscured", 200f, m.obscuredTimeline, 0.01f)
            assertEquals(h, m.total, 0.01f)
        }
    }

    @Test fun oneRowDockIsShorterButThePreviewStays() {
        val two = EditorLayout.solve(780f, SheetContent.Dock, false)
        val one = EditorLayout.solve(780f, SheetContent.Dock, false, dockRows = 1)
        assertEquals(EditorLayout.dock(1), one.sheet, 0.01f)
        assertEquals(EditorLayout.DOCK, EditorLayout.dock(2), 0.01f)
        assertEquals(two.preview, one.preview, 0.01f)
        assertEquals(two.timeline, one.timeline, 0.01f)
    }

    @Test fun stackedSheetsStayInsideTheZone() {
        for (h in phones) {
            val batch = EditorLayout.solve(h, SheetContent.Batch, false)
            assertEquals(12f + 120f, batch.sheet, 0.01f)
            assertEquals(280f - 132f, batch.timeline, 0.01f)
            val adding = EditorLayout.solve(h, SheetContent.Adding, false)
            assertEquals(280f, adding.zone, 0.01f)
            assertTrue(adding.timeline >= 0f)
        }
    }

    // Omega adicionar (área 08, P0-1): adicionar NÃO muda a zona — a barra fica
    // onde está e o conteúdo de 193 fica por cima da timeline, logo acima dela.
    @Test fun addingKeepsTheBarAndOverlaysTheContent() {
        for (h in phones) {
            val adding = EditorLayout.solve(h, SheetContent.Adding, false)
            val bar = EditorLayout.solve(h, SheetContent.AddBar, false)
            assertFalse(adding.overlay)
            assertEquals(bar.preview, adding.preview, 0.01f)
            assertEquals(bar.timeline, adding.timeline, 0.01f)
            assertEquals(EditorLayout.ADD_BAR, adding.sheet, 0.01f)
            assertEquals(193f, EditorLayout.addContent(adding), 0.01f)
            assertTrue(EditorLayout.addContent(adding) <= adding.timeline)
        }
    }

    @Test fun largerFontsGrowThePanelAndTheZone() {
        val m = EditorLayout.solve(900f, SheetContent.Panel, false, fontScale = 1.5f)
        assertEquals(220f, m.sheet, 0.01f)
        assertEquals(300f, m.timeline, 0.01f)
        assertEquals(80f, m.visibleTimeline, 0.01f)
        assertEquals(900f - 42f - 46f - 300f, m.preview, 0.01f)
        assertTrue(EditorLayout.dockTile(2f) > EditorLayout.dockTile())
        assertTrue(EditorLayout.DOCK_QUICK >= 48f)
        assertTrue(EditorLayout.DOCK_TILE >= 48f)
    }

    @Test fun shortScreensAndLargeFontsKeepEveryZoneInsideTheWindow() {
        for (height in listOf(240f, 320f, 480f, 568f, 720f, 960f)) {
            for (content in allContents) for (font in listOf(1f, 1.3f, 2f)) {
                val m = EditorLayout.solve(height, content, false, 320f, 9f / 16f, fontScale = font)
                assertTrue(m.preview > 0f && m.timeline >= 0f && m.sheet >= 0f)
                assertEquals(height, m.total, .01f)
                if (m.overlay) assertTrue(m.sheet <= m.timeline)
            }
        }
    }

    @Test fun fullscreenIsPreviewAndTransportOnly() {
        val m = EditorLayout.solve(780f, SheetContent.Panel, true)
        assertEquals(780f - EditorLayout.TRANSPORT, m.preview, 0.01f)
        assertEquals(0f, m.timeline + m.sheet + m.topBar, 0.01f)
    }

    @Test fun moreToolsScrollWithoutAddingAnotherRow() {
        assertEquals(listOf(4), dockRows(4))
        assertEquals(listOf(5), dockRows(5))
        assertEquals(listOf(6), dockRows(6))
        assertEquals(listOf(7), dockRows(7))
        assertEquals(listOf(8), dockRows(8))
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
        assertTrue(EditorLayout.isWide(560f, 320f))
        // Janela quadrada (Stage Manager / multitarefa): empilhado.
        assertTrue(!EditorLayout.isWide(1000f, 1000f))
    }

    @Test fun portraitTabletZonesAreAllVisibleAndFillTheScreen() {
        for ((w, h) in portraitTablets) for (content in allContents) for (aspect in listOf(0f, 9f / 16f, 16f / 9f, 1f)) {
            val m = EditorLayout.solve(h, content, false, w - 16f, aspect)
            val label = "$w x $h $content aspect $aspect"
            assertEquals(label, EditorLayout.TOP_BAR, m.topBar, 0.01f)
            assertEquals(label, EditorLayout.TRANSPORT, m.transport, 0.01f)
            assertEquals(label, h - 368f, m.preview, 0.01f)
            assertEquals(label, 280f, m.zone, 0.01f)
            assertEquals(label, h, m.total, 0.01f)
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
            assertTrue("sheet $sheet", sheet in 240f..420f && h - sheet > 400f)
        }
    }
}
