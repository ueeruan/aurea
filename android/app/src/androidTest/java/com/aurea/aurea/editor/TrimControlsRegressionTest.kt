package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

class TrimControlsRegressionTest {
    @get:Rule val compose = createComposeRule()

    @Test fun leftAndRightButtonsTrimAnExtendedVideoAndUndoRestoresIt() = checkTrimControls(false)

    @Test fun shortScreenWithLargeTextKeepsCutControlsReachable() = checkTrimControls(true)

    private fun checkTrimControls(shortScreen: Boolean) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            // A narrow phone must keep both cut buttons and the layer filter reachable.
            val density = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(density.density, if (shortScreen) 1.3f else 1f)) {
                AureaTheme {
                    Box(Modifier.width(320.dp).then(if (shortScreen) Modifier.height(480.dp) else Modifier.fillMaxHeight())) {
                        EditorScreen(store)
                    }
                }
            }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320,180,30f,"Trim controls regression") }
        compose.waitUntil(10000) { store.project.title == "Trim controls regression" }
        val fixture = File(context.filesDir,"trim-controls.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> fixture.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(Uri.fromFile(fixture)) }
        compose.waitUntil(20000) { store.layers.any { it.kind == 1 } }
        val id = store.layers.first { it.kind == 1 }.id
        compose.runOnIdle {
            store.setLayerRanges(longArrayOf(id),intArrayOf(0),intArrayOf(900))
            store.select(id)
            store.seek(45)
        }
        compose.waitUntil(10000) { store.primary == id && store.playhead == 45 && store.layers.first { it.id==id }.endFrame == 900 }
        compose.onNodeWithTag("timeline.showAllLayers").assertIsDisplayed().performClick()
        compose.onNodeWithTag("timeline.showAllLayers").performClick()
        compose.onNodeWithTag("timeline.cut.start").assertIsDisplayed()
        compose.onNodeWithTag("timeline.cut.end").assertIsDisplayed()
        compose.onNodeWithTag("timeline.cut.start").performClick()
        compose.waitUntil(10000) { store.layers.first { it.id==id }.startFrame == 45 }
        compose.runOnIdle { assertEquals(900,store.layers.first { it.id==id }.endFrame);store.undo() }
        compose.waitUntil(10000) { store.layers.first { it.id==id }.startFrame == 0 }
        compose.onNodeWithTag("timeline.cut.end").performClick()
        compose.waitUntil(10000) { store.layers.first { it.id==id }.endFrame == 45 }
        compose.runOnIdle { assertEquals(0,store.layers.first { it.id==id }.startFrame);store.undo() }
        compose.waitUntil(10000) { store.layers.first { it.id==id }.endFrame == 900 }
        val size = if (shortScreen) "short" else "tall"
        // Keep a visual record on the disposable test emulator for review.
        instrumentation.uiAutomation.executeShellCommand("screencap -p /sdcard/Download/aurea-timeline-$size.png").use {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(it).readBytes()
        }
    }
}
