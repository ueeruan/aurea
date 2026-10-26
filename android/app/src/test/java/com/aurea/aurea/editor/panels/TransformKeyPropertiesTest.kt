package com.aurea.aurea.editor.panels

import org.junit.Assert.assertArrayEquals
import org.junit.Test
import org.junit.Assert.assertEquals
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.ui.ds.KeyframeLook
import java.nio.ByteBuffer

class TransformKeyPropertiesTest {
    @Test fun animatorRailFollowsExactParameterAndRestoresItAfterCurveNavigation() {
        val focused = com.aurea.aurea.engine.TrackKey(40, 1, 13)
        val saved = com.aurea.aurea.engine.TrackKey(40, 0, 0)
        assertEquals(focused, animatorRailTrack(2, focused, saved))
        assertEquals(focused, animatorRailTrack(2, null, focused))
        assertEquals(saved, animatorRailTrack(1, focused, saved))
        assertEquals(saved, animatorRailTrack(1, com.aurea.aurea.engine.TrackKey(0), null))
        assertEquals(null, animatorRailTrack(0, focused, saved))
    }
    @Test fun partialXYZKeysAreCompletedBeforeTheyCanBeRemoved() {
        val buffer = ByteBuffer.allocate(LayerDetail.BYTES)
        buffer.putInt(92, 3)
        buffer.putInt(96, 3) // X/Y keyed, Z absent
        val detail = LayerDetail.read(buffer)
        assertEquals(KeyframeLook.Animated, transformLook(detail, intArrayOf(0, 1, 2)))
        assertEquals(KeyframeLook.KeyHere, transformLook(detail.copy(keyAtPlayheadMask = 7), intArrayOf(0, 1, 2)))
        assertEquals(KeyframeLook.None, transformLook(detail, intArrayOf(6, 7, 8)))
        assertEquals(KeyframeLook.Animated, transformLook(detail.copy(keyAtPlayheadMask = 0), intArrayOf(0, 1, 2)))
    }
    @Test fun depthTracksAreKeyedFor3DLayersIncludingNullParents() {
        assertArrayEquals(intArrayOf(0, 1, 2), transformKeyProperties(TransformTab.Mover, true))
        assertArrayEquals(intArrayOf(3, 4, 5), transformKeyProperties(TransformTab.Escalar, true))
        assertArrayEquals(intArrayOf(9, 10, 11), transformKeyProperties(TransformTab.Pivo, true))
        assertArrayEquals(intArrayOf(6, 7, 8), transformKeyProperties(TransformTab.Girar, true))
        assertArrayEquals(intArrayOf(6, 7, 8), transformKeyProperties(TransformTab.Girar, true, 0))
        assertArrayEquals(intArrayOf(6, 7, 8), transformKeyProperties(TransformTab.Girar, true, 1))
        assertArrayEquals(intArrayOf(8), transformKeyProperties(TransformTab.Girar, false, 0))
    }
    // Omega aparência (P0-2): Inclinar crava SkewX/SkewY; as faces que saíram voltam para Mover.
    @Test fun skewFaceKeysBothSkewTracksAndRetiredFacesFallBackToMove() {
        assertArrayEquals(intArrayOf(13, 14), transformKeyProperties(TransformTab.Inclinar, false))
        assertArrayEquals(intArrayOf(13, 14), transformKeyProperties(TransformTab.Inclinar, true))
        assertEquals(TransformTab.Mover, visibleTransformFace(TransformTab.Opacidade, false))
        assertEquals(TransformTab.Mover, visibleTransformFace(TransformTab.Lente, true))
        assertEquals(TransformTab.Mover, visibleTransformFace(TransformTab.Inclinar, true))
        assertEquals(TransformTab.Inclinar, visibleTransformFace(TransformTab.Inclinar, false))
        assertEquals(TransformTab.Pivo, visibleTransformFace(TransformTab.Pivo, true))
        assertEquals(TransformTab.Inclinar, TransformTab.byId("skew"))
        assertEquals(TransformTab.Pivo, TransformTab.byId("pivot"))
        assertEquals(null, TransformTab.byId("nada"))
    }

    @Test fun planarLayersKeepPlanarPositionScaleAndAnchor() {
        assertArrayEquals(intArrayOf(0, 1), transformKeyProperties(TransformTab.Mover, false))
        assertArrayEquals(intArrayOf(3, 4), transformKeyProperties(TransformTab.Escalar, false))
        assertArrayEquals(intArrayOf(9, 10), transformKeyProperties(TransformTab.Pivo, false))
        assertArrayEquals(intArrayOf(12), transformKeyProperties(TransformTab.Opacidade, true))
        assertArrayEquals(intArrayOf(), transformKeyProperties(TransformTab.Desfoque, true))
    }
}
