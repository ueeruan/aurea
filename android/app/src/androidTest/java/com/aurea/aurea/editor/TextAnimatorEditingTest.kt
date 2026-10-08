package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.runtime.*
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackProperty
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import com.aurea.aurea.ui.theme.AureaColors
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class TextAnimatorEditingTest {
    @get:Rule val compose = createComposeRule()

    @Test fun transformAnimatorRailOpensTheSelectedWiggleCurve() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        lateinit var store: EditorStore
        var initialized = false
        var opened by mutableStateOf<EditorPanel?>(null)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    val env = PanelEnv(store, {}, { opened = it }, {}, {}, {}, { null })
                    if (opened == EditorPanel.Curve) CurvePanel(env)
                    else TransformPanel(env, TransformTab.Animadores, {})
                }
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Animator rail") }
        compose.waitUntil(15000) { store.project.title == "Animator rail" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.primary != null }
        compose.runOnIdle {
            store.addLayerAnimator(); store.addLayerAnimator()
            store.toggleLayerAnimKey(1, 13); store.seek(30)
        }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 30 }
        compose.runOnIdle {
            store.setLayerAnimParam(1, 13, 200f)
            store.focusLayerAnimator(com.aurea.aurea.engine.TrackKey(40, 1, 13))
        }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_editar_curva_propriedade)).assertIsEnabled().performClick()
        compose.runOnIdle {
            assertEquals(EditorPanel.Curve, opened)
            assertEquals(40, store.selectedKeyframe?.second?.property)
            assertEquals(1, store.selectedKeyframe?.second?.effectIndex)
            assertEquals(13, store.selectedKeyframe?.second?.paramIndex)
        }
        compose.onNodeWithTag("curve.preset.bounce").performClick()
        compose.waitUntil(5000) {
            store.primaryKeys().any { it.property == 40 && it.effectIndex == 1 && it.paramIndex == 13 && it.time == 0 && it.interpolation == 7 }
        }
        compose.runOnIdle { assertTrue(store.primaryKeys().filter { it.property == 40 && it.paramIndex == 0 }.none { it.interpolation == 7 }) }
    }

    @Test fun textAnimationIsAnEffectAndPresetsSurviveKeysCopyUndoAndReopen() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        var opened by mutableStateOf<EditorPanel?>(null)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                EditorScreen(store)
                val env = PanelEnv(store, {}, { opened = it }, {}, {}, {}, { null })
                if (opened == null) {
                    Column(Modifier.fillMaxSize().background(AureaColors.Surface).verticalScroll(rememberScrollState())) { TextAnimSection(env) }
                } else if (opened == EditorPanel.Curve) {
                    Box(Modifier.fillMaxSize().background(AureaColors.Surface)) { CurvePanel(env) }
                } else {
                    Box(Modifier.fillMaxSize().background(AureaColors.Surface)) { EffectsPanel(env) }
                }
                }
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Text animator editing") }
        compose.waitUntil(15000) { store.project.title == "Text animator editing" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.primary != null && store.textDetail != null }
        compose.onNodeWithTag("text.anim.0.duplicate").assertDoesNotExist()
        // Existing presets still author the same data; adding the effect preserves it.
        compose.onNodeWithText(context.getString(R.string.edt_tp_pop)).performClick()
        compose.waitUntil(5000) { store.textAnimators.isNotEmpty() }
        val presetKeys = store.primaryKeys().filter { it.property == 33 }
        val presetCount = store.textAnimators.size
        compose.onNodeWithTag("text.transform.add").performScrollTo().performClick()
        compose.waitUntil(5000) { store.effects.size == 1 && opened == EditorPanel.Effects }
        val effect = store.effects.single().effectId
        assertEquals(effectTypeId("aurea.text.transform"), store.effects.single().typeId)
        compose.waitUntil(5000) { store.paramOf(effect, 0) != null }
        compose.onNodeWithTag("effects.param.$effect.0.0").performScrollTo().performClick()
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 0 }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_marcar_keyframe_aqui)).performClick()
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effect && it.paramIndex == 0 } }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 30 }
        compose.runOnIdle { store.setEffectParam(effect, store.paramOf(effect, 0)!!, 100f) }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effect && it.paramIndex == 0 } == 2 }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_editar_curva_propriedade)).performClick()
        compose.onNodeWithTag("curve.preset.bounce").performClick()
        compose.onNodeWithTag("curve.bounce.count").performClick()
        compose.onNodeWithTag("curve.bounce.strength").performTouchInput {
            down(center); moveTo(androidx.compose.ui.geometry.Offset(width * .8f, center.y)); up()
        }
        compose.waitUntil(5000) { store.primaryKeys().first { it.property == 31 && it.time == 0 }.interpolation == 7 }
        val bounce = store.engineForStress.queryKeyframeEasing(store.primary!!, 31, effect, 0, 0)!!
        assertEquals(.5f, bounce[0], .0001f)
        assertTrue("Bounce slider easing=${bounce.contentToString()}", bounce[1] > .6f); assertEquals(-10f, bounce[3], .0001f)
        compose.runOnIdle {
            assertEquals(EditorPanel.Curve, opened)
            assertEquals(TrackProperty.EFFECT_PARAM, store.selectedKeyframe?.second?.property)
            assertEquals(effect, store.selectedKeyframe?.second?.effectIndex)
            assertEquals(presetCount, store.textAnimators.size)
            assertEquals(presetKeys, store.primaryKeys().filter { it.property == 33 })
            store.copyEffects(effect); store.pasteEffects()
        }
        compose.waitUntil(5000) { store.effects.size == 2 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.effects.size == 1 }
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(10000) { saved }
        val path = store.project.path!!
        compose.runOnIdle { store.closeProject() }
        compose.waitUntil(10000) { store.layers.isEmpty() }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(10000) { store.layers.size == 1 }
        compose.runOnIdle { store.select(store.layers.single().id, openOptions = false) }
        compose.waitUntil(5000) { store.effects.size == 1 && store.textAnimators.size == presetCount }
        compose.runOnIdle {
            assertEquals(presetKeys, store.primaryKeys().filter { it.property == 33 })
            assertEquals(listOf(0, 30), store.primaryKeys().filter { it.property == TrackProperty.EFFECT_PARAM }.map { it.time })
            assertArrayEquals(bounce, store.engineForStress.queryKeyframeEasing(store.primary!!, 31, effect, 0, 0)!!, .0001f)
        }
        // Returning from a keyed Text Transform must open the NEW animator card.
        compose.runOnIdle { opened = null }
        compose.onNodeWithTag("text.animator.add").performScrollTo().performClick()
        compose.waitUntil(5000) { store.effects.size == 2 && opened == EditorPanel.Effects }
        val animator = store.effects.last { it.typeId == effectTypeId("aurea.text.animator") }.effectId
        compose.waitUntil(5000) { store.timelineFocus?.any { it.effectIndex == animator } == true }
        compose.runOnIdle {
            assertNull(store.pendingEffectFocus)
            assertEquals(presetCount, store.textAnimators.size)
        }
    }
}
