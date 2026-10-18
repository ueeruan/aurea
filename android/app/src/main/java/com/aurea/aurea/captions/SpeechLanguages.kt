package com.aurea.aurea.captions

import android.content.Context
import com.aurea.aurea.ui.i18n.AppLanguage
import com.aurea.aurea.ui.i18n.AppText
import java.util.Locale

/**
 * Idiomas da fala oferecidos às legendas (0.2.5): os sete do app mais os
 * comuns que o Whisper reconhece bem. O código é o do Whisper; o nome aparece
 * no próprio idioma (como na escolha de idioma do app). Mesma lista no iOS.
 */
object SpeechLanguages {
    val codes = listOf("pt", "en", "es", "ru", "hi", "ar", "id", "fr", "de", "it", "ja", "ko", "zh", "tr")

    private val names = mapOf(
        "pt" to "Português", "en" to "English", "es" to "Español", "ru" to "Русский", "hi" to "हिन्दी",
        "ar" to "العربية", "id" to "Bahasa Indonesia", "fr" to "Français", "de" to "Deutsch", "it" to "Italiano",
        "ja" to "日本語", "ko" to "한국어", "zh" to "中文", "tr" to "Türkçe",
    )

    fun endonym(code: String): String = names[code] ?: code

    /** Nome no idioma do app, para o leitor de tela ("Japonês"), junto do nome nativo. */
    fun spoken(context: Context, code: String): String {
        val app = AppText.resources(context).configuration.locales[0] ?: Locale.getDefault()
        val local = Locale.forLanguageTag(code).getDisplayLanguage(app).replaceFirstChar { it.titlecase(app) }
        return if (local.isBlank() || local == endonym(code)) endonym(code) else "$local (${endonym(code)})"
    }

    /**
     * Padrão: o idioma escolhido no app ou, seguindo o sistema, o do aparelho —
     * quando o Whisper o tem. Nulo = detectar automaticamente.
     */
    fun preferred(context: Context): String? {
        val chosen = AppLanguage.current(context).tag?.substringBefore('-')
        val device = Locale.getDefault().language
        return listOfNotNull(chosen, device).map { if (it == "in") "id" else it }.firstOrNull { it in codes }
    }
}
