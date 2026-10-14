package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.directBuffer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Máscara de recorte: os bits 14 (recortada) e 15 (pode criar) da linha POD
 * chegam à camada e à fileira da timeline (indicador "↳" na pílula).
 */
class ClipMaskRowTest {
    private fun row(flags: Int): LayerRow {
        val buf = directBuffer(PodLayout.LAYER_ROW_BYTES)
        val names = directBuffer(16)
        val name = "Foto".toByteArray()
        names.put(name)
        names.position(0)
        buf.putLong(PodLayout.LAYER_OFF_ID, (1L shl 32) or 7L)
        buf.putInt(PodLayout.LAYER_OFF_KIND, 2)
        buf.putInt(PodLayout.LAYER_OFF_START, 0)
        buf.putInt(PodLayout.LAYER_OFF_END, 30)
        buf.putFloat(PodLayout.LAYER_OFF_OPACITY, 1f)
        buf.putInt(PodLayout.LAYER_OFF_FLAGS, flags)
        buf.putInt(PodLayout.LAYER_OFF_NAME_OFFSET, 0)
        buf.putInt(PodLayout.LAYER_OFF_NAME_LENGTH, name.size)
        buf.putInt(PodLayout.LAYER_OFF_PARENT_INDEX, -1)
        return LayerRow.read(buf, 0, names)
    }

    @Test
    fun `bits seguem o BridgePods`() {
        assertEquals(1 shl 14, PodLayout.FLAG_CLIP_TO_BELOW)
        assertEquals(1 shl 15, PodLayout.FLAG_CLIP_CANDIDATE)
    }

    @Test
    fun `recortada chega a fileira da timeline`() {
        val clipped = row(PodLayout.FLAG_VISIBLE or PodLayout.FLAG_CLIP_TO_BELOW)
        assertTrue(clipped.clipToBelow)
        assertFalse(clipped.clipCandidate)
        assertTrue(buildRow(clipped, emptyList()).clipToBelow)

        val candidate = row(PodLayout.FLAG_VISIBLE or PodLayout.FLAG_CLIP_CANDIDATE)
        assertFalse(candidate.clipToBelow)
        assertTrue(candidate.clipCandidate)
        assertFalse(buildRow(candidate, emptyList()).clipToBelow)
        // Etiqueta e linha magnética não se misturam com os bits novos.
        assertEquals(0, candidate.label)
        assertFalse(candidate.magnetic)
    }
}
