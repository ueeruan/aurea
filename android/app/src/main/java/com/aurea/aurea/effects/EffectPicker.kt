package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectSearchText
import com.aurea.aurea.editor.panels.englishEffectName
import com.aurea.aurea.editor.panels.normalizeSearch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

// =============================================================================
//  A TELA "ADICIONAR EFEITO" (redesenho 2026-10-01, referência de layout: a tela
//  de mesmo nome de um editor de vídeo móvel conhecido; cores e medidas do Aurea).
//
//  Folha de tela cheia, de cima para baixo:
//   · barra: ✕ fecha · "Adicionar efeito" · 🔍 abre a busca em tela cheia;
//   · DESTAQUES: faixa que rola de lado com cartões grandes de prévia;
//   · RECENTES (e FAVORITOS): grade de miniaturas, 4 por linha;
//   · CATEGORIAS: ladrilhos 2 por linha com a prévia de um efeito do grupo,
//     escurecida, e o nome em branco no meio. "Todos os efeitos" vem primeiro.
//  Tocar num ladrilho abre a grade daquele grupo (‹ volta). UM toque num cartão
//  adiciona o efeito e abre os controles dele; SEGURAR põe ou tira dos
//  favoritos. As ferramentas-efeito (legendas, rastreio de câmera, máscara)
//  são cartões como os outros: o toque abre a ferramenta. A lógica (grupos,
//  busca, áudio, recentes) mora em EffectPickerLogic.kt.
// =============================================================================

/** Tamanho em que a prévia é gerada (o mesmo do cache em disco de antes). */
private const val PREVIEW_W = 320
private const val PREVIEW_H = 200

/** Largura mínima de uma coluna da grade de um grupo (3 colunas num celular de 360 dp). */
private val CardMin = 100.dp
/** Largura dos cartões grandes dos Destaques. */
private val FeaturedCard = 148.dp
/** Quantas miniaturas de Recentes/Favoritos (duas linhas de 4). */
private const val SMALL_ROWS_MAX = 8
/** Id do ladrilho "Todos os efeitos" (`effects.category.all`). */
private const val ALL_GROUP = ALL_CATEGORIES_ID

/**
 * A folha. [onPick] recebe o EFEITO tocado (quem chama adiciona e fecha);
 * [onTool] a FERRAMENTA tocada (quem chama fecha e abre a ferramenta). Os
 * recentes são gravados aqui, para os dois casos.
 */
@Composable
internal fun EffectAddSheet(
    store: EditorStore,
    layerHasAudio: Boolean,
    onPick: (EffectCatalogEntry) -> Unit,
    onTool: (EffectTool) -> Unit,
    onDismiss: () -> Unit,
) {
    var groupId by rememberSaveable { mutableStateOf<String?>(null) }
    Dialog(
        onDismissRequest = { if (groupId != null) groupId = null else onDismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        EffectAddSheetBody(store, layerHasAudio, groupId, { groupId = it }, onPick, onTool, onDismiss)
    }
}

@Composable
private fun EffectAddSheetBody(
    store: EditorStore,
    layerHasAudio: Boolean,
    groupId: String?,
    onGroup: (String?) -> Unit,
    onPick: (EffectCatalogEntry) -> Unit,
    onTool: (EffectTool) -> Unit,
    onDismiss: () -> Unit,
) {
    val prefs = store.effectPrefs
    val catalog = store.catalog
    // `detail` muda a cada quadro da reprodução: a folha só quer o TIPO da camada.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val text3dLayer = store.text3d != null
    val toolNames = EffectTool.entries.associateWith { stringResource(toolLabelRes(it)) }
    val sorted = remember(catalog, layerHasAudio, kind, text3dLayer, toolNames) {
        val textLayer = kind == 4
        val effects = pickableEffects(catalog, layerHasAudio).filter {
            (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.transform")) &&
                (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator"))
        }
        val all = effects + pickableTools(kind).map { toolCatalogEntry(it, toolNames.getValue(it)) }
        arrangeCatalog(all, effectCategories(all))
    }
    val groups = remember(sorted) { groupEntries(sorted) }
    // Grupo que sumiu (camada sem som e era "Áudio") volta para a tela inicial.
    val group = groups.firstOrNull { it.first.id == groupId }
    val showingAll = groupId == ALL_GROUP
    var searching by remember { mutableStateOf(false) }
    val recents = remember(prefs.recents, sorted) { recentEffects(prefs.recents, sorted) }
    val favorites = remember(prefs.favorites, sorted) { favoriteEffects(prefs.favorites, sorted) }
    val featured = remember(sorted) { featuredEntries(sorted) }
    val onFavorite = rememberFavoriteToggle(store)
    val pick: (EffectCatalogEntry) -> Unit = { e ->
        prefs.addRecent(e.typeId)
        val tool = effectToolOf(e.typeId)
        if (tool != null) onTool(tool) else onPick(e)
    }
    val grid = rememberLazyGridState()
    LaunchedEffect(groupId) { grid.scrollToItem(0) }

    Column(
        Modifier
            .fillMaxSize()
            .background(AureaColors.EditorPanel)
            .windowInsetsPadding(WindowInsets.safeDrawing),
    ) {
        val title = when {
            showingAll -> stringResource(R.string.effects_all_effects)
            group != null -> stringResource(effectGroupLabelRes(group.first))
            else -> stringResource(R.string.panel_adicionar_efeito)
        }
        SheetTopBar(
            title = title,
            inGroup = showingAll || group != null,
            onBack = { onGroup(null) },
            onClose = onDismiss,
            onSearch = { searching = true },
        )
        if (showingAll || group != null) {
            val shown = if (showingAll) sorted else group!!.second
            LazyVerticalGrid(
                columns = GridCells.Adaptive(CardMin),
                state = grid,
                contentPadding = PaddingValues(start = AureaDims.S3, end = AureaDims.S3, top = AureaDims.S2, bottom = AureaDims.S5),
                horizontalArrangement = Arrangement.spacedBy(AureaDims.S2),
                verticalArrangement = Arrangement.spacedBy(AureaDims.S3),
                modifier = Modifier.fillMaxWidth().weight(1f).testTag("effects.grid"),
            ) {
                if (shown.isEmpty()) {
                    item(key = "vazio", span = { GridItemSpan(maxLineSpan) }) { EmptyLine(stringResource(R.string.effect_empty_category)) }
                }
                items(shown, key = { it.typeId }, contentType = { "efeito" }) { e ->
                    EffectPickCard(e, store.effectPreviews, e.typeId in prefs.favorites, "effects.card.", pick, onFavorite)
                }
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(88.dp),
                state = grid,
                contentPadding = PaddingValues(start = AureaDims.S3, end = AureaDims.S3, top = AureaDims.S1, bottom = AureaDims.S5),
                horizontalArrangement = Arrangement.spacedBy(AureaDims.S2),
                verticalArrangement = Arrangement.spacedBy(AureaDims.S2),
                modifier = Modifier.fillMaxWidth().weight(1f).testTag("effects.home"),
            ) {
                if (featured.isNotEmpty()) {
                    sectionTitle("t.destaques", R.string.fxui_featured)
                    item(key = "destaques", span = { GridItemSpan(maxLineSpan) }) {
                        LazyRow(horizontalArrangement = Arrangement.spacedBy(AureaDims.S3)) {
                            items(featured, key = { it.typeId }) { e ->
                                EffectPickCard(
                                    e, store.effectPreviews, e.typeId in prefs.favorites, "effects.featured.", pick, onFavorite,
                                    Modifier.width(FeaturedCard), aspect = 1f, nameSize = 13f,
                                )
                            }
                        }
                    }
                }
                if (recents.isNotEmpty()) {
                    sectionTitle("t.recentes", R.string.effect_recentes)
                    items(recents.take(SMALL_ROWS_MAX), key = { "r" + it.typeId }) { e ->
                        EffectPickCard(e, store.effectPreviews, e.typeId in prefs.favorites, "effects.recent.", pick, onFavorite, aspect = 1f, nameSize = 11f)
                    }
                }
                if (favorites.isNotEmpty()) {
                    sectionTitle("t.favoritos", R.string.effect_favoritos)
                    items(favorites.take(SMALL_ROWS_MAX), key = { "f" + it.typeId }) { e ->
                        EffectPickCard(e, store.effectPreviews, true, "effects.favorite.", pick, onFavorite, aspect = 1f, nameSize = 11f)
                    }
                } else {
                    item(key = "dica", span = { GridItemSpan(maxLineSpan) }) {
                        Text(stringResource(R.string.effects_favorites_hint), style = AureaType.CardSpec, modifier = Modifier.padding(top = AureaDims.S1))
                    }
                }
                sectionTitle("t.categorias", R.string.fxui_categories)
                item(key = "g.all", span = { GridItemSpan(2) }) {
                    GroupTile(
                        label = stringResource(R.string.effects_all_effects),
                        id = ALL_GROUP,
                        banner = featured.firstOrNull { effectToolOf(it.typeId) == null },
                        glyph = CupertinoGlyph.SquareGrid2x2,
                        previews = store.effectPreviews,
                        onClick = { onGroup(ALL_GROUP) },
                    )
                }
                items(groups, key = { "g." + it.first.id }, span = { GridItemSpan(2) }) { (g, entries) ->
                    GroupTile(
                        label = stringResource(effectGroupLabelRes(g)),
                        id = g.id,
                        banner = groupBannerEntry(g, entries),
                        glyph = effectGroupGlyph(g),
                        previews = store.effectPreviews,
                        onClick = { onGroup(g.id) },
                    )
                }
            }
        }
    }

    if (searching) {
        EffectSearchSheet(
            store = store,
            sorted = sorted,
            favorites = prefs.favorites,
            onPick = {
                searching = false
                pick(it)
            },
            onFavorite = onFavorite,
            onDismiss = { searching = false },
        )
    }
}

private fun LazyGridScope.sectionTitle(key: String, res: Int) {
    item(key = key, span = { GridItemSpan(maxLineSpan) }) {
        Text(
            stringResource(res),
            style = AureaType.Section,
            modifier = Modifier.padding(top = AureaDims.S3, bottom = AureaDims.S1).semantics { heading() },
        )
    }
}

/**
 * A barra da folha: ✕ (ou ‹ dentro de um grupo) · título · 🔍. Alvos de 44 dp.
 * testTags `effects.close`, `effects.back` e `effects.search` (iguais no iOS).
 */
@Composable
private fun SheetTopBar(title: String, inGroup: Boolean, onBack: () -> Unit, onClose: () -> Unit, onSearch: () -> Unit) {
    val closeLabel = stringResource(R.string.common_close)
    val backLabel = stringResource(R.string.fxui_back_categories)
    val searchLabel = stringResource(R.string.fxui_search_effects)
    Row(
        Modifier.fillMaxWidth().height(AureaDims.EditorTopBar + AureaDims.S2).padding(horizontal = AureaDims.S1),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(AureaDims.MinTap + AureaDims.S1, AureaDims.MinTap)
                .testTag(if (inGroup) "effects.back" else "effects.close")
                .semantics { contentDescription = if (inGroup) backLabel else closeLabel; role = Role.Button }
                .tocavel(shrink = 1f, onClick = if (inGroup) onBack else onClose),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(if (inGroup) CupertinoGlyph.ChevronBack else CupertinoGlyph.Xmark, AureaDims.IconLg, AureaColors.Text)
        }
        Text(
            title,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.of(18f, FontWeight.W700),
            modifier = Modifier.weight(1f).padding(horizontal = AureaDims.S1).semantics { heading() },
        )
        Box(
            Modifier
                .size(AureaDims.MinTap + AureaDims.S1, AureaDims.MinTap)
                .testTag("effects.search")
                .semantics { contentDescription = searchLabel; role = Role.Button }
                .tocavel(shrink = 1f, onClick = onSearch),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Search, AureaDims.IconLg, AureaColors.Text)
        }
    }
}

/**
 * O LADRILHO de um grupo: a prévia real de um efeito dele, escurecida, e o nome
 * em branco, em negrito, no meio. Sem prévia (Texto, 3D, Áudio), a cartela com
 * o glifo do grupo. testTag `effects.category.<id>`.
 */
@Composable
private fun GroupTile(
    label: String,
    id: String,
    banner: EffectCatalogEntry?,
    glyph: Char,
    previews: EffectPreviewStore?,
    onClick: () -> Unit,
) {
    val a11y = stringResource(R.string.fxui_a11y_category, label)
    Box(
        Modifier
            .fillMaxWidth()
            .aspectRatio(2.2f)
            .clip(AureaShape.Card)
            .testTag("effects.category.$id")
            .semantics { contentDescription = a11y; role = Role.Button }
            .tocavel(shrink = 0.97f, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        val preview = if (banner != null) rememberEffectPreview(previews, banner.typeId, PREVIEW_W, PREVIEW_H) else null
        if (preview != null) {
            Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
        } else {
            ToolPlate(glyph, glyphSize = 30.dp, alpha = 0.35f)
        }
        Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.52f)))
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            style = AureaType.of(15f, FontWeight.W800, color = Color.White, lineHeight = 1.15f),
            modifier = Modifier.padding(horizontal = AureaDims.S2),
        )
    }
}

/** Cartela das ferramentas (e dos grupos sem prévia): degradê do Aurea e o glifo no meio. */
@Composable
private fun ToolPlate(glyph: Char, glyphSize: Dp = 26.dp, alpha: Float = 1f) {
    Box(
        Modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(AureaColors.ActionDim, AureaColors.SurfaceHigh))),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, glyphSize, AureaColors.Accent.copy(alpha = alpha))
    }
}

/** Segurar o cartão: favorita ou desfavorita, e diz o que fez (o gesto não se vê). */
@Composable
private fun rememberFavoriteToggle(store: EditorStore): (EffectCatalogEntry, String) -> Unit {
    val context = LocalContext.current
    return remember(store, context) {
        { e, name ->
            val on = store.effectPrefs.toggleFavorite(e.typeId)
            store.showToast(context.getString(if (on) R.string.effects_favorite_added else R.string.effects_favorite_removed, name))
        }
    }
}

@Composable
private fun EmptyLine(text: String) {
    Text(
        text,
        textAlign = TextAlign.Center,
        style = AureaType.of(13f, color = AureaColors.Muted, lineHeight = 1.4f),
        modifier = Modifier.fillMaxWidth().padding(AureaDims.S4),
    )
}

/**
 * O CARTÃO: a prévia real do efeito (ou a cartela enquanto ela não chega; a
 * ferramenta tem a cartela dela), a estrela quando é favorito e o nome em até
 * duas linhas. Um toque adiciona; segurar favorita. Todo o cartão é o alvo.
 */
@Composable
private fun EffectPickCard(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    favorite: Boolean,
    tagPrefix: String,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
    modifier: Modifier = Modifier,
    aspect: Float = 1.6f,
    nameSize: Float = 12f,
) {
    val name = pickerEntryName(entry)
    val tool = effectToolOf(entry.typeId)
    val addLabel = stringResource(R.string.effects_card_add, name)
    val favoriteLabel = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
    Column(
        modifier
            .testTag(tagPrefix + effectCardId(entry.typeId))
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(favoriteLabel) { onFavorite(entry, name); true })
            }
            .tocavel(shrink = 0.96f, haptic = true, onLongClick = { onFavorite(entry, name) }, onClick = { onPick(entry) }),
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(aspect).clip(AureaShape.Card)) {
            if (tool != null) {
                ToolPlate(toolGlyph(tool))
            } else {
                val preview = rememberEffectPreview(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
                if (preview != null) {
                    Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
                } else {
                    GenericPlate(categoryGlyph(entry.category))
                }
            }
            if (favorite) {
                Box(
                    Modifier
                        .align(Alignment.TopEnd)
                        .padding(AureaDims.S1)
                        .size(22.dp)
                        .clip(AureaShape.Circle)
                        .background(Color.Black.copy(alpha = 0.45f)),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.StarFill, AureaDims.IconXs, AureaColors.Accent)
                }
            }
        }
        Spacer(Modifier.height(AureaDims.S1))
        Text(
            name,
            maxLines = 2,
            minLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.of(nameSize, FontWeight.W600, lineHeight = 1.2f),
        )
    }
}

/**
 * A FOLHA DE BUSCA: tela cheia acima do teclado, com o campo já focado
 * (testTag `effects.search.field`) e a grade filtrando a cada letra. Um toque no
 * resultado (`effects.result.<id>`) adiciona e fecha.
 */
@Composable
private fun EffectSearchSheet(
    store: EditorStore,
    sorted: List<EffectCatalogEntry>,
    favorites: Set<Int>,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
    onDismiss: () -> Unit,
) {
    // O índice é montado aqui, fora do escopo que lê a busca: digitar não o refaz.
    val docs = rememberEffectSearchIndex(sorted)
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        SearchSheetBody(store, sorted, docs, favorites, onPick, onFavorite, onDismiss)
    }
}

@Composable
private fun SearchSheetBody(
    store: EditorStore,
    sorted: List<EffectCatalogEntry>,
    docs: Map<Int, EffectSearchDoc>,
    favorites: Set<Int>,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
    onDismiss: () -> Unit,
) {
    var query by rememberSaveable { mutableStateOf("") }
    val results = remember(sorted, docs, query) { if (query.isBlank()) sorted else searchEffects(sorted, docs, query) }
    val focus = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    LaunchedEffect(Unit) { focus.requestFocus() }
    val back = stringResource(R.string.common_close)
    val clear = stringResource(R.string.home_clear_search)
    Column(
        Modifier
            .fillMaxSize()
            .background(AureaColors.EditorPanel)
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .imePadding(),
    ) {
        Row(
            Modifier.fillMaxWidth().height(AureaDims.EditorTopBar + AureaDims.S2).padding(end = AureaDims.S3),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier.size(AureaDims.MinTap + AureaDims.S1, AureaDims.MinTap).semantics { contentDescription = back }.tocavel(shrink = 1f, onClick = onDismiss),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.ChevronBack, AureaDims.IconLg, AureaColors.Text)
            }
            Row(
                Modifier.weight(1f).height(AureaDims.MinTap).clip(AureaShape.Chip).background(AureaColors.FieldFilled).padding(start = AureaDims.S2),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, AureaDims.IconSm, AureaColors.Muted)
                Spacer(Modifier.width(6.dp))
                Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                    if (query.isEmpty()) {
                        Text(stringResource(R.string.effect_buscar_glitch_vhs_desfoque_cor), style = AureaType.of(14f, color = AureaColors.Muted), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    BasicTextField(
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                        textStyle = AureaType.of(15f),
                        cursorBrush = SolidColor(AureaColors.Accent),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                        keyboardActions = KeyboardActions(onSearch = { keyboard?.hide() }),
                        modifier = Modifier.fillMaxWidth().focusRequester(focus).testTag("effects.search.field"),
                    )
                }
                if (query.isNotEmpty()) {
                    Box(
                        Modifier.size(AureaDims.MinTap).semantics { contentDescription = clear }.tocavel { query = "" },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.XmarkCircleFill, AureaDims.IconSm, AureaColors.Muted)
                    }
                }
            }
        }
        LazyVerticalGrid(
            columns = GridCells.Adaptive(CardMin + AureaDims.S2),
            contentPadding = PaddingValues(start = AureaDims.S3, end = AureaDims.S3, top = AureaDims.S2, bottom = AureaDims.S5),
            horizontalArrangement = Arrangement.spacedBy(AureaDims.S3),
            verticalArrangement = Arrangement.spacedBy(AureaDims.S3),
            modifier = Modifier.fillMaxWidth().weight(1f).testTag("effects.search.results"),
        ) {
            if (results.isEmpty()) {
                item(key = "vazio", span = { GridItemSpan(maxLineSpan) }) { EmptyLine(stringResource(R.string.effect_empty_search)) }
            }
            items(results, key = { it.typeId }, contentType = { "efeito" }) { e ->
                EffectPickCard(e, store.effectPreviews, e.typeId in favorites, "effects.result.", onPick, onFavorite)
            }
        }
    }
}

// --- O índice da busca, nos 7 idiomas ------------------------------------------

/** Os idiomas do app (os 7 catálogos). No APK que não traz um deles, cai no padrão. */
private val AppLanguageTags = listOf("pt-BR", "en", "es", "ru", "hi", "ar", "id")

private fun languageResources(context: Context): List<Resources> = AppLanguageTags.map { tag ->
    val config = Configuration(context.resources.configuration).apply { setLocale(Locale.forLanguageTag(tag)) }
    context.createConfigurationContext(config).resources
}

/** O rótulo traduzido de uma categoria conhecida (as mesmas do `effectCategoryLabel`). */
private fun categoryLabelRes(category: String): Int? = when (normalizeSearch(category)) {
    "distorcer" -> R.string.cat_distort
    "glitch" -> R.string.cat_glitch
    "estilizar" -> R.string.cat_stylize
    "cor" -> R.string.cat_colour
    "luz", "glow e luz" -> R.string.cat_light
    "desfoque" -> R.string.cat_blur
    "ruido" -> R.string.cat_noise
    "nitidez" -> R.string.cat_sharpen
    "tempo" -> R.string.cat_time
    "transicao" -> R.string.cat_transition
    "recorte" -> R.string.cat_cutout
    "gerar" -> R.string.cat_generate
    "utilitario" -> R.string.cat_utility
    "controles de expressao" -> R.string.cat_expr
    "pattern" -> R.string.cat_pattern
    else -> null
}

/**
 * O índice da busca: no idioma do app, tudo o que o catálogo sabe (nome,
 * sinônimos da tabela humana, descrição); nos 7 idiomas, o nome, a categoria e
 * o GRUPO da tela (e o nome das ferramentas); e os sinônimos de categoria.
 * Refeito só quando o catálogo ou o idioma mudam.
 */
@Composable
private fun rememberEffectSearchIndex(entries: List<EffectCatalogEntry>): Map<Int, EffectSearchDoc> {
    val context = LocalContext.current
    val locale = LocalConfiguration.current.locales[0]
    val current = catalogHaystack(entries)
    val human = entries.associate { it.typeId to effectSearchText(it.typeId, it.name, it.category) }
    return remember(entries, locale, current, human) {
        val languages = languageResources(context)
        entries.associate { e ->
            val tool = effectToolOf(e.typeId)
            val names = buildList {
                add(e.name)
                if (tool != null) languages.forEach { add(it.getString(toolLabelRes(tool))) }
                else languages.forEach { add(englishEffectName(e.typeId, e.name, it)) }
            }
            val extra = buildList {
                add(current[e.typeId].orEmpty())
                add(human[e.typeId].orEmpty())
                add(e.category)
                categoryLabelRes(e.category)?.let { res -> languages.forEach { add(it.getString(res)) } }
                val groupRes = effectGroupLabelRes(effectGroupOf(e))
                languages.forEach { add(it.getString(groupRes)) }
                add(categorySynonyms(e.category))
            }
            e.typeId to effectSearchDoc(names, extra)
        }
    }
}
