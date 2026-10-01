package com.aurea.aurea.effects

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
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
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectDisplayName
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
//  A ABA "ADICIONAR" DO PAINEL EFEITOS (o escolhedor).
//
//  De cima para baixo, sem nível nenhum para descer:
//   · a BUSCA, sempre no topo — um toque abre a folha de busca por cima do
//     teclado (o painel mora embaixo da tela: um campo ali ficaria atrás dele);
//   · as CATEGORIAS em fichas de um toque, "Todos" primeiro;
//   · sem categoria: "Recentes" e "Favoritos", cada um uma linha que rola;
//   · a GRADE de cartões com a prévia real de cada efeito.
//
//  UM toque no cartão adiciona o efeito e abre os controles dele; SEGURAR o
//  cartão põe ou tira dos favoritos. A lógica (filtro, busca, áudio, recentes)
//  mora em EffectPickerLogic.kt.
// =============================================================================

/** Tamanho em que a prévia é gerada (o mesmo do cache em disco de antes). */
private const val PREVIEW_W = 320
private const val PREVIEW_H = 200

/** Largura mínima de uma coluna da grade (3 colunas num celular de 360 dp). */
private val CardMin = 100.dp
/** Largura dos cartões nas linhas de Recentes e Favoritos. */
private val RowCard = 92.dp
/** Altura da faixa de busca e da faixa de fichas: alvo de dedo inteiro. */
private val StripHeight = AureaDims.MinTap

/**
 * O escolhedor. [onPick] recebe o efeito tocado — quem chama adiciona na
 * camada, guarda nos recentes e mostra os controles dele.
 */
@Composable
internal fun EffectPicker(
    store: EditorStore,
    layerHasAudio: Boolean,
    onPick: (EffectCatalogEntry) -> Unit,
    modifier: Modifier = Modifier,
) {
    val prefs = store.effectPrefs
    val catalog = store.catalog
    val textLayer = store.detail?.kind == 4
    val text3dLayer = store.text3d != null
    val pickable = remember(catalog, layerHasAudio, textLayer, text3dLayer) {
        pickableEffects(catalog, layerHasAudio).filter {
            (textLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.transform")) &&
                (textLayer || text3dLayer || it.typeId != com.aurea.aurea.editor.panels.effectTypeId("aurea.text.animator"))
        }
    }
    val categories = remember(pickable) { effectCategories(pickable) }
    val sorted = remember(pickable, categories) { arrangeCatalog(pickable, categories) }
    var chosen by rememberSaveable { mutableStateOf<String?>(null) }
    // Categoria que sumiu (camada sem som e a ficha era "Áudio") volta a "Todos".
    val category = chosen?.takeIf { it in categories }
    var searching by remember { mutableStateOf(false) }
    val recents = remember(prefs.recents, pickable) { recentEffects(prefs.recents, pickable) }
    val favorites = remember(prefs.favorites, sorted) { favoriteEffects(prefs.favorites, sorted) }
    val shown = remember(sorted, category) { browseEffects(sorted, category) }
    val onFavorite = rememberFavoriteToggle(store)
    val grid = rememberLazyGridState()
    LaunchedEffect(category) { grid.scrollToItem(0) }

    Column(modifier.fillMaxSize()) {
        SearchLauncher(onClick = { searching = true })
        CategoryStrip(categories, category, onSelect = { chosen = it })
        LazyVerticalGrid(
            columns = GridCells.Adaptive(CardMin),
            state = grid,
            contentPadding = PaddingValues(start = AureaDims.S3, end = AureaDims.S3, top = AureaDims.S1, bottom = AureaDims.S4),
            horizontalArrangement = Arrangement.spacedBy(AureaDims.S2),
            verticalArrangement = Arrangement.spacedBy(AureaDims.S3),
            modifier = Modifier.fillMaxWidth().weight(1f).testTag("effects.grid"),
        ) {
            if (category == null) {
                if (recents.isNotEmpty()) {
                    item(key = "recentes", span = { GridItemSpan(maxLineSpan) }) {
                        PickerRow(stringResource(R.string.effect_recentes), recents, "effects.recent.", store, prefs.favorites, onPick, onFavorite)
                    }
                }
                if (favorites.isNotEmpty()) {
                    item(key = "favoritos", span = { GridItemSpan(maxLineSpan) }) {
                        PickerRow(stringResource(R.string.effect_favoritos), favorites, "effects.favorite.", store, prefs.favorites, onPick, onFavorite)
                    }
                }
                item(key = "todos", span = { GridItemSpan(maxLineSpan) }) {
                    Column {
                        if (recents.isNotEmpty() || favorites.isNotEmpty()) SectionTitle(stringResource(R.string.effects_all_effects))
                        if (favorites.isEmpty()) {
                            Text(
                                stringResource(R.string.effects_favorites_hint),
                                style = AureaType.CardSpec,
                                modifier = Modifier.padding(top = AureaDims.S1, bottom = AureaDims.S1),
                            )
                        }
                    }
                }
            }
            if (shown.isEmpty()) {
                item(key = "vazio", span = { GridItemSpan(maxLineSpan) }) { EmptyLine(stringResource(R.string.effect_empty_category)) }
            }
            items(shown, key = { it.typeId }, contentType = { "efeito" }) { e ->
                EffectPickCard(e, store.effectPreviews, e.typeId in prefs.favorites, "effects.card.", onPick, onFavorite)
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
                onPick(it)
            },
            onFavorite = onFavorite,
            onDismiss = { searching = false },
        )
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

/**
 * A busca, sempre no topo: parece o campo e abre a folha de busca por cima do
 * teclado. testTag `effects.search`.
 */
@Composable
private fun SearchLauncher(onClick: () -> Unit) {
    val hint = stringResource(R.string.effect_buscar_glitch_vhs_desfoque_cor)
    Box(
        Modifier
            .fillMaxWidth()
            .height(StripHeight)
            .padding(horizontal = AureaDims.S3)
            .testTag("effects.search")
            .semantics { contentDescription = hint }
            .tocavel(shrink = 1f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            Modifier.fillMaxWidth().height(AureaDims.SearchField - AureaDims.S1).clip(AureaShape.Chip).background(AureaColors.FieldFilled).padding(horizontal = AureaDims.S2),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Search, AureaDims.IconSm, AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            Text(hint, style = AureaType.of(14f, color = AureaColors.Muted), maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

/** As categorias em fichas de um toque: "Todos" e as do catálogo, na ordem dele. */
@Composable
private fun CategoryStrip(categories: List<String>, selected: String?, onSelect: (String?) -> Unit) {
    LazyRow(
        Modifier.fillMaxWidth().height(StripHeight),
        contentPadding = PaddingValues(horizontal = AureaDims.S3),
        horizontalArrangement = Arrangement.spacedBy(AureaDims.S2),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        item(key = ALL_CATEGORIES_ID) {
            CategoryChip(stringResource(R.string.effect_todos), selected == null, ALL_CATEGORIES_ID) { onSelect(null) }
        }
        items(categories, key = { it }) { c ->
            CategoryChip(effectCategoryLabel(c), selected == c, effectCategoryId(c)) { onSelect(c) }
        }
    }
}

@Composable
private fun CategoryChip(label: String, on: Boolean, id: String, onClick: () -> Unit) {
    // O alvo é a faixa inteira (44 dp); a ficha desenhada é a de 34 dp no meio.
    Box(
        Modifier
            .fillMaxHeight()
            .testTag("effects.category.$id")
            .semantics { selected = on }
            .tocavel(shrink = 1f, role = Role.Tab, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .height(AureaDims.ChipHeight)
                .clip(AureaShape.Chip)
                .background(if (on) AureaColors.ActionDim else AureaColors.Chip)
                .then(if (on) Modifier.border(1.dp, AureaColors.Action, AureaShape.Chip) else Modifier)
                .padding(horizontal = AureaDims.S3),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.ChipLabel.copy(color = if (on) AureaColors.Action else AureaColors.Text), maxLines = 1)
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, style = AureaType.Section, modifier = Modifier.padding(top = AureaDims.S1, bottom = AureaDims.S2))
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

/** Uma linha que rola de lado (Recentes, Favoritos): título e os cartões. */
@Composable
private fun PickerRow(
    title: String,
    entries: List<EffectCatalogEntry>,
    tagPrefix: String,
    store: EditorStore,
    favorites: Set<Int>,
    onPick: (EffectCatalogEntry) -> Unit,
    onFavorite: (EffectCatalogEntry, String) -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        SectionTitle(title)
        LazyRow(horizontalArrangement = Arrangement.spacedBy(AureaDims.S2)) {
            items(entries, key = { it.typeId }) { e ->
                EffectPickCard(e, store.effectPreviews, e.typeId in favorites, tagPrefix, onPick, onFavorite, Modifier.width(RowCard))
            }
        }
    }
}

/**
 * O CARTÃO: a prévia real do efeito (ou a cartela da categoria enquanto ela não
 * chega), a estrela quando é favorito e o nome em até duas linhas. Um toque
 * adiciona; segurar favorita. Todo o cartão é o alvo (bem mais que 44 dp).
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
) {
    val name = effectDisplayName(entry.typeId, entry.name)
    val preview = rememberEffectPreview(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
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
        Box(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(AureaShape.Card)) {
            if (preview != null) {
                Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
            } else {
                GenericPlate(categoryGlyph(entry.category))
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
            style = AureaType.of(12f, FontWeight.W600, lineHeight = 1.2f),
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
 * sinônimos da tabela humana, descrição); nos 7 idiomas, o nome e a categoria;
 * e os sinônimos de categoria. Refeito só quando o catálogo ou o idioma mudam.
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
            val names = buildList {
                add(e.name)
                languages.forEach { add(englishEffectName(e.typeId, e.name, it)) }
            }
            val extra = buildList {
                add(current[e.typeId].orEmpty())
                add(human[e.typeId].orEmpty())
                add(e.category)
                categoryLabelRes(e.category)?.let { res -> languages.forEach { add(it.getString(res)) } }
                add(categorySynonyms(e.category))
            }
            e.typeId to effectSearchDoc(names, extra)
        }
    }
}
