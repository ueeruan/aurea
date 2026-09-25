package com.aurea.aurea.editor.panels

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class TransformKeyPropertiesTest {
    @Test fun depthTracksAreKeyedFor3DLayersIncludingNullParents() {
        assertArrayEquals(intArrayOf(0, 1, 2), transformKeyProperties(TransformTab.Mover, true))
        assertArrayEquals(intArrayOf(3, 4, 5), transformKeyProperties(TransformTab.Escalar, true))
        assertArrayEquals(intArrayOf(9, 10, 11), transformKeyProperties(TransformTab.Pivo, true))
        assertArrayEquals(intArrayOf(6, 7, 8), transformKeyProperties(TransformTab.Girar, true))
    }
    @Test fun planarLayersKeepPlanarPositionScaleAndAnchor() {
        assertArrayEquals(intArrayOf(0, 1), transformKeyProperties(TransformTab.Mover, false))
        assertArrayEquals(intArrayOf(3, 4), transformKeyProperties(TransformTab.Escalar, false))
        assertArrayEquals(intArrayOf(9, 10), transformKeyProperties(TransformTab.Pivo, false))
        assertArrayEquals(intArrayOf(12), transformKeyProperties(TransformTab.Opacidade, true))
        assertArrayEquals(intArrayOf(), transformKeyProperties(TransformTab.Desfoque, true))
    }
}
