package com.aurea.aurea.editor

import android.graphics.Bitmap
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File

internal fun stabilityScreenshot(name: String) {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
    try {
        val directory = File(instrumentation.targetContext.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
        File(directory, name).outputStream().use { check(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it)) }
    } finally { screenshot.recycle() }
}
