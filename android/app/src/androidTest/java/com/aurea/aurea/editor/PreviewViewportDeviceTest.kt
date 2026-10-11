package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class PreviewViewportDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun twoKAndFourKAt60FpsStartAndKeepAdvancingOnThePhone() {
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
        val journal = StringBuilder()
        for ((width, height) in listOf(2560 to 1440, 3840 to 2160)) {
            val title = "Preview ${width}x${height} 60fps"
            compose.runOnIdle { store.newProject(width, height, 60f, title) }
            compose.waitUntil(15000) { store.project.title == title }
            compose.runOnIdle { store.addShape(0); store.setCompositionDuration(600); store.setPreviewScale(true); store.seek(0) }
            compose.waitUntil(10000) { store.layers.isNotEmpty() && store.playhead == 0 }
            val began = android.os.SystemClock.elapsedRealtime()
            compose.runOnIdle { store.play() }
            compose.waitUntil(15000) { store.playing && !store.preview.buffering && store.playhead >= 30 }
            val startup = android.os.SystemClock.elapsedRealtime() - began
            val first = store.playhead
            compose.waitUntil(10000) { store.playhead >= first + 60 }
            journal.append("$title startupToFrame30Ms=$startup progressedFrom=$first to=${store.playhead}\n")
            compose.runOnIdle { store.pause() }
            compose.waitUntil(5000) { !store.playing && !store.preview.buffering }
        }
        File(context.getExternalFilesDir(null), "preview-60fps-result.txt").writeText(journal.toString())
    }

    @Test fun automaticPreviewFitsThePhoneWhileManualAndExactCapturesKeepTheirSize() {
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
        compose.runOnIdle { store.newProject(1920,1080,30f,"Preview viewport") }
        compose.waitUntil(10000) { store.project.title == "Preview viewport" }
        compose.runOnIdle { store.addShape(0); store.setPreviewScale(false,1,1) }
        val engine = store.engineForStress
        val buffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        fun status(): EngineStatus {
            assertTrue(engine.readStatus(buffer))
            return EngineStatus().also { it.readFrom(buffer) }
        }
        compose.waitUntil(10000) { status().previewWidth == 1920 }
        val original = checkNotNull(store.captureBitmap(320))
        compose.runOnIdle { store.setPreviewScale(true) }
        compose.waitUntil(10000) { val s=status(); s.previewAuto && s.previewWidth in 1..1919 }
        val auto = status()
        val exact = checkNotNull(store.captureBitmap(320))
        assertTrue("AUTO must not change exact captures", original.sameAs(exact))
        assertEquals(320,exact.width); assertEquals(180,exact.height)
        compose.runOnIdle { store.setPreviewScale(false,1,2) }
        compose.waitUntil(10000) { val s=status(); !s.previewAuto && s.previewWidth == 960 }
        compose.runOnIdle { store.setPreviewScale(false,1,1) }
        compose.waitUntil(10000) { status().previewWidth == 1920 }
        File(context.getExternalFilesDir(null),"preview-viewport-result.txt").writeText(
            "PASS full=1920x1080 auto=${auto.previewWidth}x${auto.previewHeight} manualHalf=960x540 exact=320x180 unchanged=true\n")
        original.recycle(); exact.recycle()
    }
}
