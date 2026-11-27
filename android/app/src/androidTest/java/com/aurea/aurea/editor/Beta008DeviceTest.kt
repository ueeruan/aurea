package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs

/** Run on Vulkan AND GLES: assertions measure rendered pixels, not just imported handles. */
class Beta008DeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun launch(title: String) {
        check(context.packageName == "com.aurea.aurea.uitest")
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        project(title)
    }
    private fun project(title: String) {
        compose.runOnIdle { store.newProject(640, 360, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
    }
    private fun image(name: String): File {
        val file = File(context.filesDir, name)
        val bitmap = Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888)
        try {
            for (y in 0 until 128) for (x in 0 until 128) bitmap.setPixel(x, y,
                if ((x / 16 + y / 16) % 2 == 0) Color.RED else Color.GREEN)
            file.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
        return file
    }
    private fun capture(): ByteArray {
        compose.waitForIdle()
        val buffer = ByteBuffer.allocateDirect(640 * 360 * 4)
        val size = IntArray(2)
        assertEquals("Native GPU capture must complete", 640 * 360 * 4, store.engineForStress.captureFrame(640, buffer, size))
        assertArrayEquals(intArrayOf(640, 360), size)
        return ByteArray(buffer.capacity()).also { buffer.rewind(); buffer.get(it) }
    }
    private fun visible(pixels: ByteArray): Int = (pixels.indices step 4).count { i ->
        maxOf(pixels[i].toInt() and 255, pixels[i + 1].toInt() and 255, pixels[i + 2].toInt() and 255) > 32
    }
    private fun note(text: String) { File(context.filesDir, "beta008-result.txt").appendText(text + "\n") }

    @Test fun sameLengthTypingAndDuplicateKeepLiveTextLabels() {
        launch("Live text labels")
        val id = compose.runOnIdle { store.addText(openEditor = false) }
        assertTrue(id > 0)
        compose.waitUntil(5000) { store.primary == id }
        compose.runOnIdle { store.setTextContent("SOL") }
        compose.waitUntil(5000) { store.layers.single { it.id == id }.name == "SOL" }
        compose.runOnIdle { store.setTextContent("MAR") }
        compose.waitUntil(5000) { store.layers.single { it.id == id }.name == "MAR" }
        compose.runOnIdle { store.duplicateLayers(listOf(id)) }
        compose.waitUntil(5000) { store.layers.count { it.name == "MAR" } == 2 }
    }

    @Test fun nearbyPointLightDoesNotWashOutTheCloud() {
        launch("Cloud volume lighting")
        val cloud = compose.runOnIdle { store.addShape3D(19) }
        assertTrue(cloud > 0)
        compose.waitUntil(10000) { store.layers.any { it.id == cloud } }
        val engine = store.engineForStress
        val light = compose.runOnIdle { engine.addLight(1) }
        assertTrue(light > 0)
        assertTrue(engine.setLightParam(light, 1, 0f))
        val dark = capture()
        assertTrue(engine.setLightParam(light, 1, 8f))
        val lit = capture()
        var changed = 0
        var clipped = 0
        for (i in lit.indices step 4) {
            val red = lit[i].toInt() and 255
            if (abs(red - (dark[i].toInt() and 255)) > 12) changed++
            if (red >= 250 && (lit[i + 1].toInt() and 255) >= 250 && (lit[i + 2].toInt() and 255) >= 250) clipped++
        }
        assertTrue("The cloud must respond to the point light ($changed pixels)", changed > 70)
        assertTrue("Dense cloud detail must remain below white clipping ($clipped/$changed)", clipped < changed / 3)
    }

    @Test fun tintAndEffectChainsRenderOnImageTextAndExtrudedTextWithoutCrash() {
        launch("008 effects image")
        // Repeat project transitions while the app also saves its thumbnail.
        // A single successful readback did not catch stale GLES FBO attachments.
        for (run in 0..8) {
            val kind = run % 3
            if (run != 0) project("008 effects $run")
            compose.runOnIdle {
                when (kind) {
                    0 -> store.importImage(Uri.fromFile(image("008-image.png")))
                    1 -> { store.addText(); store.dismissTextContentEditor(); store.setTextContent("AUREA"); store.setTextSize(72f) }
                    else -> store.addText3D(openEditor = false)
                }
            }
            compose.waitUntil(15000) { store.primary != null && store.detail != null && store.busyMessage == null }
            val original = visible(capture()); assertTrue("Producer $kind must be visible", original > 100)
            for (key in listOf("aurea.color.tint", "aurea.color.exposure", "aurea.color.saturation")) {
                val count = store.effects.size
                compose.runOnIdle { store.addEffect(effectTypeId(key)) }
                compose.waitUntil(10000) { store.effects.size == count + 1 }
                assertTrue("$key made producer $kind disappear", visible(capture()) > 100)
            }
            repeat(8) { assertTrue(visible(capture()) > 100) }
            note("PASS effect chain + 8 rendered frames producer=$kind transition=$run")
        }
    }

    @Test fun importedModelAtCompositionEndAndDirectionalLightRenderActualPixels() {
        launch("008 model visibility")
        val glb = File(context.filesDir, "008-model.glb")
        instrumentation.context.assets.open("model-triangle.glb").use { input -> glb.outputStream().use { input.copyTo(it) } }
        val end = store.project.durationFrames
        // Official timeline navigation stops at the last layer. Supply content
        // up to this boundary before testing insertion at the composition end.
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(10000) { store.layers.any { it.kind == 6 && it.endFrame == end } }
        compose.runOnIdle { store.seek(end); store.importModel(Uri.fromFile(glb)) }
        compose.waitUntil(30000) { store.layers.any { it.kind == 10 } && store.busyMessage == null }
        val model = store.layers.single { it.kind == 10 }
        assertEquals("Model must begin at the insertion playhead", end, model.startFrame)
        assertTrue(model.endFrame > end)
        assertTrue("Imported GLB must draw visible pixels at composition end", visible(capture()) > 100)
        note("PASS GLB insertion at composition end + actual rendered silhouette")
        project("008 selected gltf scene")
        val selectedScene = File(context.filesDir, "008-selected.gltf")
        instrumentation.context.assets.open("beta008-selected-scene.gltf").use { input -> selectedScene.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importModel(Uri.fromFile(selectedScene)) }
        compose.waitUntil(30000) { store.layers.any { it.kind == 10 } && store.busyMessage == null }
        val silhouette = visible(capture())
        assertTrue("Only the selected scene must be fitted and drawn: $silhouette pixels", silhouette in 100..(640 * 360 / 2))
        note("PASS selected scene excludes unused million-scale node: $silhouette visible pixels")
        project("008 directional")
        compose.runOnIdle { store.addShape3D(0) }
        compose.waitUntil(10000) { store.detail?.kind == 10 }
        var light = 0L
        compose.runOnIdle { light = store.engineForStress.addLight(0); store.select(light) }
        compose.waitUntil(10000) { store.detail?.id == light }
        assertTrue(store.engineForStress.setLightParam(light, 1, 0f))
        val dark = capture()
        assertTrue(store.engineForStress.setLightParam(light, 1, 5f))
        val lit = capture()
        val changed = (lit.indices step 4).count { abs((lit[it].toInt() and 255) - (dark[it].toInt() and 255)) > 12 }
        assertTrue("Directional intensity must visibly change the object: $changed pixels", changed > 100)
        note("PASS directional 0 -> 5 changes $changed rendered pixels")
    }

    @Test fun customTextTextureAndBevelSurviveReopenAndCanBeRemovedInPanel() {
        launch("008 texture")
        compose.runOnIdle { store.addText3D(openEditor = false) }
        compose.waitUntil(15000) { store.text3d != null }
        val id = checkNotNull(store.primary)
        compose.runOnIdle { store.setText3D(store.text3d!!.copy(content = "AVATAR a\u0301o\u0308", bevel = true)); store.setShapePartImage(-1, Uri.fromFile(image("008-texture.png")), textTexture = true) }
        compose.waitUntil(15000) { store.busyMessage == null && store.hasText3DTexture() }
        val textured = capture()
        assertTrue("Texture must produce chromatic text pixels", (textured.indices step 4).count {
            abs((textured[it].toInt() and 255) - (textured[it + 1].toInt() and 255)) > 40
        } > 100)
        val project = File(context.filesDir, "008-texture.aurea")
        assertEquals(0, store.engineForStress.saveProject(project.absolutePath))
        assertEquals(0, store.engineForStress.loadProject(project.absolutePath))
        assertTrue(store.engineForStress.queryText3dTexture(id).isNotEmpty())
        assertTrue(store.engineForStress.modelMissingTextures(id).isEmpty())
        assertTrue(visible(capture()) > 100)
        compose.runOnIdle { store.select(id) }
        compose.onNodeWithTag("layer.panel.colorFill").performClick()
        compose.onNodeWithTag("text3d.texture.clear").performScrollTo().performClick()
        compose.waitUntil(10000) { !store.hasText3DTexture() }
        assertTrue(store.text3d!!.bevel)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.hasText3DTexture() }
        note("PASS custom textured/bevel text rendering, save/reopen, native remove, undo")
    }

    @Test fun tappingOrHoldingRotationNumberDoesNotRotateTheLayer() {
        launch("008 rotation field")
        compose.runOnIdle { store.addShape(0); store.setTransform(8, 45f) }
        compose.waitUntil(10000) { store.detail?.rotation?.get(2) == 45f }
        compose.onNodeWithTag("layer.panel.moveTransform").performClick()
        compose.onNodeWithContentDescription(context.getString(R.string.panel_rotacao)).performClick()
        for (tag in listOf("transform.rotation.axis.0")) {
            compose.onNodeWithTag(tag).performClick()
            compose.waitForIdle()
            assertEquals("Numeric tap must not change rotation", 45f, store.detail!!.rotation[2], .0001f)
            compose.onNodeWithText(context.getString(R.string.ds_cancelar)).performClick()
            compose.waitForIdle()
            compose.onNodeWithTag(tag).performTouchInput { longClick(durationMillis = 900) }
            compose.waitForIdle()
            assertEquals("Holding a numeric field must not rotate", 45f, store.detail!!.rotation[2], .0001f)
            // Long tap may open the keypad; dismiss it before scrubbing the ruler.
            if (compose.onAllNodesWithText(context.getString(R.string.ds_cancelar)).fetchSemanticsNodes().isNotEmpty())
                compose.onNodeWithText(context.getString(R.string.ds_cancelar)).performClick()
        }
        compose.onNodeWithTag("transform.rotation.ruler").performTouchInput {
            down(center)
            moveBy(Offset(-48f, 0f), 16)
            moveBy(Offset(-12f, 0f), 16)
            up()
        }
        compose.waitUntil(5000) { store.detail!!.rotation[2] > 55f }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { abs(store.detail!!.rotation[2] - 45f) < .001f }
        note("PASS native rotation number tap and hold preserve 45 degrees")
    }

    @Test fun directionalLightControlsRefreshAfterChangesAndUndo() {
        launch("008 directional controls")
        compose.runOnIdle { store.addShape3D(0); store.addLight(0); store.enterSceneEditor() }
        compose.waitUntil(10000) { store.detail?.kind == 9 && store.sceneEditor }
        compose.onNodeWithTag("scene.light").performClick()
        compose.onNodeWithTag("property.light.castShadows").performScrollTo().assertIsOn().performClick()
        compose.onNodeWithTag("property.light.castShadows").assertIsOff()
        assertEquals(0f, store.lightInfo()!![8], .0001f)
        compose.runOnIdle { store.undo() }
        compose.onNodeWithTag("property.light.castShadows").assertIsOn()
        assertEquals(1f, store.lightInfo()!![8], .0001f)
        compose.runOnIdle { store.setLightParam(1, 7f) }
        compose.onNodeWithTag("property.light.intensity").performScrollTo()
            .assert(hasAnyDescendant(hasText(String.format(java.util.Locale.getDefault(), "%.2f", 7f))))
        compose.runOnIdle { store.undo() }
        compose.onNodeWithTag("property.light.intensity")
            .assert(hasAnyDescendant(hasText(String.format(java.util.Locale.getDefault(), "%.2f", 3f))))
        note("PASS directional native shadow/intensity controls refresh and undo")
    }

    @Test fun draggingModelPivotMovesTheMarkerAndPreservesTheModelBounds() {
        launch("3D pivot drag")
        val id = compose.runOnIdle { store.addShape3D(0) }
        assertTrue(id > 0)
        compose.waitUntil(10000) { store.detail?.kind == 10 && store.detail?.hasWorldCorners == true }
        compose.runOnIdle {
            store.setTransform(6, 24f)
            store.setTransform(7, 37f)
            store.setTransform(2, -80f)
        }
        compose.waitUntil(5000) { store.detail?.rotation?.get(1) == 37f && store.detail?.position?.get(2) == -80f }
        val corners = store.detail!!.worldCorners.copyOf()
        val before = compose.runOnIdle { requireNotNull(store.sceneGizmoOf(id)) }
        compose.onNodeWithTag("layer.panel.moveTransform").performClick()
        compose.onNodeWithTag("panel.tab.0").performClick()
        compose.onNodeWithTag("transform.pivot.pad").performTouchInput {
            down(center)
            moveBy(Offset(45f, -35f), 16)
            moveBy(Offset(32f, -29f), 16)
            up()
        }
        compose.waitUntil(5000) { store.gizmo?.let { it[0] > before[0] + 10f && it[1] < before[1] - 10f } == true }
        assertArrayEquals("Changing the pivot must preserve the projected model", corners, store.detail!!.worldCorners, 0.15f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.gizmo?.let { abs(it[0] - before[0]) < 0.1f && abs(it[1] - before[1]) < 0.1f } == true }
    }
}
