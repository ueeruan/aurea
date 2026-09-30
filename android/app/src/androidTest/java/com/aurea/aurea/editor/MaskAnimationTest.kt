package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class MaskAnimationTest {
    @get:Rule val compose = createComposeRule()

    @Test fun scalarKeysRemainEditableAndOpenTheirOwnCurve() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        var opened: EditorPanel? = null
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    Box(Modifier.fillMaxSize().background(AureaColors.Surface)) {
                        MaskPanel(PanelEnv(store, {}, { opened = it }, {}, {}, {}, { null }))
                    }
                }
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Mask animation") }
        compose.waitUntil(15000) { store.project.title == "Mask animation" }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(5000) { store.primary != null }
        compose.runOnIdle { store.addMaskPreset(0) }
        compose.waitUntil(5000) { store.masks?.masks?.size == 1 }
        val mask = checkNotNull(store.maskEdit)
        compose.onNodeWithText(context.getString(R.string.panel_3_borda)).performClick()
        compose.onNodeWithTag("mask.$mask.key.2").performScrollTo().performClick()
        compose.waitUntil(5000) { store.primaryKeys().any { it.property == TrackProperty.MASK_PARAM && it.paramIndex == 2 } }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 30 }
        compose.runOnIdle { store.setMaskParam(mask, 2, 0.25f) }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == TrackProperty.MASK_PARAM && it.paramIndex == 2 } == 2 }
        compose.runOnIdle { assertEquals(0.25f, store.masks!!.find(mask)!!.opacity, 0.001f) }
        compose.onNodeWithTag("mask.$mask.curve.2").performScrollTo().performClick()
        compose.runOnIdle {
            assertEquals(EditorPanel.Curve, opened)
            assertEquals(TrackProperty.MASK_PARAM, store.selectedKeyframe?.second?.property)
            assertEquals(mask, store.selectedKeyframe?.second?.effectIndex)
            assertEquals(2, store.selectedKeyframe?.second?.paramIndex)
            store.undo()
        }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == TrackProperty.MASK_PARAM && it.paramIndex == 2 } == 1 }
        compose.runOnIdle { assertEquals(1f, store.masks!!.find(mask)!!.opacity, 0.001f) }
    }
}
