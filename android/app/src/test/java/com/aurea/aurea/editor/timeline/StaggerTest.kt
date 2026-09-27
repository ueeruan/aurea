package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class StaggerTest {
    @Test
    fun positiveStepKeepsTimelineOrderAndTheTopLayerStays() {
        val plan = Stagger.plan(listOf(10L, 20L, 30L), 3)!!
        assertArrayEquals(longArrayOf(10L, 20L, 30L), plan.first)
        assertEquals(3, plan.second)
    }

    @Test
    fun negativeStepCascadesFromTheBottomUp() {
        val plan = Stagger.plan(listOf(10L, 20L, 30L), -4)!!
        assertArrayEquals(longArrayOf(30L, 20L, 10L), plan.first)
        assertEquals(4, plan.second)
    }

    @Test
    fun nothingToDoWithOneLayerOrZeroStep() {
        assertNull(Stagger.plan(listOf(10L), 3))
        assertNull(Stagger.plan(listOf(10L, 10L), 3))
        assertNull(Stagger.plan(listOf(10L, 20L), 0))
    }

    @Test
    fun counterSkipsZeroAndStaysInRange() {
        assertEquals(1, Stagger.step(-1, 1))
        assertEquals(-1, Stagger.step(1, -1))
        assertEquals(Stagger.MAX, Stagger.step(Stagger.MAX, 1))
        assertEquals(Stagger.MIN, Stagger.step(Stagger.MIN, -1))
        assertEquals(4, Stagger.step(3, 1))
    }

    @Test
    fun keyframesHideOnlyOnUnselectedLayerRows() {
        assertTrue(KeyframeVisibility.visible(showAll = true, isPropertyLane = false, selected = false))
        assertFalse(KeyframeVisibility.visible(showAll = false, isPropertyLane = false, selected = false))
        assertTrue(KeyframeVisibility.visible(showAll = false, isPropertyLane = false, selected = true))
        assertTrue(KeyframeVisibility.visible(showAll = false, isPropertyLane = true, selected = false))
    }
}
