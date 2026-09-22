package com.aurea.aurea.home

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.effects.EffectDetailSheet
import com.aurea.aurea.effects.EffectFilter
import com.aurea.aurea.effects.EffectsCatalogGrid
import com.aurea.aurea.effects.arrangeCatalog
import com.aurea.aurea.effects.catalogHaystack
import com.aurea.aurea.effects.effectCategories
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType

/**
 * A ABA EFEITOS (Fase 7.3 §11–§20): o catálogo de tudo o que o motor sabe
 * fazer, com busca, categorias, favoritos, recentes, prévia visual e ficha.
 *
 * Em modo catálogo: aqui não existe camada para aplicar, então tocar num
 * efeito abre a FICHA — que mostra a prévia, o que ele faz, onde funciona e
 * quais parâmetros tem. Favoritar funciona em qualquer lugar (§18). A ação
 * de aplicar aparece quando há seleção, dentro do editor.
 *
 * A grade usa a MESMA `EffectsCatalogGrid` do navegador do editor: uma
 * implementação, dois lugares.
 */
@Composable
internal fun EffectsTab(
    store: EditorStore,
    vm: HomeViewModel,
    listState: LazyListState,
    bottomBar: Dp,
) {
    val prefs = store.effectPrefs
    val catalog = store.catalog
    var filter by remember { mutableStateOf<EffectFilter>(EffectFilter.All) }
    var detail by remember { mutableStateOf<EffectCatalogEntry?>(null) }
    // A rolagem da grade sobrevive à ida ao editor, como a das outras abas.
    val grid = rememberLazyGridState(vm.effectScrollIndex, vm.effectScrollOffset)
    DisposableEffect(grid) {
        onDispose { vm.saveEffectScroll(grid.firstVisibleItemIndex, grid.firstVisibleItemScrollOffset) }
    }

    val categories = remember(catalog) { effectCategories(catalog) }
    val sorted = remember(catalog, categories) { arrangeCatalog(catalog, categories) }
    val haystack = remember(catalog) { catalogHaystack(catalog) }

    Column(Modifier.fillMaxSize()) {
        Text(
            "Efeitos",
            style = AureaType.HeadlineLarge,
            modifier = Modifier.padding(start = AureaDims.Gutter, top = AureaDims.S5, end = AureaDims.Gutter, bottom = AureaDims.S4),
        )
        EffectsCatalogGrid(
            catalog = catalog,
            sorted = sorted,
            haystack = haystack,
            categories = categories,
            previews = store.effectPreviews,
            favorites = prefs.favorites,
            recents = prefs.recents,
            query = vm.effectQuery,
            onQuery = { vm.effectQuery = it },
            filter = filter,
            onFilter = { filter = it },
            onFavorite = { prefs.toggleFavorite(it) },
            onClick = { detail = it },
            state = grid,
            contentPadding = PaddingValues(
                start = AureaDims.Gutter,
                end = AureaDims.Gutter,
                bottom = AureaDims.ListEndSpace + bottomBar - AureaDims.TabBarHeight,
            ),
        )
    }

    detail?.let { entry ->
        EffectDetailSheet(
            store = store,
            entry = entry,
            previews = store.effectPreviews,
            favorite = prefs.isFavorite(entry.typeId),
            onFavorite = { prefs.toggleFavorite(entry.typeId) },
            onApply = null,
            onDismiss = { detail = null },
        )
    }
}
