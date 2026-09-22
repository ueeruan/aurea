package com.aurea.aurea.ui.i18n

import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import androidx.core.content.edit
import java.util.Locale

/**
 * Os idiomas do Aurea (Fase 8.1).
 *
 * O NOME de cada idioma NÃO é traduzido: "Русский" se escreve assim em qualquer
 * idioma, e é o único jeito de quem fala russo achar o russo numa lista escrita
 * em árabe. Só "Padrão do sistema" é texto de interface e vem do catálogo.
 *
 * O [tag] é o código BCP-47. `null` = seguir o sistema, que é o padrão de
 * fábrica: um aparelho em russo abre o Aurea em russo sem ninguém configurar
 * nada.
 */
enum class AppLanguage(val tag: String?, val display: String) {
    SYSTEM(null, ""),          // rótulo vem do catálogo (settings_language_system)
    PT_BR("pt-BR", "Português"),
    EN("en", "English"),
    ES("es", "Español"),
    RU("ru", "Русский"),
    HI("hi", "हिन्दी"),
    ID("id", "Bahasa Indonesia"),
    AR("ar", "العربية");

    companion object {
        private const val PREFS = "aurea.settings"
        private const val KEY = "idioma"

        /** O que está escolhido neste aparelho. */
        fun current(context: Context): AppLanguage {
            val saved = prefs(context).getString(KEY, null) ?: return SYSTEM
            return entries.firstOrNull { it.tag == saved } ?: SYSTEM
        }

        fun select(context: Context, language: AppLanguage) {
            prefs(context).edit {
                if (language.tag == null) remove(KEY) else putString(KEY, language.tag)
            }
        }

        private fun prefs(context: Context): SharedPreferences =
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

        /**
         * Aplica o idioma escolhido ao contexto.
         *
         * É o gancho do `attachBaseContext`: a partir daqui TODO `stringResource`
         * e todo `getString` do app resolvem no idioma certo, em qualquer versão
         * do Android. O `LocaleManager` do Android 13 faria o mesmo e ainda
         * apareceria nas configurações do sistema — mas só a partir do 13, e o
         * Aurea roda no 8. Uma implementação só, que funciona em todas.
         *
         * Com [SYSTEM] devolve o contexto como veio: aí quem manda é o sistema.
         */
        fun wrap(base: Context): Context {
            val chosen = current(base)
            val tag = chosen.tag ?: return base
            val locale = Locale.forLanguageTag(tag)
            Locale.setDefault(locale)
            val config = Configuration(base.resources.configuration).apply {
                setLocale(locale)
                // O idioma escolhido no app manda sobre o do sistema, inclusive
                // quando ele é o mesmo. `setLayoutDirection` sai do locale: com
                // árabe o RTL entra aqui, e o resto do app já sabe o que NÃO
                // espelhar (ver `KeepLtr`).
                setLayoutDirection(locale)
            }
            return base.createConfigurationContext(config)
        }
    }
}
