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
        compose.onNodeWithTag("curve.category.bounces").performClick()
        compose.waitUntil(5000) {
            store.primaryKeys().any { it.property == 40 && it.effectIndex == 1 && it.paramIndex == 13 && it.time == 0 && it.interpolation == 7 }
        }
        compose.runOnIdle { assertTrue(store.primaryKeys().filter { it.property == 40 && it.paramIndex == 0 }.none { it.interpolation == 7 }) }
    }

    @Test fun textAnimationOpensFromEffectsAndPreservesKeysOnReopen() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        var opened by mutableStateOf(EditorPanel.Effects)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                EditorScreen(store)
                val env = PanelEnv(store, {}, { opened = it }, {}, {}, {}, { null }, onOpenAnimators = {
                    AnimationRequest.tab = AnimationTab.Text
                    opened = EditorPanel.Animation
                })
                Box(Modifier.fillMaxSize().background(AureaColors.Surface)) {
                    when (opened) {
                        EditorPanel.Animation -> AnimationPanel(env)
                        EditorPanel.Curve -> CurvePanel(env)
                        else -> EffectsPanel(env)
                    }
                }
                }
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Text animator editing") }
        compose.waitUntil(15000) { store.project.title == "Text animator editing" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.primary != null && store.textDetail != null }
        compose.onNodeWithTag("effects.text.animators").performScrollTo().performClick()
        compose.onNodeWithTag("animation.tab.ready").performClick()
        compose.onNodeWithTag("text.motion.preset.10").performScrollTo().performClick()
        compose.waitUntil(5000) { store.textAnimators.size == 1 }
        compose.onNodeWithTag("animation.tab.text").performClick()
        compose.onNodeWithTag("anim.item.0").performScrollTo().performClick()
        compose.runOnIdle { store.seek(15) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 15 }
        val keyButton = compose.onNodeWithTag("anim.0.key.0.toggle")
        fun progressKeys() = store.primaryKeys().filter {
            it.property == TrackProperty.TEXT_ANIM_PARAM && it.effectIndex == 0 && it.paramIndex == 0
        }
        keyButton.performScrollTo().performClick()
        compose.waitUntil(5000) { progressKeys().any { it.time == 15 } }
        keyButton.performClick()
        compose.waitUntil(5000) { progressKeys().none { it.time == 15 } }
        val presetKeys = progressKeys()
        assertEquals(2, presetKeys.size)
        compose.onNodeWithTag("anim.0.duplicate").performScrollTo().performClick()
        compose.waitUntil(5000) { store.textAnimators.size == 2 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.textAnimators.size == 1 }
        keyButton.performScrollTo().performClick()
        compose.waitUntil(5000) { progressKeys().size == 3 }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_editar_curva_propriedade)).performClick()
        compose.runOnIdle {
            assertEquals(EditorPanel.Curve, opened)
            assertEquals(TrackProperty.TEXT_ANIM_PARAM, store.selectedKeyframe?.second?.property)
            assertEquals(0, store.selectedKeyframe?.second?.paramIndex)
        }
        val savedKeys = progressKeys()
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(10000) { saved }
        val path = store.project.path!!
        compose.runOnIdle { store.closeProject() }
        compose.waitUntil(10000) { store.layers.isEmpty() }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(10000) { store.layers.size == 1 }
        compose.runOnIdle { store.select(store.layers.single().id, openOptions = false) }
        compose.waitUntil(5000) { store.textAnimators.size == 1 && progressKeys().size == 3 }
        compose.runOnIdle {
            assertEquals(savedKeys, progressKeys())
        }
    }
}
