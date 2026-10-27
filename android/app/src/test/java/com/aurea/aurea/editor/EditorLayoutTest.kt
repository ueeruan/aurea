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
        // 0.3.4 painéis: a faixa de foco é régua 42 + linha 28 + folga 10.
        assertEquals(EditorLayout.TIMELINE_FOCUS, EditorLayout.timelineFocus(), 0f)
        assertEquals(42f + 28f + 10f, EditorLayout.timelineFocus(), 0f)
    }

    @Test fun previewIsTheRestAndNeverMovesWhenPanelsOpenOrClose() {
        for (h in phones) {
            val previews = allContents.map { EditorLayout.solve(h, it, false, 375f, 9f / 16f).preview }
            // P0-1 da área 02: prévia = H − 368 em todos os estados do retrato.
            for (p in previews) assertEquals("preview at $h", h - 368f, p, 0.01f)
            // O aspecto do projeto não mexe na prévia.
            assertEquals(h - 368f, EditorLayout.solve(h, SheetContent.None, false, 375f, 16f / 9f).preview, 0.01f)
        }
    }

    // 0.3.3 recursos: a divisa palco/timeline voltou (arrastar o transporte).
    @Test fun dividerRaisesTheTimelineAndThePanelFollowsItUp() {
        for (h in phones) {
            val p = h - 368f - 100f   // a timeline subiu 100
            val bar = EditorLayout.solve(h, SheetContent.AddBar, false, preferred = p)
            assertEquals(p, bar.preview, 0.01f)
            assertEquals(380f, bar.zone, 0.01f)
            assertEquals(310f, bar.timeline, 0.01f)
            assertEquals(EditorLayout.ADD_BAR, bar.sheet, 0.01f)
            assertEquals(h, bar.total, 0.01f)
            // A doca da camada tem a altura do conteúdo (no máximo 200).
            val dock = EditorLayout.solve(h, SheetContent.Dock, false, preferred = p)
            assertEquals(p, dock.preview, 0.01f)
            assertEquals(200f, dock.sheet, 0.01f)
            for (content in listOf(SheetContent.Panel, SheetContent.Curve)) {
                val m = EditorLayout.solve(h, content, false, preferred = p)
                // 0.3.4 painéis: prévia onde a pessoa deixou; o painel sobe até logo
                // abaixo da linha em foco (régua 42 + linha 28 + 10), sem vazio por cima.
                assertEquals("$content preview", p, m.preview, 0.01f)
                assertTrue(m.overlay)
                assertEquals("$content sheet", 300f, m.sheet, 0.01f)
                assertEquals("$content visible", 80f, m.visibleTimeline, 0.01f)
                assertEquals(h, m.total, 0.01f)
            }
        }
    }

    // 0.3.4 painéis: em qualquer altura da divisa e da fonte, o painel aberto deixa à
    // vista só a régua, a linha em foco e a folga.
    @Test fun openPanelAlwaysEndsJustBelowTheFocusedRow() {
        for (h in phones) for (font in listOf(1f, 1.3f, 2f)) for (pref in listOf(0f, 120f, h - 368f - 200f, h - 368f + 60f)) {
            for (content in listOf(SheetContent.Panel, SheetContent.Curve)) {
                val m = EditorLayout.solve(h, content, false, preferred = pref, fontScale = font)
                val label = "$h $font $pref $content"
                assertTrue(label, m.overlay)
                // 0.3.6: até o teto do painel; passando dele, a timeline fica com o resto.
                assertEquals(label, maxOf(EditorLayout.timelineFocus(font), m.zone - EditorLayout.panelMax(font)), m.visibleTimeline, 0.01f)
                assertTrue(label, m.sheet >= EditorLayout.layerPanel(font) - 0.01f)
                assertTrue(label, m.sheet <= EditorLayout.panelMax(font) + 0.01f)
                assertEquals(label, h, m.total, 0.01f)
            }
        }
    }

    // 0.3.6: com a divisa no alto o painel não passa do teto (era a zona inteira).
    @Test fun tallZonesCapThePanelAndGiveTheRestToTheTimeline() {
        val h = 960f
        val m = EditorLayout.solve(h, SheetContent.Panel, false, preferred = 120f)
        assertTrue(m.overlay)
        assertEquals(EditorLayout.panelMax(), m.sheet, 0.01f)
        assertEquals(m.zone - EditorLayout.panelMax(), m.visibleTimeline, 0.01f)
        assertTrue(m.visibleTimeline > EditorLayout.timelineFocus())
        assertEquals(h, m.total, 0.01f)
        // Na zona padrão nada muda: 200 + 80.
        assertEquals(200f, EditorLayout.solve(h, SheetContent.Panel, false).sheet, 0.01f)
    }

    @Test fun dividerLowersTheTimelineButPanelsGetTheWholeZone() {
        for (h in phones) {
            val p = h - 368f + 60f   // a timeline desceu 60
            val bar = EditorLayout.solve(h, SheetContent.AddBar, false, preferred = p)
            assertEquals(p, bar.preview, 0.01f)
            assertEquals(220f, bar.zone, 0.01f)
            assertEquals(150f, bar.timeline, 0.01f)
            assertEquals(EditorLayout.ADD_BAR, bar.sheet, 0.01f)
            assertEquals(h, bar.total, 0.01f)
            // Painel e adicionar abertos precisam da zona inteira (como na 0.3.2): a prévia
            // devolve o espaço enquanto estão abertos; o painel continua 200 + 80.
            for (content in listOf(SheetContent.Dock, SheetContent.Panel, SheetContent.Curve, SheetContent.Adding)) {
                val m = EditorLayout.solve(h, content, false, preferred = p)
                assertEquals("$content preview", h - 368f, m.preview, 0.01f)
                assertEquals("$content zone", 280f, m.zone, 0.01f)
                assertEquals(h, m.total, 0.01f)
            }
            assertEquals(200f, EditorLayout.solve(h, SheetContent.Panel, false, preferred = p).sheet, 0.01f)
        }
    }

    @Test fun dividerIsClampedBetweenTheFloorsAndZeroMeansAutomatic() {
        for (h in phones) {
            val ws = h - EditorLayout.TOP_BAR - EditorLayout.TRANSPORT
            assertEquals(ws - EditorLayout.ZONE_MIN, EditorLayout.maxPreview(h), 0.01f)
            val tall = EditorLayout.solve(h, SheetContent.AddBar, false, preferred = 10_000f)
            assertEquals(EditorLayout.maxPreview(h), tall.preview, 0.01f)
            assertEquals(EditorLayout.TIMELINE_MIN, tall.timeline, 0.01f)
            val short = EditorLayout.solve(h, SheetContent.None, false, preferred = 1f)
            assertEquals(EditorLayout.PREVIEW_MIN, short.preview, 0.01f)
            assertEquals(h, short.total, 0.01f)
            assertEquals(h - 368f, EditorLayout.solve(h, SheetContent.AddBar, false, preferred = 0f).preview, 0.01f)
        }
        // Telas curtas com a divisa: as zonas continuam dentro da janela.
        for (height in listOf(240f, 320f, 480f, 568f)) for (content in allContents) for (pref in listOf(1f, 200f, 10_000f)) {
            val m = EditorLayout.solve(height, content, false, preferred = pref)
            assertTrue(m.preview > 0f && m.timeline >= 0f && m.sheet >= 0f)
            assertEquals(height, m.total, .01f)
            if (m.overlay) assertTrue(m.sheet <= m.timeline)
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
        // 0.3.4 painéis: a faixa de foco cresce com a régua (+7) e a linha (+12): 99.
        assertEquals(99f, EditorLayout.timelineFocus(1.5f), 0.01f)
        assertEquals(220f, m.sheet, 0.01f)
        assertEquals(319f, m.timeline, 0.01f)
        assertEquals(99f, m.visibleTimeline, 0.01f)
        assertEquals(900f - 42f - 46f - 319f, m.preview, 0.01f)
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
