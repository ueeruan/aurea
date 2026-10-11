package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class EffectEnumKeyframesDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun discreteEffectChoicesCanBeMarkedFromTheNativeKeyframeRail() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Effect choices keyframes") }
        compose.waitUntil(10000) { store.project.title == "Effect choices keyframes" }
        compose.runOnIdle { store.addShape(1); store.addEffect(effectTypeId("aurea.motion.pulse_size")) }
        compose.waitUntil(10000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        compose.onNodeWithTag("dock.tool.Effects").performScrollTo().performClick()
        compose.waitUntil(10000) { store.effectParams[effect].orEmpty().any { it.type == 7 } }
        val choice = store.effectParams[effect]!!.first { it.type == 7 }
        assertTrue("The engine must advertise enum animation", choice.flags and 1 != 0)
        compose.onNodeWithTag("effects.choice.$effect.${choice.index}").performScrollTo()
        // Select the row label without opening its option picker.
        compose.onNode(hasText(choice.label) and hasAnyAncestor(hasTestTag("aurea.effects.stack")), useUnmergedTree = true).performClick()
        compose.onNodeWithContentDescription(context.getString(R.string.panel_marcar_keyframe_aqui)).assertIsEnabled().performClick()
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == 31 && it.effectIndex == effect && it.paramIndex == choice.index * 4 && it.time == 0 } }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 30 }
        compose.runOnIdle { store.setEffectParam(effect, store.paramOf(effect, choice.index)!!, 1f) }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == 31 && it.effectIndex == effect && it.paramIndex == choice.index * 4 } == 2 }
        compose.runOnIdle { store.seek(15) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 15 && store.paramOf(effect, choice.index)?.value?.firstOrNull() == 0f }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 30 && store.paramOf(effect, choice.index)?.value?.firstOrNull() == 1f }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == 31 && it.effectIndex == effect && it.paramIndex == choice.index * 4 } == 1 }
    }
}
