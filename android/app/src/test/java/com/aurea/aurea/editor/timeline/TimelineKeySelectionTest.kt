package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackKey
import org.junit.Assert.*
import org.junit.Test

/** Lógica pura da seleção de keyframes da timeline (várias trilhas de UMA camada). */
class TimelineKeySelectionTest {
    private val posX0 = KeyframeRow(0, -1, 0, 10f, 1, 0)
    private val posX30 = KeyframeRow(0, -1, 30, 20f, 1, 0)
    private val scale15 = KeyframeRow(3, -1, 15, 1f, 1, 0)
    private val scale30 = KeyframeRow(3, -1, 30, 2f, 1, 0)
    private val blur30 = KeyframeRow(31, 7, 30, 5f, 1, 2)
    private val all = listOf(posX0, posX30, scale15, scale30, blur30)

    @Test fun toggleAddsAndRemovesOneKeyAcrossTracks() {
        var sel = KeySelection(5)
        sel = sel.toggle(posX30).toggle(scale15)
        assertEquals(2, sel.size)
        assertTrue(posX30 in sel)
        assertTrue(scale15 in sel)
        // Same time, other track: not selected.
        assertFalse(scale30 in sel)
        sel = sel.toggle(posX30)
        assertEquals(setOf(KeyRef.of(scale15)), sel.keys)
    }

    @Test fun summaryGroupTogglesEveryTrackAtThatInstant() {
        val group = listOf(posX30, scale30, blur30)
        var sel = KeySelection(5).toggle(scale30)
        // Partially selected: the tap adds the missing ones.
        sel = sel.toggleGroup(group)
        assertEquals(3, sel.size)
        assertTrue(group.all { it in sel })
        // Fully selected: the same tap removes the whole instant.
        sel = sel.toggleGroup(group)
        assertTrue(sel.isEmpty())
        assertSame(sel, sel.toggleGroup(emptyList()))
    }

    @Test fun shiftMovesEveryReferenceByTheSameDelta() {
        val sel = KeySelection(5).toggle(posX30).toggle(scale15).toggle(blur30)
        val moved = sel.shifted(7)
        assertEquals(setOf(KeyRef(0, -1, 0, 37), KeyRef(3, -1, 0, 22), KeyRef(31, 7, 2, 37)), moved.keys)
        assertEquals(5L, moved.layer)
        assertSame(sel, sel.shifted(0))
        assertEquals(sel, moved.shifted(-7))
    }

    @Test fun validationDropsTheSelectionWhenAnyKeyIsGone() {
        val sel = KeySelection(5).toggle(posX30).toggle(scale15)
        assertSame(sel, sel.validated(all))
        // Scale key was moved/deleted elsewhere (undo, another panel).
        assertNull(sel.validated(all - scale15))
        // Same time, different value/interpolation still counts as the same key.
        assertSame(sel, sel.validated(listOf(posX30.copy(value = 99f, interpolation = 2), scale15)))
        // An empty selection (Select mode with nothing picked) stays valid.
        val empty = KeySelection(5)
        assertSame(empty, empty.validated(emptyList()))
    }

    @Test fun referencesPackFourIntegersPerKeyForTheEngine() {
        val sel = KeySelection(5).toggle(blur30).toggle(posX0)
        val refs = sel.references()
        assertEquals(8, refs.size)
        val groups = refs.toList().chunked(4).toSet()
        assertEquals(setOf(listOf(31L, 7L, 2L, 30L), listOf(0L, -1L, 0L, 0L)), groups)
    }

    @Test fun duplicateStartsOneFrameAfterTheLastSelectedKey() {
        val sel = KeySelection(5).toggle(posX30).toggle(scale15)
        assertEquals(15, sel.minTime())
        assertEquals(30, sel.maxTime())
        // Pasted anchored at the earliest: 15 → 31, 30 → 46.
        assertEquals(16, sel.duplicateDelta())
        assertEquals(setOf(KeyRef(0, -1, 0, 46), KeyRef(3, -1, 0, 31)), sel.shifted(16).keys)
        assertNull(KeySelection(5).duplicateDelta())
        val edge = KeySelection(5).toggle(posX0.copy(time = Int.MAX_VALUE))
        assertNull(edge.duplicateDelta())
    }

    @Test fun allSelectsVisibleKeysAndRespectsTrackFocus() {
        assertEquals(all.size, KeySelection.all(5, all).size)
        val focused = KeySelection.all(5, all, listOf(TrackKey(3)))
        assertEquals(setOf(KeyRef.of(scale15), KeyRef.of(scale30)), focused.keys)
        assertEquals(listOf(scale15, scale30), focused.rows(all))
    }

    @Test fun singleSelectionIsOnlyTheTappedKey() {
        val sel = KeySelection.single(9, scale30)
        assertEquals(9L, sel.layer)
        assertEquals(setOf(KeyRef(3, -1, 0, 30)), sel.keys)
        assertTrue(sel.containsAny(listOf(posX30, scale30)))
        assertFalse(sel.containsAny(listOf(posX30, blur30)))
    }
}
