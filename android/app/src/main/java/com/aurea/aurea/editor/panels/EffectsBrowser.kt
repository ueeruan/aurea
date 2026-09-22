package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.effects.EffectDetailSheet
import com.aurea.aurea.effects.EffectFilter
import com.aurea.aurea.effects.EffectsCatalogGrid
import com.aurea.aurea.effects.arrangeCatalog
import com.aurea.aurea.effects.catalogHaystack
import com.aurea.aurea.effects.effectCategories
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.text.Normalizer

/** Busca sem acento e sem caixa ("saturacao" acha "Saturação"). */
internal fun normalizeSearch(s: String): String =
    Normalizer.normalize(s, Normalizer.Form.NFD).replace(Regex("\\p{Mn}+"), "").lowercase().trim()

/**
 * O NAVEGADOR DE EFEITOS DO EDITOR (Fase 7.3 §11–§20): TELA CHEIA.
 *
 * A folha de 72 % da Fase 7.2 virou uma tela: o catálogo cresceu, ganhou
 * prévia visual, ficha e parâmetros, e num aparelho de 6" uma folha parcial
 * deixava duas colunas de cartão — o espaço não cabia o que o navegador faz.
 *
 * A grade, a busca e as fichas são `EffectsCatalogGrid` / `EffectDetailSheet`.
 * O toque no cartão abre a FICHA, e é a ficha que aplica — assim dá para ler o
 * que o efeito faz antes de sujar a pilha. Favoritos e recentes são do aparelho.
 */
@Composable
internal fun EffectsBrowser(store: EditorStore, onDismiss: () -> Unit) {
    val prefs = remember(store) { store.effectPrefs }
    var filter by remember { mutableStateOf<EffectFilter>(EffectFilter.All) }
    var query by remember { mutableStateOf("") }
    var detail by remember { mutableStateOf<EffectCatalogEntry?>(null) }

    val catalog = store.catalog
    val categories = remember(catalog) { effectCategories(catalog) }
    val sorted = remember(catalog, categories) { arrangeCatalog(catalog, categories) }
    val haystack = remember(catalog) { catalogHaystack(catalog) }
    // Os recentes que a lista mostra são os de ANTES desta sessão: aplicar um
    // efeito não reordena a lista debaixo do dedo de quem tocou.
    val recents = remember(prefs.recents) { prefs.recents }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(
            Modifier
                .fillMaxSize()
                .background(AureaColors.EditorPanel)
                .windowInsetsPadding(WindowInsets.safeDrawing)
                .imePadding(),
        ) {
            Row(
                Modifier.fillMaxWidth().height(AureaDims.EditorTopBar).padding(start = AureaDims.S2, end = AureaDims.S2),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.tocavel(shrink = 1f, onClick = onDismiss).padding(AureaDims.S2), Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronBack, AureaDims.IconLg, AureaColors.Text)
                }
                Spacer(Modifier.padding(horizontal = 2.dp))
                Text(
                    "Efeitos",
                    style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, fontWeight = FontWeight.W700)),
                    modifier = Modifier.weight(1f),
                )
                Text(
                    "${catalog.size} efeitos",
                    style = AureaType.CardSpec,
                    modifier = Modifier.padding(end = AureaDims.S3),
                )
            }
            EffectsCatalogGrid(
                catalog = catalog,
                sorted = sorted,
                haystack = haystack,
                categories = categories,
                previews = store.effectPreviews,
                favorites = prefs.favorites,
                recents = recents,
                query = query,
                onQuery = { query = it },
                filter = filter,
                onFilter = { filter = it },
                onFavorite = { prefs.toggleFavorite(it) },
                onClick = { detail = it },
                contentPadding = PaddingValues(start = AureaDims.S3, end = AureaDims.S3, bottom = AureaDims.S5),
            )
        }
    }

    detail?.let { entry ->
        EffectDetailSheet(
            store = store,
            entry = entry,
            previews = store.effectPreviews,
            favorite = prefs.isFavorite(entry.typeId),
            onFavorite = { prefs.toggleFavorite(entry.typeId) },
            onApply = {
                store.addEffect(entry.typeId)
                prefs.addRecent(entry.typeId)
                detail = null
                onDismiss()
            },
            onDismiss = { detail = null },
        )
    }
}
