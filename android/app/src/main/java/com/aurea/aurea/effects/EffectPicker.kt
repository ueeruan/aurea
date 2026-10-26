package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
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
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalLayoutDirection
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
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.TextStyle
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
import kotlinx.coroutines.launch
import java.util.Locale

// =============================================================================
//  O NAVEGADOR DE EFEITOS (0.2.5): ✕ · título · 🔍; destaques em cartões
//  grandes; Recentes/Favoritos em miniaturas; ladrilhos das categorias. A
//  categoria abre uma grade de cartões quadrados. A busca mantém a comparação
//  Antes/Depois do motor. Um toque adiciona; segurar favorita. Ferramentas
//  abrem seus próprios painéis.
// =============================================================================

private const val PREVIEW_W = 320
private const val PREVIEW_H = 200
/** Quanto a animação do cartão segue depois de soltar: um laço de 16 quadros. */
private const val HELD_LINGER_MS = 1100L
/** "Prévias de efeitos" (Ajustes): o navegador inteiro lê daqui, sem passar por cada cartão. */
private val LocalEffectPreviewsOn = androidx.compose.runtime.staticCompositionLocalOf { true }
/** Os destaques são maiores: a prévia deles é gerada com mais pixels. */
private const val FEATURED_W = 512
private const val FEATURED_H = 320
private val CardMin = 152.dp

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
        val effects = pickableEffects(catalog, layerHasAudio, layerKind = kind).filter {
            (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.transform")) &&
                (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator"))
        }
        val all = effects + pickableTools(kind).map { toolCatalogEntry(it, toolNames.getValue(it)) }
        arrangeCatalog(all, effectCategories(all))
    }
    val groups = remember(sorted) { groupEntries(sorted) }
    val group = groups.firstOrNull { it.first.id == groupId }
    var searching by remember { mutableStateOf(false) }
    var managingPlugins by remember { mutableStateOf(false) }
    val recents = remember(prefs.recents, sorted) { recentEffects(prefs.recents, sorted) }
    val favorites = remember(prefs.favorites, sorted) { favoriteEffects(prefs.favorites, sorted) }
    val featured = remember(sorted) { featuredEntries(sorted) }
    val onFavorite = rememberFavoriteToggle(store)
    // 0.3.2: efeito Hyper sem o acesso abre a tela bloqueada (HyperEffectGate).
    var hyperLocked by remember { mutableStateOf(false) }
    val pick: (EffectCatalogEntry) -> Unit = { e ->
        if (HyperEffectGate.locked(e.typeId)) {
            hyperLocked = true
        } else {
            prefs.addRecent(e.typeId)
            val tool = effectToolOf(e.typeId)
            if (tool != null) onTool(tool) else onPick(e)
        }
    }
    Column(Modifier.fillMaxSize().background(AureaColors.EditorPanel).windowInsetsPadding(WindowInsets.safeDrawing)) {
        SheetTopBar(
            title = group?.let { stringResource(effectGroupLabelRes(it.first)) } ?: stringResource(R.string.fx_browser_title),
            inGroup = group != null,
            onBack = { onGroup(null) }, onClose = onDismiss, onSearch = { searching = true },
        )
        CompositionLocalProvider(LocalEffectPreviewsOn provides prefs.previewsEnabled) {
            if (group == null) {
                BrowserHome(store, featured, recents, favorites, groups, prefs.favorites, pick, onFavorite, { managingPlugins = true }) { onGroup(it.id) }
            } else {
                val (previous, next) = adjacentGroups(groups.map { it.first }, group.first)
                GroupGrid(store, group.first, group.second, prefs.favorites, previous, next, pick, onFavorite) { onGroup(it.id) }
            }
        }
    }
    if (searching) EffectSearchSheet(store, sorted, prefs.favorites, { searching = false; pick(it) }, onFavorite, { searching = false })
    if (hyperLocked) com.aurea.aurea.hyper.HyperLockedDialog { hyperLocked = false }
    // 0.3.0: plugins de efeito (instalar, ver, remover); o catálogo relido entra nos grupos acima.
    if (managingPlugins) PluginManagerSheet(store) { managingPlugins = false }
}

/**
 * A TELA INICIAL do navegador, de cima para baixo: DESTAQUES (cartões grandes
 * que rolam para o lado), RECENTES e FAVORITOS (duas páginas de miniaturas, 5
 * por linha, trocadas pelo relógio e pela estrela) e os LADRILHOS das
 * categorias, dois por linha, com a prévia real escurecida e o nome no meio.
 */
@Composable
private fun BrowserHome(
    store: EditorStore,
    featured: List<EffectCatalogEntry>,
    recents: List<EffectCatalogEntry>,
    favorites: List<EffectCatalogEntry>,
    groups: List<Pair<EffectGroup, List<EffectCatalogEntry>>>,
    favoriteIds: Set<Int>,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
    onPlugins: () -> Unit,
    onGroup: (EffectGroup) -> Unit,
) {
    val screen = LocalConfiguration.current.screenWidthDp.dp
    val featuredLabel = stringResource(R.string.fx_browser_featured)
    val categoriesLabel = stringResource(R.string.fx_browser_categories)
    LazyColumn(
        Modifier.fillMaxSize().testTag("effects.home"),
        contentPadding = PaddingValues(top = AureaDims.S2, bottom = AureaDims.S6),
        verticalArrangement = Arrangement.spacedBy(AureaDims.S4),
    ) {
        if (featured.isNotEmpty()) item(key = "destaques") {
            val cardW = ((screen - 44.dp) / 1.7f).coerceIn(150.dp, 300.dp)
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                modifier = Modifier.fillMaxWidth().testTag("effects.featured").semantics { contentDescription = featuredLabel },
            ) {
                items(featured, key = { it.typeId }) { e ->
                    FeaturedEffectCard(e, store.effectPreviews, cardW, e.typeId in favoriteIds, onPick, onFavorite)
                }
            }
        }
        item(key = "rapidos") { QuickPages(store, recents, favorites, favoriteIds, screen, onPick, onFavorite) }
        item(key = "titulo-categorias") {
            Spacer(Modifier.height(0.dp).semantics { contentDescription = categoriesLabel; heading() })
        }
        // A chave da linha leva prefixo: com um plugin instalado o grupo "plugins" pode abrir
        // uma linha e colidir com a porta fixa abaixo ("Key was already used" fechava o app).
        items(groups.chunked(2), key = { row -> groupRowKey(row.map { it.first }) }) { row ->
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                row.forEach { (g, entries) ->
                    GroupTile(g, entries, store.effectPreviews, Modifier.weight(1f)) { onGroup(g) }
                }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
        }
        // 0.3.0: a porta dos plugins fica sempre à vista (a categoria só aparece com um instalado).
        item(key = PLUGIN_DOOR_KEY) {
            Row(
                Modifier.padding(horizontal = 16.dp).fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
                    .testTag("effects.plugins").tocavel(onClick = onPlugins).padding(horizontal = 14.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.SquareStack3dUp, 20.dp, AureaColors.Accent)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.plug_manage), modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
                CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Muted)
            }
        }
    }
}

/** Cartão grande da faixa de destaques: a prévia antes | depois do motor e o nome embaixo. */
@Composable
private fun FeaturedEffectCard(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    width: Dp,
    favorite: Boolean,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
) {
    val name = pickerEntryName(entry)
    val addLabel = stringResource(R.string.effects_card_add, name)
    val favoriteLabel = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
    Column(
        Modifier.width(width).testTag("effects.featured." + effectCardId(entry.typeId))
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(favoriteLabel) { onFavorite(entry, name); true })
            }
            .tocavel(shrink = 0.96f, haptic = true, onLongClick = { onFavorite(entry, name) }, onClick = { onPick(entry) }),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.width(width).aspectRatio(1.6f).clip(RoundedCornerShape(18.dp)).background(AureaColors.EditorPanelHigh)) {
            EffectThumb(entry, previews, FEATURED_W, FEATURED_H, labels = true)
            EffectBadges(entry, favorite)
        }
        Spacer(Modifier.height(AureaDims.S2))
        Text(name, style = AureaType.of(14f, FontWeight.W500), textAlign = TextAlign.Center, maxLines = 2,
            overflow = TextOverflow.Ellipsis, modifier = Modifier.fillMaxWidth())
    }
}

/**
 * RECENTES e FAVORITOS: duas páginas que deslizam, 5 miniaturas por linha
 * (até 2 linhas), e embaixo o relógio e a estrela que dizem (e trocam) a página.
 */
@Composable
private fun QuickPages(
    store: EditorStore,
    recents: List<EffectCatalogEntry>,
    favorites: List<EffectCatalogEntry>,
    favoriteIds: Set<Int>,
    screen: Dp,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
) {
    val pager = rememberPagerState(pageCount = { 2 })
    val scope = rememberCoroutineScope()
    val tile = ((screen - 32.dp - 40.dp) / 5).coerceIn(52.dp, 96.dp)
    val rows = maxOf(1, minOf(2, (maxOf(recents.size, favorites.size) + 4) / 5))
    val pageHeight = (tile + 34.dp) * rows + 10.dp * (rows - 1)
    Column(Modifier.fillMaxWidth().testTag("effects.quick")) {
        HorizontalPager(pager, Modifier.fillMaxWidth().height(pageHeight), key = { it }) { page ->
            val shown = (if (page == 0) recents else favorites).take(QUICK_PAGE_SIZE)
            if (shown.isEmpty()) {
                Box(Modifier.fillMaxSize().padding(horizontal = 24.dp), contentAlignment = Alignment.Center) {
                    Text(stringResource(if (page == 0) R.string.fx_browser_recent_empty else R.string.fx_browser_favorites_empty),
                        style = AureaType.of(13f, color = AureaColors.Muted), textAlign = TextAlign.Center)
                }
            } else {
                Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    shown.chunked(5).forEach { row ->
                        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            row.forEach { e ->
                                QuickTile(e, store.effectPreviews, tile, e.typeId in favoriteIds,
                                    if (page == 0) "effects.recent." else "effects.favorite.", onPick, onFavorite)
                            }
                        }
                    }
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(top = AureaDims.S1), horizontalArrangement = Arrangement.Center) {
            listOf(
                Triple(CupertinoGlyph.Clock, R.string.fx_browser_page_recent, "effects.quick.recent"),
                Triple(CupertinoGlyph.StarFill, R.string.fx_browser_page_favorites, "effects.quick.favorites"),
            ).forEachIndexed { index, (glyph, label, tag) ->
                val active = pager.currentPage == index
                val description = stringResource(label)
                Box(
                    Modifier.size(AureaDims.MinTap).testTag(tag)
                        .semantics { contentDescription = description; selected = active; role = Role.Tab }
                        .tocavel(shrink = 1f, role = null, onClick = { scope.launch { pager.animateScrollToPage(index) } }),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(glyph, 16.dp, if (active) AureaColors.Accent else AureaColors.Muted.copy(alpha = 0.6f))
                }
            }
        }
    }
}

/** Miniatura de Recentes/Favoritos: quadradinho com a prévia e o nome numa linha. */
@Composable
private fun QuickTile(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    size: Dp,
    favorite: Boolean,
    tagPrefix: String,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
) {
    val name = pickerEntryName(entry)
    val addLabel = stringResource(R.string.effects_card_add, name)
    val favoriteLabel = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
    Column(
        Modifier.width(size).testTag(tagPrefix + effectCardId(entry.typeId))
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(favoriteLabel) { onFavorite(entry, name); true })
            }
            .tocavel(shrink = 0.94f, haptic = true, onLongClick = { onFavorite(entry, name) }, onClick = { onPick(entry) }),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.size(size).clip(RoundedCornerShape(12.dp)).background(AureaColors.EditorPanelHigh)) {
            EffectThumb(entry, previews, PREVIEW_W, PREVIEW_H)
            if (effectCost(entry.effectClass) >= 3) HeavyBadge(Modifier.align(Alignment.TopEnd).padding(4.dp))
            if (HyperEffectGate.locked(entry.typeId)) HyperLockBadge(Modifier.align(Alignment.TopStart).padding(4.dp))
        }
        Text(name, style = AureaType.of(11.5f), textAlign = TextAlign.Center, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth().padding(top = 6.dp))
    }
}

/**
 * O LADRILHO de uma categoria: a prévia real de um efeito dela, escurecida, e o
 * nome em branco, em negrito, no meio (`effects.category.<id>`). Grupo sem
 * prévia (Texto, Áudio, Utilitário) usa a cartela com o glifo do grupo.
 */
@Composable
private fun GroupTile(
    group: EffectGroup,
    entries: List<EffectCatalogEntry>,
    previews: EffectPreviewStore?,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val label = stringResource(effectGroupLabelRes(group))
    val description = stringResource(R.string.fx_browser_open_group, label)
    val banner = remember(group, entries) { groupBannerEntry(group, entries) }
    Box(
        modifier.height(78.dp).clip(RoundedCornerShape(16.dp)).background(AureaColors.EditorPanelHigh)
            .testTag("effects.category." + group.id)
            .semantics { contentDescription = description; role = Role.Button }
            .tocavel(shrink = 0.97f, role = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (banner != null) EffectThumb(banner, previews, PREVIEW_W, PREVIEW_H, comparison = false) else ToolPlate(effectGroupGlyph(group), alpha = 0.35f)
        Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.Black.copy(alpha = 0.38f), Color.Black.copy(alpha = 0.58f)))))
        Text(
            label,
            style = AureaType.of(17f, FontWeight.W800, color = Color.White).copy(shadow = Shadow(Color.Black.copy(alpha = 0.6f), blurRadius = 8f)),
            textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 10.dp),
        )
    }
}

/**
 * Dentro de uma categoria: a grade de cartões com a prévia ANTES | DEPOIS do
 * motor, o nome e a categoria (o cartão de sempre, dois por linha no celular)
 * e, no fim, os atalhos para a categoria anterior e a próxima.
 */
@Composable
private fun GroupGrid(
    store: EditorStore,
    group: EffectGroup,
    entries: List<EffectCatalogEntry>,
    favoriteIds: Set<Int>,
    previous: EffectGroup?,
    next: EffectGroup?,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
    onGroup: (EffectGroup) -> Unit,
) {
    val grid = rememberLazyGridState()
    LaunchedEffect(group) { grid.scrollToItem(0) }
    val previewsOn = LocalEffectPreviewsOn.current
    LazyVerticalGrid(
        columns = GridCells.Adaptive(CardMin + AureaDims.S2), state = grid,
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = AureaDims.S2, bottom = AureaDims.S6),
        horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize().testTag("effects.grid"),
    ) {
        if (entries.isEmpty()) item(key = "empty", span = { GridItemSpan(maxLineSpan) }) {
            EmptyLine(stringResource(R.string.effect_empty_category))
        }
        items(entries, key = { it.typeId }, contentType = { "effect" }) { e ->
            EffectPickCard(e, store.effectPreviews, e.typeId in favoriteIds, "effects.card.", onPick, onFavorite, nameSize = 14f,
                previewsOn = previewsOn)
        }
        if (previous != null || next != null) item(key = "vizinhos", span = { GridItemSpan(maxLineSpan) }) {
            Row(Modifier.fillMaxWidth().padding(top = AureaDims.S2), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                NeighborButton(previous, forward = false, Modifier.weight(1f), onGroup)
                NeighborButton(next, forward = true, Modifier.weight(1f), onGroup)
            }
        }
    }
}

@Composable
private fun NeighborButton(group: EffectGroup?, forward: Boolean, modifier: Modifier, onGroup: (EffectGroup) -> Unit) {
    if (group == null) { Spacer(modifier); return }
    val label = stringResource(effectGroupLabelRes(group))
    val description = stringResource(if (forward) R.string.fx_browser_next_group else R.string.fx_browser_prev_group, label)
    Row(
        modifier.heightIn(min = AureaDims.MinTap).clip(AureaShape.Chip).background(AureaColors.SurfaceHigh)
            .testTag(if (forward) "effects.group.next" else "effects.group.previous")
            .semantics { contentDescription = description }
            .tocavel(shrink = 1f, onClick = { onGroup(group) }).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = if (forward) Arrangement.End else Arrangement.Start,
    ) {
        if (!forward) CupertinoIcon(CupertinoGlyph.ChevronLeft, AureaDims.IconSm, AureaColors.Muted)
        Text(label, style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Muted), maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 6.dp).weight(1f, fill = false))
        if (forward) CupertinoIcon(CupertinoGlyph.ChevronRight, AureaDims.IconSm, AureaColors.Muted)
    }
}

/** Selos do cartão: "Novo" no canto de cima, estrela do favorito e o "!" do efeito pesado. */
@Composable
private fun BoxScope.EffectBadges(entry: EffectCatalogEntry, favorite: Boolean) {
    if (entry.isNew) {
        Text(stringResource(R.string.fx_browser_badge), style = AureaType.of(10.5f, FontWeight.W700, color = AureaColors.EditorPanel),
            modifier = Modifier.align(Alignment.TopStart).padding(6.dp).clip(AureaShape.Chip).background(AureaColors.Accent)
                .padding(horizontal = 6.dp, vertical = 2.dp))
    }
    Row(Modifier.align(Alignment.TopEnd).padding(5.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        if (HyperEffectGate.locked(entry.typeId)) HyperLockBadge()
        if (effectCost(entry.effectClass) >= 3) HeavyBadge()
        if (favorite) {
            Box(Modifier.size(20.dp).clip(AureaShape.Circle).background(Color.Black.copy(alpha = 0.5f)), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.StarFill, AureaDims.IconXs, AureaColors.Accent)
            }
        }
    }
}

/** O "!" de efeito pesado (temporal ou global): avisa antes de pôr no projeto. */
@Composable
private fun HeavyBadge(modifier: Modifier = Modifier) {
    val label = stringResource(R.string.fx_browser_heavy)
    Box(
        modifier.size(20.dp).clip(AureaShape.Circle).background(Color.White).semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Text("!", style = AureaType.of(13f, FontWeight.W900, color = Color.Black))
    }
}

/**
 * A prévia do motor no quadro: a COMPARAÇÃO inteira, antes | depois com o traço
 * no meio (cortada pelo centro quando o quadro é mais estreito), com os selos
 * "Antes" e "Depois" se [labels]; ou, com [comparison] desligado, só a metade
 * "depois" (o fundo do ladrilho da categoria). Ferramenta mostra a cartela
 * dela; enquanto a prévia não chega, a cartela genérica da categoria.
 */
@Composable
private fun EffectThumb(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    width: Int,
    height: Int,
    comparison: Boolean = true,
    labels: Boolean = false,
) {
    val tool = effectToolOf(entry.typeId)
    if (tool != null) { ToolPlate(toolGlyph(tool)); return }
    // "Prévias de efeitos" desligado em Ajustes (0.2.4): só a cartela, sem pedir render.
    val image = rememberEffectPreviewState(previews?.takeIf { LocalEffectPreviewsOn.current }, entry.typeId, width, height).image
    if (image == null) { GenericPlate(categoryGlyph(entry.category)); return }
    androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
        if (size.width < 1f || size.height < 1f) return@Canvas
        val crop = if (comparison) centerCrop(image.width, image.height, size.width / size.height)
            else effectOnlyCrop(image.width, image.height, size.width / size.height)
        drawImage(
            image,
            srcOffset = IntOffset(crop[0], crop[1]), srcSize = IntSize(crop[2], crop[3]),
            dstSize = IntSize(size.width.toInt(), size.height.toInt()), filterQuality = FilterQuality.Medium,
        )
    }
    if (labels) {
        CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
            Box(Modifier.fillMaxSize()) {
                Row(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    PreviewLabel(stringResource(R.string.fx_browser_before))
                    PreviewLabel(stringResource(R.string.fx_browser_after))
                }
            }
        }
    }
}

/** Corte pelo centro da imagem inteira para o aspecto [dstAspect]: [x, y, largura, altura]. */
private fun centerCrop(width: Int, height: Int, dstAspect: Float): IntArray {
    if (width <= 0 || height <= 0 || !(dstAspect > 0f)) return intArrayOf(0, 0, maxOf(width, 1), maxOf(height, 1))
    val src = width.toFloat() / height
    return if (src > dstAspect) {
        val w = (height * dstAspect).toInt().coerceIn(1, width)
        intArrayOf((width - w) / 2, 0, w, height)
    } else {
        val h = (width / dstAspect).toInt().coerceIn(1, height)
        intArrayOf(0, (height - h) / 2, width, h)
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
            textAlign = TextAlign.Center,
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
    nameSize: Float = 14f,
    /** "Prévias de efeitos" desligado em Ajustes: só a cartela, sem pedir render. */
    previewsOn: Boolean = true,
) {
    val name = pickerEntryName(entry)
    val tool = effectToolOf(entry.typeId)
    val addLabel = stringResource(R.string.effects_card_add, name)
    // SEGURADO: toca a animação que a ficha já gerou (cena Foto, a mesma da
    // grade). A grade nunca pede sequência ao motor; sem tira guardada, nada
    // muda. Desligado por "Prévias de efeitos" e por "Remover animações".
    var held by remember { mutableStateOf(false) }
    // Ao soltar, completa um laço (~1 s) antes de voltar ao quadro parado — o
    // toque longo que favorita termina em 0,45 s, igual no iOS.
    var lingering by remember { mutableStateOf(false) }
    LaunchedEffect(held) {
        if (held) lingering = true else { kotlinx.coroutines.delay(HELD_LINGER_MS); lingering = false }
    }
    val reduceMotion = rememberReduceMotion()
    val heldStore = previews?.animated?.takeIf { (held || lingering) && previewsOn && !reduceMotion && tool == null }
    val heldAnimation = rememberAnimatedEffectPreview(heldStore, entry.typeId, EffectPreviewScene.PHOTO.id, generate = false)
    val favoriteLabel = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
    Column(
        modifier
            .clip(AureaShape.Card).background(AureaColors.EditorPanelHigh)
            .testTag(tagPrefix + effectCardId(entry.typeId))
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(favoriteLabel) { onFavorite(entry, name); true })
            }
            .pointerInput(Unit) {
                // Só observa (passo Initial, sem consumir): o toque, o toque
                // longo e a rolagem da grade continuam com quem já os trata.
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
                    held = true
                    try {
                        do {
                            val event = awaitPointerEvent(PointerEventPass.Initial)
                        } while (event.changes.any { it.pressed })
                    } finally {
                        held = false
                    }
                }
            }
            .tocavel(shrink = 0.96f, haptic = true, onLongClick = { onFavorite(entry, name) }, onClick = { onPick(entry) }),
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(aspect).clip(AureaShape.Card)) {
            if (tool != null) {
                ToolPlate(toolGlyph(tool))
            } else if (!previewsOn) {
                GenericPlate(categoryGlyph(entry.category))
            } else if (heldAnimation.frames.isNotEmpty()) {
                AnimatedPreviewFrames(heldAnimation.frames, playing = true, contentScale = ContentScale.Fit)
                CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
                    Row(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                        PreviewLabel(stringResource(R.string.fx_browser_before))
                        PreviewLabel(stringResource(R.string.fx_browser_after))
                    }
                }
            } else {
                val state = rememberEffectPreviewState(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
                val preview = state.image
                if (preview != null) {
                    Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Fit, filterQuality = FilterQuality.Medium)
                    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
                        Row(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                            PreviewLabel(stringResource(R.string.fx_browser_before))
                            PreviewLabel(stringResource(R.string.fx_browser_after))
                        }
                    }
                } else {
                    GenericPlate(categoryGlyph(entry.category))
                    Text(stringResource(when {
                        entry.effectClass == 3 || entry.effectClass == 4 -> R.string.fx_browser_timeline_preview
                        state.loading -> R.string.fx_browser_loading
                        else -> R.string.fx_browser_preview_unavailable
                    }), style = AureaType.of(12f, color = AureaColors.Text), textAlign = TextAlign.Center,
                        modifier = Modifier.align(Alignment.Center).padding(8.dp))
                }
            }
            if (entry.isNew) {
                Text(stringResource(R.string.fx_browser_badge), style = AureaType.of(11f, FontWeight.W700, color = AureaColors.EditorPanel),
                    modifier = Modifier.align(Alignment.TopStart).padding(6.dp).clip(AureaShape.Chip).background(AureaColors.Accent).padding(horizontal = 6.dp, vertical = 3.dp))
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
            if (HyperEffectGate.locked(entry.typeId)) HyperLockBadge(Modifier.align(Alignment.TopEnd).padding(top = if (favorite) 30.dp else AureaDims.S1, end = AureaDims.S1))
        }
        Spacer(Modifier.height(AureaDims.S1))
        Text(
            name,
            maxLines = 3,
            minLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.of(nameSize, FontWeight.W600, lineHeight = 1.2f),
            modifier = Modifier.padding(horizontal = 10.dp),
        )
        Text(stringResource(effectGroupLabelRes(effectGroupOf(entry))), style = AureaType.of(12f, color = AureaColors.Muted),
            maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 10.dp, end = 10.dp, top = 4.dp, bottom = 10.dp))
    }
}

@Composable
private fun PreviewLabel(text: String) {
    Text(text, style = AureaType.of(10.5f, FontWeight.W600, color = Color.White),
        modifier = Modifier.clip(AureaShape.Chip).background(Color.Black.copy(alpha = 0.75f)).padding(horizontal = 5.dp, vertical = 2.dp))
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
                Modifier.size(AureaDims.MinTap + AureaDims.S1, AureaDims.MinTap).testTag("effects.search.back").semantics { contentDescription = back }.tocavel(shrink = 1f, onClick = onDismiss),
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
                EffectPickCard(e, store.effectPreviews, e.typeId in favorites, "effects.result.", onPick, onFavorite,
                    previewsOn = store.effectPrefs.previewsEnabled)
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
                // Ladrilho / repetição de borda: "motion tile", "mosaico"... nos 7 idiomas.
                languages.forEach { add(com.aurea.aurea.editor.panels.tileSearchTerms(e.typeId, it)) }
            }
            e.typeId to effectSearchDoc(names, extra)
        }
    }
}
