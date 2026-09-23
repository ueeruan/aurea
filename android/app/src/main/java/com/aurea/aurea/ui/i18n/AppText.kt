package com.aurea.aurea.ui.i18n

import android.content.Context
import android.content.res.Resources
import androidx.annotation.PluralsRes
import androidx.annotation.StringRes

/**
 * Texto do catálogo FORA do Compose (Fase 8.1): ViewModel, exportador, legendas.
 *
 * Por que não `getApplication<Application>().getString(...)`: o `Application`
 * não passa pelo `attachBaseContext` da Activity — ele resolve no idioma do
 * SISTEMA. Com o app em árabe num aparelho em português, o toast sairia em
 * português. Aqui o contexto é embrulhado pelo MESMO [AppLanguage.wrap] da
 * Activity, e o resultado fica guardado até o idioma escolhido mudar.
 */
object AppText {
    @Volatile private var cache: Pair<String?, Resources>? = null

    fun resources(context: Context): Resources {
        val app = context.applicationContext ?: context
        val tag = AppLanguage.current(app).tag
        cache?.let { (t, r) -> if (t == tag) return r }
        val res = AppLanguage.wrap(app).resources
        cache = tag to res
        return res
    }

    fun get(context: Context, @StringRes id: Int): String = resources(context).getString(id)

    fun get(context: Context, @StringRes id: Int, vararg args: Any): String =
        resources(context).getString(id, *args)

    /** Contagem com a concordância do idioma (russo 3 formas, árabe 6). */
    fun plural(context: Context, @PluralsRes id: Int, count: Int, vararg args: Any): String =
        resources(context).getQuantityString(id, count, *(if (args.isEmpty()) arrayOf<Any>(count) else args))
}
