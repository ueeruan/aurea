package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.foundation.LocalOverscrollFactory
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.LazyRow






import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
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
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue


import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
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
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

// Catálogo móvel: toque para aplicar; opções preservam a ficha, os favoritos e os presets.

/** Onde o detalhe abriu: a lista que ele pagina e o item tocado. */
private class DetailRequest(val entries: List<EffectCatalogEntry>, val index: Int)

/** A pilha do navegador: categoria aberta, busca (com fichas) e detalhe. */
@Stable
private class BrowserNav {
    var plugins by mutableStateOf(false)
    var groupId by mutableStateOf<String?>(null)
    var searching by mutableStateOf(false)
    var searchChips by mutableStateOf<List<String>>(emptyList())
    var searchEpoch by mutableIntStateOf(0)
    var detail by mutableStateOf<DetailRequest?>(null)
    var detailClose by mutableIntStateOf(0)

    fun openSearch(chips: List<String>) {
        searchChips = chips
        searchEpoch++
        searching = true
    }

    /** O voltar do sistema desempilha: detalhe → busca → categoria → fecha. */
    fun back(onDismiss: () -> Unit) {
        when {
            detail != null -> detailClose++
            searching -> searching = false
            groupId != null -> groupId = null
            else -> onDismiss()
        }
    }
}

/**
 * A folha. [onPick] recebe o EFEITO base escolhido (quem chama adiciona e
 * fecha); [onTool] a FERRAMENTA (quem chama fecha e abre a ferramenta).
 *
 * [onPickVariant] (Omega efeitos): recebe TODA adição, com a variação (índice em
 * `store.effectPresets(typeId)`; −1 = só o efeito); quem chama adiciona e fecha.
 * Nulo: o efeito base vai para [onPick] e a variação a
 * [EditorStore.addEffectWithPreset], e a folha fecha com [onDismiss]. Os
 * recentes são gravados aqui, nos três casos.
 */
@Composable
internal fun EffectAddSheet(
    store: EditorStore,
    layerHasAudio: Boolean,
    onPick: (EffectCatalogEntry) -> Unit,
    onTool: (EffectTool) -> Unit,
    onDismiss: () -> Unit,
    onPickVariant: ((EffectCatalogEntry, Int) -> Unit)? = null,
    /** Sem a folha modal: a galeria desenhada no lugar (a aba Efeitos da biblioteca do Aurea PC). */
    inline: Boolean = false,
) {
    val nav = remember { BrowserNav() }
    val content: @Composable () -> Unit = {
        // The stretch left over at either end of a fling can consume the next tap.
        CompositionLocalProvider(LocalOverscrollFactory provides null) {
            EffectBrowserBody(store, layerHasAudio, nav, onPick, onTool, onPickVariant, onDismiss)
        }
    }
    if (inline) {
        SheetWidthScope(content)
        return
    }
    val maxSize = LocalEffectSheetMaxSize.current
    Dialog(
        onDismissRequest = { nav.back(onDismiss) },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        if (maxSize == null) {
            content()
        } else {
            // Aurea PC: um cartão no meio da janela, não a tela inteira; clicar fora fecha.
            Box(
                Modifier.fillMaxSize().pointerInput(onDismiss) { detectTapGestures { nav.back(onDismiss) } },
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    Modifier.sizeIn(maxWidth = maxSize.width, maxHeight = maxSize.height).fillMaxSize()
                        .clip(RoundedCornerShape(12.dp)).pointerInput(Unit) { detectTapGestures { } },
                ) {
                    SheetWidthScope(content)
                }
            }
        }
    }
}

/**
 * A galeria fora da tela inteira (só no Aurea PC): a grade conta colunas pela
 * largura da "tela", então a tela vira o espaço que a galeria tem de verdade.
 */
@Composable
private fun SheetWidthScope(content: @Composable () -> Unit) {
    androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxSize()) {
        val base = LocalConfiguration.current
        val w = maxWidth.value.toInt()
        val h = maxHeight.value.toInt()
        val scoped = remember(base, w, h) {
            android.content.res.Configuration(base).apply { screenWidthDp = w; screenHeightDp = h; smallestScreenWidthDp = minOf(w, h) }
        }
        androidx.compose.runtime.CompositionLocalProvider(LocalConfiguration provides scoped) { content() }
    }
}

/**
 * Tamanho máximo da galeria de efeitos. Nulo (celular e tablet) = a tela
 * inteira; o Aurea PC fornece um cartão no meio da janela.
 */
val LocalEffectSheetMaxSize = androidx.compose.runtime.staticCompositionLocalOf<androidx.compose.ui.unit.DpSize?> { null }

/**
 * Tocar num efeito dos recentes/favoritos: falso (celular e tablet) = adiciona
 * na hora; verdadeiro (Aurea PC) = abre o detalhe, com a prévia e o adicionar.
 */
val LocalEffectTapOpensDetail = androidx.compose.runtime.staticCompositionLocalOf { false }

@Composable
private fun EffectBrowserBody(
    store: EditorStore,
    layerHasAudio: Boolean,
    nav: BrowserNav,
    onPick: (EffectCatalogEntry) -> Unit,
    onTool: (EffectTool) -> Unit,
    onPickVariant: ((EffectCatalogEntry, Int) -> Unit)?,
    onDismiss: () -> Unit,
) {
    val prefs = store.effectPrefs
    val catalog = store.catalog
    // `detail` muda a cada quadro da reprodução: a folha só quer o TIPO da camada.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val text3dLayer = store.text3d != null
    val toolNames = EffectTool.entries.associateWith { stringResource(toolLabelRes(it)) }
    // Funções do AUREA (13/11/2026): as ferramentas que valem para esta camada.
    val tools = com.aurea.aurea.editor.panels.toolsForSelection(store)
    val sorted = remember(catalog, layerHasAudio, kind, text3dLayer, toolNames, tools) {
        val textLayer = kind == 4
        val effects = pickableEffects(catalog, layerHasAudio, layerKind = kind).filter {
            (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.transform")) &&
                (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.progress")) &&
                (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.randomizer")) &&
                (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.spacing")) &&
                (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator")) &&
                (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator_pro")) &&
                (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.prism"))
        }
        // Bugs 0.3.4 (bug 3): um efeito por typeId (a chave das grades é o typeId).
        val all = distinctEffects(effects + tools.map { toolCatalogEntry(it, toolNames.getValue(it)) })
        arrangeCatalog(all, effectCategories(all))
    }
    val (pluginCatalog, browserEntries) = remember(sorted, store.plugins) {
        sorted.partition { PluginTexts.isPlugin(it.typeId) || effectGroupOf(it) == EffectGroup.Plugins }
    }
    val safeMode = PluginRuntime.safeMode
    val pluginEntries = remember(pluginCatalog, store.plugins, safeMode) {
        val available = store.plugins.filter { it.enabled && it.ok }.map { it.effectTypeId }.toSet()
        if (safeMode) emptyList() else pluginCatalog.filter { it.typeId in available }
    }
    val locale = LocalConfiguration.current.locales[0]
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val english = remember(context, configuration) { englishResources(context) }
    // Dentro da categoria, ordem ALFABÉTICA pelo nome mostrado (como a referência).
    val groups = remember(browserEntries, locale, toolNames, english) {
        groupEntries(browserEntries).map { (group, entries) ->
            group to sortedByName(entries, locale) { entry ->
                effectToolOf(entry.typeId)?.let(toolNames::get) ?: englishEffectName(entry.typeId, entry.name, english)
            }
        }
    }
    val presetCount: (Int) -> Int = remember(store) { { t: Int -> if (effectToolOf(t) != null) 0 else store.effectPresets(t).size } }
    val recents = remember(prefs.recentRefs, browserEntries) { recentItems(prefs.recentRefs, browserEntries, presetCount) }
    val favorites = remember(prefs.favoriteRefs, browserEntries) { favoriteItems(prefs.favoriteRefs, browserEntries, presetCount) }
    val applied = store.effects.map { it.typeId }
    val recommended = remember(browserEntries, kind, applied, layerHasAudio) { recommendedEffects(browserEntries, kind, applied, 6, layerHasAudio) }
    // 0.3.2: efeito pago sem o acesso abre o paywall das assinaturas (HyperEffectGate).
    var hyperLocked by remember { mutableStateOf<EffectCatalogEntry?>(null) }
    val add: (EffectCatalogEntry, Int) -> Unit = { e, preset ->
        if (HyperEffectGate.locked(e.typeId)) {
            hyperLocked = e
        } else {
            val tool = effectToolOf(e.typeId)
            val variant = if (tool != null) -1 else preset
            prefs.addRecent(EffectRef(e.typeId, variant))
            when {
                tool != null -> onTool(tool)
                onPickVariant != null -> onPickVariant(e, variant)
                variant < 0 -> onPick(e)
                else -> { store.addEffectWithPreset(e.typeId, variant); onDismiss() }
            }
        }
    }
    val openDetail: (List<EffectCatalogEntry>, Int) -> Unit = { list, i -> nav.detail = DetailRequest(list, i) }
    val homeState = rememberLazyListState()
    val groupState = key(nav.groupId) { rememberLazyListState() }
    val previewsOn = prefs.previewsEnabled

    Box(Modifier.fillMaxSize().background(AureaColors.FxListSurface)) {
        Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
            val openId = nav.groupId?.takeIf { id -> groups.any { it.first.id == id } }
            val open = groups.firstOrNull { it.first.id == openId }
            val scrolled = if (open == null) homeState.canScrollBackward else groupState.canScrollBackward
            BrowserTopBar(
                title = open?.let { stringResource(effectGroupLabelRes(it.first)) } ?: stringResource(R.string.fx_browser_title),
                inGroup = open != null,
                divider = scrolled,
                onBack = { nav.groupId = null },
                onClose = onDismiss,
                onSearch = { nav.openSearch(emptyList()) },
            )
            if (open == null) BrowserTabs(nav.plugins) { nav.plugins = it }
            if (nav.plugins) {
                val pluginRecents = remember(prefs.recentRefs, pluginEntries) { recentItems(prefs.recentRefs, pluginEntries, presetCount) }
                val pluginFavorites = remember(prefs.favoriteRefs, pluginEntries) { favoriteItems(prefs.favoriteRefs, pluginEntries, presetCount) }
                val perPage = favoritesPerPage(LocalConfiguration.current.screenWidthDp.toFloat())
                val pages = remember(pluginRecents, pluginFavorites, perPage) { quickPages(pluginRecents, pluginFavorites, perPage) }
                PluginManagerPane(
                    store, Modifier.weight(1f), pluginEntries,
                    onOpenEffect = { entry -> add(entry, -1) },
                    shortcuts = if (pages.isEmpty()) null else ({
                        QuickBand(store, pages, perPage, pluginFavorites.isEmpty(), previewsOn, add, openDetail)
                    }),
                )
            } else Box(Modifier.fillMaxWidth().weight(1f)) {
                if (open == null) {
                    BrowserHome(
                        store, homeState, recommended, recents, favorites, groups, previewsOn,
                        onAdd = add, onDetail = openDetail, onGroup = { nav.groupId = it.id },
                    )
                } else {
                    key(open.first) {
                        EffectResults(store, groupState, open.second, previewsOn, add, openDetail)
                    }
                }
            }
        }
        if (nav.searching) {
            key(nav.searchEpoch) {
                SearchPane(
                    store, if (nav.plugins) pluginEntries else browserEntries, nav.searchChips, onChips = { nav.searchChips = it }, previewsOn = previewsOn,
                    onAdd = add, onDetail = openDetail, onClose = { nav.searching = false },
                )
            }
        }
        nav.detail?.let { request ->
            key(request) {
                EffectDetailOverlay(
                    store = store,
                    entries = request.entries,
                    startIndex = request.index,
                    closeSignal = nav.detailClose,
                    onClosed = { if (nav.detail === request) nav.detail = null },
                    variants = true,
                    onAdd = add,
                    onTag = { tag -> nav.detail = null; nav.openSearch(listOf(tag)) },
                )
            }
        }
    }
    hyperLocked?.let { locked ->
        com.aurea.aurea.hyper.PaywallDialog(com.aurea.aurea.hyper.PaidItem.effect(locked.typeId), pickerEntryName(locked)) { hyperLocked = null }
    }
}

@Composable
private fun BrowserTabs(plugins: Boolean, onSelect: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)
            .clip(RoundedCornerShape(16.dp)).background(AureaColors.FxLowSurface).padding(4.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        for (isPluginTab in listOf(false, true)) {
            val active = isPluginTab == plugins
            val color = if (active) AureaColors.Accent else AureaColors.FxTextSecondary
            Row(
                Modifier.weight(1f).heightIn(min = 44.dp).clip(RoundedCornerShape(12.dp))
                    .background(if (active) AureaColors.Accent.copy(alpha = .15f) else AureaColors.FxLowSurface)
                    .testTag(if (isPluginTab) "effects.plugins" else "effects.tab.catalog")
                    .selectable(selected = active, role = Role.Tab) { onSelect(isPluginTab) },
                horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(if (isPluginTab) CupertinoGlyph.SquareStack3dUp else CupertinoGlyph.Sparkles, 18.dp, color)
                Text(
                    stringResource(if (isPluginTab) R.string.fxui_group_plugins else R.string.panel_efeitos),
                    style = AureaType.of(14f, FontWeight.SemiBold, color = color), maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

/**
 * O cabeçalho de 44: ✕ (ou ‹ dentro de uma categoria) · título 17 · 🔍 (some
 * na categoria). Fio de 0,5 embaixo só com o conteúdo rolado. Ids
 * `effects.close`, `effects.back` e `effects.search` (iguais no iOS).
 */
@Composable
private fun BrowserTopBar(title: String, inGroup: Boolean, divider: Boolean, onBack: () -> Unit, onClose: () -> Unit, onSearch: () -> Unit) {
    val closeLabel = stringResource(R.string.common_close)
    val backLabel = stringResource(R.string.fxui_back_categories)
    val searchLabel = stringResource(R.string.fxui_search_effects)
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(AureaDims.MinTap)
                    .testTag(if (inGroup) "effects.back" else "effects.close")
                    .semantics { contentDescription = if (inGroup) backLabel else closeLabel; role = Role.Button }
                    .tocavel(shrink = 1f, role = null, onClick = if (inGroup) onBack else onClose),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(if (inGroup) CupertinoGlyph.ChevronBack else CupertinoGlyph.Xmark, 22.dp, AureaColors.Text)
            }
            Text(
                title,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.of(19f, FontWeight.SemiBold, color = AureaColors.Text),
                modifier = Modifier.weight(1f).padding(horizontal = AureaDims.S1).semantics { heading() },
            )
            Box(
                Modifier.size(AureaDims.MinTap).testTag("effects.search")
                    .semantics { contentDescription = searchLabel; role = Role.Button }
                    .tocavel(shrink = 1f, role = null, onClick = onSearch),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, 22.dp, AureaColors.Accent)
            }
        }
        if (divider) Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.FxLowSurface))
        else Spacer(Modifier.height(0.5.dp))
    }
}

/**
 * A TELA INICIAL: Recomendados, Recentes/Favoritos, as categorias em 2
 * colunas (`effects.home`).
 */
@Composable
private fun BrowserHome(
    store: EditorStore,
    state: LazyListState,
    recommended: List<EffectCatalogEntry>,
    recents: List<QuickItem>,
    favorites: List<QuickItem>,
    groups: List<Pair<EffectGroup, List<EffectCatalogEntry>>>,
    previewsOn: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
    onGroup: (EffectGroup) -> Unit,
) {
    val perPage = favoritesPerPage(LocalConfiguration.current.screenWidthDp.toFloat())
    val pages = remember(recents, favorites, perPage) { quickPages(recents, favorites, perPage) }
    // Só Recentes à vista: uma página de Favoritos com o convite da estrela.
    val hint = favorites.isEmpty() && recents.isNotEmpty()
    CompositionLocalProvider(LocalFxThumbnailWork provides !state.isScrollInProgress) {
        LazyColumn(
            state = state, modifier = Modifier.fillMaxSize().testTag("effects.home"),
            contentPadding = PaddingValues(top = 4.dp, bottom = 12.dp),
        ) {
            if (pages.isNotEmpty()) item(key = "shortcuts") {
                QuickBand(store, pages, perPage, hint, previewsOn, onAdd, onDetail)
            }
            item(key = "category-heading") { CatalogHeading(stringResource(R.string.fx_browser_categories)) }
            items(groups, key = { it.first.id }) { (group, entries) ->
                val label = stringResource(effectGroupLabelRes(group))
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 2.dp)
                        .clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface)
                        .testTag("effects.category." + group.id)
                        .clickable(role = Role.Button) { onGroup(group) }
                        .heightIn(min = 48.dp).padding(horizontal = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CupertinoIcon(effectGroupGlyph(group), 18.dp, AureaColors.Accent)
                    Text(label, Modifier.weight(1f), style = AureaType.of(14f, FontWeight.Medium, color = AureaColors.Text))
                    Text(entries.size.toString(), style = AureaType.of(12f, color = AureaColors.FxTextSecondary))
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 13.dp, AureaColors.FxTextSecondary)
                }
            }
            if (recommended.isNotEmpty()) item(key = "recommended-heading") {
                CatalogHeading(stringResource(R.string.fx_browser_featured))
            }
            itemsIndexed(recommended, key = { _, entry -> "recommended:" + effectCardId(entry.typeId) }) { index, entry ->
                CatalogEffectRow(entry, store.effectPreviews, previewsOn, "effects.featured.",
                    onDetails = { onDetail(recommended, index) }) { onAdd(entry, -1) }
            }
        }
    }
}

@Composable
private fun CatalogHeading(label: String) {
    Text(label, style = AureaType.of(15f, FontWeight.SemiBold, color = AureaColors.Text),
        modifier = Modifier.padding(start = 16.dp, top = 12.dp, bottom = 8.dp).semantics { heading() })
}

@Composable
private fun QuickBand(
    store: EditorStore,
    pages: List<Pair<Boolean, List<QuickItem>>>,
    perPage: Int,
    hint: Boolean,
    previewsOn: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
) {
    val prefs = store.effectPrefs
    val total = pages.size + if (hint) 1 else 0
    val pager = rememberPagerState(initialPage = prefs.quickPage.coerceIn(0, (total - 1).coerceAtLeast(0))) { total }
    LaunchedEffect(pager) { snapshotFlow { pager.settledPage }.collect { prefs.updateQuickPage(it) } }
    val columns = (perPage / 2).coerceAtLeast(1)
    val rowCount = if (pages.any { it.second.size > columns }) 2 else 1
    // O detalhe pagina por todos os efeitos da faixa (um por efeito, sem repetir).
    val detailList = remember(pages) { pages.flatMap { it.second }.map { it.entry }.distinctBy { it.typeId } }
    val openDetail: (QuickItem) -> Unit = { item ->
        onDetail(detailList, detailList.indexOfFirst { it.typeId == item.entry.typeId }.coerceAtLeast(0))
    }
    val recentLabel = stringResource(R.string.om_fx_recent)
    val favoritesLabel = stringResource(R.string.om_fx_favorites)
    Column(Modifier.fillMaxWidth().height(if (rowCount == 1) 112.dp else 194.dp)
        .background(AureaColors.FxListSurface).testTag("effects.quick")) {
        HorizontalPager(
            pager,
            Modifier.fillMaxWidth().weight(1f),
            contentPadding = PaddingValues(vertical = 5.dp),
        ) { page ->
            val p = pages.getOrNull(page)
            if (p == null) {
                Box(Modifier.fillMaxSize().padding(horizontal = 34.dp), contentAlignment = Alignment.Center) {
                    Text(stringResource(R.string.om_fx_favorites_empty_star), style = AureaType.of(13f, color = AureaColors.FxTextSecondary),
                        textAlign = TextAlign.Center)
                }
            } else {
                Row(Modifier.fillMaxSize().padding(horizontal = 10.dp)) {
                    repeat(columns) { c ->
                        Column(
                            Modifier.weight(1f).fillMaxHeight(),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.SpaceEvenly,
                        ) {
                            repeat(rowCount) { row ->
                                val item = p.second.getOrNull(c * rowCount + row)
                                if (item == null) Spacer(Modifier.size(62.dp, 82.dp))
                                else QuickTile(item, p.first, store.effectPreviews, previewsOn, onAdd, openDetail)
                            }
                        }
                    }
                }
            }
        }
        Row(Modifier.fillMaxWidth().padding(bottom = 5.dp), horizontalArrangement = Arrangement.Center) {
            repeat(total) { i ->
                val active = pager.currentPage == i
                val recent = pages.getOrNull(i)?.first == true
                val label = stringResource(R.string.om_fx_quick_page, i + 1, total) + ", " + (if (recent) recentLabel else favoritesLabel)
                Box(
                    Modifier.padding(horizontal = 4.dp).size(9.dp).testTag("effects.quick.page.$i")
                        .semantics { contentDescription = label; selected = active },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(
                        if (recent) CupertinoGlyph.Clock else CupertinoGlyph.StarFill, 9.dp,
                        if (active) AureaColors.Text else AureaColors.FxDisabledText,
                    )
                }
            }
        }
    }
}

/** Item de 62×82: miniatura 55 (com o código da variação na base), selo e nome 9 em 2 linhas. */
@Composable
private fun QuickTile(
    item: QuickItem,
    recent: Boolean,
    previews: EffectPreviewStore?,
    previewsOn: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onDetail: (QuickItem) -> Unit,
) {
    val e = item.entry
    val name = pickerEntryName(e)
    val code = presetCode(item.preset)
    val id = effectCardId(e.typeId) + if (item.preset >= 0) ".${item.preset}" else ""
    val addLabel = stringResource(R.string.effects_card_add, if (code.isEmpty()) name else "$name $code")
    val detailsLabel = stringResource(R.string.om_fx_menu_details)
    val panel = AureaColors.FxPanel
    val tapOpensDetail = LocalEffectTapOpensDetail.current
    Column(
        Modifier
            .size(62.dp, 82.dp)
            .testTag((if (recent) "effects.recent." else "effects.favorite.") + id)
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(detailsLabel) { onDetail(item); true })
            }
            .tocavel(shrink = 0.94f, haptic = true, onLongClick = { onDetail(item) }, onClick = { if (tapOpensDetail) onDetail(item) else onAdd(e, item.preset) }),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.padding(top = 8.dp).size(55.dp)) {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(5.dp)).background(panel)) {
                EffectAfterPreview(e, previews, previewsOn)
                if (code.isNotEmpty()) {
                    Text(
                        code,
                        style = AureaType.of(10f, FontWeight.W600, color = AureaColors.Text),
                        textAlign = TextAlign.Center,
                        modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth()
                            .background(Brush.verticalGradient(listOf(panel.copy(alpha = 0f), panel))),
                    )
                }
            }
            EffectBadge(e, Modifier.align(Alignment.TopEnd).offset(x = 4.dp, y = (-4).dp))
        }
        Text(
            name,
            style = AureaType.of(9f, color = AureaColors.Text, lineHeight = 1.05f),
            textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/**
 * O LADRILHO de uma categoria (65 dp): a prévia de um efeito dela a 70 %, véu
 * «painel» a 50 % e o nome 15 negrito (`effects.category.<id>`). Grupo sem
 * prévia (Texto, Áudio, Utilitário) usa a cartela com o glifo do grupo.
 */
@Composable
private fun EffectResults(
    store: EditorStore,
    state: LazyListState,
    entries: List<EffectCatalogEntry>,
    previewsOn: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
) {
    CompositionLocalProvider(LocalFxThumbnailWork provides !state.isScrollInProgress) {
        LazyColumn(
            modifier = Modifier.fillMaxSize().testTag("effects.grid"),
            state = state, contentPadding = PaddingValues(vertical = 8.dp),
        ) {
            if (entries.isEmpty()) item(key = EMPTY_ITEM_KEY) {
                EmptyLine(stringResource(R.string.effect_empty_category))
            }
            itemsIndexed(entries, key = { _, e -> effectItemKey(e.typeId) }) { index, entry ->
                CatalogEffectRow(entry, store.effectPreviews, previewsOn, "effects.card.",
                    onDetails = { onDetail(entries, index) }) { onAdd(entry, -1) }
            }
        }
    }
}

@Composable
private fun EffectBadge(entry: EffectCatalogEntry, modifier: Modifier = Modifier) {
    when {
        HyperEffectGate.locked(entry.typeId) -> HyperLockBadge(modifier)
        effectCost(entry.effectClass) >= 3 -> HeavyBadge(modifier)
    }
}

/** O "!" de efeito pesado (temporal ou global): avisa antes de pôr no projeto. */
@Composable
private fun HeavyBadge(modifier: Modifier = Modifier) {
    val label = stringResource(R.string.fx_browser_heavy)
    Box(
        modifier.size(23.dp).clip(CircleShape).background(AureaColors.OnImage).semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Text("!", style = AureaType.of(13f, FontWeight.W900, color = AureaColors.FxDarkest))
    }
}

/** Selo "Grátis por enquanto" (efeito que será pago numa versão futura): o mesmo desenho do "Novo". */
@Composable
private fun FreeForNowBadge(modifier: Modifier = Modifier) {
    Text(
        stringResource(R.string.headbop_free_for_now),
        style = AureaType.of(9f, FontWeight.W700, color = AureaColors.OnAccent),
        maxLines = 1,
        modifier = modifier.clip(RoundedCornerShape(4.dp)).background(AureaColors.Accent).padding(horizontal = 4.dp, vertical = 1.dp)
            .testTag("effects.badge.free_for_now"),
    )
}

/** O selo "Novo" do AUREA (efeito marcado como novo pelo motor). */
@Composable
private fun NewBadge(modifier: Modifier = Modifier) {
    Text(
        stringResource(R.string.fx_browser_badge),
        style = AureaType.of(9f, FontWeight.W700, color = AureaColors.OnAccent),
        maxLines = 1,
        modifier = modifier.clip(RoundedCornerShape(4.dp)).background(AureaColors.Accent).padding(horizontal = 4.dp, vertical = 1.dp),
    )
}

@Composable
private fun EmptyLine(text: String) {
    Text(
        text,
        textAlign = TextAlign.Center,
        style = AureaType.of(13f, color = AureaColors.FxTextSecondary, lineHeight = 1.4f),
        modifier = Modifier.fillMaxWidth().padding(AureaDims.S4),
    )
}

// --- A BUSCA ------------------------------------------------------------------

/** Caractere invisível antes do texto quando há fichas: apagá-lo (⌫) apaga a última ficha. */
private const val CHIP_SENTINEL = "​"

/**
 * A BUSCA, cobrindo o cabeçalho (fundo «busca»): ‹ · campo «campo» de raio 10
 * com 🔍, as fichas de etiqueta, o texto e ✕. Vazia: etiquetas sugeridas em
 * coluna. Digitando: as etiquetas que casam numa fileira e os resultados em
 * linhas. Toque no resultado → detalhe. Ids `effects.search.field`,
 * `effects.search.back`, `effects.search.clear`, `effects.search.tag.<tag>`,
 * `effects.search.chip.<tag>` e `effects.result.<id>`.
 */
@Composable
private fun SearchPane(
    store: EditorStore,
    sorted: List<EffectCatalogEntry>,
    chips: List<String>,
    onChips: (List<String>) -> Unit,
    previewsOn: Boolean,
    onAdd: (EffectCatalogEntry, Int) -> Unit,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
    onClose: () -> Unit,
) {
    // O índice é montado aqui, fora do que lê o texto digitado: digitar não o refaz.
    val index = rememberEffectSearchIndex(sorted)
    val tagLabels = LinkedHashMap<String, String>().apply {
        EffectTagIds.forEach { t -> effectTagLabelRes(t)?.let { put(t, stringResource(it)) } }
    }
    val available = remember(sorted, index) { EffectTagIds.filter { t -> sorted.any { t in index.tags[it.typeId].orEmpty() } } }
    val locale = LocalConfiguration.current.locales[0]
    var field by remember {
        mutableStateOf(if (chips.isEmpty()) TextFieldValue("") else TextFieldValue(CHIP_SENTINEL, TextRange(1)))
    }
    val query = field.text.replace(CHIP_SENTINEL, "")
    val results = remember(sorted, index, query, chips, locale) {
        val base = effectsWithTags(sorted, chips) { index.tags[it.typeId].orEmpty() }
        when {
            query.isNotBlank() -> searchEffects(base, index.docs, query, index.fields)
            chips.isNotEmpty() -> sortedByName(base, locale) { index.names[it.typeId] ?: it.name }
            else -> emptyList()
        }
    }
    val matching = remember(query, chips, available, tagLabels) {
        matchingTags(tagLabels.filterKeys { it in available }, query, chips)
    }
    val focus = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
    val addChip: (String) -> Unit = { tag ->
        if (tag !in chips) onChips(chips + tag)
        field = TextFieldValue(CHIP_SENTINEL, TextRange(1))
    }
    val removeChip: (String) -> Unit = { tag ->
        val next = chips - tag
        onChips(next)
        val text = (if (next.isEmpty()) "" else CHIP_SENTINEL) + query
        field = TextFieldValue(text, TextRange(text.length))
    }
    val backLabel = stringResource(R.string.common_close)
    val clearLabel = stringResource(R.string.om_fx_search_clear)
    Column(
        Modifier
            .fillMaxSize()
            .background(AureaColors.FxSearch)
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .imePadding(),
    ) {
        Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.size(AureaDims.MinTap).testTag("effects.search.back")
                    .semantics { contentDescription = backLabel; role = Role.Button }
                    .tocavel(shrink = 1f, role = null, onClick = onClose),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.ChevronBack, 22.dp, AureaColors.Text)
            }
            Row(
                Modifier
                    .weight(1f)
                    .padding(end = 10.dp)
                    .height(AureaDims.MinTap)
                    .clip(RoundedCornerShape(10.dp))
                    .background(AureaColors.FxField)
                    .padding(start = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, 16.dp, AureaColors.FxTextSecondary)
                Spacer(Modifier.width(6.dp))
                if (chips.isNotEmpty()) {
                    Row(Modifier.widthIn(max = 180.dp).horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
                        chips.forEach { tag -> FieldChip(tag, tagLabels[tag] ?: tag) { removeChip(tag) } }
                    }
                }
                Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                    if (query.isEmpty() && chips.isEmpty()) {
                        Text(stringResource(R.string.effect_buscar_glitch_vhs_desfoque_cor), style = AureaType.of(15f, color = AureaColors.FxTextSecondary),
                            maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    BasicTextField(
                        value = field,
                        onValueChange = { v ->
                            field = when {
                                chips.isEmpty() -> v
                                // ⌫ apagou o caractere invisível: sai a última ficha, o texto fica.
                                !v.text.contains(CHIP_SENTINEL) -> {
                                    val next = chips.dropLast(1)
                                    onChips(next)
                                    if (next.isEmpty()) v else TextFieldValue(CHIP_SENTINEL + v.text, TextRange(v.selection.start + 1, v.selection.end + 1))
                                }
                                // O cursor foi para antes do invisível: ele volta para o começo.
                                !v.text.startsWith(CHIP_SENTINEL) -> {
                                    val text = CHIP_SENTINEL + v.text.replace(CHIP_SENTINEL, "")
                                    TextFieldValue(text, TextRange(text.length))
                                }
                                v.selection.min == 0 -> v.copy(selection = TextRange(maxOf(1, v.selection.start), maxOf(1, v.selection.end)))
                                else -> v
                            }
                        },
                        singleLine = true,
                        textStyle = AureaType.of(15f, color = AureaColors.Text),
                        cursorBrush = SolidColor(AureaColors.Accent),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                        keyboardActions = KeyboardActions(onSearch = { keyboard?.hide() }),
                        modifier = Modifier.fillMaxWidth().focusRequester(focus).testTag("effects.search.field"),
                    )
                }
                if (query.isNotEmpty() || chips.isNotEmpty()) {
                    Box(
                        Modifier.size(AureaDims.MinTap).testTag("effects.search.clear")
                            .semantics { contentDescription = clearLabel; role = Role.Button }
                            .tocavel(shrink = 1f, role = null) {
                                onChips(emptyList())
                                field = TextFieldValue("")
                                runCatching { focus.requestFocus() }
                            },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.FxTextSecondary)
                    }
                }
            }
        }
        if (chips.isEmpty() && query.isBlank()) {
            // Vazia: as etiquetas sugeridas, em coluna.
            val title = stringResource(R.string.om_fx_search_tags)
            Column(
                Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).padding(top = 20.dp, bottom = 20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(title, style = AureaType.of(12f, FontWeight.W600, color = AureaColors.FxTextSecondary),
                    modifier = Modifier.padding(bottom = 6.dp).semantics { heading() })
                available.forEach { tag -> SearchTagChip(tag, tagLabels[tag] ?: tag) { addChip(tag) } }
            }
        } else {
            if (matching.isNotEmpty()) {
                LazyRow(
                    Modifier.fillMaxWidth().padding(top = 4.dp),
                    contentPadding = PaddingValues(horizontal = 15.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    items(matching.distinct(), key = { tagItemKey(it) }) { tag -> SearchTagChip(tag, tagLabels[tag] ?: tag) { addChip(tag) } }
                }
            }
            LazyColumn(Modifier.fillMaxWidth().weight(1f).testTag("effects.search.results")) {
                if (results.isEmpty()) item(key = EMPTY_ITEM_KEY) { EmptyLine(stringResource(R.string.effect_empty_search)) }
                itemsIndexed(results, key = { _, e -> effectItemKey(e.typeId) }) { i, e ->
                    CatalogEffectRow(e, store.effectPreviews, previewsOn, "effects.result.",
                        onDetails = { keyboard?.hide(); onDetail(results, i) }) {
                        keyboard?.hide()
                        onAdd(e, -1)
                    }
                }
            }
        }
    }
}

/** Etiqueta sugerida ou que casa: 23 de altura (12, padding 15/3, borda 0,8), num alvo de 44. */
@Composable
private fun SearchTagChip(tag: String, label: String, onClick: () -> Unit) {
    val shape = RoundedCornerShape(10.dp)
    Box(
        Modifier.heightIn(min = AureaDims.MinTap).testTag("effects.search.tag.$tag")
            .semantics { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.96f, role = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.height(23.dp).clip(shape).background(AureaColors.FxListSurface).border(0.8.dp, AureaColors.FxDarkest, shape)
                .padding(horizontal = 15.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.of(12f, color = AureaColors.Text), maxLines = 1)
        }
    }
}

/** A ficha dentro do campo: tocar tira a etiqueta (o ⌫ também). */
@Composable
private fun FieldChip(tag: String, label: String, onRemove: () -> Unit) {
    val remove = stringResource(R.string.om_fx_search_tag_remove, label)
    val shape = RoundedCornerShape(10.dp)
    Box(
        Modifier.heightIn(min = AureaDims.MinTap).padding(end = 4.dp).testTag("effects.search.chip.$tag")
            .semantics { contentDescription = remove; role = Role.Button }
            .tocavel(shrink = 0.96f, role = null, onClick = onRemove),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.height(23.dp).clip(shape).background(AureaColors.FxListSurface).border(0.8.dp, AureaColors.FxDarkest, shape)
                .padding(horizontal = 10.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.of(12f, color = AureaColors.Text), maxLines = 1)
        }
    }
}

/** Linha de resultado: miniatura 80, nome 17, nome em inglês 14 (se diferente) e a descrição 11 até a base. */
@Composable
private fun CatalogEffectRow(
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    previewsOn: Boolean,
    tagPrefix: String,
    onDetails: (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    val name = pickerEntryName(entry)
    val details = stringResource(R.string.om_fx_menu_details)
    val tag = effectCardId(entry.typeId)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 3.dp)
            .clip(RoundedCornerShape(12.dp)).background(AureaColors.Surface),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier.weight(1f).heightIn(min = 64.dp).testTag(tagPrefix + tag)
                .semantics { contentDescription = name }
                .clickable(role = Role.Button, onClick = onClick)
                .padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Box(Modifier.size(40.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.FxPanel)) {
                EffectAfterPreview(entry, previews, previewsOn)
            }
            Column(Modifier.weight(1f)) {
                Text(name, style = AureaType.of(14f, FontWeight.SemiBold, color = AureaColors.Text),
                    maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(effectDescription(entry.typeId, entry.category),
                    style = AureaType.of(11f, color = AureaColors.FxTextSecondary), maxLines = 2,
                    overflow = TextOverflow.Ellipsis)
            }
            EffectBadge(entry)
            if (effectToolOf(entry.typeId) != null) {
                CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, AureaColors.Accent)
            } else if (!HyperEffectGate.locked(entry.typeId)) {
                CupertinoIcon(CupertinoGlyph.Plus, 16.dp, AureaColors.Accent)
            }
        }
        if (onDetails != null) {
            Box(
                Modifier.size(44.dp).testTag("effects.options." + tag)
                    .semantics { contentDescription = details }
                    .clickable(role = Role.Button, onClick = onDetails),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 16.dp, AureaColors.FxTextSecondary)
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

private fun englishResources(context: Context): Resources =
    context.createConfigurationContext(Configuration(context.resources.configuration).apply { setLocale(Locale.ENGLISH) }).resources

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
 * O que a busca sabe do catálogo: [docs] (o texto em 7 idiomas, para filtrar e
 * para a ordem do AUREA), [fields] (o que a pontuação da referência olha), as
 * etiquetas de cada efeito ([tags]), o nome em inglês ([english]) e o nome
 * mostrado ([names]).
 */
private class SearchIndex(
    val docs: Map<Int, EffectSearchDoc>,
    val fields: Map<Int, EffectScoreFields>,
    val tags: Map<Int, List<String>>,
    val english: Map<Int, String>,
    val names: Map<Int, String>,
)

/**
 * O índice da busca: no idioma do app, tudo o que o catálogo sabe (nome,
 * sinônimos da tabela humana, descrição); nos 7 idiomas, o nome, a categoria, o
 * GRUPO da tela, as ETIQUETAS (e o nome das ferramentas); e os sinônimos de
 * categoria. Refeito só quando o catálogo ou o idioma mudam.
 */
@Composable
private fun rememberEffectSearchIndex(entries: List<EffectCatalogEntry>): SearchIndex {
    val context = LocalContext.current
    val locale = LocalConfiguration.current.locales[0]
    val current = catalogHaystack(entries)
    val human = entries.associate { it.typeId to effectSearchText(it.typeId, it.name, it.category) }
    val shown = entries.associate { it.typeId to pickerEntryName(it) }
    val descriptions = entries.associate { it.typeId to effectDescription(it.typeId, it.category) }
    return remember(entries, locale, current, human, shown, descriptions) {
        val languages = languageResources(context)
        val english = englishResources(context)
        val tagsById = entries.associate { it.typeId to effectTags(it) }
        val englishById = entries.associate { e ->
            e.typeId to (effectToolOf(e.typeId)?.let { english.getString(toolLabelRes(it)) } ?: englishEffectName(e.typeId, e.name, english))
        }
        val docs = entries.associate { e ->
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
                // Etiquetas (P2-4) nos 7 idiomas: "glitch", "сбой", "retrô"...
                tagsById[e.typeId].orEmpty().forEach { t -> effectTagLabelRes(t)?.let { res -> languages.forEach { add(it.getString(res)) } } }
            }
            e.typeId to effectSearchDoc(names, extra)
        }
        val fields = entries.associate { e ->
            val tagLabels = tagsById[e.typeId].orEmpty().mapNotNull { t -> effectTagLabelRes(t)?.let { context.getString(it) } }
            e.typeId to EffectScoreFields(tagLabels, shown[e.typeId].orEmpty(), englishById[e.typeId].orEmpty(), descriptions[e.typeId].orEmpty())
        }
        SearchIndex(docs, fields, tagsById, englishById, shown)
    }
}
