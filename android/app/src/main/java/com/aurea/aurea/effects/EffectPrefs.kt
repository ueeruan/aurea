package com.aurea.aurea.effects

import android.content.Context
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Favoritos e recentes do catálogo de efeitos.
 *
 * É preferência do APARELHO, não do projeto (como na A.01): o mesmo efeito é
 * favorito em qualquer projeto. Guardado por `typeId` — a chave estável do
 * efeito, então renomear o efeito não perde o favorito.
 *
 * [favorites] e [recents] são estado observável: o navegador reagenda sozinho
 * quando alguém favorita um efeito.
 */
@Stable
class EffectPrefs(context: Context) {

    private val prefs = context.applicationContext.getSharedPreferences("aurea.efeitos", Context.MODE_PRIVATE)

    var favorites by mutableStateOf(readFavorites())
        private set

    var recents by mutableStateOf(readRecents())
        private set

    /**
     * "Prévias de efeitos" (Ajustes): ligado (padrão) mostra cada efeito
     * aplicado sobre a imagem de amostra; desligado, os cartões mostram só a
     * cartela genérica com o ícone da categoria e NENHUMA prévia (parada ou
     * animada) é pedida ao motor. Mesma chave/semântica do iOS (`FxEffectPrefs.previewsEnabledKey`).
     */
    var previewsEnabled by mutableStateOf(prefs.getBoolean(KEY_PREVIEWS, true))
        private set

    fun updatePreviewsEnabled(on: Boolean) {
        previewsEnabled = on
        prefs.edit().putBoolean(KEY_PREVIEWS, on).apply()
    }

    fun isFavorite(typeId: Int): Boolean = typeId in favorites

    /** Marca/desmarca e grava. Devolve o estado novo. */
    fun toggleFavorite(typeId: Int): Boolean {
        val on = typeId !in favorites
        favorites = toggleFavorite(favorites, typeId)
        prefs.edit().putStringSet(KEY_FAVORITES, favorites.mapTo(HashSet()) { it.toString() }).apply()
        return on
    }

    /** Põe o efeito na frente dos recentes (sem repetir, no máximo [RECENTS_MAX]). */
    fun addRecent(typeId: Int) {
        recents = pushRecent(recents, typeId, RECENTS_MAX)
        prefs.edit().putString(KEY_RECENTS, formatRecents(recents)).apply()
    }

    fun clearRecents() {
        recents = emptyList()
        prefs.edit().remove(KEY_RECENTS).apply()
    }

    private fun readFavorites(): Set<Int> =
        prefs.getStringSet(KEY_FAVORITES, emptySet()).orEmpty().mapNotNullTo(HashSet()) { it.toIntOrNull() }

    private fun readRecents(): List<Int> =
        parseRecents(prefs.getString(KEY_RECENTS, ""))

    companion object {
        const val RECENTS_MAX = 12
        private const val KEY_FAVORITES = "favoritos"
        private const val KEY_RECENTS = "recentes"
        private const val KEY_PREVIEWS = "previas"
    }
}
