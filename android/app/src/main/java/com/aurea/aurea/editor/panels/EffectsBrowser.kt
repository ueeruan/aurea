package com.aurea.aurea.editor.panels

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.EffectTile
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import java.text.Normalizer

/**
 * "CUSTO N" a partir da classe do efeito (`aurea::EffectClass`), já que o motor
 * não publica o custo da A.01: PerPixel = 1 (funde com os vizinhos, quase de
 * graça), Neighborhood = 2 (lê a vizinhança, pode crescer a região), Domain = 2
 * (quebra a fusão, textura própria), Temporal = 3 (recurso persistente entre
 * quadros). O selo só aparece com custo > 1, como na A.01.
 */
internal fun effectCost(effectClass: Int): Int = when (effectClass) {
    0 -> 1
    1, 2 -> 2
    3 -> 3
    else -> 1
}

/** Busca sem acento e sem caixa ("saturacao" acha "Saturação"). */
internal fun normalizeSearch(s: String): String =
    Normalizer.normalize(s, Normalizer.Form.NFD).replace(Regex("\\p{Mn}+"), "").lowercase().trim()

/** Ordem das categorias da A.01; as que o motor inventar entram no fim. */
private val CategoryOrder = listOf("Cor", "Estilizar", "Distorcer", "Diversos", "Glow e Luz", "Luz", "Lente", "Desfoque", "Glitch", "Tempo", "Gerar", "Recorte", "Utilitário")

/**
 * Favoritos e recentes do catálogo: preferência do APARELHO (não do projeto),
 * como na A.01. Guardados por `typeId`.
 */
private class EffectPrefs(context: Context) {
    private val prefs = context.getSharedPreferences("aurea.efeitos", Context.MODE_PRIVATE)

    fun favorites(): Set<Int> = prefs.getStringSet("favoritos", emptySet())!!.mapNotNull { it.toIntOrNull() }.toSet()

    fun setFavorites(v: Set<Int>) = prefs.edit().putStringSet("favoritos", v.map { it.toString() }.toSet()).apply()

    fun recents(): List<Int> = prefs.getString("recentes", "")!!.split(',').mapNotNull { it.toIntOrNull() }

    fun addRecent(type: Int) {
        val list = (listOf(type) + recents().filter { it != type }).take(12)
        prefs.edit().putString("recentes", list.joinToString(",")).apply()
    }
}

/** O filtro aceso na fileira de chips. */
private sealed interface Filter {
    data object All : Filter
    data object Recent : Filter
    data object Favorite : Filter
    data class Category(val name: String) : Filter
}

/**
 * O NAVEGADOR DE EFEITOS [A] (tela96b / 16): folha modal `#0F141A`, topo 20,
 * até 62 % da tela, padding 14. "Efeitos" 18 sp w700, busca
 * "glow, rgb split, pixelate...", chips "<categoria> N" (raio 9; aceso
 * `actionDim` + borda `action`), grade de colunas = largura/118 (2..5), vão 10,
 * aspecto 0,7. Toque = aplicar nas camadas escolhidas e fechar. Modal como na
 * A.01 (véu por cima do editor).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun EffectsBrowser(store: EditorStore, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val prefs = remember(context) { EffectPrefs(context.applicationContext) }
    var favorites by remember { mutableStateOf(prefs.favorites()) }
    val recents = remember { prefs.recents() }
    var query by remember { mutableStateOf("") }
    var filter by remember { mutableStateOf<Filter>(Filter.All) }
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    val maxHeight = (LocalConfiguration.current.screenHeightDp * 0.62f).dp

    val catalog = store.catalog
    val categories = remember(catalog) {
        val names = catalog.map { it.category }.distinct()
        names.sortedBy { n -> CategoryOrder.indexOf(n).let { if (it < 0) CategoryOrder.size + names.indexOf(n) else it } }
    }
    val q = normalizeSearch(query)
    val results: List<EffectCatalogEntry> = when {
        q.isNotEmpty() -> catalog.filter { normalizeSearch(it.name).contains(q) || normalizeSearch(it.category).contains(q) }
        else -> when (val f = filter) {
            Filter.All -> catalog
            Filter.Recent -> recents.mapNotNull { id -> catalog.firstOrNull { it.typeId == id } }
            Filter.Favorite -> catalog.filter { it.typeId in favorites }
            is Filter.Category -> catalog.filter { it.category == f.name }
        }
    }

    fun close(then: () -> Unit = {}) {
        scope.launch { sheet.hide() }.invokeOnCompletion {
            then()
            onDismiss()
        }
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheet,
        containerColor = AureaColors.EditorPanel,
        contentColor = AureaColors.Text,
        scrimColor = Color(0x8A000000),
        shape = RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp),
        dragHandle = null,
        // Os recuos (barra de navegação e teclado da busca) são aplicados aqui
        // dentro, uma vez só.
        contentWindowInsets = { WindowInsets(0) },
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .heightIn(max = maxHeight)
                .navigationBarsPadding()
                .imePadding()
                .padding(start = 14.dp, top = 12.dp, end = 14.dp, bottom = 8.dp),
        ) {
            Text("Efeitos", style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, fontWeight = FontWeight.W700)))
            Spacer(Modifier.height(8.dp))
            SearchField(query, onChange = { query = it })
            Spacer(Modifier.height(8.dp))
            if (q.isEmpty()) {
                Row(
                    Modifier.fillMaxWidth().height(34.dp).horizontalScroll(rememberScrollState()),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    FilterChip("Todos ${catalog.size}", filter == Filter.All) { filter = Filter.All }
                    if (recents.isNotEmpty()) {
                        FilterChip("Recentes ${recents.count { id -> catalog.any { it.typeId == id } }}", filter == Filter.Recent) {
                            filter = if (filter == Filter.Recent) Filter.All else Filter.Recent
                        }
                    }
                    FilterChip("★ Favoritos ${favorites.count { id -> catalog.any { it.typeId == id } }}", filter == Filter.Favorite) {
                        filter = if (filter == Filter.Favorite) Filter.All else Filter.Favorite
                    }
                    categories.forEach { c ->
                        val on = filter == Filter.Category(c)
                        FilterChip("$c ${catalog.count { it.category == c }}", on) {
                            filter = if (on) Filter.All else Filter.Category(c)
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))
            }
            if (results.isEmpty()) {
                Box(Modifier.fillMaxWidth().padding(24.dp), contentAlignment = Alignment.Center) {
                    Text(
                        if (filter == Filter.Favorite && q.isEmpty()) "Nenhum favorito ainda. Toque na estrela de um efeito."
                        else "Nada encontrado. Tente \"glow\", \"rgb\", \"pixel\" ou \"shake\".",
                        textAlign = TextAlign.Center,
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
                    )
                }
            } else {
                BoxWithConstraints(Modifier.fillMaxWidth().weight(1f, fill = false)) {
                    val cols = (maxWidth.value / 118f).toInt().coerceIn(2, 5)
                    LazyVerticalGrid(
                        columns = GridCells.Fixed(cols),
                        contentPadding = PaddingValues(bottom = 8.dp),
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        items(results, key = { it.typeId }) { e ->
                            EffectTile(
                                name = e.name,
                                category = e.category,
                                cost = effectCost(e.effectClass),
                                favorite = e.typeId in favorites,
                                onFavorite = {
                                    favorites = if (e.typeId in favorites) favorites - e.typeId else favorites + e.typeId
                                    prefs.setFavorites(favorites)
                                },
                                onClick = {
                                    store.addEffect(e.typeId)
                                    prefs.addRecent(e.typeId)
                                    close()
                                },
                                modifier = Modifier.aspectRatio(0.7f),
                            )
                        }
                    }
                }
            }
        }
    }
}

/** A busca [A] (`CupertinoSearchTextField`): 36 dp, raio 9, lupa, texto 14 sp. */
@Composable
private fun SearchField(value: String, onChange: (String) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(36.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(Color(0xFF272B33))
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Search, 18.dp, AureaColors.Muted)
        Spacer(Modifier.width(6.dp))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (value.isEmpty()) {
                Text("glow, rgb split, pixelate...", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
            }
            BasicTextField(
                value = value,
                onValueChange = onChange,
                singleLine = true,
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp)),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth(),
            )
        }
        if (value.isNotEmpty()) {
            Box(Modifier.tocavel { onChange("") }.padding(4.dp)) {
                CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.Muted)
            }
        }
    }
}

/** Chip de filtro [A]: padding 12 × 8, raio 9, 12 sp w600; aceso `actionDim` + borda `action`. */
@Composable
private fun FilterChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .padding(end = 8.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(if (on) AureaColors.ActionDim else AureaColors.Chip)
            .then(if (on) Modifier.border(1.dp, AureaColors.Action, RoundedCornerShape(9.dp)) else Modifier)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Action else AureaColors.Text)))
    }
}
