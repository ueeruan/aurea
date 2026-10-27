package com.aurea.aurea.editor.timeline

import androidx.compose.ui.graphics.toArgb
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega 0.3.2 (área 03): as regras puras da timeline da referência. Os mesmos
 * números estão em `engine/platform/ios/verify/check_timeline_omega.py` (iOS).
 */
class TimelineOmegaTest {
    private fun clip(id: Long, line: Int, start: Int, end: Int) = RowModel(
        id = id, type = LayerType.Video, start = start, end = end, offset = 0,
        visible = true, locked = false, magnetic = false, animated = false, name = "c$id", label = 0,
        instants = IntArray(0), keysAt = emptyArray(), line = line,
    )

    @Test fun `P0-1 uma camada por linha, a de cima primeiro, sem mudar o trackId`() {
        // Três trechos da MESMA linha (split): antes uma fileira, agora três.
        val rows = listOf(clip(3, 7, 60, 90), clip(2, 7, 30, 60), clip(1, 7, 0, 30))
        assertEquals(1, timelineLayerRows(rows, perLayer = false).size)
        val omega = timelineLayerRows(rows, perLayer = true)
        assertEquals(listOf(3L, 2L, 1L), omega.map { it.id })
        assertTrue(omega.all { it.line == 7 && it.shared == null })
        // Reordenar anda uma camada por vez (a linha não arrasta as outras).
        val keys = timelineGroupKeys(omega, byLine = false)
        assertEquals(3, keys.toSet().size)
        assertEquals(1, timelineGroupKeys(omega).toSet().size)
    }

    @Test fun `034 cor solida do tipo, sem degrade nem variacao por id, igual ao iOS`() {
        // A cor do cartão sai da cor do tipo: 1/5 dessaturada e 1/8 mais escura (os mesmos números do iOS).
        assertEquals(0xFF5D4CAF.toInt(), ClipColors.solid(LayerType.Video.color.toArgb()))
        assertEquals(0xFF3D60AB.toInt(), ClipColors.solid(LayerType.Image.color.toArgb()))
        assertEquals(0xFF28757A.toInt(), ClipColors.solid(LayerType.Audio.color.toArgb()))
        assertEquals(0xFF916C26.toInt(), ClipColors.solid(LayerType.Text.color.toArgb()))
        assertEquals(0xFF347B51.toInt(), ClipColors.solid(LayerType.Shape.color.toArgb()))
        assertEquals(0xFF8E4068.toInt(), ClipColors.solid(LayerType.Particles.color.toArgb()))
        // Opaca mesmo que a base venha sem alfa; cinza continua cinza.
        assertEquals(0xFF5D4CAF.toInt(), ClipColors.solid(0x006A52E0))
        assertEquals(0xFF707070.toInt(), ClipColors.solid(0xFF808080.toInt()))
        // Rótulo branco legível em todos os tipos (contraste WCAG ≥ 4,5).
        for (type in LayerType.entries) {
            val c = ClipColors.solid(type.color.toArgb())
            assertTrue("contraste do rótulo em $type", contrastWithWhite(c) >= 4.5)
        }
        assertEquals(0xFF000000.toInt() or 0x808080, ClipColors.mix(0xFF000000.toInt(), 0xFFFFFFFF.toInt(), 1, 2))
    }

    private fun contrastWithWhite(argb: Int): Double {
        fun lin(v: Int): Double { val c = v / 255.0; return if (c <= 0.04045) c / 12.92 else Math.pow((c + 0.055) / 1.055, 2.4) }
        val l = 0.2126 * lin((argb ushr 16) and 0xFF) + 0.7152 * lin((argb ushr 8) and 0xFF) + 0.0722 * lin(argb and 0xFF)
        return 1.05 / (l + 0.05)
    }

    @Test fun `P1-3 cor do marcador - o motor guarda R no byte baixo`() {
        // A cor padrão do motor (0xFFF7C34F) é, pela convenção dele, #4FC3F7:
        // a paleta antiga escrevia ARGB e lia RGBA. A Omega usa os quatro papéis.
        assertEquals(0xFF4FC3F7.toInt(), MarkerColors.unpack(0xFFF7C34F.toInt()))
        assertEquals(0xFF8665F1.toInt(), MarkerColors.pack(0xFFF16586.toInt()))
        val roles = intArrayOf(0xFFF16586.toInt(), 0xFFFAD3A5.toInt(), 0xFF4BE29D.toInt(), 0xFF3C8CE7.toInt())
        for (i in roles.indices) {
            assertEquals(roles[i], MarkerColors.unpack(MarkerColors.pack(roles[i])))
            assertEquals(i, MarkerColors.appearance(MarkerColors.pack(roles[i]), roles))
        }
        // Marca nova do toque = a primeira aparência; a batida laranja do motor fica no pêssego.
        assertEquals(0, MarkerColors.appearance(MarkerColors.ENGINE_DEFAULT, roles))
        assertEquals(1, MarkerColors.appearance(0xFF4DB7FF.toInt(), roles))
    }

    // 0.3.3 recursos: o marcador antigo (seis cores) voltou, com a correção acima.
    @Test fun `marcador antigo - seis cores gravadas certas e as da 032 mostradas como pretendiam`() {
        assertEquals(0xFFF7C34F.toInt(), MarkerColors.display(MarkerColors.ENGINE_DEFAULT))
        for ((i, argb) in MarkerColors.LEGACY.withIndex()) {
            assertEquals(argb, MarkerColors.display(MarkerColors.pack(argb)))
            assertEquals(argb, MarkerColors.display(argb))   // literal ARGB cru gravado pela 0.3.2
            assertEquals(i, MarkerColors.legacyIndex(MarkerColors.pack(argb)))
        }
        assertEquals(0xFFFF00FF.toInt(), MarkerColors.display(0xFFFF00FF.toInt()))
        assertEquals(-1, MarkerColors.legacyIndex(0xFF123456.toInt()))
    }

    @Test fun `P0-5 passo dos tiques em potencia de 2`() {
        assertEquals(1, OmegaRuler.frameStep(100f, 30f))
        assertEquals(16, OmegaRuler.frameStep(10f, 30f))
        assertEquals(1, OmegaRuler.frameStep(800f, 30f))
        assertEquals(1, OmegaRuler.secondStep(100f))
        assertEquals(2, OmegaRuler.secondStep(3f))
        assertEquals(OmegaRuler.SECOND, OmegaRuler.tickClass(0, 30f))
        assertEquals(OmegaRuler.SECOND, OmegaRuler.tickClass(30, 30f))
        assertEquals(OmegaRuler.HALF, OmegaRuler.tickClass(15, 30f))
        assertEquals(OmegaRuler.FRAME, OmegaRuler.tickClass(16, 30f))
        // 29,97 fps: o segundo 1 começa no quadro 30 (o mesmo do relógio).
        assertEquals(30L, OmegaRuler.frameOf(1.0, 29.97f))
        assertEquals(OmegaRuler.SECOND, OmegaRuler.tickClass(30, 29.97f))
        assertEquals(18f, OmegaRuler.heightDp(OmegaRuler.SECOND), 0f)
        assertEquals(12f, OmegaRuler.heightDp(OmegaRuler.HALF), 0f)
        assertEquals(5f, OmegaRuler.heightDp(OmegaRuler.FRAME), 0f)
    }

    @Test fun `P1-5 e P1-6 auto-rolagem da referencia`() {
        val zone = 38f
        // 1× ao entrar na faixa e 5× na borda; nada fora dela nem sem ter ido para lá.
        assertEquals(0f, OmegaScroll.edgeFactor(300f, 200f, 0f, 600f, zone, 4f), 0f)
        assertEquals(1f, OmegaScroll.edgeFactor(563f, 300f, 0f, 600f, zone, 4f), 0.001f)
        assertEquals(5f, OmegaScroll.edgeFactor(600f, 300f, 0f, 600f, zone, 4f), 0.001f)
        assertEquals(-5f, OmegaScroll.edgeFactor(0f, 300f, 0f, 600f, zone, 4f), 0.001f)
        assertEquals(0f, OmegaScroll.edgeFactor(590f, 590f, 0f, 600f, zone, 4f), 0f)
        // Reordenar: em cima até rowsTop + 19; embaixo a partir de altura − 38.
        assertEquals(0f, OmegaScroll.reorderFactor(150f, 280f, 42f, zone), 0f)
        assertEquals(-1f, OmegaScroll.reorderFactor(60f, 280f, 42f, zone), 0.001f)
        assertTrue(OmegaScroll.reorderFactor(20f, 280f, 42f, zone) < -1f)
        assertEquals(1f, OmegaScroll.reorderFactor(270f, 280f, 42f, zone), 0.001f)
    }

    @Test fun `P1-1 e P1-7 prazos do toque longo por alvo`() {
        assertEquals(380L, OmegaHold.millis(400, HitKind.HEADER, compact = false))
        assertEquals(380L, OmegaHold.millis(400, HitKind.BODY, compact = false))
        assertEquals(100L, OmegaHold.millis(400, HitKind.TRIM_START, compact = true))
        assertEquals(100L, OmegaHold.millis(400, HitKind.BODY, compact = true))
        assertEquals(360L, OmegaHold.millis(400, HitKind.KEYFRAME, compact = true))
        assertEquals(400L, OmegaHold.millis(400, HitKind.NONE, compact = false))
        assertEquals(400L, OmegaHold.millis(400, HitKind.CURRENT_TIME, compact = false))
        assertEquals(50L, OmegaHold.quietMs(100))
        assertEquals(Press.STILL_MS, OmegaHold.quietMs(380))
    }

    @Test fun `P1-2 toque na regua navega entre marcadores`() {
        val frames = intArrayOf(10, 40, 90)
        val xOf: (Int) -> Float = { it * 4f }   // 4 px por quadro
        // Perto de um marcador (21 px): vai até ele.
        assertEquals(40, MarkerNav.target(frames, 170f, 400f, 0, 21f, xOf))
        // Metade direita: o próximo depois do cabeçote; esquerda: o anterior.
        assertEquals(90, MarkerNav.target(frames, 300f, 400f, 50, 21f, xOf))
        assertEquals(40, MarkerNav.target(frames, 100f, 400f, 50, 21f, xOf))
        assertEquals(Snap.NONE, MarkerNav.target(frames, 300f, 400f, 95, 21f, xOf))
        assertEquals(Snap.NONE, MarkerNav.target(intArrayOf(), 100f, 400f, 0, 21f, xOf))
    }

    @Test fun `P1-5 pulso ao levantar`() {
        assertEquals(1f, LiftPulse.scale(0), 0f)
        assertEquals(1.04f, LiftPulse.scale(65), 0.0005f)
        assertEquals(1f, LiftPulse.scale(130), 0f)
        assertTrue(LiftPulse.scale(30) in 1f..1.04f)
    }

    @Test fun `P0-4 reticencias no meio`() {
        val fits: (String) -> Boolean = { it.length <= 7 }
        assertEquals("Camada", MiddleEllipsis.fit("Camada", fits))
        assertEquals("Cam…ova", MiddleEllipsis.fit("Camada nova", fits))
        assertEquals("", MiddleEllipsis.fit("Camada nova") { false })
        // Pontos de código: um emoji não é cortado ao meio.
        val cut = MiddleEllipsis.cut("🎬 filme 🎬", 2)
        assertEquals("🎬…🎬", cut)
    }

    @Test fun `seleção múltipla da referência`() {
        assertTrue(omegaMultiSelection(layerSelectMode = true, selected = 0))
        assertTrue(omegaMultiSelection(layerSelectMode = false, selected = 2))
        assertFalse(omegaMultiSelection(layerSelectMode = false, selected = 1))
        assertNotEquals(omegaMultiSelection(false, 1), omegaMultiSelection(true, 1))
    }

    @Test fun `geometria da Omega`() {
        val m = TimelineMetrics(1f, referenceTrial = true)
        assertEquals(62f, m.headerColumn, 0f)
        assertEquals(3f, m.barRadius, 0f)
        assertEquals(0f, m.bottomPad, 0f)
        assertEquals(17f, m.gutterEyeCx, 0f)
        assertEquals(36f, m.eyeHitRight, 0f)
        assertEquals(1.5f, m.playhead, 0f)
        assertEquals(18f, m.omegaTabTop, 0f)
        assertEquals(31f, m.omegaTimecodeBaseline, 0f)
        assertEquals(m.bar * 0.65f, m.omegaKeyEdit, 0f)
        assertEquals(m.bar * 0.4f, m.omegaKeyAlt, 0f)
        // A zona do tempo atual: 100 × 38 centrada, de y 5 a 43.
        assertTrue(omegaCurrentTimeHit(m, 200f, 20f, 200f))
        assertTrue(omegaCurrentTimeHit(m, 250f, 43f, 200f))
        assertFalse(omegaCurrentTimeHit(m, 251f, 20f, 200f))
        assertFalse(omegaCurrentTimeHit(m, 200f, 4f, 200f))
        assertFalse(omegaCurrentTimeHit(TimelineMetrics(1f), 200f, 20f, 200f))
    }
}
