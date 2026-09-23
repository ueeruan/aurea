package com.aurea.aurea.effects

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.editor.panels.effectDisplayName
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Largura de geração da prévia: o maior cartão da grade cabe nisto. */
private const val PREVIEW_W = 320
private const val PREVIEW_H = 200

/**
 * O CARTÃO DE UM EFEITO NO CATÁLOGO (Fase 7.3 §12–§14).
 *
 * A prévia é 16:10 e mostra o EFEITO REAL sobre a cartela de demonstração
 * ([EffectPreviewStore]); até ela ficar pronta — ou se o efeito não puder ser
 * pré-visualizado — o cartão desenha a cartela genérica com o ícone da
 * categoria, que é rótulo visual e não finge ser a prévia.
 *
 * O selo de custo só aparece quando é maior que 1 (o efeito que não se funde
 * com os vizinhos custa mais, e é justo avisar).
 */
@Composable
fun EffectCatalogTile(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val name = effectDisplayName(entry.typeId, entry.name)
    val preview = rememberEffectPreview(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
    val cost = effectCost(entry.effectClass)
    Column(
        modifier
            .semantics { contentDescription = "Ver $name" }
            .tocavel(shrink = 0.97f, haptic = true, onClick = onClick),
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(AureaShape.Card)) {
            if (preview != null) {
                Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
            } else {
                GenericPlate(categoryGlyph(entry.category))
            }
            if (cost > 1) {
                Text(
                    costLabel(cost),
                    style = AureaType.of(9f, FontWeight.W500, color = Color.White),
                    modifier = Modifier
                        .align(Alignment.BottomStart)
                        .padding(AureaDims.S2)
                        .clip(AureaShape.Sm)
                        .background(Color.Black.copy(alpha = 0.55f))
                        .padding(horizontal = 5.dp, vertical = 2.dp),
                )
            }
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .size(AureaDims.MinTap)
                    .semantics { contentDescription = if (favorite) "Tirar dos favoritos" else "Pôr nos favoritos" }
                    .tocavel(onClick = onFavorite),
                contentAlignment = Alignment.Center,
            ) {
                Box(Modifier.size(28.dp).clip(AureaShape.Circle).background(Color.Black.copy(alpha = 0.35f)), Alignment.Center) {
                    CupertinoIcon(
                        if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star,
                        AureaDims.IconSm,
                        if (favorite) AureaColors.Accent else Color.White,
                    )
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.CardTitle)
        Text(entry.category, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.CardSpec)
    }
}

/**
 * A cartela de recuo: degradê vertical, disco quente e a barra clara — a
 * "cena" do catálogo — com o ícone da categoria por cima. É o que aparece
 * antes da prévia real e para efeito que não se pré-visualiza.
 */
@Composable
fun GenericPlate(glyph: Char, modifier: Modifier = Modifier) {
    androidx.compose.foundation.Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(AureaColors.EffectPreviewTop, AureaColors.EffectPreviewBottom)))
        val w = size.width
        val h = size.height
        drawCircle(AureaColors.EffectPreviewDisc.copy(alpha = 0.85f), radius = w * 0.19f, center = Offset(w * 0.34f, h * 0.60f))
        drawRect(Color.White.copy(alpha = 0.6f), topLeft = Offset(w * 0.20f, h * 0.18f), size = androidx.compose.ui.geometry.Size(w * 0.60f, h * 0.012f))
        drawRect(Color.White.copy(alpha = 0.25f), topLeft = Offset(w * 0.20f, h * 0.26f), size = androidx.compose.ui.geometry.Size(w * 0.40f, h * 0.012f))
    }
    Box(Modifier.fillMaxSize().padding(AureaDims.S2), contentAlignment = Alignment.BottomEnd) {
        CupertinoIcon(glyph, AureaDims.IconSm, Color.White.copy(alpha = 0.85f))
    }
}

/** A busca do catálogo: campo preenchido com lupa e o "x". */
@Composable
fun EffectSearchField(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .height(AureaDims.SearchField)
            .clip(AureaShape.Chip)
            .background(AureaColors.FieldFilled)
            .padding(horizontal = AureaDims.S2),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Search, 18.dp, AureaColors.Muted)
        Spacer(Modifier.width(6.dp))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (value.isEmpty()) {
                Text(stringResource(R.string.effect_buscar_glitch_vhs_desfoque_cor), style = AureaType.of(14f, color = AureaColors.Muted), maxLines = 1)
            }
            BasicTextField(
                value = value,
                onValueChange = onChange,
                singleLine = true,
                textStyle = AureaType.of(14f),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth(),
            )
        }
        if (value.isNotEmpty()) {
            Box(Modifier.tocavel { onChange("") }.padding(AureaDims.S1)) {
                CupertinoIcon(CupertinoGlyph.XmarkCircleFill, AureaDims.IconSm, AureaColors.Muted)
            }
        }
    }
}

/** Um chip do filtro. */
@Composable
fun EffectFilterChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .padding(end = AureaDims.S2)
            .clip(AureaShape.Chip)
            .background(if (on) AureaColors.ActionDim else AureaColors.Chip)
            .then(if (on) Modifier.border(1.dp, AureaColors.Action, AureaShape.Chip) else Modifier)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = AureaDims.S3, vertical = AureaDims.S2),
    ) {
        Text(
            label,
            style = AureaType.ChipLabel.copy(color = if (on) AureaColors.Action else AureaColors.Text),
            maxLines = 1,
        )
    }
}

/**
 * O NAVEGADOR: busca, fichas de filtro e a grade de cartões.
 *
 * [onClick] recebe o cartão tocado. Quem chama decide o que fazer com ele:
 * o navegador do editor abre a FICHA, e é a ficha que aplica na seleção — o
 * mesmo caminho em todo lugar, porque fora do editor não há seleção para
 * aplicar.
 */
@Composable
fun EffectsCatalogGrid(
    catalog: List<EffectCatalogEntry>,
    sorted: List<EffectCatalogEntry>,
    haystack: Map<Int, String>,
    categories: List<String>,
    previews: EffectPreviewStore?,
    favorites: Set<Int>,
    recents: List<Int>,
    query: String,
    onQuery: (String) -> Unit,
    filter: EffectFilter,
    onFilter: (EffectFilter) -> Unit,
    onFavorite: (Int) -> Unit,
    onClick: (EffectCatalogEntry) -> Unit,
    modifier: Modifier = Modifier,
    state: androidx.compose.foundation.lazy.grid.LazyGridState = rememberLazyGridState(),
    header: (@Composable () -> Unit)? = null,
    contentPadding: PaddingValues = PaddingValues(bottom = AureaDims.S5),
) {
    val results = remember(catalog, sorted, haystack, query, filter, recents, favorites) {
        filterCatalog(catalog, sorted, haystack, query, filter, recents, favorites)
    }
    val searching = query.isNotBlank()

    BoxWithConstraints(modifier.fillMaxSize()) {
        val cols = (maxWidth.value / 168f).toInt().coerceIn(2, 5)
        LazyVerticalGrid(
            columns = GridCells.Fixed(cols),
            state = state,
            contentPadding = contentPadding,
            horizontalArrangement = Arrangement.spacedBy(AureaDims.S3),
            verticalArrangement = Arrangement.spacedBy(AureaDims.S4),
            modifier = Modifier.fillMaxSize(),
        ) {
            if (header != null) {
                item(key = "cabecalho", span = { GridItemSpan(cols) }) { header() }
            }
            item(key = "busca", span = { GridItemSpan(cols) }) {
                EffectSearchField(query, onQuery)
            }
            if (!searching) {
                item(key = "fichas", span = { GridItemSpan(cols) }) {
                    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
                        EffectFilterChip(stringResource(R.string.effect_todos), filter == EffectFilter.All) { onFilter(EffectFilter.All) }
                        if (recents.any { id -> catalog.any { it.typeId == id } }) {
                            EffectFilterChip(stringResource(R.string.effect_recentes), filter == EffectFilter.Recent) {
                                onFilter(if (filter == EffectFilter.Recent) EffectFilter.All else EffectFilter.Recent)
                            }
                        }
                        EffectFilterChip(stringResource(R.string.effect_favoritos), filter == EffectFilter.Favorite) {
                            onFilter(if (filter == EffectFilter.Favorite) EffectFilter.All else EffectFilter.Favorite)
                        }
                        categories.forEach { c ->
                            val on = filter == EffectFilter.Category(c)
                            EffectFilterChip(c, on) { onFilter(if (on) EffectFilter.All else EffectFilter.Category(c)) }
                        }
                    }
                }
            }
            if (results.isEmpty()) {
                item(key = "vazio", span = { GridItemSpan(cols) }) {
                    Text(
                        emptyMessage(filter, searching),
                        textAlign = TextAlign.Center,
                        style = AureaType.of(13f, color = AureaColors.Muted, lineHeight = 1.4f),
                        modifier = Modifier.fillMaxWidth().padding(AureaDims.S5),
                    )
                }
            }
            items(results, key = { it.typeId }, contentType = { "efeito" }) { e ->
                EffectCatalogTile(
                    entry = e,
                    previews = previews,
                    favorite = e.typeId in favorites,
                    onFavorite = { onFavorite(e.typeId) },
                    onClick = { onClick(e) },
                )
            }
        }
    }
}

private fun emptyMessage(filter: EffectFilter, searching: Boolean): String = when {
    searching -> "Nada encontrado. Tente \"glitch\", \"vhs\", \"desfoque\" ou \"cor\"."
    filter == EffectFilter.Favorite -> "Nenhum favorito ainda. Toque na estrela de um efeito."
    filter == EffectFilter.Recent -> "Os efeitos que você usar aparecem aqui."
    else -> "Nenhum efeito nesta categoria."
}

/**
 * A FICHA DE UM EFEITO (§20, §68): prévia grande, o que ele faz, onde
 * funciona, custo e a lista de parâmetros com tipo e faixa. Termina na ação —
 * "Adicionar à seleção" no editor; no catálogo, sem camada por perto, a ação
 * é favoritar (o que funciona de verdade em qualquer lugar).
 */
@Composable
fun EffectDetailSheet(
    store: com.aurea.aurea.state.EditorStore,
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onApply: (() -> Unit)?,
    onDismiss: () -> Unit,
) {
    val name = effectDisplayName(entry.typeId, entry.name)
    val preview = rememberEffectPreview(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
    val specs = remember(entry.typeId) { store.effectSpecs(entry.typeId) }
    val cost = effectCost(entry.effectClass)

    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        LazyColumn(
            Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(start = AureaDims.S5, end = AureaDims.S5, top = AureaDims.S2, bottom = AureaDims.S6),
        ) {
            item(key = "previa") {
                Box(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(AureaShape.Card)) {
                    if (preview != null) {
                        Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
                    } else {
                        GenericPlate(categoryGlyph(entry.category))
                    }
                }
                Spacer(Modifier.height(AureaDims.S4))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(name, style = AureaType.of(22f, FontWeight.W700, -0.4f))
                        Spacer(Modifier.height(2.dp))
                        Text(if (cost > 1) "${entry.category} · ${costLabel(cost).lowercase()} para o celular" else entry.category, style = AureaType.CardSpec)
                    }
                    Box(
                        Modifier
                            .size(AureaDims.MinTap)
                            .semantics { contentDescription = if (favorite) "Tirar dos favoritos" else "Pôr nos favoritos" }
                            .tocavel(onClick = onFavorite),
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(
                            if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star,
                            AureaDims.IconLg,
                            if (favorite) AureaColors.Accent else AureaColors.Muted,
                        )
                    }
                }
                Spacer(Modifier.height(AureaDims.S3))
                Text(effectDescription(entry.typeId, entry.category), style = AureaType.of(14f, lineHeight = 1.45f))
                Spacer(Modifier.height(AureaDims.S3))
                Text(effectCompatibilityLine(entry.typeId), style = AureaType.CardSpec)
                Spacer(Modifier.height(AureaDims.S5))
            }
            if (specs.isNotEmpty()) {
                item(key = "titulo-params") {
                    Text("PARÂMETROS", style = AureaType.Section)
                    Spacer(Modifier.height(AureaDims.S2))
                }
                items(specs.size) { i ->
                    val p = specs[i]
                    Row(Modifier.fillMaxWidth().padding(vertical = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(p.label, style = AureaType.of(14f), modifier = Modifier.weight(1f))
                        Text(paramSummary(p), style = AureaType.CardSpec, textAlign = TextAlign.End)
                    }
                }
            }
            if (onApply != null) {
                item(key = "acao") {
                    Spacer(Modifier.height(AureaDims.S5))
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(AureaDims.ButtonHeight)
                            .clip(AureaShape.Lg)
                            .background(AureaColors.Accent)
                            .tocavel(shrink = 1f, onClick = onApply),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(stringResource(R.string.effect_adicionar_selecao), style = AureaType.Button)
                    }
                }
            } else {
                item(key = "acao-catalogo") {
                    Spacer(Modifier.height(AureaDims.S5))
                    Text(
                        stringResource(R.string.effect_abra_projeto_aplicar_este_efeito),
                        style = AureaType.CardSpec,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }
}

/** Peso do efeito no celular, em palavra (o número relativo fica interno). */
private fun costLabel(cost: Int): String = when {
    cost >= 3 -> "Pesado"
    cost == 2 -> "Médio"
    else -> "Leve"
}

/**
 * O que o controle faz, em linguagem de edição: "Empurrar, Puxar, Torcer…",
 * "0 a 100 %", "Cor", "Liga/desliga", "Um ponto na tela".
 */
private fun paramSummary(p: com.aurea.aurea.engine.EffectParam): String {
    if (p.enumLabels.isNotEmpty()) {
        val shown = p.enumLabels.take(3).joinToString(", ")
        return if (p.enumLabels.size > 3) "$shown…" else shown
    }
    return when (p.type) {
        com.aurea.aurea.engine.ParamType.COLOR -> "Cor"
        com.aurea.aurea.engine.ParamType.BOOL -> "Liga/desliga"
        com.aurea.aurea.engine.ParamType.POINT2D -> "Um ponto na tela"
        else -> {
            val unit = if (p.unit.isNotEmpty()) " ${p.unit}" else ""
            if (p.min.isFinite() && p.max.isFinite() && p.max > p.min) "${trimNumber(p.min)} a ${trimNumber(p.max)}$unit" else unit.trim()
        }
    }
}

private fun trimNumber(v: Float): String =
    if (v == v.toInt().toFloat()) v.toInt().toString() else String.format(java.util.Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.')
