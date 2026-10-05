package com.aurea.aurea.editor

import android.os.Environment
import androidx.core.content.FileProvider
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.*
import org.junit.Test

class LegacyExportSharingTest {
    @Test fun legacyExportsUseReadableContentUrisWithoutExposingProjectFiles() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        val directory = File(context.getExternalFilesDir(Environment.DIRECTORY_MOVIES), "Aurea").apply { mkdirs() }
        val file = File(directory, "sharing-regression.mp4")
        try {
            val payload = byteArrayOf(0, 1, 2, 3)
            file.writeBytes(payload)
            val uri = FileProvider.getUriForFile(context, "${context.packageName}.exports", file)
            assertEquals("content", uri.scheme)
            assertArrayEquals(payload, context.contentResolver.openInputStream(uri)!!.use { it.readBytes() })
            assertTrue(runCatching {
                FileProvider.getUriForFile(context, "${context.packageName}.exports", File(context.filesDir, "projetos/private.aurea"))
            }.exceptionOrNull() is IllegalArgumentException)
        } finally { file.delete() }
    }
}
