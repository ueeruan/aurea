package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.timeline.*
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.primaryKeys
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaTheme
import com.aurea.aurea.ui.theme.AureaTimeline
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import kotlin.math.abs
import kotlin.math.roundToInt

/** Production painter/controller and live engine, with a controllable viewport for subframe tests. */
class TimelineCoordinatesTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private lateinit var state: TimelineState
    private lateinit var controller: TimelineController
    private lateinit var metrics: TimelineMetrics
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun surface() = compose.onNodeWithTag("coordinates.timeline")
    private var id = 0L

    private fun launch(keys: List<Int> = listOf(0, 30, 60)) {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val density = LocalDensity.current
            state = remember { TimelineState() }
            metrics = remember { TimelineMetrics(density.density, density.fontScale) }
            val scope = rememberCoroutineScope()
            controller = remember { TimelineController(store, state, scope) }
            val measurer = rememberTextMeasurer()
            val painter = remember { TimelinePainter(metrics, measurer) }
            SideEffect { controller.metrics = metrics; state.timecodeBox = true }
            DisposableEffect(controller) { onDispose { controller.dispose() } }
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store) // Owns the real rendering loop and engine refresh.
                    Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(240.dp)
                        .testTag("coordinates.timeline").clipToBounds().background(AureaColors.EditorCanvas)
                        .onSizeChanged { state.width = it.width; state.height = it.height }
                        .pointerInput(controller) { with(controller) { handleGestures() } }
                        .drawBehind { painter.draw(this, controller) })
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Timeline coordinates") }
        compose.waitUntil(15000) { store.project.title == "Timeline coordinates" }
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        compose.runOnIdle { id = store.layers.single().id; store.select(id, openOptions = false); store.snapping = false }
        for (time in keys) {
            compose.runOnIdle { store.seek(time) }
            compose.waitUntil(5000) { store.playhead == time }
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
            compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == time } }
        }
    }

    private fun select(local: Int, timeline: Int, view: Double, zoom: Float) {
        compose.runOnIdle {
            store.seek(timeline)
            store.selectKeyframe(id, store.keyframes[id].orEmpty().single { it.property == 0 && it.time == local })
            state.heldView = view
            state.pps = zoom
        }
        compose.waitUntil(5000) { store.playhead == timeline }
        compose.waitForIdle()
    }

    private fun assertPixels(frame: Int, message: String) {
        val pixels = surface().captureToImage().toPixelMap()
        val cy = metrics.rowsTop + if (state.compact) metrics.diamondCyCompact else metrics.diamondCyNormal
        val row = (cy - 2 * metrics.density).roundToInt()
        fun near(a: Color, b: Color) = abs(a.red - b.red) < .04f && abs(a.green - b.green) < .04f && abs(a.blue - b.blue) < .04f
        val diamond = (0 until pixels.width).filter { near(pixels[it, row], AureaTimeline.KeyframeOn) }
        val line = (0 until pixels.width).filter { near(pixels[it, (cy + 35 * metrics.density).roundToInt()], AureaColors.Accent) }
        assertTrue("$message: diamond must be drawn", diamond.size >= 2)
        assertTrue("$message: playhead must be drawn", line.isNotEmpty())
        val center = (diamond.first() + diamond.last() + 1) / 2.0
        val lineCenter = (line.first() + line.last() + 1) / 2.0
        val expected = pixels.width / 2.0 + (frame - state.heldView) * state.pps * metrics.density / 30.0
        assertEquals("$message: diamond center in physical pixels", expected, center, 1.0)
        assertEquals("$message: playhead and diamond share the same origin", center, lineCenter, 1.0)
        val top = metrics.rowsTop.roundToInt()
        val bottom = (metrics.rowsTop + metrics.bar).roundToInt()
        for (y in top until (metrics.rowsTop + metrics.row).roundToInt()) {
            for (x in (center - 10 * metrics.density).toInt()..(center + 10 * metrics.density).toInt()) {
                if (near(pixels[x, y], AureaTimeline.KeyframeOn)) {
                    assertTrue("$message: diamond stays inside the clip", y < bottom)
                }
            }
        }
    }

    private fun assertExternalCap(frame: Int, message: String) {
        val pixels = surface().captureToImage().toPixelMap()
        val x0 = pixels.width / 2f + (frame - state.heldView).toFloat() * state.pps * metrics.density / 30f
        val y = (metrics.rowsTop + 5 * metrics.density).roundToInt()
        fun whiteAt(x: Float): Boolean {
            val color = pixels[x.roundToInt(), y]
            return color.red > .85f && color.green > .85f && color.blue > .85f
        }
        assertTrue("$message: cap is outside the temporal start", whiteAt(x0 - 10 * metrics.density))
        assertFalse("$message: body starts immediately after the playhead", whiteAt(x0 + 4 * metrics.density))
        assertFalse("$message: external cap has bounded width", whiteAt(x0 - metrics.capWidth - 3 * metrics.density))
        assertPixels(frame, message)
    }

    @Test fun compactCapEndsAtTheActualStartAcrossZoomScrollMoveAndTrim() {
        launch()
        compose.runOnIdle { state.compact = true }
        for (zoom in listOf(20f, 80f, 800f)) for (fraction in listOf(0.0, .375)) {
            select(0, 0, fraction, zoom)
            assertExternalCap(0, "compact zero, zoom=$zoom view=$fraction")
        }
        compose.runOnIdle { store.moveLayers(listOf(id), 60) }
        compose.waitUntil(5000) { store.layers.single().startFrame == 60 }
        select(0, 60, 60.0, 120f)
        assertExternalCap(60, "moved compact layer")
        compose.runOnIdle { store.trimStart(id, 90); store.trimEnd(id, 150) }
        compose.waitUntil(5000) { store.layers.single().startFrame == 90 && store.layers.single().endFrame == 150 }
        select(30, 90, 90.375, 800f)
        assertExternalCap(90, "trimmed compact layer")
        // A start hidden under the header must not leave a white cap stuck to the viewport.
        val view = 90.0 + (state.width / 2.0 - metrics.headerColumn + 10 * metrics.density) / (120 * metrics.density / 30)
        select(30, 90, view, 120f)
        val pixels = surface().captureToImage().toPixelMap()
        val pixel = pixels[(metrics.headerColumn + 5 * metrics.density).roundToInt(), (metrics.rowsTop + 5 * metrics.density).roundToInt()]
        assertFalse("hidden start must not create a new visible cap", pixel.red > .85f && pixel.green > .85f && pixel.blue > .85f)
        // Grab the left half of the diamond at the cap/body boundary, then release on frame 103.
        select(30, 90, 90.0, 120f)
        val cy = metrics.rowsTop + metrics.diamondCyCompact
        val ppf = 120f * metrics.density / 30f
        val grabX = state.width / 2f - 2 * metrics.density
        surface().performTouchInput {
            down(Offset(grabX, cy))
            moveTo(Offset(grabX + 5 * ppf, cy), 80)
            moveTo(Offset(grabX + 10 * ppf, cy), 80)
            updatePointerTo(0, Offset(grabX + 13 * ppf, cy))
            up()
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 43 } }
        assertEquals("dragging the diamond must not trim the layer", 90, store.layers.single().startFrame)
        select(43, 103, 103.375, 800f)
        assertPixels(103, "compact boundary key released at exact frame")
    }

    @Test fun renderedCentersStayExactThroughZoomScrollTrimMoveDurationAndRemap() {
        launch()
        for (zoom in listOf(20f, 80f, 800f)) for (fraction in listOf(0.0, .375)) {
            select(0, 0, fraction, zoom)
            assertPixels(0, "zero, zoom=$zoom view=$fraction")
        }
        compose.runOnIdle { store.moveLayers(listOf(id), 60) }
        compose.waitUntil(5000) { store.layers.single().startFrame == 60 }
        select(30, 90, 90.375, 800f)
        assertPixels(90, "moved layer")
        compose.runOnIdle { store.trimStart(id, 80); store.trimEnd(id, 160) }
        compose.waitUntil(5000) { store.layers.single().startFrame == 80 && store.layers.single().endFrame == 160 }
        assertEquals(20, store.layers.single().offsetFrames)
        assertPixels(90, "trim preserves absolute key time")
        compose.runOnIdle { store.trimEnd(id, 110) }
        compose.waitUntil(5000) { store.layers.single().endFrame == 110 }
        assertPixels(90, "duration is not a keyframe scale")
        compose.runOnIdle { store.enableManualTimeRemap(); store.remapInsert(30); store.remapMove(1, 30, 5f) }
        compose.waitUntil(5000) { store.timeRemap != null }
        assertPixels(90, "remap changes media sampling, not property-key time")
    }

    @Test fun denseSelectedKeysKeepTheirOwnDiamondAndDragCommitsExactReleaseFrame() {
        launch(listOf(0, 1, 2, 30, 90))
        for (key in 0..2) {
            select(key, key, key + .375, 80f)
            assertPixels(key, "selected dense key $key")
        }
        compose.runOnIdle { store.moveLayers(listOf(id), 60) }
        compose.waitUntil(5000) { store.layers.single().startFrame == 60 }
        compose.runOnIdle { store.trimStart(id, 80) }
        compose.waitUntil(5000) { store.layers.single().offsetFrames == 20 }
        compose.runOnIdle { state.compact = true }
        select(30, 90, 90.0, 120f)
        val cy = metrics.rowsTop + metrics.diamondCyCompact
        val ppf = 120f * metrics.density / 30f
        val cx = state.width / 2f
        val grab = -2f * metrics.density
        surface().performTouchInput {
            down(Offset(cx + grab, cy))
            moveTo(Offset(cx + 5 * ppf + grab, cy), 80)
            moveTo(Offset(cx + 10 * ppf + grab, cy), 80)
            // The UP event carries the last position (no intervening MOVE dispatch).
            updatePointerTo(0, Offset(cx + 13 * ppf + grab, cy))
            up()
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 43 } }
        assertFalse(store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 30 })
        select(43, 103, 103.375, 800f)
        assertPixels(103, "released frame survives viewport change")
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 30 } }
    }

    @Test fun effectKeysUseTheVisiblePlayheadImmediatelyAfterSeekAndDrag() {
        launch(emptyList())
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.motion.oscillate.cycles")); store.moveLayers(listOf(id), 60) }
        compose.waitUntil(5000) { store.effects.size == 1 && store.layers.single().startFrame == 60 }
        val effect = store.effects.single().effectId
        compose.runOnIdle { store.trimStart(id, 80) }
        compose.waitUntil(5000) { store.detail?.offsetFrames == 20 && store.paramOf(effect, 2) != null }
        // Seek and edit in one UI turn: no render/query round trip may change the timestamp.
        compose.runOnIdle { store.seek(95); store.toggleEffectKeyframe(effect, store.paramOf(effect, 2)!!) }
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == 31 && it.time == 35 } }
        compose.waitUntil(5000) { store.paramOf(effect, 2)?.animated == true }
        compose.runOnIdle { store.seek(113); store.setEffectParam(effect, store.paramOf(effect, 2)!!, 4f) }
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == 31 && it.time == 53 } }
        fun pick(local: Int, frame: Int, view: Double, zoom: Float) {
            compose.runOnIdle {
                store.seek(frame)
                store.selectKeyframe(id, store.primaryKeys().single { it.property == 31 && it.time == local })
                state.compact = true; state.heldView = view; state.pps = zoom
            }
            compose.waitForIdle()
        }
        for (zoom in listOf(20f, 120f, 800f)) {
            pick(53, 113, 113.375, zoom)
            assertPixels(113, "effect key after immediate seek at zoom $zoom")
        }
        pick(53, 113, 113.0, 120f)
        val cy = metrics.rowsTop + metrics.diamondCyCompact
        val ppf = 120f * metrics.density / 30f
        surface().performTouchInput {
            down(Offset(state.width / 2f, cy))
            moveBy(Offset(5 * ppf, 0f), 80)
            updatePointerTo(0, Offset(state.width / 2f + 9 * ppf, cy)); up()
        }
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == 31 && it.time == 62 } }
        pick(62, 122, 122.375, 800f)
        assertPixels(122, "effect key release")
        compose.runOnIdle { store.seek(95); store.toggleEffectKeyframe(effect, store.paramOf(effect, 2)!!) }
        compose.waitUntil(5000) { store.primaryKeys().none { it.property == 31 && it.time == 35 } }
        assertTrue(store.primaryKeys().any { it.property == 31 && it.time == 62 })
    }
}
