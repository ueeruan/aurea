package com.aurea.aurea.effects

import android.content.Context
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Favoritos e recentes do catálogo de efeitos.
 *
 * É preferência do APARELHO, não do projeto (como na A.01): o mesmo efeito é
 * favorito em qualquer projeto. Guardado por `typeId` — a chave estável do
 * efeito, então renomear o efeito não perde o favorito.
 *
 * Omega efeitos (navegador, P0-6): favorito e recente guardam o efeito OU uma
 * variação dele ([EffectRef]: "<typeId>" ou "<typeId>/<preset>"). As listas de
 * inteiros gravadas pelas versões antigas continuam valendo (viram o efeito
 * base na leitura) e são regravadas no formato novo na próxima mudança.
 *
 * [favoriteRefs], [recentRefs] e [quickPage] são estado observável: o navegador
 * se redesenha sozinho quando alguém favorita.
 */
@Stable
class EffectPrefs(context: Context) {

    private val prefs = context.applicationContext.getSharedPreferences("aurea.efeitos", Context.MODE_PRIVATE)

    /** Favoritos: efeitos base e variações. */
    var favoriteRefs by mutableStateOf(readFavorites())
        private set

    /** Recentes, o último na frente (no máximo [RECENTS_MAX]). */
    var recentRefs by mutableStateOf(readRecents())
        private set

    /** A página da faixa Recentes/Favoritos que estava à vista (lembrada como na referência). */
    var quickPage by mutableIntStateOf(prefs.getInt(KEY_QUICK_PAGE, 0))
        private set

    /** Os tipos favoritos como EFEITO BASE (quem só conhece o `typeId`: busca de comandos, ficha). */
    val favorites: Set<Int> get() = favoriteRefs.filter { it.isBase }.mapTo(HashSet()) { it.typeId }

    /** Os tipos usados por último (sem repetir), na ordem. */
    val recents: List<Int> get() = recentRefs.map { it.typeId }.distinct()

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

    fun isFavorite(typeId: Int): Boolean = EffectRef(typeId) in favoriteRefs

    fun isFavorite(ref: EffectRef): Boolean = ref in favoriteRefs

    /** Marca/desmarca o efeito base e grava. Devolve o estado novo. */
    fun toggleFavorite(typeId: Int): Boolean = toggleFavorite(EffectRef(typeId))

    /** Marca/desmarca o efeito ou a variação e grava. Devolve o estado novo. */
    fun toggleFavorite(ref: EffectRef): Boolean {
        val on = ref !in favoriteRefs
        favoriteRefs = toggleFavoriteRef(favoriteRefs, ref)
        prefs.edit().putStringSet(KEY_FAVORITES, favoriteRefs.mapTo(HashSet()) { it.key }).apply()
        return on
    }

    /** Põe o efeito base na frente dos recentes (sem repetir, no máximo [RECENTS_MAX]). */
    fun addRecent(typeId: Int) = addRecent(EffectRef(typeId))

    /** Põe o efeito ou a variação na frente dos recentes. */
    fun addRecent(ref: EffectRef) {
        recentRefs = pushRecentRef(recentRefs, ref, RECENTS_MAX)
        prefs.edit().putString(KEY_RECENTS, formatEffectRefs(recentRefs)).apply()
    }

    fun clearRecents() {
        recentRefs = emptyList()
        prefs.edit().remove(KEY_RECENTS).apply()
    }

    /** Lembra a página da faixa Recentes/Favoritos. */
    fun updateQuickPage(page: Int) {
        if (page == quickPage || page < 0) return
        quickPage = page
        prefs.edit().putInt(KEY_QUICK_PAGE, page).apply()
    }

    private fun readFavorites(): Set<EffectRef> =
        parseEffectRefSet(prefs.getStringSet(KEY_FAVORITES, emptySet()).orEmpty())

    private fun readRecents(): List<EffectRef> =
        parseEffectRefs(prefs.getString(KEY_RECENTS, "")).take(RECENTS_MAX)

    companion object {
        /** Recentes: uma página de 8, como a referência. */
        const val RECENTS_MAX = QUICK_PAGE_SIZE
        private const val KEY_FAVORITES = "favoritos"
        private const val KEY_RECENTS = "recentes"
        private const val KEY_PREVIEWS = "previas"
        private const val KEY_QUICK_PAGE = "pagina_rapida"
    }
}
