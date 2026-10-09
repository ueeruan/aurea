package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.primaryKeys
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Captures the production editor layout; no replacement panels or preview overlays. */
class EditorUiCaptureTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private fun settle() {
        compose.waitForIdle()
        // UiAutomation captures SurfaceFlinger, which presents after Compose settles.
        Thread.sleep(650)
        instrumentation.waitForIdleSync()
    }
    private fun capture(name: String) {
        settle()
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
    }
    private fun seek(frame: Int) {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(15000) { store.detail?.localPlayhead == frame }
    }
    private fun back() {
        instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
        settle()
    }

    @Test fun compactTimelineKeepsTheAddMenuAccessible() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "AUREA · Timeline") }
        compose.waitUntil(10000) { store.project.title == "AUREA · Timeline" }
        val video = File(context.filesDir, "timeline-reference.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(android.net.Uri.fromFile(video)) }
        compose.waitUntil(20000) { store.layers.any { it.kind == 1 } }
        compose.runOnIdle { store.clearSelection(); store.seek(0) }
        // A codec may need to fall back before its first frame is ready. Verify
        // the actual preview before capturing it, rather than saving a black frame.
        compose.waitUntil(20000) {
            val bounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
            val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            var colored = 0
            for (y in bounds.center.y.toInt()-80 until bounds.center.y.toInt()+80 step 8)
                for (x in bounds.center.x.toInt()-80 until bounds.center.x.toInt()+80 step 8) {
                    val p = bitmap.getPixel(x,y)
                    if (android.graphics.Color.red(p) + android.graphics.Color.green(p) + android.graphics.Color.blue(p) > 100) colored++
                }
            bitmap.recycle()
            colored > 200
        }
        compose.onNodeWithTag("timeline.add").assertDoesNotExist()
        compose.onNodeWithTag("editor.addBar").assertIsDisplayed()
        capture("timeline-reference")
        compose.onNodeWithTag("addBar.Media").performScrollTo().performClick()
        compose.onNodeWithContentDescription(context.getString(R.string.editor_fechar_adicionar)).assertIsDisplayed()
        back()
        compose.onNodeWithTag("editor.addBar").assertIsDisplayed()
        compose.onNodeWithTag("timeline.add").assertDoesNotExist()
    }

    @Test fun captureOscillateAndEditableBounceInTheProductionEditor() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720,900,30f,"AUREA · Oscillate") }
        compose.waitUntil(15000) { store.project.title == "AUREA · Oscillate" }
        compose.runOnIdle { store.select(store.engineForStress.addText("AUREA")) }
        compose.waitUntil(10000) { store.textDetail != null && store.layers.size == 1 }
        compose.runOnIdle { store.setTextSize(100f); store.addEffect(effectTypeId("aurea.motion.oscillate.cycles")) }
        compose.waitUntil(5000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        compose.onNodeWithTag("dock.tool.Effects").performScrollTo().performClick()
        compose.waitUntil(5000) { store.paramOf(effect,2) != null }
        capture("oscillate")
        compose.onNodeWithText(context.getString(R.string.fx_frequencia),useUnmergedTree=true).performScrollTo().performClick()
        seek(0)
        compose.onNodeWithContentDescription(context.getString(R.string.panel_marcar_keyframe_aqui)).performClick()
        seek(30)
        compose.runOnIdle { store.setEffectParam(effect,store.paramOf(effect,2)!!,4f) }
        compose.waitUntil(5000) { store.primaryKeys().count { it.property == 31 && it.effectIndex == effect && it.paramIndex == 8 } == 2 }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_editar_curva_propriedade)).performClick()
        compose.onNodeWithTag("curve.preset.bounce").performClick()
        compose.onNodeWithTag("curve.bounce.count").performClick()
        compose.onNodeWithTag("curve.bounce.strength").performTouchInput {
            down(center); moveTo(androidx.compose.ui.geometry.Offset(width*.65f,center.y)); up()
        }
        compose.onNodeWithTag("curve.power").assertTextEquals(context.getString(R.string.pn_textpreset_bounce))
        capture("bounce")
    }

    @Test fun captureCurrentEditorPanels() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 900, 30f, "AUREA · Motion") }
        compose.waitUntil(15000) { store.project.title == "AUREA · Motion" }
        var mask = -1
        compose.runOnIdle {
            val id = store.engineForStress.addText("AUREA")
            assertTrue(id >= 0); store.select(id); store.renameLayer(id, "Título")
        }
        compose.waitUntil(15000) { store.primary != null && store.textDetail != null && store.layers.size == 1 }
        compose.runOnIdle {
            store.setTextSize(100f)
            store.addTextAnimator(1 shl 9)
            store.setTextAnimParam(0, 6, 0.1f); store.setTextAnimParam(0, 7, 0.9f); store.setTextAnimParam(0, 8, 0.75f)
            store.toggleTextAnimKey(0, 6)
            store.addMaskPreset(0)
            mask = checkNotNull(store.maskEdit)
            store.toggleMaskParamKey(mask, 0); store.toggleMaskParamKey(mask, 2)
            store.setCompositionDuration(90)
        }
        seek(60)
        compose.runOnIdle { store.setTextAnimParam(0, 6, 0.8f); store.setMaskParam(mask, 0, 40f); store.setMaskParam(mask, 2, 0.6f) }
        seek(0)
        capture("editor")
        compose.onNodeWithTag("dock.tool.TextOptions").performScrollTo().performClick()
        settle()
        compose.onNodeWithTag("text.transform.add").performScrollTo().performClick()
        capture("text-stack")
        back()
        // A máscara não tem mais ficha na doca (virou efeito): sem a captura dela aqui.
        repeat(2) { n ->
            compose.runOnIdle { store.addShape(1) }
            compose.waitUntil(5000) { store.layers.size == n + 2 }
        }
        compose.runOnIdle {
            val ids = store.layers.map { it.id }.toLongArray()
            store.setLayerRanges(ids, intArrayOf(0, 35, 60), intArrayOf(90, 60, 90))
            store.selectAll(); store.seek(10)
        }
        compose.onNodeWithTag("timeline.batch.tools").assertIsDisplayed()
        capture("layers")
    }
}
