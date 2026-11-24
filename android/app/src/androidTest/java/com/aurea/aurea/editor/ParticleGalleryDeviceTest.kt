package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.ParticlesPanel
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File

class ParticleGalleryDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun gallerySelectsNativeShapeAndFormRendersTheResult() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        lateinit var store: EditorStore
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    Box(Modifier.fillMaxSize().background(AureaColors.Surface)) {
                        ParticlesPanel(PanelEnv(store, {}, {}, {}, {}, {}, { null }))
                    }
                }
            }
        }
        compose.waitUntil(30000) { store.engineReady }
        compose.runOnIdle { store.newProject(480, 480, 30f, "Particle gallery") }
        compose.waitUntil(15000) { store.project.title == "Particle gallery" && !store.projectOperationBusy }
        compose.runOnIdle { store.addParticles(20) }
        val type = effectTypeId("aurea.generate.particular")
        compose.waitUntil(10000) { store.effects.any { it.typeId == type && store.paramOf(it.effectId, 55) != null } }
        val id = store.effects.first { it.typeId == type }.effectId
        fun set(index: Int, value: Float) {
            val parameter = checkNotNull(store.paramOf(id, index))
            store.setEffectParam(id, parameter, value)
        }
        compose.runOnIdle {
            set(52, 1f) // Form
            set(55, 1f); set(56, 1f); set(57, 1f)
            set(24, 160f); set(25, 0f); set(26, 100f)
            set(35, 0f)
        }
        compose.waitUntil(6000) { store.paramOf(id, 52)?.value?.firstOrNull() == 1f }
        compose.waitForIdle()
        val before = checkNotNull(store.captureBitmap(240))

        val field = "effects.choice.$id.48"
        compose.onNodeWithTag("effects.settings.body").performScrollToNode(hasTestTag(field))
        compose.onNodeWithTag(field).performTouchInput { click() }
        compose.onNodeWithTag("particles.gallery").assertIsDisplayed()
        val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-particle-gallery.png").outputStream().use {
            assertTrue(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it))
        }
        screenshot.recycle()
        val cube = "particles.gallery.shape.21"
        compose.onNodeWithTag("particles.gallery.grid").performScrollToNode(hasTestTag(cube))
        compose.onNodeWithTag(cube).performTouchInput { click() }
        compose.waitUntil(6000) { store.paramOf(id, 48)?.value?.firstOrNull() == 21f }
        compose.onNodeWithTag("particles.gallery").assertDoesNotExist()
        compose.waitForIdle()

        val after = checkNotNull(store.captureBitmap(240))
        assertFalse("Sphere and cube need different native output", before.sameAs(after))
        before.recycle()
        after.recycle()

        val particles = checkNotNull(store.primary)
        compose.runOnIdle { store.addVectorLayer(3) }
        compose.waitUntil(6000) { store.layers.size == 2 && store.primary != particles }
        val guide = checkNotNull(store.primary)
        compose.runOnIdle {
            store.setLayerVisible(guide, false)
            store.select(particles)
        }
        compose.waitUntil(6000) { store.primary == particles && store.paramOf(id, 52) != null }
        compose.runOnIdle { set(52, 3f) }
        compose.waitUntil(6000) { store.paramOf(id, 52)?.value?.firstOrNull() == 3f }
        val withoutGuide = checkNotNull(store.captureBitmap(240))
        val guideIndex = (guide and 0xFFFF_FFFFL).toFloat()
        compose.runOnIdle { set(53, guideIndex) }
        compose.waitUntil(6000) { store.paramOf(id, 53)?.value?.firstOrNull() == guideIndex }
        val withGuide = checkNotNull(store.captureBitmap(240))
        assertFalse("A linked shape must feed the path emitter", withoutGuide.sameAs(withGuide))
        withoutGuide.recycle()
        withGuide.recycle()
    }
}
