package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
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
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
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
import androidx.compose.ui.text.style.Hyphens
import androidx.compose.ui.text.style.LineBreak
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

// =============================================================================
//  O NAVEGADOR DE EFEITOS (0.3.2 Omega, área 07 P0-6): as medidas e os fluxos
//  da referência, com os extras do AUREA.
//
//   · Moldura: fundo «superfície de lista», cabeçalho de 44 (✕/‹ · título 17 ·
//     🔍, que some dentro da categoria) e o fio de 0,5 só com conteúdo rolado.
//     Entrar numa categoria desliza 100 dp em 100 ms; voltar faz o inverso.
//   · Início: RECOMENDADOS (faixa de 186, até 6, sem os efeitos que a camada já
//     tem), RECENTES/FAVORITOS (faixa de 210 em páginas de 2 linhas; toque
//     ADICIONA, toque longo abre o DETALHE) e as CATEGORIAS (2 colunas, 65 dp).
//     A porta "Gerenciar plugins" continua no fim.
//   · Categoria: grade de cartões de 88 dp em ordem alfabética, anterior/próxima
//     nas pontas. Toque no cartão → detalhe.
//   · Busca: etiquetas sugeridas, etiqueta vira ficha no campo, resultados em
//     linhas por pontos (etiqueta ×7, nome ×3, inglês ×2, descrição ×1) somados
//     à busca do AUREA em 7 idiomas. Toque → detalhe.
//   · Detalhe (EffectsCatalogView.kt): folha paginada com as variações; tocar
//     numa variação adiciona (efeito + preset, um passo de desfazer) e fecha.
//
//  Extras do AUREA mantidos: ferramentas-efeito (Máscara, Rastreio, Legendas
//  abrem o painel delas), plugins, selo "Novo", "!" de efeito pesado, cadeado
//  Hyper e as prévias reais do motor (só o "depois", sem rótulos).
// =============================================================================

/** Onde o detalhe abriu: a lista que ele pagina e o item tocado. */
private class DetailRequest(val entries: List<EffectCatalogEntry>, val index: Int)

/** A pilha do navegador: categoria aberta, busca (com fichas) e detalhe. */
@Stable
private class BrowserNav {
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
) {
    val nav = remember { BrowserNav() }
    Dialog(
        onDismissRequest = { nav.back(onDismiss) },
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        EffectBrowserBody(store, layerHasAudio, nav, onPick, onTool, onPickVariant, onDismiss)
    }
}

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
    val sorted = remember(catalog, layerHasAudio, kind, text3dLayer, toolNames) {
        val textLayer = kind == 4
        val effects = pickableEffects(catalog, layerHasAudio, layerKind = kind).filter {
            (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.transform")) &&
                (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator"))
        }
        val all = effects + pickableTools(kind).map { toolCatalogEntry(it, toolNames.getValue(it)) }
        arrangeCatalog(all, effectCategories(all))
    }
    val locale = LocalConfiguration.current.locales[0]
    val names = sorted.associate { it.typeId to pickerEntryName(it) }
    // Dentro da categoria, ordem ALFABÉTICA pelo nome mostrado (como a referência).
    val groups = remember(sorted, names, locale) {
        groupEntries(sorted).map { (g, list) -> g to sortedByName(list, locale) { names[it.typeId] ?: it.name } }
    }
    val presetCount: (Int) -> Int = remember(store) { { t: Int -> if (effectToolOf(t) != null) 0 else store.effectPresets(t).size } }
    val recents = remember(prefs.recentRefs, sorted) { recentItems(prefs.recentRefs, sorted, presetCount) }
    val favorites = remember(prefs.favoriteRefs, sorted) { favoriteItems(prefs.favoriteRefs, sorted, presetCount) }
    val applied = store.effects.map { it.typeId }
    val recommended = remember(sorted, kind, applied, layerHasAudio) { recommendedEffects(sorted, kind, applied, 6, layerHasAudio) }
    var managingPlugins by remember { mutableStateOf(false) }
    // 0.3.2: efeito Hyper sem o acesso abre a tela bloqueada (HyperEffectGate).
    var hyperLocked by remember { mutableStateOf(false) }
    val add: (EffectCatalogEntry, Int) -> Unit = { e, preset ->
        if (HyperEffectGate.locked(e.typeId)) {
            hyperLocked = true
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
    val gridState = rememberLazyGridState()
    val previewsOn = prefs.previewsEnabled
    val shift = with(LocalDensity.current) { 100.dp.roundToPx() } * (if (LocalLayoutDirection.current == LayoutDirection.Rtl) -1 else 1)

    Box(Modifier.fillMaxSize().background(AureaColors.FxListSurface)) {
        Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
            val openId = nav.groupId?.takeIf { id -> groups.any { it.first.id == id } }
            val open = groups.firstOrNull { it.first.id == openId }
            val scrolled = if (open == null) homeState.canScrollBackward else gridState.canScrollBackward
            BrowserTopBar(
                title = open?.let { stringResource(effectGroupLabelRes(it.first)) } ?: stringResource(R.string.fx_browser_title),
                inGroup = open != null,
                divider = scrolled,
                onBack = { nav.groupId = null },
                onClose = onDismiss,
                onSearch = { nav.openSearch(emptyList()) },
            )
            AnimatedContent(
                targetState = openId,
                // Anterior/próxima trocam a categoria NO LUGAR: só início ↔ categoria anima.
                contentKey = { it != null },
                transitionSpec = {
                    if (targetState != null) {
                        (slideInHorizontally(tween(100, easing = LinearOutSlowInEasing)) { shift } + fadeIn(tween(25)))
                            .togetherWith(ExitTransition.None)
                    } else {
                        EnterTransition.None.togetherWith(
                            slideOutHorizontally(tween(100, easing = FastOutLinearInEasing)) { shift } +
                                fadeOut(tween(25, delayMillis = 75)),
                        ).apply { targetContentZIndex = -1f }
                    }
                },
                label = "fx-categoria",
                modifier = Modifier.fillMaxWidth().weight(1f),
            ) { id ->
                val shown = id?.let { g -> groups.firstOrNull { it.first.id == g } }
                if (shown == null) {
                    BrowserHome(
                        store, homeState, recommended, recents, favorites, groups, previewsOn,
                        onAdd = add, onDetail = openDetail, onGroup = { nav.groupId = it.id }, onPlugins = { managingPlugins = true },
                    )
                } else {
                    val (previous, next) = adjacentGroups(groups.map { it.first }, shown.first)
                    GroupGrid(store, gridState, shown.first, shown.second, previous, next, previewsOn, openDetail) { nav.groupId = it.id }
                }
            }
        }
        if (nav.searching) {
            key(nav.searchEpoch) {
                SearchPane(
                    store, sorted, nav.searchChips, onChips = { nav.searchChips = it }, previewsOn = previewsOn,
                    onDetail = openDetail, onClose = { nav.searching = false },
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
    if (hyperLocked) com.aurea.aurea.hyper.HyperLockedDialog { hyperLocked = false }
    // 0.3.0: plugins de efeito (instalar, ver, remover); o catálogo relido entra nos grupos acima.
    if (managingPlugins) PluginManagerSheet(store) { managingPlugins = false }
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
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
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
                style = AureaType.of(17f, FontWeight.W500, color = AureaColors.Text),
                textAlign = TextAlign.Center,
                modifier = Modifier.weight(1f).padding(horizontal = AureaDims.S1).semantics { heading() },
            )
            if (inGroup) {
                Spacer(Modifier.size(AureaDims.MinTap))
            } else {
                Box(
                    Modifier
                        .size(AureaDims.MinTap)
                        .testTag("effects.search")
                        .semantics { contentDescription = searchLabel; role = Role.Button }
                        .tocavel(shrink = 1f, role = null, onClick = onSearch),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Search, 22.dp, AureaColors.Text)
                }
            }
        }
        if (divider) Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.FxLowSurface))
        else Spacer(Modifier.height(0.5.dp))
    }
}

/**
 * A TELA INICIAL: Recomendados, Recentes/Favoritos, as categorias em 2
 * colunas e a porta "Gerenciar plugins" (`effects.home`).
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
    onPlugins: () -> Unit,
) {
    val perPage = favoritesPerPage(LocalConfiguration.current.screenWidthDp.toFloat())
    val pages = remember(recents, favorites, perPage) { quickPages(recents, favorites, perPage) }
    // Só Recentes à vista: uma página de Favoritos com o convite da estrela.
    val hint = favorites.isEmpty() && recents.isNotEmpty()
    val categoriesLabel = stringResource(R.string.fx_browser_categories)
    LazyColumn(state = state, modifier = Modifier.fillMaxSize().testTag("effects.home")) {
        if (recommended.isNotEmpty()) item(key = "recomendados") { RecommendedBand(store, recommended, previewsOn, onDetail) }
        if (pages.isNotEmpty()) item(key = "rapidos") { QuickBand(store, pages, perPage, hint, previewsOn, onAdd, onDetail) }
        item(key = "titulo-categorias") {
            Spacer(Modifier.height(0.dp).semantics { contentDescription = categoriesLabel; heading() })
        }
        // A chave da linha leva prefixo: com um plugin instalado o grupo "plugins" pode abrir
        // uma linha e colidir com a porta fixa abaixo ("Key was already used" fechava o app).
        items(groups.chunked(2), key = { row -> groupRowKey(row.map { it.first }) }) { row ->
            Row(
                Modifier.fillMaxWidth().background(AureaColors.FxDarkest).padding(bottom = 1.dp),
                horizontalArrangement = Arrangement.spacedBy(1.dp),
            ) {
                row.forEach { (g, entries) -> GroupTile(store, g, entries, previewsOn, Modifier.weight(1f)) { onGroup(g) } }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
        }
        // 0.3.0: a porta dos plugins fica sempre à vista (a categoria só aparece com um instalado).
        item(key = PLUGIN_DOOR_KEY) {
            Row(
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = 52.dp)
                    .background(AureaColors.FxListSurface)
                    .testTag("effects.plugins")
                    .tocavel(shrink = 1f, onClick = onPlugins)
                    .padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.SquareStack3dUp, 20.dp, AureaColors.Accent)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.plug_manage), modifier = Modifier.weight(1f), style = AureaType.of(14f, color = AureaColors.Text))
                CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.FxTextSecondary)
            }
        }
    }
}

/**
 * RECOMENDADOS: faixa de 186 «superfície baixa» que rola para o lado, itens de
 * 121×153 (miniatura 115) e véus de 80 nas bordas só quando dá para rolar
 * daquele lado. Toque → detalhe paginado pelos recomendados.
 */
@Composable
private fun RecommendedBand(
    store: EditorStore,
    entries: List<EffectCatalogEntry>,
    previewsOn: Boolean,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
) {
    val label = stringResource(R.string.om_fx_recommended)
    val row = rememberLazyListState()
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val veil = AureaColors.FxSunken
    val clear = veil.copy(alpha = 0f)
    Box(
        Modifier.fillMaxWidth().height(186.dp).background(AureaColors.FxLowSurface)
            .testTag("effects.featured").semantics { contentDescription = label },
    ) {
        LazyRow(
            state = row,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(horizontal = 7.5.dp),
            horizontalArrangement = Arrangement.spacedBy(7.5.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            itemsIndexed(entries, key = { _, e -> e.typeId }) { i, e ->
                RecommendedItem(e, store.effectPreviews, previewsOn) { onDetail(entries, i) }
            }
        }
        if (row.canScrollBackward) {
            Box(
                Modifier.align(Alignment.CenterStart).width(80.dp).fillMaxHeight()
                    .background(Brush.horizontalGradient(if (rtl) listOf(clear, veil) else listOf(veil, clear))),
            )
        }
        if (row.canScrollForward) {
            Box(
                Modifier.align(Alignment.CenterEnd).width(80.dp).fillMaxHeight()
                    .background(Brush.horizontalGradient(if (rtl) listOf(veil, clear) else listOf(clear, veil))),
            )
        }
    }
}

@Composable
private fun RecommendedItem(entry: EffectCatalogEntry, previews: EffectPreviewStore?, previewsOn: Boolean, onClick: () -> Unit) {
    val name = pickerEntryName(entry)
    Column(
        Modifier
            .width(121.dp)
            .height(153.dp)
            .testTag("effects.featured." + effectCardId(entry.typeId))
            .semantics { contentDescription = name }
            .tocavel(shrink = 0.96f, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.padding(top = 8.dp).size(115.dp)) {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(5.dp)).background(AureaColors.FxPanel)) {
                EffectAfterPreview(entry, previews, previewsOn, FX_PREVIEW_LARGE_W, FX_PREVIEW_LARGE_H)
            }
            if (entry.isNew) NewBadge(Modifier.align(Alignment.TopStart).padding(5.dp))
            EffectBadge(entry, Modifier.align(Alignment.TopEnd).offset(x = 5.dp, y = (-5).dp))
        }
        Text(
            name,
            style = AureaType.of(10f, color = AureaColors.Text, lineHeight = 1.2f),
            textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(top = 4.dp, start = 2.dp, end = 2.dp),
        )
    }
}

/**
 * RECENTES/FAVORITOS: faixa de 210 com páginas de 2 linhas (a de Recentes, se
 * houver, e as de Favoritos) e o indicador de 9 dp embaixo (relógio = Recentes,
 * estrela = cada página de Favoritos). A página à vista é lembrada.
 * Toque ADICIONA; toque longo abre o DETALHE.
 */
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
    // O detalhe pagina por todos os efeitos da faixa (um por efeito, sem repetir).
    val detailList = remember(pages) { pages.flatMap { it.second }.map { it.entry }.distinctBy { it.typeId } }
    val openDetail: (QuickItem) -> Unit = { item ->
        onDetail(detailList, detailList.indexOfFirst { it.typeId == item.entry.typeId }.coerceAtLeast(0))
    }
    val recentLabel = stringResource(R.string.om_fx_recent)
    val favoritesLabel = stringResource(R.string.om_fx_favorites)
    Column(Modifier.fillMaxWidth().height(210.dp).background(AureaColors.FxListSurface).testTag("effects.quick")) {
        HorizontalPager(
            pager,
            Modifier.fillMaxWidth().weight(1f).padding(horizontal = 10.dp),
            contentPadding = PaddingValues(vertical = 5.dp),
        ) { page ->
            val p = pages.getOrNull(page)
            if (p == null) {
                Box(Modifier.fillMaxSize().padding(horizontal = 24.dp), contentAlignment = Alignment.Center) {
                    Text(stringResource(R.string.om_fx_favorites_empty_star), style = AureaType.of(13f, color = AureaColors.FxTextSecondary),
                        textAlign = TextAlign.Center)
                }
            } else {
                Row(Modifier.fillMaxSize()) {
                    repeat(columns) { c ->
                        Column(
                            Modifier.weight(1f).fillMaxHeight(),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.SpaceEvenly,
                        ) {
                            repeat(2) { r ->
                                val (col, row) = c to r
                                val item = p.second.getOrNull(col * 2 + row)
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
    Column(
        Modifier
            .size(62.dp, 82.dp)
            .testTag((if (recent) "effects.recent." else "effects.favorite.") + id)
            .semantics {
                contentDescription = addLabel
                customActions = listOf(CustomAccessibilityAction(detailsLabel) { onDetail(item); true })
            }
            .tocavel(shrink = 0.94f, haptic = true, onLongClick = { onDetail(item) }, onClick = { onAdd(e, item.preset) }),
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
private fun GroupTile(
    store: EditorStore,
    group: EffectGroup,
    entries: List<EffectCatalogEntry>,
    previewsOn: Boolean,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val label = stringResource(effectGroupLabelRes(group))
    val description = stringResource(R.string.fx_browser_open_group, label)
    val banner = remember(group, entries) { groupBannerEntry(group, entries) }
    Box(
        modifier
            .height(65.dp)
            .background(AureaColors.FxPanel)
            .testTag("effects.category." + group.id)
            .semantics { contentDescription = description; role = Role.Button }
            .tocavel(shrink = 1f, role = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(Modifier.fillMaxSize().graphicsLayer { alpha = 0.7f }) {
            if (banner != null) EffectAfterPreview(banner, store.effectPreviews, previewsOn)
            else EffectToolPlate(effectGroupGlyph(group), alpha = 0.35f)
        }
        Box(Modifier.fillMaxSize().background(AureaColors.FxPanel.copy(alpha = 0.5f)))
        Text(
            label,
            style = AureaType.of(15f, FontWeight.Bold, color = AureaColors.Text),
            textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 8.dp),
        )
    }
}

/**
 * Dentro de uma categoria: a grade de cartões de 88 dp (⌊(L − 30) / 103⌋
 * colunas) e, embaixo, anterior/próxima nas pontas (desabilitados nas pontas),
 * que trocam a categoria no lugar. Toque no cartão → detalhe.
 */
@Composable
private fun GroupGrid(
    store: EditorStore,
    state: LazyGridState,
    group: EffectGroup,
    entries: List<EffectCatalogEntry>,
    previous: EffectGroup?,
    next: EffectGroup?,
    previewsOn: Boolean,
    onDetail: (List<EffectCatalogEntry>, Int) -> Unit,
    onGroup: (EffectGroup) -> Unit,
) {
    LaunchedEffect(group) { state.scrollToItem(0) }
    val columns = categoryGridColumns(LocalConfiguration.current.screenWidthDp.toFloat())
    Column(Modifier.fillMaxSize()) {
        LazyVerticalGrid(
            columns = GridCells.Fixed(columns),
            state = state,
            contentPadding = PaddingValues(start = 15.dp, end = 15.dp, top = 4.dp, bottom = 8.dp),
            modifier = Modifier.fillMaxWidth().weight(1f).testTag("effects.grid"),
        ) {
            if (entries.isEmpty()) item(key = "vazio", span = { GridItemSpan(maxLineSpan) }) {
                EmptyLine(stringResource(R.string.effect_empty_category))
            }
            itemsIndexed(entries, key = { _, e -> e.typeId }) { i, e ->
                Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.TopCenter) {
                    GridCard(e, store.effectPreviews, previewsOn) { onDetail(entries, i) }
                }
            }
        }
        Row(Modifier.fillMaxWidth().height(50.dp), verticalAlignment = Alignment.CenterVertically) {
            NeighborArrow(previous, forward = false, onGroup)
            Spacer(Modifier.weight(1f))
            NeighborArrow(next, forward = true, onGroup)
        }
    }
}

/** Cartão de 88: miniatura 80 (raio 5), selo 23 no canto e nome 10 em 2 linhas (com hifenização). */
@Composable
private fun GridCard(entry: EffectCatalogEntry, previews: EffectPreviewStore?, previewsOn: Boolean, onClick: () -> Unit) {
    val name = pickerEntryName(entry)
    Column(
        Modifier
            .width(88.dp)
            .testTag("effects.card." + effectCardId(entry.typeId))
            .semantics { contentDescription = name }
            .tocavel(shrink = 0.96f, onClick = onClick)
            .padding(bottom = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.padding(top = 8.dp).size(80.dp)) {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(5.dp)).background(AureaColors.FxPanel)) {
                EffectAfterPreview(entry, previews, previewsOn)
            }
            if (entry.isNew) NewBadge(Modifier.align(Alignment.TopStart).padding(4.dp))
            EffectBadge(entry, Modifier.align(Alignment.TopEnd).offset(x = 4.dp, y = (-4).dp))
        }
        Text(
            name,
            style = AureaType.of(10f, color = AureaColors.Text, lineHeight = 1.2f).copy(hyphens = Hyphens.Auto, lineBreak = LineBreak.Paragraph),
            textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
        )
    }
}

/** Anterior/próxima categoria: 40×50 na referência, alvo de 44×50 aqui. */
@Composable
private fun NeighborArrow(target: EffectGroup?, forward: Boolean, onGroup: (EffectGroup) -> Unit) {
    val targetLabel = target?.let { stringResource(effectGroupLabelRes(it)) }
    val label = if (targetLabel != null) {
        stringResource(if (forward) R.string.fx_browser_next_group else R.string.fx_browser_prev_group, targetLabel)
    } else {
        stringResource(if (forward) R.string.om_fx_category_next else R.string.om_fx_category_prev)
    }
    val enabled = target != null
    Box(
        Modifier
            .size(width = AureaDims.MinTap, height = 50.dp)
            .testTag(if (forward) "effects.category.next" else "effects.category.prev")
            .semantics { contentDescription = label; role = Role.Button }
            .tocavel(enabled = enabled, shrink = 1f, role = null, onClick = { target?.let(onGroup) }),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(
            if (forward) CupertinoGlyph.ChevronRight else CupertinoGlyph.ChevronLeft, 20.dp,
            if (enabled) AureaColors.Text else AureaColors.FxDisabledText,
        )
    }
}

/** O selo de 23 no canto: cadeado Hyper ou o "!" de efeito pesado. */
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
                    items(matching, key = { it }) { tag -> SearchTagChip(tag, tagLabels[tag] ?: tag) { addChip(tag) } }
                }
            }
            LazyColumn(Modifier.fillMaxWidth().weight(1f).testTag("effects.search.results")) {
                if (results.isEmpty()) item(key = "vazio") { EmptyLine(stringResource(R.string.effect_empty_search)) }
                itemsIndexed(results, key = { _, e -> e.typeId }) { i, e ->
                    ResultRow(e, index.english[e.typeId], store.effectPreviews, previewsOn) {
                        keyboard?.hide()
                        onDetail(results, i)
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
private fun ResultRow(
    entry: EffectCatalogEntry,
    english: String?,
    previews: EffectPreviewStore?,
    previewsOn: Boolean,
    onClick: () -> Unit,
) {
    val name = pickerEntryName(entry)
    val description = effectDescription(entry.typeId, entry.category)
    val second = english?.takeIf { it.isNotBlank() && it != name }
    Row(
        Modifier
            .fillMaxWidth()
            .testTag("effects.result." + effectCardId(entry.typeId))
            .semantics { contentDescription = name }
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 15.dp, vertical = 8.dp),
    ) {
        Box(Modifier.size(80.dp)) {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(5.dp)).background(AureaColors.FxPanel)) {
                EffectAfterPreview(entry, previews, previewsOn)
            }
            if (entry.isNew) NewBadge(Modifier.align(Alignment.TopStart).padding(4.dp))
            EffectBadge(entry, Modifier.align(Alignment.TopEnd).offset(x = 4.dp, y = (-4).dp))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f).height(80.dp)) {
            Text(name, style = AureaType.of(17f, color = AureaColors.Text), maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (second != null) {
                Text(second, style = AureaType.of(14f, color = AureaColors.FxTextSecondary), maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Text(
                description,
                style = AureaType.of(11f, color = AureaColors.Text, lineHeight = 1.3f),
                maxLines = if (second != null) 2 else 3, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(top = 2.dp),
            )
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
