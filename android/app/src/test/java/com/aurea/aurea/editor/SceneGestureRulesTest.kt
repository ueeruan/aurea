package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Bug 9 da 0.3.4 (Cena 3D): sem seleção os gestos são da câmera da cena; com
 * seleção, do objeto (como antes); tocar de novo no escolhido solta. O iOS
 * segue a mesma regra no `sceneEvent` (PreviewMetalView.swift).
 */
class SceneGestureRulesTest {
    @Test fun withoutSelectionOneFingerOrbitsEvenOverAnObject() {
        assertNull(SceneGestureRules.dragTarget(hasSelection = false, picked = 7L))
        assertNull(SceneGestureRules.dragTarget(hasSelection = false, picked = null))
    }

    @Test fun withSelectionOneFingerOnAnObjectMovesIt() {
        assertEquals(7L, SceneGestureRules.dragTarget(hasSelection = true, picked = 7L))
        // No vazio continua orbitando.
        assertNull(SceneGestureRules.dragTarget(hasSelection = true, picked = null))
    }

    @Test fun pinchIsTheCameraZoomWithoutSelection() {
        assertFalse(SceneGestureRules.pinchEditsObject(hasSelection = false, editable = true, zoomLock = false))
        assertTrue(SceneGestureRules.pinchEditsObject(hasSelection = true, editable = true, zoomLock = false))
        // A lupa do canto devolve a pinça à câmera; travada/sem base também.
        assertFalse(SceneGestureRules.pinchEditsObject(hasSelection = true, editable = true, zoomLock = true))
        assertFalse(SceneGestureRules.pinchEditsObject(hasSelection = true, editable = false, zoomLock = false))
    }

    @Test fun tappingTheSelectedObjectAgainReleasesIt() {
        assertEquals(SceneGestureRules.TAP_DESELECT, SceneGestureRules.tap(primary = 7L, selectionSize = 1, picked = 7L))
        assertEquals(SceneGestureRules.TAP_SELECT, SceneGestureRules.tap(primary = null, selectionSize = 0, picked = 7L))
        assertEquals(SceneGestureRules.TAP_SELECT, SceneGestureRules.tap(primary = 3L, selectionSize = 1, picked = 7L))
        // Vários escolhidos: o toque deixa só ele (não solta).
        assertEquals(SceneGestureRules.TAP_SELECT, SceneGestureRules.tap(primary = 7L, selectionSize = 2, picked = 7L))
        assertEquals(SceneGestureRules.TAP_EMPTY, SceneGestureRules.tap(primary = 7L, selectionSize = 1, picked = null))
    }
}
