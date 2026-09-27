package com.aurea.aurea.diagnostics

import android.os.Build
import androidx.test.platform.app.InstrumentationRegistry
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipInputStream
import org.junit.Assert.*
import org.junit.Test

class ExitDiagnosticsTest {
    @Test fun exportsSystemExitReportWithoutOpeningEngineOrImportingAnotherVideo() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue("Tests must use the disposable package", context.packageName.endsWith(".uitest"))
        ExitDiagnostics.mark(context, ExitDiagnostics.Phase.VIDEO_NATIVE)
        val output = ByteArrayOutputStream()
        ExitDiagnostics.writeArchive(context, output)
        val entries = mutableMapOf<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(output.toByteArray())).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                entries[entry.name] = zip.readBytes()
                zip.closeEntry()
            }
        }
        val report = entries.getValue("diagnostico.txt").toString(Charsets.UTF_8)
        assertTrue(report.contains(Build.MODEL))
        assertTrue(report.contains("SDK ${Build.VERSION.SDK_INT}"))
        assertTrue(report.contains("Decodificadores disponíveis"))
        assertFalse(report.contains("Histórico indisponível"))
        assertTrue(entries.containsKey("leia-me.txt"))
        assertTrue(entries.values.all { it.size <= 2 * 1024 * 1024 })
    }
}
