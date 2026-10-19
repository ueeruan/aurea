package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 0.2.6: exportar não termina em "erro N" quando outro jeito funciona.
 * Resolução acima do que o aparelho exporta sai no MAIOR tamanho que cabe
 * (antes: "resolução acima do que este aparelho exporta", erro 6), e a escada
 * de tentativas do motor responde igual pela ponte do Android.
 */
class ExportLadderDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    private fun waitResult(): ExportProgress {
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val deadline = SystemClock.elapsedRealtime() + 300_000L
        do {
            assertTrue(store.engineForStress.exportProgress(buffer)); progress.readFrom(buffer)
            if (progress.finished) return progress
            SystemClock.sleep(50)
        } while (SystemClock.elapsedRealtime() < deadline)
        store.engineForStress.cancelExport()
        fail("Export exceeded finite test deadline")
        return progress
    }

    @Test fun aboveTheDeviceLimitExportsAtTheLargestSizeThatFits() {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "Escada de export") }
        compose.waitUntil(15000) { store.project.title == "Escada de export" && !store.projectOperationBusy }
        compose.runOnIdle {
            assertTrue(store.engineForStress.addShape(1) > 0)
            assertTrue(store.engineForStress.addText("AUREA 0.2.6") > 0)
            store.setCompositionDuration(15)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 15 }

        // Lado menor 8640 (16K em retrato): acima de qualquer encoder e textura.
        val output = File(context.filesDir, "export-ladder-above-device.mp4")
        output.delete()
        assertEquals(0, store.engineForStress.startExport(output.absolutePath, 8640, 30.0, 0, 8))
        val progress = waitResult()
        assertEquals(progress.message, 0, progress.result)
        assertEquals(15, progress.framesDone)
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(output.absolutePath)
            val w = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)!!.toInt()
            val h = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)!!.toInt()
            File(context.filesDir, "export-ladder-above-device.txt").writeText("${w}x$h")
            // Retrato 9:16, no maior tamanho que o aparelho aceita: nunca o pedido.
            assertTrue("$w x $h", w in 16 until 8640 && h > w)
            assertEquals(16.0 / 9.0, h.toDouble() / w, 0.03)
        } finally {
            retriever.release()
        }

        // A escada pela ponte: memória → menos pixels; sem espaço → nada ajuda.
        assertEquals(3, AureaEngine.nativeExportNextAttempt(0, 8, 0, false, false, 1080, 30.0, false, false))
        assertEquals(0, AureaEngine.nativeExportNextAttempt(0, 28, 0, false, false, 1080, 30.0, false, false))
        assertEquals(AureaEngine.EXPORT_ATTEMPT_WAIT, AureaEngine.nativeExportNextAttempt(0, 5, 0, false, false, 1080, 30.0, false, false))
        assertEquals(720, AureaEngine.nativeExportAttemptShortSide(3, 1080))
    }
}
