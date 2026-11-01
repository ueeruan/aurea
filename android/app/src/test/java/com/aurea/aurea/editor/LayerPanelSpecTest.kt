package com.aurea.aurea.editor

import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega painel (área 04, P0-4): a grade fixa do painel da camada por tipo, as
 * vagas invisíveis que guardam espaço e os colapsos da referência. Gêmeo do
 * `LayerPanelSpec.of` do iOS (mesmos ids `layer.panel.<tag>`).
 */
class LayerPanelSpecTest {
    private fun spec(
        type: LayerType,
        vector: Boolean = false,
        drawing: Boolean = false,
        text3D: Boolean = false,
        effects: Int = 0,
        adjustment: Boolean = false,
    ) = layerPanelSpec(
        PanelLayer(
            id = 1, kind = type.kind, locked = false, start = 0, end = 30, adjustment = adjustment,
            vector = vector, drawing = drawing, text3D = text3D, effectCount = effects,
        ),
    )

    private val c = LayerSlot.ColorFill
    private val b = LayerSlot.BorderShadow
    private val m = LayerSlot.BlendOpacity
    private val k = LayerSlot.Mask
    private val std = listOf(LayerSlot.MoveTransform, LayerSlot.Edit, LayerSlot.Presets, LayerSlot.Effects)

    @Test fun shapesKeepTheFullGridAndTheirEditSlot() {
        val shape = spec(LayerType.Shape)
        assertEquals(listOf(c, b, m, k), shape.row2)
        assertEquals(std, shape.row3)
        assertEquals(LayerEdit.Shape, shape.edit)
        assertFalse(shape.tallRow3)
        assertEquals(SpeedRoute.Constant, shape.speed)
        assertEquals(LayerEdit.Points, spec(LayerType.Shape, vector = true).edit)
        assertEquals(LayerEdit.Drawing, spec(LayerType.Shape, vector = true, drawing = true).edit)
    }

    @Test fun imageAndVideoAreShapesWithMedia() {
        // Evidência REA (seção 8): imagem e vídeo são forma com mídia ("Editar forma").
        for (type in listOf(LayerType.Image, LayerType.Video)) {
            val s = spec(type)
            assertEquals(listOf(null, b, m), s.row2)
            assertEquals(std, s.row3)
            assertEquals(LayerEdit.MediaShape, s.edit)
        }
        assertEquals(SpeedRoute.Panel, spec(LayerType.Video).speed)
        assertEquals(SpeedRoute.Constant, spec(LayerType.Image).speed)
    }

    @Test fun groupHasTheEmbeddedSpeedWithTheArrow() {
        val g = spec(LayerType.Group)
        assertTrue(g.speedArrow)
        assertEquals(SpeedRoute.Group, g.speed)
        assertEquals(LayerEdit.Group, g.edit)
        for (type in LayerType.entries.filter { it != LayerType.Group }) assertFalse("arrow on $type", spec(type).speedArrow)
    }

    @Test fun rowTwoCollapsesIntoTallButtons() {
        val audio = spec(LayerType.Audio)
        assertTrue(audio.tallRow3)
        assertEquals(listOf(LayerSlot.Effects), audio.row3)
        assertEquals(SpeedRoute.Panel, audio.speed)

        val camera = spec(LayerType.Camera)
        assertTrue(camera.tallRow3)
        assertEquals(listOf(LayerSlot.MoveTransform, LayerSlot.Edit, LayerSlot.Presets, null), camera.row3)
        assertEquals(LayerEdit.Camera, camera.edit)

        // Nulo: a vaga de edição sai da cadeia (Mover | Presets).
        val nul = spec(LayerType.Null)
        assertTrue(nul.tallRow3)
        assertEquals(listOf(LayerSlot.MoveTransform, LayerSlot.Presets), nul.row3)
        assertNull(nul.edit)

        val light = spec(LayerType.Light)
        assertEquals(listOf(LayerSlot.MoveTransform, LayerSlot.Edit, null, null), light.row3)
        assertEquals(LayerSlot.Effects, spec(LayerType.Light, effects = 1).row3.last())
    }

    @Test fun every3dAndAdjustmentTypeHasItsSlots() {
        assertEquals(LayerEdit.Material, spec(LayerType.Model3D).edit)
        assertEquals(listOf(c, null, m), spec(LayerType.Model3D).row2)
        assertEquals(LayerEdit.Particles, spec(LayerType.Particles).edit)
        assertEquals(listOf(null, null, m, k), spec(LayerType.Particles).row2)
        val adjustment = spec(LayerType.Adjustment)
        assertEquals(listOf(null, null, m), adjustment.row2)
        assertEquals(listOf(null, null, null, LayerSlot.Effects), adjustment.row3)
        // A flag de ajuste vale em qualquer tipo.
        assertEquals(adjustment, spec(LayerType.Shape, adjustment = true))
    }

    @Test fun gridShapeIsAlwaysFixed() {
        for (type in LayerType.entries) {
            val s = spec(type)
            // Bug #9b: 3 vagas, + Máscara no fim onde o Editar não é o recorte.
            assertEquals("row 2 of $type", if (LayerSlot.Mask in s.row2) 4 else 3, s.row2.size)
            if (LayerSlot.Mask in s.row2) assertEquals("mask last in $type", LayerSlot.Mask, s.row2.last())
            assertTrue("row 3 of $type", s.row3.size in 1..4)
            // Fileira 2 vazia ⇔ botões altos (colapso "a").
            assertEquals("tall of $type", s.row2.all { it == null }, s.tallRow3)
            // A vaga Editar só existe quando o tipo diz o que ela é.
            assertEquals("edit of $type", LayerSlot.Edit in s.row3, s.edit != null)
        }
    }

    /** Bug #9b: a máscara voltou para forma (inclusive vetor e desenho), texto, grupo e partículas. */
    @Test fun maskReachesEveryVisualLayer() {
        for (s in listOf(spec(LayerType.Shape), spec(LayerType.Shape, vector = true), spec(LayerType.Shape, vector = true, drawing = true),
            spec(LayerType.Text), spec(LayerType.Group), spec(LayerType.Particles))) {
            assertTrue("mask in ${s.edit}", LayerSlot.Mask in s.row2)
        }
        // Imagem e vídeo chegam nela pelo Editar (MediaShape): sem botão repetido.
        for (type in listOf(LayerType.Image, LayerType.Video)) assertFalse("mask twice on $type", LayerSlot.Mask in spec(type).row2)
        // Sem nada para recortar (ou sem textura própria): nada de Máscara.
        for (type in listOf(LayerType.Audio, LayerType.Camera, LayerType.Light, LayerType.Null, LayerType.Adjustment, LayerType.Model3D)) {
            val s = spec(type)
            assertFalse("mask on $type", LayerSlot.Mask in s.row2 || LayerSlot.Mask in s.row3)
        }
        assertFalse(LayerSlot.Mask in spec(LayerType.Shape, adjustment = true).row2)
    }

    @Test fun eightToolsPackIntoTwoRowsOfFour() {
        val shape = spec(LayerType.Shape)
        val packed = packedToolRows((shape.row2 + shape.row3).filterNotNull())
        assertEquals(listOf(listOf(c, b, m, k), std), packed)
        // Os outros tamanhos seguem como na 0.4.1.
        assertEquals(listOf(3, 3), packedToolRows((1..6).toList()).map { it.size })
        assertEquals(listOf(3, 4), packedToolRows((1..7).toList()).map { it.size })
        assertEquals(listOf(2, 3), packedToolRows((1..5).toList()).map { it.size })
        assertEquals(listOf(4), packedToolRows((1..4).toList()).map { it.size })
    }

    @Test fun audioButtonFollowsTheEngineBits() {
        assertFalse(layerAudioEnabled(0))
        assertTrue(layerAudioEnabled(LayerPanelBits.HAS_AUDIO))
        // Sem áudio, mas com mais de um keyframe de volume: continua habilitado.
        assertTrue(layerAudioEnabled(LayerPanelBits.GAIN_KEYS))
        assertFalse(layerAudioEnabled(LayerPanelBits.LOCKED or LayerPanelBits.MISSING_MEDIA))
    }

    @Test fun idsAreTheSameOnBothPlatforms() {
        assertEquals(
            listOf("colorFill", "borderShadow", "blendOpacity", "moveTransform", "edit", "presets", "effects", "mask"),
            LayerSlot.entries.map { it.tag },
        )
        // Os bits são os de Engine::query_layer_panel (EngineOmegaLayer.cpp).
        assertEquals(32, LayerPanelBits.MOVE_START)
        assertEquals(1024, LayerPanelBits.RETIME_EXTEND_END)
        assertEquals(32768, LayerPanelBits.SPEED_BAKEABLE)
    }
}
