package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Real effect controls, real pointer gestures and independent native reads. */
class EffectVectorRowsDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private var layer = 0L
    private var effect = 0
    private var parameter = 0
    private val type = effectTypeId("aurea.stylize.motion_tile")
    private val rows = ByteBuffer.allocateDirect(64 * EffectParam.ROW_BYTES).order(ByteOrder.nativeOrder())
    private val blob = ByteBuffer.allocateDirect(64 * 1024).order(ByteOrder.nativeOrder())
    private val keys = ByteBuffer.allocateDirect(256 * PodLayout.KEYFRAME_ROW_BYTES).order(ByteOrder.nativeOrder())
    private val prefix get() = "effects.vector.$effect.$parameter"

    private fun initialize() {
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1080, 30f, "Beta3 effect vector controls") }
        compose.waitUntil(15000) { store.project.title == "Beta3 effect vector controls" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.detail != null }
        layer = checkNotNull(store.primary)
        compose.runOnIdle { store.addEffect(type, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
        effect = store.effects.single { it.typeId == type }.effectId
        compose.waitUntil(10000) { store.effectParams[effect]?.any { it.type == ParamType.POINT2D && !it.hidden } == true }
        parameter = store.effectParams.getValue(effect).single { it.type == ParamType.POINT2D && !it.hidden }.index
        compose.onNodeWithTag("layer.panel.effects").performClick()
        openControls()
        revealCenter()
        compose.onNodeWithTag(axisTag(0)).performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag(axisTag(1)).assertIsDisplayed()
        compose.runOnIdle { assertEquals(ParamType.POINT2D, nativeParameter(layer, effect).type) }
    }

    /** Omega (área 07): a lista nasce fechada; tocar na linha abre os controles do efeito. */
    private fun openControls() {
        val card = effectCardId(type)
        if (compose.onAllNodesWithTag("effects.settings.$card").fetchSemanticsNodes().isNotEmpty()) return
        compose.onNodeWithTag("effects.expand.$card").performClick()
        compose.waitUntil(10000) { compose.onAllNodesWithTag("effects.settings.$card").fetchSemanticsNodes().isNotEmpty() }
    }

    /** Omega (área 07, P1-3): caixa de eixo `effects.axis.<efeito>.<parâmetro>.<eixo>`. */
    private fun axisTag(axis: Int) = "effects.axis.$effect.$parameter.$axis"

    private fun revealCenter() {
        // Motion Tile's center is an advanced parameter: the "Avançado" section (Omega).
        if (compose.onAllNodesWithTag(axisTag(0)).fetchSemanticsNodes().isEmpty())
            compose.onNodeWithTag("effects.section.$effect.advanced").performScrollTo().performClick()
    }

    private fun nativeParameter(owner: Long, fx: Int): EffectParam {
        val count = store.engineForStress.queryEffectParams(owner, fx, rows, 64, blob)
        assertTrue("The real engine must publish effect metadata", count > 0)
        return (0 until count).map { EffectParam.read(rows, it, blob) }.single { it.index == parameter }
    }
    private fun nativeValues(owner: Long = layer, fx: Int = effect): FloatArray = nativeParameter(owner, fx).value.copyOf(2)
    private fun nativeKeys(): List<KeyframeRow> {
        val count = store.engineForStress.queryKeyframes(layer, keys, 256)
        assertTrue(count >= 0)
        return (0 until count).map { KeyframeRow.read(keys, it) }.filter {
            it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effect && it.paramIndex / 4 == parameter
        }
    }
    private fun choose(axis: Int) = compose.onNodeWithTag(axisTag(axis)).performScrollTo().performClick()
    // The Omega ruler flings above 300 px/s; the finger rests before lifting so
    // each scrub is exactly one gesture (and one undo step) with no fling.
    private fun scrub() = compose.onNodeWithTag("$prefix.ruler").performTouchInput {
        down(center)
        moveBy(Offset(-48f, 0f), 16)
        moveBy(Offset(-12f, 0f), 16)
        moveBy(Offset.Zero, 120)
        up()
    }
    private fun shot(name: String) {
        val config = context.resources.configuration
        stabilityScreenshot("beta3-effects-${config.screenWidthDp}x${config.screenHeightDp}dp-$name.png")
    }
    private fun assertNear(expected: FloatArray, actual: FloatArray, label: String) =
        assertArrayEquals(label, expected, actual, .00001f)

    @Test fun rapidNormalAxisGesturesPreserveEachOtherAndHaveTwoUndoSteps() {
        initialize()
        lateinit var before: FloatArray
        compose.runOnIdle { before = nativeValues() }
        choose(0)
        shot("before")
        scrub()
        // No waitUntil, artificial batch, native read, or forced refresh between
        // X and Y. This is the reachable UI sequence of two completed gestures.
        val afterX = store.effectParams.getValue(effect).single { it.index == parameter }.value.copyOf(2)
        assertTrue("The first real gesture must edit X", abs(afterX[0] - before[0]) > .00001f)
        choose(1)
        scrub()
        compose.runOnIdle {
            val actual = nativeValues()
            assertEquals("Editing Y must retain the X from the completed gesture", afterX[0], actual[0], .00001f)
            assertTrue("The second real gesture must edit Y", abs(actual[1] - before[1]) > .00001f)
            store.undo()
        }
        compose.waitForIdle()
        compose.runOnIdle {
            assertNear(afterX, nativeValues(), "One undo removes only the Y gesture")
            store.undo()
        }
        compose.waitForIdle()
        compose.runOnIdle { assertNear(before, nativeValues(), "A second undo removes only the X gesture") }
        shot("two-undo-steps")
    }

    @Test fun groupedDiamondAndAnimatedAxisGesturesKeepExactlyTwoNativeKeys() {
        initialize()
        choose(0)
        compose.onNodeWithContentDescription(context.getString(R.string.panel_marcar_keyframe_aqui)).performClick()
        compose.waitUntil(10000) { store.keyframes[layer].orEmpty().count {
            it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effect && it.paramIndex / 4 == parameter
        } == 2 }
        lateinit var before: FloatArray
        compose.runOnIdle {
            before = nativeValues()
            assertEquals(setOf(parameter * 4, parameter * 4 + 1), nativeKeys().map { it.paramIndex }.toSet())
        }
        shot("grouped-keyframe")
        scrub()
        val afterX = store.effectParams.getValue(effect).single { it.index == parameter }.value.copyOf(2)
        assertTrue(abs(afterX[0] - before[0]) > .00001f)
        choose(1)
        scrub()
        compose.runOnIdle {
            val actual = nativeValues()
            assertEquals(afterX[0], actual[0], .00001f)
            assertTrue(abs(actual[1] - before[1]) > .00001f)
            val result = nativeKeys()
            assertEquals("Editing the selected channel updates, rather than duplicates, its key", 2, result.size)
            assertTrue(result.all { it.time == 0 })
            assertEquals(setOf(parameter * 4, parameter * 4 + 1), result.map { it.paramIndex }.toSet())
            store.undo()
        }
        compose.waitForIdle()
        compose.runOnIdle { assertNear(afterX, nativeValues(), "Animated Y is one undo step"); store.undo() }
        compose.waitForIdle()
        compose.runOnIdle {
            assertNear(before, nativeValues(), "Animated X is one undo step")
            assertEquals(2, nativeKeys().size)
        }
        shot("animated-axes")
    }

    @Test fun anOldEffectNumericSheetCannotCommitToAnotherLayer() {
        initialize()
        lateinit var firstValues: FloatArray
        compose.runOnIdle { firstValues = nativeValues(); store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 2 && store.primary != layer }
        val second = checkNotNull(store.primary)
        compose.runOnIdle { store.addEffect(type, listOf(second)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
        val secondEffect = store.effects.single { it.typeId == type }.effectId
        lateinit var secondValues: FloatArray
        compose.runOnIdle { secondValues = nativeValues(second, secondEffect); store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.effectParams[effect]?.any { it.index == parameter } == true }
        openControls()
        revealCenter()
        // First tap selects, second tap opens the production NumericKeypadSheet.
        choose(0)
        choose(0)
        compose.onNodeWithText("9").assertIsDisplayed().performClick()
        compose.runOnIdle { store.select(second) }
        compose.waitUntil(10000) { store.primary == second && store.detail?.id == second }
        shot("old-keypad-owner")
        compose.onNodeWithText("OK").assertIsDisplayed().performClick()
        compose.waitForIdle()
        compose.runOnIdle {
            assertNear(secondValues, nativeValues(second, secondEffect), "An old sheet cannot edit the new layer")
            assertNear(firstValues, nativeValues(layer, effect), "Discarding a stale sheet cannot edit its old owner")
        }
        shot("owner-guarded")
    }
}
