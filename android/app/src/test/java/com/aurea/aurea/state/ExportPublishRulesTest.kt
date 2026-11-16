package com.aurea.aurea.state

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

/**
 * 0.5.3 (P0-2): o vídeo renderizado nunca é apagado sem uma cópia confirmada;
 * (P2) a cópia para a galeria pelo kernel, com a volta ao jeito de sempre.
 */
class ExportPublishRulesTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun rendered(bytes: Int = 300_000): File =
        File(tmp.newFolder("cache", "export"), "Aurea 20261112-101500-123.mp4").apply {
            writeBytes(ByteArray(bytes) { (it * 31 + 7).toByte() })
        }

    @Test fun aCopyThatCannotFitIsNotStarted() {
        val mb = 1L shl 20
        assertTrue(GalleryPublish.copyFits(100 * mb, 200 * mb))
        assertTrue(GalleryPublish.copyFits(100 * mb, 100 * mb + GalleryPublish.COPY_MARGIN_BYTES))
        assertFalse(GalleryPublish.copyFits(100 * mb, 100 * mb + GalleryPublish.COPY_MARGIN_BYTES - 1))
        assertFalse(GalleryPublish.copyFits(100 * mb, 0L))
        // Sem medida (StatFs falhou) ou arquivo vazio: tenta como antes.
        assertTrue(GalleryPublish.copyFits(100 * mb, -1L))
        assertTrue(GalleryPublish.copyFits(0L, 0L))
    }

    @Test fun tempIsOnlyDeletedWhenACopyExists() {
        assertEquals(GalleryPublish.TempFate.Delete, GalleryPublish.tempFate(galleryOk = true, fallbackOk = false))
        assertEquals(GalleryPublish.TempFate.Delete, GalleryPublish.tempFate(galleryOk = false, fallbackOk = true))
        assertEquals(GalleryPublish.TempFate.Delete, GalleryPublish.tempFate(galleryOk = true, fallbackOk = true))
        // As duas falharam: o temporário é o único vídeo que existe.
        assertEquals(GalleryPublish.TempFate.Rescue, GalleryPublish.tempFate(galleryOk = false, fallbackOk = false))
    }

    @Test fun bothCopiesFailedTheFileIsKeptAndMovedOutOfTheCache() {
        val temp = rendered()
        val expected = temp.readBytes()
        val safe = File(tmp.root, "files/exports")   // ainda não existe: o resgate cria
        val moved = GalleryPublish.rescue(temp, safe)
        assertEquals(File(safe, temp.name), moved)
        assertFalse("o temporário saiu da pasta que a abertura do app apaga", temp.exists())
        assertArrayEquals(expected, moved!!.readBytes())
    }

    @Test fun rescueNeverInventsAFileAndNeverDeletesWhatItCouldNotMove() {
        val safe = tmp.newFolder("files", "exports")
        assertNull(GalleryPublish.rescue(File(tmp.root, "sumiu.mp4"), safe))
        val empty = File(tmp.root, "vazio.mp4").apply { writeBytes(ByteArray(0)) }
        assertNull(GalleryPublish.rescue(empty, safe))
        // Destino impossível (um arquivo no lugar da pasta): o temporário fica intacto.
        val temp = rendered(4096)
        val blocked = File(tmp.root, "bloqueado").apply { writeText("x") }
        assertNull(GalleryPublish.rescue(temp, blocked))
        assertTrue(temp.isFile)
        assertEquals(4096L, temp.length())
    }

    @Test fun oldAndroidUsesTheBufferedCopy() {
        val temp = rendered()
        val out = ByteArrayOutputStream()
        val copied = GalleryPublish.copyFile(temp, out, sdk = 28) { _, _ -> fail("sem cópia pelo kernel antes do Android 10"); 0L }
        assertEquals(temp.length(), copied)
        assertArrayEquals(temp.readBytes(), out.toByteArray())
    }

    @Test fun android10UsesTheKernelCopy() {
        val temp = rendered()
        val out = ByteArrayOutputStream()
        var calls = 0
        val copied = GalleryPublish.copyFile(temp, out, sdk = 29) { input, output -> calls++; input.copyTo(output) }
        assertEquals(1, calls)
        assertEquals(temp.length(), copied)
        assertArrayEquals(temp.readBytes(), out.toByteArray())
    }

    @Test fun kernelCopyThatFailsHalfwayRestartsFromTheBeginningOfTheFile() {
        val temp = rendered()
        val dst = File(tmp.root, "galeria.mp4")
        FileOutputStream(dst).use { out ->
            val copied = GalleryPublish.copyFile(temp, out, sdk = 35) { input, output ->
                val piece = ByteArray(1000)
                output.write(piece, 0, input.read(piece))   // escreveu um pedaço…
                throw IOException("EIO")                     // …e falhou
            }
            assertEquals(temp.length(), copied)
        }
        // Sem o pedaço duplicado no começo: o arquivo é idêntico ao original.
        assertArrayEquals(temp.readBytes(), dst.readBytes())
    }

    @Test fun shortKernelCopyIsNotTrusted() {
        val temp = rendered()
        val dst = File(tmp.root, "curto.mp4")
        FileOutputStream(dst).use { out ->
            GalleryPublish.copyFile(temp, out, sdk = 34) { input, output ->
                val piece = ByteArray(10)
                output.write(piece, 0, input.read(piece))
                10L
            }
        }
        assertArrayEquals(temp.readBytes(), dst.readBytes())
    }

    @Test fun destinationThatCannotRewindReportsTheOriginalError() {
        val temp = rendered()
        val boom = IOException("ENOSPC")
        try {
            GalleryPublish.copyFile(temp, ByteArrayOutputStream(), sdk = 33) { _, _ -> throw boom }
            fail("a falha tem de chegar a quem desfaz o item pendente")
        } catch (e: IOException) {
            assertSame(boom, e)
        }
    }
}
