package com.aurea.aurea.ui.i18n

import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.LayoutDirection
import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import androidx.core.content.edit
import java.util.Locale

/**
 * Raiz do Compose: mantém o idioma (textos e direção) escolhido no app quando
 * o sistema troca a configuração sem recriar a Activity (`configChanges`). Sem
 * isso, girar o aparelho com o app em árabe num telefone em inglês deixava o
 * editor em inglês e LTR até reabrir.
 */
@Composable
fun KeepAppLanguage(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val fixed = remember(configuration) { AppLanguage.override(context, configuration) }
    if (fixed == null) {
        content()
        return
    }
    val direction = if (fixed.layoutDirection == View.LAYOUT_DIRECTION_RTL) LayoutDirection.Rtl else LayoutDirection.Ltr
    CompositionLocalProvider(LocalConfiguration provides fixed, LocalLayoutDirection provides direction, content = content)
}

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
    ID("id", "Bahasa Indonesia"),
    // Árabe: o app inteiro em RTL, menos timeline, réguas, curvas, palco e
    // transporte (ver `KeepLtr`). Números em algarismos ocidentais.
    AR("ar", "العربية");
    // hi: catálogo parcial guardado em res/values-hi, fora do APK por enquanto
    // (androidResources.localeFilters). Volta numa fase própria.

    companion object {
        private const val PREFS = "aurea.settings"
        private const val KEY = "idioma"
        private const val CHOSEN = "language_chosen"
        /** Idiomas do sistema que o APK traz completos (o resto vira inglês). */
        private val SYSTEM_FOLLOWED = setOf("pt", "en", "es", "ru", "id", "in", "ar")

        fun needsChoice(context: Context): Boolean = !prefs(context).getBoolean(CHOSEN, false)

        /** O que está escolhido neste aparelho. */
        fun current(context: Context): AppLanguage {
            val saved = prefs(context).getString(KEY, null) ?: return SYSTEM
            return entries.firstOrNull { it.tag == saved } ?: SYSTEM
        }

        fun select(context: Context, language: AppLanguage) {
            prefs(context).edit {
                if (language.tag == null) remove(KEY) else putString(KEY, language.tag)
                putBoolean(CHOSEN, true)
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
            val config = override(base, base.resources.configuration) ?: return base
            Locale.setDefault(config.locales[0])
            return base.createConfigurationContext(config)
        }

        /**
         * [base] com o idioma do app reaplicado; `null` = o sistema manda e não
         * há nada a mudar.
         *
         * Serve ao [wrap] e também a cada mudança de configuração que a
         * Activity trata sozinha (`configChanges`: rotação, uiMode, densidade):
         * nessas o sistema entrega a configuração DELE, sem o idioma escolhido
         * no app — o editor voltava para o idioma do aparelho (e para LTR) no
         * meio da sessão. Ver `MainActivity.onConfigurationChanged` e
         * [KeepAppLanguage].
         */
        fun override(context: Context, base: Configuration): Configuration? {
            // Sistema: os idiomas do APK (pt, en, es, ru, id, ar — "in" é o
            // código antigo do indonésio no Java) seguem o sistema; QUALQUER
            // outro cai no inglês — e em LTR. Sem isso, um aparelho em hindi
            // abriria com os textos em português (o padrão). Um aparelho em
            // árabe segue o sistema: textos em árabe e layout em RTL.
            val tag = current(context).tag ?: run {
                val sys = base.locales[0]
                // Árabe do sistema também passa por aqui: os algarismos.
                if (sys.language == "ar") return@run sys.toLanguageTag()
                if (sys.language in SYSTEM_FOLLOWED) return null
                "en"
            }
            val locale = westernDigits(Locale.forLanguageTag(tag))
            return Configuration(base).apply {
                setLocale(locale)
                // O idioma escolhido no app manda sobre o do sistema, inclusive
                // quando ele é o mesmo. `setLayoutDirection` sai do locale: com
                // árabe o RTL entra aqui, e o resto do app já sabe o que NÃO
                // espelhar (ver `KeepLtr`).
                setLayoutDirection(locale)
            }
        }

        /**
         * Árabe com algarismos ocidentais (0-9), não árabe-índicos (٠-٩).
         *
         * Decisão de produto: timecode, régua, valores de painel e coordenadas
         * do Aurea são sempre 0-9 — é o que os editores profissionais em árabe
         * mostram e é o que o motor desenha no preview. Sem a extensão `nu-latn`
         * o `String.format` do Java (e o `getString(id, n)`) num locale "ar"
         * escreveria ١٢٣ no meio de uma tela onde a timeline diz 123. Os
         * recursos continuam vindo de `values-ar` (a extensão não muda o idioma).
         */
        internal fun westernDigits(locale: Locale): Locale =
            if (locale.language != "ar") locale
            else Locale.Builder().setLocale(locale).setUnicodeLocaleKeyword("nu", "latn").build()
    }
}
