package com.aurea.aurea.state

import com.aurea.aurea.R
import javax.xml.parsers.DocumentBuilderFactory
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
    // JVM: resolve the real Portuguese catalog without an Android Application.
    private fun localizedError(code: Int): String {
        val id = humanErrorResource(code)
        val name = R.string::class.java.fields.first { it.getInt(null) == id }.name
        val strings = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File("src/main/res/values/strings.xml")).getElementsByTagName("string")
        val text = (0 until strings.length).map { strings.item(it) }
            .first { it.attributes.getNamedItem("name").nodeValue == name }.textContent
        return String.format(java.util.Locale.ROOT, text, code)
    }

    @Test
    fun standardizedCodesHaveHumanMessages() {
        for (code in listOf(20, 14, 31, 29, 30, 28)) {   // GPU_OOM, DECODER, ENCODER, ASSET, PROJECT, STORAGE
            val msg = localizedError(code)
            assertFalse("código $code sem mensagem própria", msg.startsWith("Erro inesperado"))
        }
        assertTrue(localizedError(28).contains("espaço"))
        assertTrue(localizedError(12).contains("versão mais nova"))
        assertTrue(localizedError(999).contains("999"))
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
