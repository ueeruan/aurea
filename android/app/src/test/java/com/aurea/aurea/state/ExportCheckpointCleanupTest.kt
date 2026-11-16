package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * 0.5.3 (P0-1): a limpeza da abertura do app apagava `cacheDir/export` inteiro,
 * e com ela o checkpoint do export retomável — a retomada depois de o sistema
 * matar o app não tinha o que retomar. Mais a regra do degrau de memória.
 */
class ExportCheckpointCleanupTest {
    @get:Rule val tmp = TemporaryFolder()

    // Os nomes do motor (ExportCheckpoint.cpp): "aurea-ckpt-" + 16 hex + ".aurea-pkt" [+ ".ckpt" | ".pcm"].
    private val log = "aurea-ckpt-00c0ffee12345678.aurea-pkt"

    private fun file(dir: File, name: String, bytes: Int = 8): File =
        File(dir, name).apply { parentFile?.mkdirs(); writeBytes(ByteArray(bytes)) }

    @Test fun checkpointLogAndManifestSurviveTheLaunchCleanup() {
        val dir = tmp.newFolder("export")
        val keptLog = file(dir, log, 4096)
        val keptManifest = file(dir, "$log.ckpt")
        val spill = file(dir, "$log.pcm", 2048)
        val partial = file(dir, "Meu video 20261112-101500-123.mp4", 1024)
        val muxTemp = file(dir, "Meu video 20261112-101500-123.mp4.aurea-ffmpeg-1.tmp")
        val nested = file(File(dir, "sub"), log)   // só a raiz da pasta é do motor

        val leftovers = CacheStorage.exportLeftovers(dir).toSet()
        assertEquals(setOf(spill, partial, muxTemp, nested), leftovers)
        // O que a limpeza faz com a lista: apaga. O checkpoint continua no disco.
        leftovers.forEach { assertTrue(it.delete()) }
        assertTrue(keptLog.isFile)
        assertEquals(4096L, keptLog.length())
        assertTrue(keptManifest.isFile)
        assertFalse(spill.exists())
        assertFalse(partial.exists())
    }

    @Test fun onlyTheEngineNamesCountAsCheckpoint() {
        assertTrue(CacheStorage.isExportCheckpoint(log))
        assertTrue(CacheStorage.isExportCheckpoint("$log.ckpt"))
        assertFalse(CacheStorage.isExportCheckpoint("$log.pcm"))            // nunca é retomado
        assertFalse(CacheStorage.isExportCheckpoint("video.aurea-pkt"))     // sem o prefixo
        assertFalse(CacheStorage.isExportCheckpoint("aurea-ckpt-1.mp4"))
        assertFalse(CacheStorage.isExportCheckpoint("aurea-ckpt-1.aurea-pkt.tmp"))
        assertFalse(CacheStorage.isExportCheckpoint(""))
    }

    @Test fun missingFolderOrPlainFileNeverThrows() {
        assertTrue(CacheStorage.exportLeftovers(File(tmp.root, "nao-existe")).isEmpty())
        val stray = file(tmp.root, "export")   // um arquivo no lugar da pasta sai inteiro
        assertEquals(listOf(stray), CacheStorage.exportLeftovers(stray))
    }

    @Test fun memoryStepClimbsOnePerKillUpToThreeAndStaysOtherwise() {
        assertEquals(0, ExportKillMemory.stepAfterStart(0, died = false))
        assertEquals(1, ExportKillMemory.stepAfterStart(0, died = true))
        assertEquals(2, ExportKillMemory.stepAfterStart(1, died = true))
        assertEquals(3, ExportKillMemory.stepAfterStart(2, died = true))
        assertEquals(3, ExportKillMemory.stepAfterStart(3, died = true))    // teto
        assertEquals(2, ExportKillMemory.stepAfterStart(2, died = false))   // só o sucesso zera (ExportKillMemory.done)
        // Valor estragado no disco nunca passa da faixa.
        assertEquals(3, ExportKillMemory.stepAfterStart(99, died = false))
        assertEquals(1, ExportKillMemory.stepAfterStart(-5, died = true))
        assertEquals(3, ExportKillMemory.MAX_STEP)
    }
}
