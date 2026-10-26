package com.aurea.aurea.editor

import android.app.Application
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/**
 * Omega conteúdo (área 06, P0-1): no MODO DE TEXTO o teclado real não cobre a
 * faixa de entrada — se o teclado for mais alto que o painel grande, a faixa
 * sobe junto. Texto longo rola dentro da faixa de 60 dp.
 */
class TextContentSheetDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    private fun orientation() {
        check(context.packageName.endsWith(".uitest"))
        if (InstrumentationRegistry.getArguments().getString("beta3.orientation") == "landscape") {
            InstrumentationRegistry.getInstrumentation().runOnMainSync {
                ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).single()
                    .requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            compose.waitUntil(10000) { context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE }
        }
    }

    private fun waitForRealKeyboard() {
        compose.waitUntil(10000) {
            val descriptor = InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand("dumpsys input_method")
            android.os.ParcelFileDescriptor.AutoCloseInputStream(descriptor).bufferedReader().use { reader ->
                reader.readText().contains("mInputShown=true")
            }
        }
    }

    /** O topo do teclado na janela (px); sem teclado, a altura da janela. */
    private fun imeTop(): Float {
        var top = 0f
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            val view = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).single().window.decorView
            val ime = ViewCompat.getRootWindowInsets(view)?.getInsets(WindowInsetsCompat.Type.ime())?.bottom ?: 0
            top = (view.height - ime).toFloat()
        }
        return top
    }

    @Test fun keyboardNeverCoversTheInputStripAndLongTextStaysInside() {
        orientation()
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "Text strip test") }
        compose.waitUntil(15000) { store.project.title == "Text strip test" }
        compose.runOnIdle { assertTrue(store.addText() >= 0) }
        compose.onNodeWithTag("text.edit.input").assertIsDisplayed()
        waitForRealKeyboard()
        compose.waitForIdle()
        val strip = compose.onNodeWithTag("text.edit.input").fetchSemanticsNode().boundsInWindow
        assertTrue("The keyboard covers the input strip: $strip, ime top ${imeTop()}", strip.bottom <= imeTop() + 1f)
        val longText = (1..150).joinToString("\n") { "Line $it continues here" }
        compose.onNodeWithTag("text.edit.input").performTextReplacement(longText)
        compose.waitUntil(5000) { store.textDetail?.content == longText }
        val density = context.resources.displayMetrics.density
        val input = compose.onNodeWithTag("text.edit.input").fetchSemanticsNode().boundsInWindow
        assertTrue("Long text must scroll inside the 60 dp strip: $input", input.height <= 60f * density + 1f)
        val config = context.resources.configuration
        stabilityScreenshot("omega-text-mode-${config.screenWidthDp}x${config.screenHeightDp}dp-long.png")
        compose.onNodeWithTag("text.style.done").performClick()
        compose.waitUntil(5000) { store.textEditLayer == null }
        compose.onNodeWithTag("text.edit.mode").assertDoesNotExist()
    }
}
