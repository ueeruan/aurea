package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
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
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

// =============================================================================
//  Catálogo de efeitos: busca visível, abas Todos/Novos/Favoritos/Recentes,
//  categorias horizontais e cartões com comparação Antes/Depois do motor.
//  Um toque adiciona; segurar favorita. Ferramentas abrem seus próprios painéis.
// =============================================================================

private const val PREVIEW_W = 320
private const val PREVIEW_H = 200
private val CardMin = 152.dp
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
    val group = groups.firstOrNull { it.first.id == groupId }
    var section by rememberSaveable { mutableStateOf(0) }
    var searching by remember { mutableStateOf(false) }
    val recents = remember(prefs.recents, sorted) { recentEffects(prefs.recents, sorted) }
    val favorites = remember(prefs.favorites, sorted) { favoriteEffects(prefs.favorites, sorted) }
    val newlyAdded = remember(sorted) { sorted.filter { it.isNew } }
    val onFavorite = rememberFavoriteToggle(store)
    val pick: (EffectCatalogEntry) -> Unit = { e ->
        prefs.addRecent(e.typeId)
        val tool = effectToolOf(e.typeId)
        if (tool != null) onTool(tool) else onPick(e)
    }
    val shown = when (section) {
        1 -> newlyAdded
        2 -> favorites
        3 -> recents
        else -> group?.second ?: sorted
    }
    val grid = rememberLazyGridState()
    LaunchedEffect(groupId, section) { grid.scrollToItem(0) }
    Column(Modifier.fillMaxSize().background(AureaColors.EditorPanel).windowInsetsPadding(WindowInsets.safeDrawing)) {
        SheetTopBar(
            title = group?.let { stringResource(effectGroupLabelRes(it.first)) } ?: stringResource(R.string.panel_adicionar_efeito),
            inGroup = groupId != null,
            onBack = { onGroup(null) }, onClose = onDismiss, onSearch = { searching = true },
        )
        // Search is always visible; the existing multilingual index searches the whole catalog.
        Row(
            Modifier.padding(horizontal = 16.dp).fillMaxWidth().height(48.dp)
                .clip(AureaShape.Chip).background(AureaColors.FieldFilled)
                .testTag("effects.search.bar").semantics { role = Role.Button }
                .tocavel(shrink = 1f, onClick = { searching = true }).padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Search, 20.dp, AureaColors.Muted)
            Text(stringResource(R.string.effect_buscar_glitch_vhs_desfoque_cor),
                style = AureaType.of(14f, color = AureaColors.Muted), maxLines = 1,
                overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 8.dp))
        }
        val configuration = LocalConfiguration.current
        val scrollTabs = configuration.fontScale > 1.2f || configuration.screenWidthDp < 360
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp).testTag("effects.tabs")
            .then(if (scrollTabs) Modifier.horizontalScroll(rememberScrollState()) else Modifier)) {
            val labels = listOf(R.string.fx_browser_all, R.string.fx_browser_new, R.string.effect_favoritos, R.string.effect_recentes)
            val ids = listOf("all", "new", "favorites", "recent")
            labels.forEachIndexed { index, res ->
                val active = section == index
                val tabWidth = if (scrollTabs) Modifier.width((96f * configuration.fontScale.coerceAtLeast(1f)).dp) else Modifier.weight(1f)
                Box(tabWidth.height(48.dp).clip(AureaShape.Chip)
                    .background(if (active) AureaColors.AccentDim else Color.Transparent)
                    .testTag("effects.tab." + ids[index]).semantics { selected = active; role = Role.Tab }
                    .tocavel(shrink = 1f, onClick = { section = index; onGroup(null) }), contentAlignment = Alignment.Center) {
                    Text(stringResource(res), style = AureaType.of(13f, FontWeight.W600,
                        color = if (active) AureaColors.Accent else AureaColors.Muted), maxLines = 1)
                }
            }
        }
        if (section == 0) {
            LazyRow(contentPadding = PaddingValues(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth().testTag("effects.categories")) {
                item(key = "all") {
                    CategoryChip(stringResource(R.string.effects_all_effects), "all", groupId == null || groupId == ALL_GROUP) { onGroup(null) }
                }
                items(groups, key = { it.first.id }) { (g, entries) ->
                    CategoryChip(stringResource(effectGroupLabelRes(g)), g.id, groupId == g.id) { onGroup(g.id) }
                }
            }
        }
        Text(stringResource(R.string.fx_browser_count, shown.size), style = AureaType.of(12f, color = AureaColors.Muted),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp).testTag("effects.count"))
        val minWidth = if (LocalConfiguration.current.fontScale > 1.2f) 184.dp else 152.dp
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minWidth), state = grid,
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp),
            modifier = Modifier.fillMaxWidth().weight(1f).testTag(if (groupId != null) "effects.grid" else "effects.home"),
        ) {
            if (shown.isEmpty()) item(key = "empty", span = { GridItemSpan(maxLineSpan) }) {
                EmptyLine(stringResource(if (section == 2) R.string.effects_favorites_hint else R.string.effect_empty_category))
            }
            items(shown, key = { it.typeId }, contentType = { "effect" }) { e ->
                EffectPickCard(e, store.effectPreviews, e.typeId in prefs.favorites, "effects.card.", pick, onFavorite, nameSize = 14f)
            }
        }
    }
    if (searching) EffectSearchSheet(store, sorted, prefs.favorites, { searching = false; pick(it) }, onFavorite, { searching = false })
}

@Composable
private fun CategoryChip(label: String, id: String, active: Boolean, onClick: () -> Unit) {
    Box(Modifier.heightIn(min = 48.dp).clip(AureaShape.Chip)
        .background(if (active) AureaColors.EditorPanelHigh else AureaColors.SurfaceHigh)
        .testTag("effects.category.$id").semantics { selected = active; role = Role.Button }
        .tocavel(shrink = 1f, onClick = onClick).padding(horizontal = 14.dp, vertical = 10.dp), contentAlignment = Alignment.Center) {
        Text(label, style = AureaType.of(13f, FontWeight.W600, color = if (active) AureaColors.Text else AureaColors.Muted), maxLines = 1)
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
) {
    val name = pickerEntryName(entry)
    val tool = effectToolOf(entry.typeId)
    val addLabel = stringResource(R.string.effects_card_add, name)
    val favoriteLabel = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
    Column(
        modifier
            .clip(AureaShape.Card).background(AureaColors.EditorPanelHigh)
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
