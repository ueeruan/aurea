package com.aurea.aurea.editor

import android.content.pm.ActivityInfo
import android.content.res.Configuration
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Real dialog/IME bounds: long text cannot push the fixed actions outside the viewport. */
class TextContentSheetDeviceTest {
    @get:Rule val compose = createComposeRule()
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
        compose.onNodeWithTag("text.content.input").performClick()
        compose.waitUntil(10000) {
            val descriptor = InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand("dumpsys input_method")
            android.os.ParcelFileDescriptor.AutoCloseInputStream(descriptor).bufferedReader().use { reader ->
                reader.readText().contains("mInputShown=true")
            }
        }
    }

    @Test fun shortEditorStaysCompactAndLongTextKeepsActionsVisible() {
        orientation()
        var saved: String? = null
        val open = mutableStateOf(true)
        compose.setContent {
            AureaTheme {
                Box(Modifier.fillMaxSize().background(Color.Black))
                if (open.value) TextContentDialog(EditorStore.TextContentRequest(1L, "Title", false, true),
                    onSave = { saved = it; open.value = false; true }, onDismiss = { open.value = false })
            }
        }
        compose.onNodeWithTag("text.content.input").assertIsDisplayed()
        waitForRealKeyboard()
        compose.onNodeWithTag("text.content.cancel").assertIsDisplayed()
        compose.onNodeWithTag("text.content.done").assertIsDisplayed()
        val density = context.resources.displayMetrics.density
        val short = compose.onNodeWithTag("text.content.sheet").fetchSemanticsNode().boundsInWindow
        assertTrue("A short text editor must wrap its content instead of taking the full screen: $short",
            short.height <= 280f * density)
        val longText = (1..150).joinToString("\n") { "Line $it continues here" }
        compose.onNodeWithTag("text.content.input").performTextReplacement(longText)
        waitForRealKeyboard()
        compose.onNodeWithTag("text.content.done").assertIsDisplayed()
        compose.onNodeWithTag("text.content.cancel").assertIsDisplayed()
        val input = compose.onNodeWithTag("text.content.input").fetchSemanticsNode().boundsInWindow
        assertTrue("Long text must scroll inside its field: $input", input.height <= 180f * density + 1f)
        val config = context.resources.configuration
        stabilityScreenshot("beta3-text-sheet-${config.screenWidthDp}x${config.screenHeightDp}dp-long.png")
        compose.onNodeWithTag("text.content.done").performClick()
        compose.waitForIdle()
        assertEquals(longText, saved)
        compose.onNodeWithTag("text.content.sheet").assertDoesNotExist()
    }

    @Test fun empty3DTextExplainsTheErrorWithoutHidingCancel() {
        orientation()
        val open = mutableStateOf(true)
        var called = false
        compose.setContent {
            AureaTheme {
                Box(Modifier.fillMaxSize().background(Color.Black))
                if (open.value) TextContentDialog(EditorStore.TextContentRequest(2L, "", true, true),
                    onSave = { called = true; true }, onDismiss = { open.value = false })
            }
        }
        compose.onNodeWithTag("text.content.input").assertIsDisplayed()
        waitForRealKeyboard()
        compose.onNodeWithTag("text.content.done").assertIsDisplayed().assertIsNotEnabled()
        compose.onNodeWithTag("text.content.cancel").assertIsDisplayed().performClick()
        compose.waitForIdle()
        assertFalse(called)
        compose.onNodeWithTag("text.content.sheet").assertDoesNotExist()
    }
}
