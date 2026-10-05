package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** The recording's text at 00:01:24, using actual touch input and the native command queue. */
class TransformPadStabilityTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun animatedText(): Long {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().weight(1f))
                    PanelContent(store, EditorPanel.Transform, {}, {}, {}, Modifier.height(280.dp))
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1080, 30f, "Animated text move pad") }
        compose.waitUntil(15000) { store.project.title == "Animated text move pad" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.detail != null }
        val layer = store.primary!!
        compose.runOnIdle {
            store.setTransform2(0, 540f, 1, 515f)
            store.autoKeyTransforms = true
        }
        compose.waitUntil(5000) { store.detail?.position?.get(1) == 515f }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0, 1)) }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().count { it.time == 0 } == 2 }
        compose.runOnIdle { store.seek(54) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 54 }
        return layer
    }

    @Test fun movingAnimatedTextAtOneSecond24FramesUpdatesTheCurrentKeyAndUndo() {
        val layer = animatedText()
        val original = store.detail!!.position.toList()
        compose.onNodeWithTag("transform.move.pad").performTouchInput {
            down(center)
            repeat(8) { moveBy(Offset(12f, 0f), 8) }
            up()
        }
        compose.waitUntil(5000) { store.detail!!.position[0] > original[0] + 20f }
        assertEquals(original[1], store.detail!!.position[1], .01f)
        assertTrue(store.keyframes[layer].orEmpty().any { it.property == 0 && it.time == 54 })
        assertEquals(original[0], store.keyframes[layer].orEmpty().single { it.property == 0 && it.time == 0 }.value, .01f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().none { it.time == 54 } }
        assertEquals(original[0], store.detail!!.position[0], .01f)
    }

    @Test fun expressionDrivenPositionShowsAnActionableExplanationWithoutRemovingTheFormula() {
        val layer = animatedText()
        val keys = listOf(TrackKey(0), TrackKey(1))
        compose.runOnIdle { assertTrue(store.engineForStress.setExpression(layer, keys, "515")?.ok == true) }
        compose.waitUntil(5000) { store.expressions.any { it.enabled } }
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val notice = AppText.get(context, R.string.transform_expression_controlled)
        compose.onNodeWithText(notice).assertExists()
        stabilityScreenshot("expression-controlled.png")
        compose.onNodeWithTag("transform.move.pad").performTouchInput {
            down(center)
            moveBy(Offset(80f, 30f), 100)
            up()
        }
        compose.waitForIdle()
        assertTrue(store.expressions.filter { it.key in keys }.all { it.enabled })
        assertEquals(515f, store.detail!!.position[0], .01f)
        assertEquals(515f, store.detail!!.position[1], .01f)
    }
}
