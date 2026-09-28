package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.timeline.KeyRef
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

    @Test fun horizontalSwipeOnClipOnlyScrolls() {
        launch()
        val initial = store.layers.single()
        timeline().performTouchInput {
            swipe(Offset(width / 2f + 30 * density, 52 * density),
                Offset(width / 2f - 50 * density, 52 * density), 240)
        }
        compose.runOnIdle {
            assertEquals(initial.startFrame, store.layers.single().startFrame)
            assertTrue(store.selection.isEmpty())
        }
    }

    @Test fun draggingUnselectedClipMovesWithoutOpeningOptionsAndUndoRestoresIt() {
        launch()
        val initial = store.layers.single()
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            down(Offset(width / 2f + 30 * density, 52 * density))
            advanceEventTime(600)
            moveTo(Offset(width / 2f + 100 * density, 52 * density), 240)
            up()
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
            down(Offset(width / 2f + 30 * density, 52 * density))
            advanceEventTime(600)
            moveTo(Offset(width / 2f + 100 * density, 52 * density), 240)
            up()
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

    /**
     * "Às vezes a camada é escolhida e vai junto": (1) o dedo rasteja abaixo do
     * slop enquanto o prazo do toque longo corre e então rola — o prazo vencia e
     * o movimento seguinte levantava a camada (reordenar); (2) uma rolagem um
     * pouco torta (dx > dy por pouco) sobre um clipe não escolhido era "mover".
     * Nenhuma das duas escolhe, move ou reordena: a 1ª rola, a 2ª faz scrub.
     */
    @Test fun creepingOrSlightlyDiagonalSwipesOnClipsNeverSelectMoveOrReorder() {
        launch(24)
        val before = store.layers.map { Triple(it.id, it.startFrame, it.endFrame) }
        val order = store.layers.map { it.id }
        val height = timeline().fetchSemanticsNode().size.height
        // (1) 1,2 dp a cada 100 ms: 6 dp em 500 ms, abaixo do slop de 8 — e só então rola.
        timeline().performTouchInput {
            val x = width * .72f
            val y0 = height - 18 * density
            down(Offset(x, y0))
            for (i in 1..5) moveTo(Offset(x, y0 - 1.2f * i * density), 100)
            moveTo(Offset(x, y0 - 40 * density), 60)
            moveTo(Offset(x, 54 * density), 250)
            advanceEventTime(120)
            up()
        }
        compose.runOnIdle {
            assertTrue("A creeping start must not select", store.selection.isEmpty())
            assertEquals("A creeping start must not reorder", order, store.layers.map { it.id })
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        timeline().performTouchInput { click(Offset(width * .72f, 54 * density)) }
        compose.runOnIdle {
            val index = order.indexOf(store.primary)
            assertTrue("A creeping start must still scroll, index=$index", index >= 2)
            store.clearSelection()
        }
        timeline().performTouchInput {
            swipe(Offset(width * .72f, 55 * density), Offset(width * .72f, height - 5 * density), 300)
        }
        compose.waitForIdle()
        // (2) Rolagem um pouco torta sobre o corpo da 1ª camada: 7:6 além do slop, para a esquerda.
        timeline().performTouchInput {
            val x = width / 2f + 30 * density
            val y = 52 * density
            down(Offset(x, y))
            advanceEventTime(60)
            moveTo(Offset(x - 14 * density, y + 12 * density), 40)
            moveTo(Offset(x - 60 * density, y + 40 * density), 200)
            advanceEventTime(60)
            up()
        }
        compose.waitUntil(5000) { store.playhead > 0 }
        compose.runOnIdle {
            assertTrue("A slightly diagonal swipe must scrub, not select", store.selection.isEmpty())
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
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

    /**
     * Segundo dedo DEPOIS do toque longo aceito (o primeiro pousou 600 ms
     * antes, quieto, sobre um clipe não escolhido): é pinça, como antes do
     * prazo. O movimento do primeiro dedo não escolhe nem levanta a camada, e o
     * zoom acontece — a 80 dp/s, 40 dp da régua valem 15 quadros; com a pinça
     * (dedos de 80 para 140 dp) valem menos.
     */
    @Test fun secondFingerAfterAcceptedHoldPinchesInsteadOfSelectingOrLiftingTheLayer() {
        launch(5)
        val before = store.layers.map { Triple(it.id, it.startFrame, it.endFrame) }
        val order = store.layers.map { it.id }
        val height = timeline().fetchSemanticsNode().size.height
        timeline().performTouchInput {
            val x = width / 2f + 40 * density
            val y = 52 * density
            down(0, Offset(x, y))
            advanceEventTime(600)
            down(1, Offset(x + 80 * density, y))
            for (i in 1..3) {
                updatePointerTo(0, Offset(x - 10 * i * density, y))
                updatePointerTo(1, Offset(x + (80 + 10 * i) * density, y))
                move(50)
            }
            advanceEventTime(60)
            up(0)
            up(1)
        }
        compose.waitForIdle()
        compose.runOnIdle {
            assertTrue("A pinça depois do toque longo não escolhe", store.selection.isEmpty())
            assertEquals("A pinça depois do toque longo não reordena", order, store.layers.map { it.id })
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
        assertEquals(height, timeline().fetchSemanticsNode().size.height)
        val playhead = compose.runOnIdle { store.playhead }
        timeline().performTouchInput { click(Offset(width / 2f + 40 * density, 16 * density)) }
        compose.waitUntil(5000) { store.playhead != playhead }
        compose.runOnIdle {
            val frames = store.playhead - playhead
            assertTrue("A pinça deveria ter ampliado a timeline: 40 dp = $frames quadros", frames in 1..14)
        }
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

    /**
     * Seleção de keyframes entre PROPRIEDADES na timeline: Posição X e Escala X
     * em frames diferentes, trilhas abertas, modo "Selecionar", um keyframe de
     * cada trilha; arrastar um move os dois (e só eles) num passo de desfazer.
     * Excluir e Duplicar agem na seleção inteira.
     */
    @Test fun multiSelectedKeysAcrossPropertiesMoveDeleteAndDuplicateTogether() {
        launch()
        val id = store.layers.single().id
        compose.runOnIdle { store.select(id, openOptions = false); store.snapping = false }
        // Posição X em 0 e 30; Escala X em 15 e 45.
        for ((frame, property) in listOf(0 to 0, 30 to 0, 15 to 3, 45 to 3)) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(5000) { store.playhead == frame }
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(property)) }
            compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == property && it.time == frame } }
        }
        compose.runOnIdle { store.clearSelection(); store.seek(30) }
        compose.waitUntil(5000) { store.playhead == 30 }
        val before = store.keyframes[id].orEmpty()
        assertEquals(4, before.size)
        // Geometria da timeline (dp): régua 38, linha da camada 36, trilhas de 28 com o
        // losango a 20 do topo. Abertas: Transform (74), Position X (102), Scale X (130).
        val positionY = 122 * density
        val scaleY = 150 * density
        // A vista É o cabeçote (30): 80 dp/s = 8/3 dp por frame a partir do centro.
        fun keyX(width: Int, frame: Int) = width / 2f + (frame - 30) * 80f / 30f * density

        // Abrir as trilhas pelo glifo do tipo na calha da camada.
        timeline().performTouchInput { click(Offset(14 * density, 50 * density)) }
        compose.waitForIdle()

        fun selectPositionAndScale() {
            // Toque simples: só a Posição X @30 (abre a curva; timeline compacta).
            timeline().performTouchInput { click(Offset(keyX(width, 30), positionY)) }
            compose.waitUntil(5000) { store.keySelection?.size == 1 }
            compose.onNodeWithTag("timeline.keys.select").performClick()
            compose.waitUntil(5000) { store.keySelectMode }
            compose.waitForIdle()
            // O modo fecha o painel: a timeline volta alta com as trilhas abertas.
            timeline().performTouchInput { click(Offset(keyX(width, 45), scaleY)) }
            compose.waitUntil(5000) { store.keySelection?.size == 2 }
            compose.runOnIdle {
                assertEquals(setOf(KeyRef(0, -1, 0, 30), KeyRef(3, -1, 0, 45)), store.keySelection?.keys)
            }
        }

        // --- Arrastar um move os dois -----------------------------------------------------
        selectPositionAndScale()
        timeline().performTouchInput {
            swipe(Offset(keyX(width, 30), positionY), Offset(keyX(width, 40), positionY), 400)
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().none { it.property == 0 && it.time == 30 } }
        compose.runOnIdle {
            val keys = store.keyframes[id].orEmpty()
            val moved = keys.single { it.property == 0 && it.time != 0 }.time - 30
            assertTrue("Selection should move about 10 frames, moved=$moved", moved in 8..12)
            assertTrue("Scale key moves by the same delta", keys.any { it.property == 3 && it.time == 45 + moved })
            // Os NÃO escolhidos ficam.
            assertTrue(keys.any { it.property == 0 && it.time == 0 })
            assertTrue(keys.any { it.property == 3 && it.time == 15 })
            assertEquals(before.size, keys.size)
            // A seleção acompanhou o motor.
            assertEquals(setOf(KeyRef(0, -1, 0, 30 + moved), KeyRef(3, -1, 0, 45 + moved)), store.keySelection?.keys)
            store.undo()
        }
        try { compose.waitUntil(5000) { store.keyframes[id] == before } }
        catch (error: Throwable) { throw AssertionError("One undo must restore both properties. Before=$before After=${store.keyframes[id]}", error) }
        compose.runOnIdle { assertNull("Undo clears the key selection", store.keySelection) }

        // --- Excluir -----------------------------------------------------------------------
        selectPositionAndScale()
        compose.onNodeWithTag("timeline.keys.delete").performClick()
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 2 }
        compose.runOnIdle {
            val keys = store.keyframes[id].orEmpty()
            assertTrue(keys.any { it.property == 0 && it.time == 0 })
            assertTrue(keys.any { it.property == 3 && it.time == 15 })
            assertNull(store.keySelection)
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id] == before }

        // --- Duplicar: cópia 1 frame depois do último (anc. 30 → 46) ------------------------
        selectPositionAndScale()
        compose.onNodeWithTag("timeline.keys.duplicate").performClick()
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 6 }
        compose.runOnIdle {
            val keys = store.keyframes[id].orEmpty()
            assertTrue(keys.any { it.property == 0 && it.time == 46 })
            assertTrue(keys.any { it.property == 3 && it.time == 61 })
            assertEquals(before, keys.filterNot { (it.property == 0 && it.time == 46) || (it.property == 3 && it.time == 61) })
            // As cópias viram a seleção.
            assertEquals(setOf(KeyRef(0, -1, 0, 46), KeyRef(3, -1, 0, 61)), store.keySelection?.keys)
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id] == before }
    }

    @Test fun trimmingBeyondProjectEndGrowsDurationAndUndoRestoresIt() {
        trimPastEnd(openTransform = false)
    }

    /** Fileira compacta (camada escolhida, doca aberta): o arrasto vertical troca de camada, sem mover nada. */
    @Test fun verticalSwipeOnCompactRowStepsThroughLayers() {
        launch(5)
        val before = store.layers.map { Triple(it.id, it.startFrame, it.endFrame) }
        compose.runOnIdle { store.select(before[2].first) }
        compose.waitForIdle()
        val x = compose.onNodeWithTag("editor.timeline").fetchSemanticsNode().size.width * .72f
        timeline().performTouchInput { swipe(Offset(x, height - 4 * density), Offset(x, 4 * density), 300) }
        compose.runOnIdle {
            val index = before.indexOfFirst { it.first == store.primary }
            assertTrue("Swipe up should choose a layer below, index=$index", index > 2)
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
        val middle = before.indexOfFirst { it.first == store.primary }
        timeline().performTouchInput { swipe(Offset(x, 4 * density), Offset(x, height - 4 * density), 300) }
        compose.runOnIdle {
            val index = before.indexOfFirst { it.first == store.primary }
            assertTrue("Swipe down should go back up, index=$index", index < middle)
            assertEquals(before, store.layers.map { Triple(it.id, it.startFrame, it.endFrame) })
        }
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
