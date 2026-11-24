package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

class SceneAddonDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private lateinit var store: EditorStore

    @Before fun openEditor() {
        val app = instrumentation.targetContext.applicationContext as Application
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(app))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(480, 360, 30.0, "Scene add-ons test", null, UI_MODE_COMPOSITION) }
        compose.waitUntil(15000) { store.project.title == "Scene add-ons test" && !store.projectOperationBusy }
    }

    private fun effect(key: String): Int {
        val type = effectTypeId(key)
        compose.waitUntil(8000) { store.effects.any { it.typeId == type && it.known } }
        return store.effects.first { it.typeId == type }.effectId
    }

    private fun set(effect: Int, index: Int, value: Float, component: Int = 0) {
        compose.runOnIdle { store.setEffectParam(effect, checkNotNull(store.paramOf(effect, index)), value, component) }
        compose.waitUntil(5000) { store.paramOf(effect, index)?.value?.getOrNull(component) == value }
    }

    private fun add(key: String): Int {
        compose.runOnIdle { store.addEffect(effectTypeId(key)) }
        return effect(key)
    }

    private fun frame(name: String): Bitmap {
        val bitmap = checkNotNull(store.captureBitmap(360)) { "Missing rendered frame: $name" }
        File(instrumentation.targetContext.filesDir, "ui-addon-$name.png").outputStream().use {
            assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
        }
        return bitmap
    }

    private fun contentBounds(image: Bitmap): List<Int> {
        val background = image.getPixel(0, 0)
        var left = image.width
        var top = image.height
        var right = -1
        var bottom = -1
        for (y in 0 until image.height) for (x in 0 until image.width) {
            if (image.getPixel(x, y) == background) continue
            left = minOf(left, x)
            top = minOf(top, y)
            right = maxOf(right, x)
            bottom = maxOf(bottom, y)
        }
        check(right >= left && bottom >= top)
        return listOf(left, top, right, bottom)
    }

    @Test fun repeatedModelUsesOneLayerAndToonChangesTheMeshRender() {
        compose.runOnIdle { assertTrue(store.addShape3D(0) > 0) }
        compose.waitUntil(8000) { store.layers.size == 1 && store.primary != null }
        compose.runOnIdle {
            store.setCompositionBackground(0.91f, 0.93f, 0.96f)
            store.setTransform(TrackProperty.SCALE_X, 0.36f)
            store.setTransform(TrackProperty.SCALE_Y, 0.36f)
            store.setTransform(TrackProperty.SCALE_Z, 0.36f)
            store.setTransform(TrackProperty.ROTATION_X, -15f)
            store.setTransform(TrackProperty.ROTATION_Y, 22f)
        }
        val original = frame("original")
        compose.runOnIdle { store.enterSceneEditor() }
        compose.onNodeWithTag("scene.addons").performScrollTo().performClick()
        compose.onNodeWithTag("scene.addons.repeater").performClick()
        val copies = effect("aurea.3d.replicator")
        compose.onNodeWithTag("effects.settings.body").assertIsDisplayed()
        set(copies, 0, 3f)
        set(copies, 2, 315f)
        set(copies, 2, 0f, component = 1)
        compose.runOnIdle { store.exitSceneEditor() }
        val repeated = frame("three-copies")
        assertEquals(1, store.layers.size)
        assertFalse("Repeating the model should change the native frame", original.sameAs(repeated))

        add("aurea.3d.toon")
        val cartoon = frame("toon")
        assertFalse("Toon must shade the real mesh", repeated.sameAs(cartoon))
        assertTrue("Shading must preserve the mesh silhouette", contentBounds(repeated).zip(contentBounds(cartoon)).all {
            (before, after) -> abs(before - after) <= 2
        })
        original.recycle(); repeated.recycle(); cartoon.recycle()

        compose.runOnIdle { store.enterSceneEditor() }
        compose.onNodeWithTag("scene.addons").performScrollTo().performClick()
        compose.onNodeWithTag("scene.addons.drawing").performClick()
        compose.waitUntil(8000) { !store.sceneEditor && store.isDrawingLayer && store.layers.size == 2 }
        assertTrue("The new drawing must be a plane in the scene", store.layers.first { it.id == store.primary }.isThreeD)
    }

    @Test fun inkAndCopyLayoutChangeTheRenderedFrame() {
        compose.runOnIdle {
            store.setCompositionBackground(1f, 1f, 1f)
            store.addShape(1)
        }
        compose.waitUntil(8000) { store.primary != null && store.layers.size == 1 }
        val original = frame("2d-original")
        val copies = add("aurea.repeat.arrange")
        set(copies, 2, 65f)
        set(copies, 19, 85f)
        val colored = frame("2d-copies")
        assertFalse(original.sameAs(colored))
        val ink = add("aurea.transition.ink_bloom")
        set(ink, 0, 0f)
        val empty = frame("ink-start")
        set(ink, 0, 57f)
        val drop = frame("ink-drop")
        assertFalse("Growing the ink must change the frame", empty.sameAs(drop))
        original.recycle(); colored.recycle(); empty.recycle(); drop.recycle()
    }
}
