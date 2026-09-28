package com.aurea.aurea.i18n

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Toda chave do catálogo padrão (pt-BR) existe no inglês: sem ela, quem usa o
 * app em inglês vê o texto em português pelo fallback do Android.
 */
class EnglishCatalogTest {
    private fun keys(folder: String): Set<String> {
        val file = listOf("src/main/res/$folder/strings.xml", "app/src/main/res/$folder/strings.xml").map(::File).first { it.isFile }
        return Regex("""<(?:string|plurals) name="([^"]+)"(?![^>]*translatable="false")""").findAll(file.readText()).map { it.groupValues[1] }.toSet()
    }

    @Test
    fun everyDefaultKeyHasEnglish() {
        val missing = keys("values") - keys("values-en")
        assertTrue("chaves sem inglês em values-en/strings.xml: ${missing.sorted()}", missing.isEmpty())
    }
}
