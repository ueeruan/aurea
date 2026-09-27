package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.Ease
import com.aurea.aurea.editor.panels.applyEase
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Real pointer dispatch through the editor, using only the disposable test app. */
class TimelineGesturesTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val density get() = context.resources.displayMetrics.density
    private fun timeline() = compose.onNodeWithTag("editor.timeline")

    private fun launch(count: Int = 1) {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Timeline gestures") }
        compose.waitUntil(15000) { store.project.title == "Timeline gestures" }
        repeat(count) {
            compose.runOnIdle { store.addNull(false) }
            compose.waitUntil(5000) { store.layers.size == it + 1 }
        }
        compose.runOnIdle { store.clearSelection(); store.seek(0) }
        compose.waitForIdle()
    }

    @Test fun draggingUnselectedClipMovesWithoutOpeningOptionsAndUndoRestoresIt() {
        launch()
        val initial = store.layers.single()
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            swipe(Offset(width / 2f + 30 * density, 52 * density),
                Offset(width / 2f + 100 * density, 52 * density), 240)
        }
        compose.waitUntil(5000) { store.layers.single().startFrame > initial.startFrame }
        compose.runOnIdle {
            assertEquals(setOf(initial.id), store.timelineOnlySelection)
            assertEquals(initial.endFrame - initial.startFrame, store.layers.single().endFrame - store.layers.single().startFrame)
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.single().startFrame == initial.startFrame }
        compose.runOnIdle { assertEquals(initial.endFrame, store.layers.single().endFrame) }
        timeline().performTouchInput { click(Offset(width / 2f + 40 * density, 52 * density)) }
        compose.runOnIdle { assertTrue(store.timelineOnlySelection.isEmpty()) }
        assertTrue("A real tap should still open layer options", timeline().fetchSemanticsNode().size.height < height)
    }

    @Test fun snappingCanBeDisabledAndEnabledForTheSameClipGesture() {
        launch()
        compose.runOnIdle { store.snapping = false }
        fun drag() = timeline().performTouchInput {
            swipe(Offset(width / 2f + 30 * density, 52 * density),
                Offset(width / 2f + 100 * density, 52 * density), 240)
        }
        drag()
        compose.waitUntil(5000) { store.layers.single().startFrame > 0 }
        val freeFrame = store.layers.single().startFrame
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.single().startFrame == 0 }
        compose.runOnIdle { store.seek(freeFrame + 1) }
        compose.waitUntil(5000) { store.playhead == freeFrame + 1 }
        compose.runOnIdle { store.toggleMarker(); store.seek(0); store.clearSelection() }
        compose.waitUntil(5000) { store.playhead == 0 && store.markers.size > 0 }
        drag()
        compose.waitUntil(5000) { store.layers.single().startFrame > 0 }
        compose.runOnIdle { assertEquals(freeFrame, store.layers.single().startFrame); store.undo() }
        compose.waitUntil(5000) { store.layers.single().startFrame == 0 }
        compose.runOnIdle { store.snapping = true; store.clearSelection() }
        drag()
        compose.waitUntil(5000) { store.layers.single().startFrame > 0 }
        compose.runOnIdle { assertEquals(freeFrame + 1, store.layers.single().startFrame) }
    }

    @Test fun verticalSwipeScrollsManyLayersWithoutMovingOrOpeningAny() {
        launch(24)
        val before = store.layers.map { Triple(it.id, it.startFrame, it.endFrame) }
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            val x = width * .72f
            down(Offset(x, height - 18 * density))
            advanceEventTime(80)
            // Scrolling begins before the deliberate hold used for reordering.
            moveTo(Offset(x, height - 45 * density), 50)
            moveTo(Offset(x, 54 * density), 250)
            advanceEventTime(120)
            up()
        }
        compose.runOnIdle {
            assertTrue(store.selection.isEmpty())
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        timeline().performTouchInput { click(Offset(width * .72f, 54 * density)) }
        compose.runOnIdle {
            val index = before.indexOfFirst { it.first == store.primary }
            assertTrue("Vertical scroll should reveal lower layers, index=$index", index >= 2)
        }
        // Reverse the same list back to the first layer.
        compose.runOnIdle { store.clearSelection() }
        timeline().performTouchInput {
            swipe(Offset(width * .72f, 55 * density), Offset(width * .72f, height - 5 * density), 300)
        }
        compose.waitForIdle()
        timeline().performTouchInput { click(Offset(width * .72f, 54 * density)) }
        compose.runOnIdle { assertEquals(before.first().first, store.primary) }
    }

    @Test fun holdingLayerBodyReordersWithoutChangingTimingAndOneUndoRestoresOrder() {
        launch(5)
        val before = store.layers.map { it.id }
        val ranges = store.layers.associate { it.id to Triple(it.startFrame, it.endFrame, it.offsetFrames) }
        val duration = store.project.durationFrames
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            val x = width / 2f + 40 * density
            down(Offset(x, 52 * density))
            advanceEventTime(650)
            moveTo(Offset(x, 75 * density), 50)
            moveTo(Offset(x, 148 * density), 300)
            up()
        }
        compose.waitUntil(5000) { store.layers.map { it.id } != before }
        compose.runOnIdle {
            assertEquals(before.toSet(), store.layers.map { it.id }.toSet())
            assertEquals(ranges, store.layers.associate { it.id to Triple(it.startFrame, it.endFrame, it.offsetFrames) })
            assertEquals(duration, store.project.durationFrames)
            assertEquals(setOf(before.first()), store.timelineOnlySelection)
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.map { it.id } == before }
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) { store.layers.map { it.id } != before }
    }

    @Test fun editingOneCurveLeavesCoincidentAxisKeysAndLaterSegmentsUnchanged() {
        launch()
        val id = store.layers.single().id
        compose.runOnIdle { store.select(id, openOptions = false) }
        for (frame in listOf(0, 30, 60)) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(5000) { store.playhead == frame }
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0, 1, 2)) }
            compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.time == frame } == 3 }
        }
        val before = store.keyframes[id].orEmpty()
        val key = before.single { it.property == 0 && it.time == 0 }
        compose.runOnIdle {
            store.beginGesture("Independent curve")
            applyEase(store, id, key, Ease(6, 0.2f, -0.5f, 0.8f, 1.5f))
            store.endGesture()
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().single { it.property == 0 && it.time == 0 }.interpolation == 6 }
        compose.runOnIdle {
            assertEquals(before.filterNot { it.property == 0 && it.time == 0 },
                store.keyframes[id].orEmpty().filterNot { it.property == 0 && it.time == 0 })
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id] == before }
    }

    @Test fun trimmingBeyondProjectEndGrowsDurationAndUndoRestoresIt() {
        trimPastEnd(openTransform = false)
    }

    @Test fun compactTimelineKeepsTrimHandlesUsableWithTransformPanelOpen() {
        trimPastEnd(openTransform = true)
    }

    private fun trimPastEnd(openTransform: Boolean) {
        launch()
        val id = store.layers.single().id
        val end = store.project.durationFrames
        compose.runOnIdle {
            store.setLayerRanges(longArrayOf(id), intArrayOf(0), intArrayOf(end))
            store.select(id, openOptions = openTransform)
            store.seek(end - 10)
        }
        compose.waitUntil(5000) { store.playhead == end - 10 }
        if (openTransform) {
            compose.onNodeWithContentDescription(context.getString(R.string.sh_dock_transform)).performClick()
        }
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            // At 80 dp/s, ten frames are 26.67 dp from the playhead.
            val edge = width / 2f + (80f / 3f - 5f) * density
            swipe(Offset(edge, 50 * density), Offset(width - 15 * density, 50 * density), 350)
        }
        compose.waitUntil(5000) { store.layers.single().endFrame > end }
        compose.runOnIdle { assertTrue(store.project.durationFrames >= store.layers.single().endFrame) }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.single().endFrame == end }
        compose.runOnIdle { assertEquals(end, store.project.durationFrames) }
    }
}
