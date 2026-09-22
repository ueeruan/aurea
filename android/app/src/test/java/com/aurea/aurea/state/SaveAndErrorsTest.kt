package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * Fase 8G: o sidecar da Home grava atômico (nunca pela metade) e todo código
 * padronizado do motor (§115) chega à pessoa como frase, não como número.
 * Os números são os de `aurea::Errc` (core/Result.hpp, com static_assert lá).
 */
class SaveAndErrorsTest {
    @Test
    fun standardizedCodesHaveHumanMessages() {
        for (code in listOf(20, 14, 31, 29, 30, 28)) {   // GPU_OOM, DECODER, ENCODER, ASSET, PROJECT, STORAGE
            val msg = humanError(code)
            assertFalse("código $code sem mensagem própria", msg.startsWith("Erro inesperado"))
        }
        assertTrue(humanError(28).contains("espaço"))
        assertTrue(humanError(12).contains("versão mais nova"))
        assertTrue(humanError(999).contains("999"))
    }

    @Test
    fun sidecarIsWrittenAtomically() {
        val dir = Files.createTempDirectory("aurea_meta").toFile()
        try {
            val meta = File(dir, "Projeto.aurea.meta.json")
            writeTextAtomic(meta, """{"title":"A"}""")
            writeTextAtomic(meta, """{"title":"B"}""")
            assertEquals("""{"title":"B"}""", meta.readText())
            assertFalse("temporário ficou para trás", File(dir, "Projeto.aurea.meta.json.tmp").exists())
        } finally {
            dir.deleteRecursively()
        }
    }
}
