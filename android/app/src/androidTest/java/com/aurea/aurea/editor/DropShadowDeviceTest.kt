package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/** Native UI toggle plus the real device GPU; source and expected pixels are procedural. */
class DropShadowDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun clipCutoutShadowOnlyToggleAndReopenKeepExactPixels() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        var showPanel by mutableStateOf(false)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    // The production panel is controlled independently of the
                    // editor's remembered dock state, as in EffectsAddSheetTest.
                    if (showPanel) Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().fillMaxHeight(.45f)) {
                        PanelContent(store, EditorPanel.Effects, onClose = { showPanel = false },
                            onOpenPanel = {}, onOpenEffectsBrowser = {})
                    }
                }
            }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val title = "Drop Shadow cutout regression"
        compose.runOnIdle { store.newProject(160, 96, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        val engine = store.engineForStress
        val source = File(context.filesDir, "drop-shadow-solid.png")
        val bitmap = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(Color.RED)
        val input = ByteBuffer.allocateDirect(32 * 32 * 4)
        bitmap.copyPixelsToBuffer(input); input.rewind()
        source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
        var layer = 0L
        compose.runOnIdle {
            store.setCompositionBackground(1f, 1f, 1f, 1f)
            layer = engine.importImage(input, 32, 32, "Shadow source", source.absolutePath)
            assertTrue(layer > 0)
            store.select(layer)
        }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }
        val cropType = effectTypeId("aurea.transform.crop")
        val shadowType = effectTypeId("aurea.stylize.drop_shadow")
        compose.runOnIdle {
            store.setTransform2(TrackProperty.POSITION_X, 48f, TrackProperty.POSITION_Y, 48f)
            store.setTransform2(TrackProperty.SCALE_X, 1f, TrackProperty.SCALE_Y, 1f)
            store.addEffect(cropType, listOf(layer))
        }
        compose.waitUntil(10000) { store.effects.any { it.typeId == cropType } }
        val crop = store.effects.single { it.typeId == cropType }.effectId
        compose.runOnIdle { store.addEffect(shadowType, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == shadowType } }
        val shadow = store.effects.single { it.typeId == shadowType }.effectId
        compose.waitUntil(10000) { store.paramOf(shadow, 5) != null && store.paramOf(crop, 0) != null }
        fun set(effect: Int, param: Int, value: Float) {
            compose.runOnIdle { store.setEffectParam(effect, checkNotNull(store.paramOf(effect, param)), value) }
            compose.waitUntil(10000) { store.paramOf(effect, param)?.value?.get(0) == value }
        }
        compose.runOnIdle {
            val spec = store.effectSpecs(shadowType).single { it.index == 5 }
            assertEquals(ParamType.BOOL, spec.type)
            assertFalse(spec.hidden)
            assertEquals(0f, spec.defaultValue[0], .001f)
        }
        set(shadow, 1, 100f); set(shadow, 2, 0f); set(shadow, 3, 32f); set(shadow, 4, 0f)
        fun capture(): ByteBuffer {
            val out = ByteBuffer.allocateDirect(160 * 96 * 4)
            val size = IntArray(2)
            assertEquals(160 * 96 * 4, engine.captureFrame(160, out, size))
            assertArrayEquals(intArrayOf(160, 96), size)
            return out
        }
        fun channel(frame: ByteBuffer, x: Int, y: Int, c: Int) = frame.get((y * 160 + x) * 4 + c).toInt() and 255
        fun white(frame: ByteBuffer, x: Int) = (0..2).all { channel(frame, x, 48, it) > 240 }
        fun black(frame: ByteBuffer, x: Int) = (0..2).all { channel(frame, x, 48, it) < 20 }
        val opaque = capture()
        assertTrue("Opaque clip is visible", channel(opaque, 48, 48, 0) > 240 && channel(opaque, 48, 48, 1) < 20)
        assertTrue("Clip edge must not repeat over its expanded shadow", black(opaque, 80))
        assertTrue(white(opaque, 16))
        set(crop, 0, 50f)
        val cutout = capture()
        assertTrue("Crop alpha removes the source's left half", white(cutout, 40))
        assertTrue("Shadow follows the cutout alpha", white(cutout, 72) && black(cutout, 88))
        fun diagnostic(phase: String) {
            val root = compose.onRoot(useUnmergedTree = true)
            root.printToLog("AureaDropShadow")
            File(context.filesDir, "drop-shadow-$phase-semantics.txt").writeText(root.printToString())
            stabilityScreenshot("drop-shadow-$phase.png")
        }
        diagnostic("before-panel")
        try {
            compose.runOnIdle { showPanel = true }
            // The first (Crop) card starts expanded. The second card is a lazy item:
            // waiting for its semantics cannot create it; scroll the actual stack.
            val stack = compose.onNodeWithTag("aurea.effects.stack")
            // Header tags use the stable catalog/card ID; parameter controls use
            // the layer's effect instance ID (EffectCardItem -> EffectToggleRow).
            val header = "effects.expand.${effectCardId(shadowType)}"
            stack.assertExists().performScrollToNode(hasTestTag(header))
            val toggle = compose.onNodeWithTag("effects.toggle.$shadow.5")
            if (compose.onAllNodesWithTag("effects.toggle.$shadow.5").fetchSemanticsNodes().isEmpty()) {
                compose.onNodeWithTag(header).performClick()
            }
            stack.performScrollToNode(hasTestTag("effects.toggle.$shadow.5"))
            toggle.performScrollTo().assertContentDescriptionEquals(context.getString(R.string.fxo_shadow_only)).assertIsOff()
            diagnostic("before-toggle")
            toggle.performClick()
            compose.waitUntil(10000) { store.paramOf(shadow, 5)?.value?.get(0) == 1f }
            toggle.assertIsOn()
            val isolated = capture()
            assertTrue("Shadow Only removes the visible clip", white(isolated, 56))
            assertTrue("Shadow Only retains its shifted silhouette", black(isolated, 88))
            set(shadow, 1, 0f)
            assertTrue("Zero-opacity isolated shadow must not restore the original clip", white(capture(), 56))
            set(shadow, 1, 100f)
            val project = File(context.filesDir, "drop-shadow-only.aurea")
            assertEquals(0, engine.saveProject(project.absolutePath))
            assertEquals(0, engine.loadProject(project.absolutePath))
            val reopened = capture()
            assertTrue("Shadow Only survives reopening", white(reopened, 56) && black(reopened, 88))
            stabilityScreenshot("drop-shadow-only.png")
        } catch (failure: Throwable) {
            runCatching { diagnostic("failure") }
            throw failure
        }
    }
}
