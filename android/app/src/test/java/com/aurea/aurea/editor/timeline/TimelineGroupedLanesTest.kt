package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

/**
 * Trilhas agrupadas por propriedade (Posição X/Y/Z numa trilha), várias
 * camadas abertas, auto-rolagem em rampa, toque das trilhas baixas e seleção
 * por retângulo — o comportamento de timeline que veio do app antigo.
 */
class TimelineGroupedLanesTest {
    private fun row(id: Long, start: Int = 100, end: Int = 200, offset: Int = 20) =
        RowModel(id, LayerType.Video, start, end, offset, true, false, false, true, "Clip", 0, IntArray(0), emptyArray())

    private fun key(property: Int, time: Int, param: Int = 0, effect: Int = -1) = KeyframeRow(property, effect, time, 1f, 1, param)

    private val px = key(0, 10)
    private val py = key(1, 10)
    private val pz = key(2, 10)
    private val px2 = key(0, 40)
    private val py3 = key(1, 60)
    private val opacity = key(12, 10)
    private val scaleX = key(3, 30)

    private fun expand(open: Set<Long>, groups: Set<LaneGroupKey> = emptySet(), keys: Map<Long, List<KeyframeRow>>) =
        expandedRows(listOf(row(5), row(6)), open, groups, keys) { emptyList() }

    @Test fun multiAxisPropertyBecomesOneLaneWithTheUnionOfInstants() {
        val out = expand(setOf(5L), keys = mapOf(5L to listOf(px, py, pz, px2, py3, opacity, scaleX)))
        val position = TimelineTrack(0, -1, 0, group = true)
        val lane = out.single { it.track == position }
        assertEquals("  Position", lane.name)
        // Um losango por instante: 10 (X, Y, Z), 40 (X), 60 (Y) — na timeline (+ start − offset).
        assertArrayEquals(intArrayOf(90, 120, 140), lane.instants)
        assertEquals(setOf(px, py, pz), lane.keysAt[0].toSet())
        // Eixos por dentro ficam escondidos até abrir o grupo.
        assertTrue(out.none { it.track == TimelineTrack(0) || it.track == TimelineTrack(1) || it.track == TimelineTrack(2) })
        // Propriedade de um componente e grupo com UM eixo animado seguem trilha própria.
        assertNotNull(out.singleOrNull { it.track == TimelineTrack(12) })
        assertNotNull(out.singleOrNull { it.track == TimelineTrack(3) })
        assertTrue(out.none { it.track == TimelineTrack(3, -1, 0, group = true) })
        // A trilha de grupo mora antes da Opacidade (ordem das propriedades).
        assertTrue(out.indexOf(lane) < out.indexOfFirst { it.track == TimelineTrack(12) })
    }

    @Test fun openGroupShowsTheAxisLanesRightBelowIt() {
        val position = TimelineTrack(0, -1, 0, group = true)
        val out = expand(setOf(5L), setOf(LaneGroupKey(5L, position)), mapOf(5L to listOf(px, py, pz, opacity)))
        val at = out.indexOfFirst { it.track == position }
        assertEquals(listOf(TimelineTrack(0), TimelineTrack(1), TimelineTrack(2)), out.subList(at + 1, at + 4).map { it.track })
        assertEquals("      Position Y", out[at + 2].name)
        // O grupo aberto de OUTRA camada não abre este.
        val other = expand(setOf(5L), setOf(LaneGroupKey(6L, position)), mapOf(5L to listOf(px, py, pz)))
        assertTrue(other.none { it.track == TimelineTrack(1) })
    }

    @Test fun groupLaneDragMovesEveryAxisAtThatInstant() {
        val out = expand(setOf(5L), keys = mapOf(5L to listOf(px, py, pz, px2, py3)))
        val lane = out.single { it.track?.group == true }
        val keys = lane.keysForDrag(0, focused = false)
        assertEquals(setOf(px, py, pz), keys.toSet())
        // Limites de arrasto: nenhum eixo do grupo encosta no próprio vizinho.
        val instants = lane.dragInstants(keys)
        assertArrayEquals(intArrayOf(90, 120, 140), instants)
        val limits = IntArray(2)
        Keyframes.dragLimits(instants, 0, lane.start, lane.end, limits)
        assertEquals(90, limits[0])
        assertEquals(119, limits[1])
        // Tocar escolhe todos os eixos do instante (um passo move todos juntos).
        val sel = KeySelection.all(5L, lane.keysAt[0])
        assertEquals(3, sel.size)
        assertTrue(sel.containsAnyOn(5L, listOf(pz)))
        assertEquals(setOf(10), sel.shifted(0).keys.map { it.time }.toSet())
        assertEquals(setOf(15), sel.shifted(5).keys.map { it.time }.toSet())
    }

    @Test fun transformAndPartGroupsFollowTheEngineAxisLayout() {
        assertEquals(TimelineTrack(0, -1, 0, true), trackGroup(TimelineTrack(2)))
        assertEquals(TimelineTrack(3, -1, 0, true), trackGroup(TimelineTrack(5)))
        assertEquals(TimelineTrack(6, -1, 0, true), trackGroup(TimelineTrack(7)))
        assertEquals(TimelineTrack(9, -1, 0, true), trackGroup(TimelineTrack(11)))
        assertEquals(TimelineTrack(13, -1, 0, true), trackGroup(TimelineTrack(14)))
        assertNull(trackGroup(TimelineTrack(12)))
        assertNull(trackGroup(TimelineTrack(31, 2, 1)))
        assertEquals(TimelineTrack(42, 1, 3, true), trackGroup(TimelineTrack(42, 1, 5)))
        assertNull(trackGroup(TimelineTrack(42, 1, 9)))
    }

    @Test fun severalLayersStayExpandedAtOnce() {
        val keys = mapOf(5L to listOf(opacity), 6L to listOf(key(12, 50)))
        val out = expand(setOf(5L, 6L), keys = keys)
        // row5 + Transform + Opacity, row6 + Transform + Opacity.
        assertEquals(6, out.size)
        assertEquals(listOf(5L, 5L, 5L, 6L, 6L, 6L), out.map { it.id })
        assertNull(out[0].track)
        assertNull(out[3].track)
        assertArrayEquals(intArrayOf(130), out[5].instants)
        // Nada aberto: a MESMA lista.
        val base = listOf(row(5))
        assertSame(base, expandedRows(base, emptySet(), emptySet(), keys) { emptyList() })
        // As trilhas abertas andam com a camada dona ao reordenar.
        val groupKeys = timelineGroupKeys(out)
        assertEquals(groupKeys[0], groupKeys[2])
        assertEquals(groupKeys[3], groupKeys[5])
        assertNotEquals(groupKeys[0], groupKeys[3])
        // Com trilhas abertas a timeline não fica compacta (as regras de sempre).
        assertFalse(timelineCompact(panel = false, dock = true, tracksOpen = true, selectingKeys = false))
    }

    @Test fun autoScrollRampsWithDepthInsideTheEdgeZone() {
        val zone = AutoScroll.zone(48f, 600f)
        assertEquals(48f, zone, 0f)
        // Janela baixa: a faixa nunca passa de 1/3 dela.
        assertEquals(30f, AutoScroll.zone(48f, 90f), 0.001f)
        // Janela 40..640: faixa 40..88 e 592..640.
        assertEquals(0f, AutoScroll.speed(300f, 200f, 40f, 640f, zone, 4f), 0f)
        assertEquals(0f, AutoScroll.speed(600f, 600f, 40f, 640f, zone, 4f), 0f)       // pegou já na borda
        assertEquals(0.5f, AutoScroll.speed(616f, 300f, 40f, 640f, zone, 4f), 0.001f)
        assertEquals(1f, AutoScroll.speed(640f, 300f, 40f, 640f, zone, 4f), 0.001f)
        assertEquals(1f, AutoScroll.speed(700f, 300f, 40f, 640f, zone, 4f), 0.001f)   // além da borda: teto
        assertEquals(-0.25f, AutoScroll.speed(76f, 300f, 40f, 640f, zone, 4f), 0.001f)
        assertEquals(-1f, AutoScroll.speed(10f, 300f, 40f, 640f, zone, 4f), 0.001f)
        // Mais fundo = mais rápido.
        assertTrue(AutoScroll.speed(630f, 300f, 40f, 640f, zone, 4f) > AutoScroll.speed(600f, 300f, 40f, 640f, zone, 4f))
        // Passo: 360 dp/s no fundo, dt limitado a 0,05 s.
        assertEquals(6f, AutoScroll.step(1f, 360f, 1f / 60f), 0.001f)
        assertEquals(18f, AutoScroll.step(1f, 360f, 0.5f), 0.001f)
        assertEquals(-9f, AutoScroll.step(-0.5f, 360f, 0.05f), 0.001f)
        assertEquals(0f, AutoScroll.step(1f, 360f, -1f), 0f)
        // Métrica: faixa de 48 dp e 360 dp/s.
        val m = TimelineMetrics(2f)
        assertEquals(96f, m.autoEdge, 0f)
        assertEquals(720f, m.autoSpeed, 0f)
    }

    @Test fun lowLanesBorrowTouchFromTheNeighbourLane() {
        // Camada 0..36, trilhas de 16: 36..52, 52..68, 68..84.
        val tops = floatArrayOf(0f, 36f, 52f, 68f, 84f)
        val lane = { i: Int -> i >= 1 }
        // Dedo a 2 px da borda de baixo da trilha 1: tenta ela, depois a 2.
        assertArrayEquals(intArrayOf(1, 2), LaneTouch.order(lane, tops, 1, 50f, 16f))
        // No meio da trilha 2: as duas vizinhas estão a 16 do dedo.
        assertArrayEquals(intArrayOf(2, 1, 3), LaneTouch.order(lane, tops, 2, 60f, 16f))
        // A camada (não trilha) nunca é vizinha emprestada.
        assertFalse(0 in LaneTouch.order(lane, tops, 1, 37f, 16f))
        // Alvo vertical de cada losango numa trilha de 16: 16 + 2·8 = 32 dp.
        val center = 60f
        assertTrue(2 in LaneTouch.order(lane, tops, 1, center - 16f, 16f))
        assertTrue(2 in LaneTouch.order(lane, tops, 3, center + 16f, 16f))
        assertFalse(2 in LaneTouch.order(lane, tops, 1, center - 16.5f, 16f))
        // Trilha: 16 dp; camada normal fica como estava.
        val r = row(5)
        assertEquals(16f * 2f, timelineRowHeight(RowModel(5, LayerType.Video, 0, 1, 0, true, false, false, false, "", 0, IntArray(0), emptyArray(), TimelineTrack(0)), 36f * 2f, 2f), 0f)
        assertEquals(72f, timelineRowHeight(r, 72f, 2f), 0f)
    }

    @Test fun boxSelectPicksDiamondsInsideAcrossLanesAndLayers() {
        val a = key(12, 10)
        val b = key(12, 30)
        val c = key(0, 30)
        val layer5 = RowModel(5, LayerType.Video, 0, 100, 0, true, false, false, true, "A", 0,
            intArrayOf(10, 30), arrayOf(listOf(a), listOf(b, c)))
        val lane5 = RowModel(5, LayerType.Video, 0, 100, 0, true, false, false, true, "  Opacity", 0,
            intArrayOf(10, 30), arrayOf(listOf(a), listOf(b)), TimelineTrack(12))
        val d = key(12, 20)
        val layer6 = RowModel(6, LayerType.Image, 50, 150, 0, true, false, false, true, "B", 0,
            intArrayOf(70), arrayOf(listOf(d)))
        val rows = listOf(layer5, lane5, layer6)
        val tops = floatArrayOf(0f, 36f, 52f, 88f)
        val cy = { r: RowModel -> if (r.track != null) 8f else 28.5f }
        val all = { _: RowModel -> true }
        // Só a trilha (y 40..50) e frames 25..35: o keyframe b.
        assertEquals(mapOf(5L to listOf(b)), BoxSelect.pick(rows, tops, cy, 25.0, 35.0, 40f, 50f, all))
        // De baixo para cima e da direita para a esquerda dá o mesmo.
        assertEquals(mapOf(5L to listOf(b)), BoxSelect.pick(rows, tops, cy, 35.0, 25.0, 50f, 40f, all))
        // Tudo: camada 5 (resumo + trilha, sem repetir) e camada 6.
        val everything = BoxSelect.pick(rows, tops, cy, 0.0, 100.0, 0f, 88f, all)
        assertEquals(setOf(a, b, c), everything.getValue(5L).toSet())
        assertEquals(3, everything.getValue(5L).size)
        assertEquals(listOf(d), everything[6L])
        // Losangos escondidos não entram; retângulo fora do tempo não pega nada.
        assertEquals(setOf(5L), BoxSelect.pick(rows, tops, cy, 0.0, 100.0, 0f, 88f) { it.track != null }.keys)
        assertTrue(BoxSelect.pick(rows, tops, cy, 31.0, 69.0, 0f, 88f, all).isEmpty())
        // Soma à seleção que havia (não alterna) e a principal fica a mesma.
        val base = KeySelection.single(5L, a)
        val next = base.plusAll(everything)
        assertEquals(5L, next.layer)
        assertEquals(4, next.size)
        assertEquals(setOf(KeyRef.of(d)), next.on(6L))
        assertEquals(next, next.plusAll(everything))
        assertSame(base, base.plusAll(emptyMap()))
    }
}
