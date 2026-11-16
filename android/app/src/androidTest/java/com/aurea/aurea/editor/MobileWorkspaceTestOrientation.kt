package com.aurea.aurea.editor

import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import androidx.compose.ui.test.junit4.ComposeContentTestRule
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import org.junit.Assert.assertTrue

internal fun configureMobileWorkspaceOrientation(compose: ComposeContentTestRule) {
    val requested = InstrumentationRegistry.getArguments().getString("edit.orientation") ?: return
    val expected = when (requested) {
        "landscape" -> Configuration.ORIENTATION_LANDSCAPE
        "portrait" -> Configuration.ORIENTATION_PORTRAIT
        else -> error("Unsupported edit.orientation $requested")
    }
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    instrumentation.runOnMainSync {
        ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).single()
            .requestedOrientation = if (expected == Configuration.ORIENTATION_LANDSCAPE)
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }
    compose.waitUntil(10000) { instrumentation.targetContext.resources.configuration.orientation == expected }
}

internal fun assertMobileWorkspaceScreenshotOrientation(bitmap: Bitmap, name: String) {
    val requested = InstrumentationRegistry.getArguments().getString("edit.orientation") ?: return
    val matches = when (requested) {
        "landscape" -> bitmap.width > bitmap.height
        "portrait" -> bitmap.height > bitmap.width
        else -> error("Unsupported edit.orientation $requested")
    }
    assertTrue("$name is ${bitmap.width}x${bitmap.height}, expected $requested", matches)
}
