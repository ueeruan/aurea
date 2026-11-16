package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Exercise the mobile scene through the editor's normal entry point. */
class SceneWorkspaceUiTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    @Before fun configureOrientation() { configureMobileWorkspaceOrientation(compose) }

    private fun capture(name: String) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        compose.waitForIdle()
        Thread.sleep(650)
        instrumentation.waitForIdleSync()
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        assertMobileWorkspaceScreenshotOrientation(bitmap, name)
        File(instrumentation.targetContext.filesDir, "ui-$name.png").outputStream().use {
            assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
        }
        bitmap.recycle()
    }

    @Test fun compactScenePanClampsAtBoundsAndKeepsPlayheadCentered() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30.0, "Compact scene navigation", null, UI_MODE_COMPOSITION) }
        compose.waitUntil(15000) { store.project.title == "Compact scene navigation" && !store.projectOperationBusy }
        compose.runOnIdle { store.setCompositionDuration(300); store.addNull(true); store.seek(0); store.enterSceneEditor() }
        compose.waitUntil(10000) {
            store.sceneEditor && store.project.durationFrames == 300 && store.layers.singleOrNull()?.endFrame == 300 &&
                compose.onAllNodesWithTag("scene.playhead").fetchSemanticsNodes().isNotEmpty()
        }
        val head = compose.onNodeWithTag("scene.playhead").fetchSemanticsNode().boundsInRoot
        val dock = compose.onNodeWithTag("scene.dock").fetchSemanticsNode().boundsInRoot
        assertEquals(dock.center.x, head.center.x, 2f)
        val density = context.resources.displayMetrics.density
        compose.onNodeWithTag("scene.timeline").performTouchInput {
            val x = width * .85f
            val y = 10f * density
            swipe(Offset(x, y), Offset(x - 72f * density, y), 300)
        }
        compose.waitUntil(5000) { store.playhead == 30 }
        assertEquals(head, compose.onNodeWithTag("scene.playhead").fetchSemanticsNode().boundsInRoot)
        repeat(12) {
            compose.onNodeWithTag("scene.timeline").performTouchInput {
                val y = 10f * density
                swipe(Offset(width * .85f, y), Offset(width * .15f, y), 200)
            }
        }
        compose.waitUntil(5000) { store.playhead == 299 }
        assertEquals(head, compose.onNodeWithTag("scene.playhead").fetchSemanticsNode().boundsInRoot)
        repeat(12) {
            compose.onNodeWithTag("scene.timeline").performTouchInput {
                val y = 10f * density
                swipe(Offset(width * .15f, y), Offset(width * .85f, y), 200)
            }
        }
        compose.waitUntil(5000) { store.playhead == 0 }
        assertEquals(head, compose.onNodeWithTag("scene.playhead").fetchSemanticsNode().boundsInRoot)
        compose.runOnIdle { assertEquals(1, store.layers.size) }
    }

    @Test fun hierarchySelectsRealLayersAndTransformPanelsKeepTheSceneContext() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle {
            store.newProject(320, 240, 30.0, "Mobile scene workspace", null, UI_MODE_COMPOSITION)
        }
        compose.waitUntil(15000) { store.project.title == "Mobile scene workspace" && !store.projectOperationBusy }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.detail != null }
        val first = store.layers.single().id
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(10000) { store.layers.size == 2 && store.primary != first }
        compose.runOnIdle { store.enterSceneEditor() }
        compose.waitUntil(10000) {
            store.sceneEditor && compose.onAllNodesWithTag("scene.hierarchy").fetchSemanticsNodes().isNotEmpty()
        }
        compose.onNodeWithTag("scene.transport.play").assertIsDisplayed()
        compose.onNodeWithTag("scene.hierarchy.panel").assertDoesNotExist()
        compose.onNodeWithTag("scene.hierarchy").performClick()
        compose.onNodeWithTag("scene.hierarchy.panel").assertIsDisplayed()
        capture("scene-hierarchy-open")
        compose.onNodeWithTag("scene.hierarchy.row.$first").performScrollTo().assertIsDisplayed().performClick()
        compose.waitUntil(5000) { store.primary == first && store.detail?.id == first }
        val visible = store.layers.first { it.id == first }.visible
        compose.onNodeWithTag("scene.hierarchy.visible.$first").performScrollTo().assertIsDisplayed().performClick()
        compose.waitUntil(5000) { store.layers.first { it.id == first }.visible != visible }
        compose.onNodeWithTag("scene.hierarchy.visible.$first").performScrollTo().assertIsDisplayed().performClick()
        compose.waitUntil(5000) { store.layers.first { it.id == first }.visible == visible }
        compose.onNodeWithTag("scene.hierarchy").performClick()
        compose.onNodeWithTag("scene.hierarchy.panel").assertDoesNotExist()
        compose.onNodeWithTag("scene.tool.move").performClick()
        compose.onNodeWithTag("scene.transform.panel").assertIsDisplayed()
        compose.runOnIdle { assertEquals(first, checkNotNull(store.primary)) }
        compose.onNodeWithTag("scene.dock.back").performClick()
        compose.onNodeWithTag("scene.transform.panel").assertDoesNotExist()
        compose.onNodeWithTag("scene.timeline.expand").performClick()
        compose.onNodeWithTag("scene.timeline").assertIsDisplayed()
        compose.runOnIdle {
            assertTrue(store.sceneEditor)
            assertEquals(2, store.layers.size)
            assertEquals(0, store.engineForStress.precompDepth())
        }
        var cube = 0L
        compose.runOnIdle { cube = store.addShape3D(0); assertTrue(cube > 0) }
        compose.waitUntil(10000) { store.primary == cube && store.detail?.id == cube && store.shape3d != null }
        compose.runOnIdle {
            store.select(cube, openOptions = false)
            sceneFrameAll(store)
        }
        compose.onNodeWithTag("scene.timeline.expand").performClick()
        compose.onNodeWithTag("scene.tool.move").assertIsDisplayed()
        capture("scene-mobile")
    }
}
