package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 0.3.2 (CheckpointManager do motor) pela ponte do Android, com o encoder do
 * aparelho: um export interrompido no meio continua do último GOP íntegro do
 * checkpoint (registro de pacotes), publica o MP4 inteiro e não deixa nada
 * para trás; o cancelamento do usuário apaga o checkpoint. A interrupção aqui
 * é a suspensão do motor (o mesmo caminho de quando o processo morre: o
 * registro fica no disco e a próxima partida igual o encontra).
 */
class ExportCheckpointDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
    private val progress = ExportProgress()

    private fun poll(): ExportProgress {
        assertTrue(store.engineForStress.exportProgress(buffer)); progress.readFrom(buffer)
        return progress
    }

    private fun waitUntil(frames: Int = Int.MAX_VALUE): ExportProgress {
        val deadline = SystemClock.elapsedRealtime() + 300_000L
        do {
            val p = poll()
            if (p.finished || p.framesDone >= frames) return p
            SystemClock.sleep(20)
        } while (SystemClock.elapsedRealtime() < deadline)
        store.engineForStress.cancelExport()
        fail("Export exceeded finite test deadline")
        return progress
    }

    private fun videoFrames(file: File): Int {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(file.absolutePath)
            val track = (0 until extractor.trackCount).first {
                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true
            }
            extractor.selectTrack(track)
            var frames = 0
            while (extractor.sampleTime >= 0) { frames++; extractor.advance() }
            return frames
        } finally { extractor.release() }
    }

    private fun checkpointFiles(): List<String> {
        val dir = File(context.cacheDir, "export")   // EngineConfig::exportTemporaryDirectory (cache/export)
        return dir.listFiles()?.map { it.name }?.filter { it.startsWith("aurea-ckpt-") } ?: emptyList()
    }

    @Test fun interruptedExportContinuesFromTheCheckpointAndCancelRemovesIt() {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Checkpoint de export") }
        compose.waitUntil(15000) { store.project.title == "Checkpoint de export" && !store.projectOperationBusy }
        compose.runOnIdle {
            assertTrue(store.engineForStress.addShape(1) > 0)
            assertTrue(store.engineForStress.addText("AUREA 0.3.2") > 0)
            store.setCompositionDuration(240)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 240 }
        val engine = store.engineForStress
        val output = File(context.cacheDir, "export/checkpoint-device.mp4").apply { parentFile?.mkdirs(); delete() }
        // shortSide 360, 30 qps, H.264, taxa automática, sem IA, quadros 240.
        fun start(): Int = engine.startExport(output.absolutePath, 360, 30.0, 0, 0, 0, false, 1, 1, 0)

        // 1) Interrompido no meio: o checkpoint fica (não é o usuário cancelando).
        assertEquals(0, start())
        val mid = waitUntil(150)
        assertFalse("export ended before the interruption: ${mid.message}", mid.finished)
        engine.suspend()
        val stopped = poll()
        assertTrue(stopped.finished)
        assertTrue("interruption must keep a checkpoint (flags=${stopped.flags})", stopped.resumable)
        assertFalse(output.exists())
        engine.resume()
        val candidate = engine.exportResumeCandidate(360, 30.0, 0, 0, 0, false, 1, 1)
        assertNotNull("the app that reopens must find the checkpoint", candidate)
        assertTrue(candidate!!.resumeFrame in 1 until 240)
        assertEquals(240, candidate.totalFrames)
        assertEquals(0, candidate.safeMode)

        // 2) O mesmo export continua de lá e publica o vídeo inteiro.
        assertEquals(0, start())
        val resumed = poll()
        assertTrue("same request must resume (flags=${resumed.flags})", resumed.resumed)
        assertEquals(candidate.resumeFrame, resumed.framesDone)
        val done = waitUntil()
        assertEquals(done.message, 0, done.result)
        assertEquals(240, done.framesDone)
        assertTrue(output.isFile && output.length() > 0)
        assertEquals(240, videoFrames(output))
        assertTrue("a published export leaves no checkpoint: ${checkpointFiles()}", checkpointFiles().isEmpty())
        val diagnostics = engine.exportDiagnostics()
        assertTrue(diagnostics, diagnostics.contains("\"checkpoint\":\"resumed\""))
        output.delete()

        // 3) Cancelar (o usuário) durante uma continuação: nada publicado, nada sobra.
        assertEquals(0, start())
        waitUntil(150)
        engine.suspend(); engine.resume()
        assertEquals(0, start())
        assertTrue(poll().resumed)
        waitUntil(poll().framesDone + 10)
        engine.cancelExport()
        val cancelled = waitUntil()
        assertTrue(cancelled.finished)
        assertFalse(cancelled.resumable)
        assertFalse(output.exists())
        assertTrue("user cancel removes the checkpoint: ${checkpointFiles()}", checkpointFiles().isEmpty())
        assertNull(engine.exportResumeCandidate(360, 30.0, 0, 0, 0, false, 1, 1))
    }
}
