package com.aurea.aurea.editor

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Typeface
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.io.File
import java.text.DateFormat
import java.util.Date

// =============================================================================
// Omega conteúdo (área 06, P0-2 / P0-3 / P1-12): o NAVEGADOR DE FONTES em tela
// cheia e as preferências de fonte do aparelho (favoritas, recentes, uso,
// filtros). O mini navegador do modo de texto (TextMode.kt) usa as mesmas
// regras daqui. Par do FontBrowserView.swift (mesmos ids e chaves).
// =============================================================================

/** Bits de escrita do motor (FontEntry::scripts) e categorias (FontEntry::category). */
internal object FontScripts {
    const val LATIN = 1 shl 0
    const val GREEK = 1 shl 1
    const val CYRILLIC = 1 shl 2
    const val HEBREW = 1 shl 3
    const val ARABIC = 1 shl 4
    const val THAI = 1 shl 5
    const val KOREAN = 1 shl 6
    const val JAPANESE = 1 shl 7
    const val CHINESE = 1 shl 8
    const val DEVANAGARI = 1 shl 9
    /** A ordem do menu "Escrita" (bit, rótulo). */
    val menu = listOf(
        LATIN to R.string.om_fb_script_latin, GREEK to R.string.om_fb_script_greek, CYRILLIC to R.string.om_fb_script_cyrillic,
        HEBREW to R.string.om_fb_script_hebrew, ARABIC to R.string.om_fb_script_arabic, THAI to R.string.om_fb_script_thai,
        KOREAN to R.string.om_fb_script_korean, JAPANESE to R.string.om_fb_script_japanese, CHINESE to R.string.om_fb_script_chinese,
        DEVANAGARI to R.string.om_fb_script_devanagari,
    )
    /** Categorias (FontCategory do motor) e o filtro "Importadas" (bit 5). */
    val categories = listOf(
        0 to R.string.om_fb_cat_sans, 1 to R.string.om_fb_cat_serif, 2 to R.string.om_fb_cat_display,
        3 to R.string.om_fb_cat_handwriting, 4 to R.string.om_fb_cat_mono,
    )
    val categoryTags = listOf("sans", "serif", "display", "handwriting", "mono")
    const val FILTER_FAVORITE = 1 shl 5
    const val FILTER_IMPORTED = 1 shl 6

    /**
     * Amostra própria do AUREA: latim, ou — se a face cobre UMA só escrita não
     * latina — uma amostra curta dessa escrita (+ "Abcd" no navegador).
     * Ideogramas contam junto do japonês/coreano.
     */
    fun sample(scripts: Int, long: Boolean): String {
        var other = scripts and LATIN.inv()
        // Grego e cirílico vêm de brinde em fontes de outras escritas: não contam.
        val primary = JAPANESE or KOREAN or CHINESE or ARABIC or HEBREW or THAI or DEVANAGARI
        if (other and primary != 0) other = other and (GREEK or CYRILLIC).inv()
        if (other and (JAPANESE or KOREAN) != 0) other = other and CHINESE.inv()
        val own = when (other) {
            GREEK -> "Αβγ"
            CYRILLIC -> "Абв"
            HEBREW -> "אבג"
            ARABIC -> "أبج"
            THAI -> "กขค"
            KOREAN -> "가나다"
            JAPANESE -> "あいう"
            CHINESE -> "中文字"
            DEVANAGARI -> "कखग"
            else -> null
        }
        return when {
            own == null -> if (long) "Abcdefg" else "Abc"
            long -> "$own Abcd"
            else -> own
        }
    }
}

/**
 * Preferências de fonte do aparelho (`aurea.fonts`): favoritas, recentes
 * (até 12), quantas vezes cada uma foi usada ("Mais usadas"), ordenação,
 * filtros e escrita. Chave da face = "família|peso|itálico".
 */
internal object FontPrefs {
    private const val FILE = "aurea.fonts"
    private var prefs: SharedPreferences? = null

    var favorites by mutableStateOf<Set<String>>(emptySet())
        private set
    var recents by mutableStateOf<List<String>>(emptyList())
        private set
    var useCount by mutableStateOf<Map<String, Int>>(emptyMap())
        private set
    /** 0 mais usadas, 1 A–Z. */
    var sort by mutableStateOf(0)
        private set
    /** Bits 0..4 categorias, 5 favoritas, 6 importadas. */
    var filters by mutableStateOf(0)
        private set
    /** Bit da escrita escolhida (0 = todas). */
    var script by mutableStateOf(0)
        private set

    fun key(f: EditorStore.FontItem): String = "${f.family}|${f.weight}|${if (f.italic) 1 else 0}"

    fun load(context: Context) {
        if (prefs != null) return
        val p = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        prefs = p
        favorites = p.getStringSet("favorites", emptySet())?.toSet() ?: emptySet()
        recents = p.getString("recents", "").orEmpty().split('\n').filter { it.isNotEmpty() }
        useCount = p.getString("useCount", "").orEmpty().split('\n').mapNotNull { line ->
            val t = line.lastIndexOf('\t')
            if (t <= 0) null else line.substring(0, t) to (line.substring(t + 1).toIntOrNull() ?: 0)
        }.toMap()
        sort = p.getInt("sort", 0).coerceIn(0, 1)
        filters = p.getInt("filters", 0)
        script = p.getInt("script", 0)
    }

    fun toggleFavorite(f: EditorStore.FontItem) {
        val k = key(f)
        favorites = if (k in favorites) favorites - k else favorites + k
        prefs?.edit()?.putStringSet("favorites", favorites)?.apply()
    }

    /** A face foi aplicada: vai para o topo das recentes e conta um uso. */
    fun recordUse(f: EditorStore.FontItem) {
        val k = key(f)
        recents = (listOf(k) + recents.filter { it != k }).take(12)
        useCount = useCount + (k to ((useCount[k] ?: 0) + 1))
        prefs?.edit()
            ?.putString("recents", recents.joinToString("\n"))
            ?.putString("useCount", useCount.entries.joinToString("\n") { "${it.key}\t${it.value}" })
            ?.apply()
    }

    @JvmName("saveSort") fun setSort(v: Int) { sort = v; prefs?.edit()?.putInt("sort", v)?.apply() }
    fun toggleFilter(bit: Int) { filters = filters xor bit; prefs?.edit()?.putInt("filters", filters)?.apply() }
    @JvmName("saveScript") fun setScript(bit: Int) { script = bit; prefs?.edit()?.putInt("script", bit)?.apply() }

    /**
     * O mini navegador (regra da referência): favoritas em A–Z; recentes sem as
     * 3 primeiras favoritas — todas sem favoritas, senão max(6 − favoritas, 3);
     * a fonte atual na frente se não estiver na lista. Devolve (face, cabeçalho)
     * com cabeçalho 1 = relógio (1ª recente), 2 = estrela (1ª favorita).
     */
    fun miniList(all: List<EditorStore.FontItem>, current: EditorStore.FontItem?): List<Pair<EditorStore.FontItem?, Int>> {
        val byKey = all.associateBy { key(it) }
        val favs = favorites.mapNotNull { byKey[it] }.sortedWith(compareBy(String.CASE_INSENSITIVE_ORDER) { "${it.family} ${it.style}" })
        val firstFavs = favs.take(3).map { key(it) }.toSet()
        val rec = recents.mapNotNull { byKey[it] }.filter { key(it) !in firstFavs }
        val n = if (favs.isEmpty()) rec.size else maxOf(6 - favs.size, 3)
        val recShown = rec.take(n)
        val out = ArrayList<Pair<EditorStore.FontItem?, Int>>()
        val listed = (recShown + favs).map { key(it) }.toSet()
        if (current == null || key(current) !in listed) out += current to 0
        recShown.forEachIndexed { i, f -> out += f to (if (i == 0) 1 else 0) }
        favs.forEachIndexed { i, f -> out += f to (if (i == 0) 2 else 0) }
        return out
    }
}

/** Pedido de abrir o navegador de fontes fora do modo de texto (o Dock da UI Edição). */
internal object FontBrowserRequest {
    var open by mutableStateOf(false)
}

/** Prévia de cada face na PRÓPRIA fonte (um Typeface por arquivo, guardado). */
private val faceCache = HashMap<String, FontFamily?>()

internal fun fontFamilyOf(path: String?): FontFamily? {
    if (path.isNullOrEmpty()) return null
    return faceCache.getOrPut(path) {
        try {
            if (File(path).exists()) FontFamily(Typeface.createFromFile(path)) else null
        } catch (_: Exception) {
            null
        }
    }
}

/** Busca da referência: o início do nome ou o início de uma palavra dele. */
internal fun fontMatches(name: String, query: String): Int {
    if (query.isBlank()) return 0
    val n = name.lowercase()
    val q = query.trim().lowercase()
    if (n.startsWith(q)) return 0
    val at = n.indexOf(" $q")
    return if (at >= 0) at + 1 else -1
}

/** Nome com o trecho buscado em destaque e o resto em texto secundário. */
private fun highlighted(name: String, query: String): AnnotatedString {
    val at = fontMatches(name, query)
    if (query.isBlank() || at < 0) return AnnotatedString(name)
    val end = (at + query.trim().length).coerceAtMost(name.length)
    return buildAnnotatedString {
        withStyle(SpanStyle(color = AureaColors.Muted)) { append(name.substring(0, at)) }
        withStyle(SpanStyle(color = AureaColors.Accent)) { append(name.substring(at, end)) }
        withStyle(SpanStyle(color = AureaColors.Muted)) { append(name.substring(end)) }
    }
}

/**
 * O navegador de fontes em tela cheia: barra ☰ · título · 🔍, grade de 2
 * colunas com uma célula de 100 dp por face, ★ para favoritar, ícone das
 * importadas (detalhes e "Apagar fonte") e a gaveta de filtros. Tocar na
 * célula aplica a face ([onPick]) e fecha.
 */
@Composable
internal fun FontBrowserDialog(store: EditorStore, onPick: (EditorStore.FontItem) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { FontPrefs.load(context); store.loadFonts() }
    val fonts by remember(store) { derivedStateOf { store.fonts } }
    var query by remember { mutableStateOf("") }
    var searching by remember { mutableStateOf(false) }
    var drawer by remember { mutableStateOf(false) }
    var details by remember { mutableStateOf<EditorStore.FontItem?>(null) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
        if (uris.isNotEmpty()) store.importFonts(uris)
    }
    val filters = FontPrefs.filters
    val script = FontPrefs.script
    val sort = FontPrefs.sort
    val favorites = FontPrefs.favorites
    val visible = remember(fonts, query, filters, script, sort, favorites, FontPrefs.useCount) {
        val cats = (0..4).filter { filters and (1 shl it) != 0 }.toSet()
        val importedOn = filters and FontScripts.FILTER_IMPORTED != 0
        val favOn = filters and FontScripts.FILTER_FAVORITE != 0
        val noCategory = cats.isEmpty() && !importedOn
        fonts.filter { f ->
            val fav = FontPrefs.key(f) in favorites
            val catOk = noCategory || f.category in cats || (importedOn && f.imported)
            val scriptOk = script == 0 || (f.scripts and script) != 0
            ((favOn && fav) || (catOk && scriptOk && (!favOn || !noCategory || fav))) &&
                fontMatches("${f.family} ${f.style}", query) >= 0
        }.let { list ->
            if (sort == 0) list.sortedWith(compareByDescending<EditorStore.FontItem> { FontPrefs.useCount[FontPrefs.key(it)] ?: 0 }
                .thenBy(String.CASE_INSENSITIVE_ORDER) { it.family }.thenBy { it.italic }.thenBy { it.weight })
            else list.sortedWith(compareBy<EditorStore.FontItem, String>(String.CASE_INSENSITIVE_ORDER) { it.family }.thenBy { it.italic }.thenBy { it.weight })
        }
    }
    val title = browserTitle(filters, script)
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        // Dentro da janela do diálogo: o Voltar fecha a gaveta e a busca antes do navegador.
        BackHandler(enabled = drawer || searching) {
            if (drawer) drawer = false else { searching = false; query = "" }
        }
        Box(Modifier.fillMaxSize().background(AureaColors.ContentBrowser).windowInsetsPadding(WindowInsets.safeDrawing).testTag("font.browser")) {
            Column(Modifier.fillMaxSize()) {
                BrowserBar(title, query, searching,
                    onMenu = { drawer = true },
                    onSearch = { searching = true },
                    onQuery = { query = it },
                    onCancel = { searching = false; query = "" },
                    onClose = onDismiss)
                if (visible.isEmpty()) {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        Text(stringResource(R.string.om_fb_empty), style = AureaType.of(13f, color = AureaColors.Muted))
                    }
                } else {
                    LazyVerticalGrid(
                        columns = GridCells.Fixed(2),
                        modifier = Modifier.fillMaxSize().testTag("font.browser.grid"),
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(8.dp),
                    ) {
                        items(visible, key = { it.path + "#" + FontPrefs.key(it) }) { f ->
                            FontCell(f, query, FontPrefs.key(f) in favorites,
                                onPick = { onPick(f) },
                                onFavorite = { FontPrefs.toggleFavorite(f) },
                                onImported = { details = f })
                        }
                    }
                }
            }
            if (drawer) {
                Row(Modifier.fillMaxSize()) {
                    FilterDrawer(fonts, onImport = {
                        drawer = false
                        pick.launch(arrayOf("font/ttf", "font/otf", "font/collection", "font/woff", "application/font-woff", "application/x-font-ttf", "application/octet-stream", "*/*"))
                    })
                    Box(Modifier.weight(1f).fillMaxHeight().background(AureaColors.FxScrim).tocavel(shrink = 1f) { drawer = false })
                }
            }
        }
    }
    details?.let { f -> ImportedFontDetails(store, f, onDismiss = { details = null }) }
}

@Composable
private fun browserTitle(filters: Int, script: Int): String {
    val cats = FontScripts.categories.filter { filters and (1 shl it.first) != 0 }
    val favOn = filters and FontScripts.FILTER_FAVORITE != 0
    val importedOn = filters and FontScripts.FILTER_IMPORTED != 0
    val count = cats.size + (if (favOn) 1 else 0) + (if (importedOn) 1 else 0)
    val scriptLabel = FontScripts.menu.firstOrNull { it.first == script }?.second
    return when {
        count == 0 && scriptLabel == null -> stringResource(R.string.om_fb_title_all)
        count == 0 && scriptLabel != null -> stringResource(R.string.om_fb_title_of, stringResource(scriptLabel))
        count == 1 && scriptLabel == null -> stringResource(R.string.om_fb_title_of, stringResource(
            cats.firstOrNull()?.second ?: if (favOn) R.string.om_fb_cat_favorite else R.string.om_fb_cat_imported))
        else -> stringResource(R.string.om_fb_title_filtered)
    }
}

@Composable
private fun BrowserBar(
    title: String,
    query: String,
    searching: Boolean,
    onMenu: () -> Unit,
    onSearch: () -> Unit,
    onQuery: (String) -> Unit,
    onCancel: () -> Unit,
    onClose: () -> Unit,
) {
    val line = AureaColors.ContentBrowserLine
    val focus = remember { FocusRequester() }
    Row(
        Modifier.fillMaxWidth().height(42.dp).drawBehind {
            val h = 0.5.dp.toPx()
            drawRect(line, Offset(0f, size.height - h), androidx.compose.ui.geometry.Size(size.width, h))
        },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BarIcon(stringResource(R.string.common_close), "font.browser.close", onClose) { CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, AureaColors.Text) }
        if (!searching) BarIcon(stringResource(R.string.om_fb_menu_cd), "font.browser.menu", onMenu) { CupertinoIcon(CupertinoGlyph.LineHorizontal3, 22.dp, AureaColors.Text) }
        Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
            if (searching) {
                LaunchedEffect(Unit) { focus.requestFocus() }
                BasicTextField(
                    value = query,
                    onValueChange = onQuery,
                    singleLine = true,
                    textStyle = AureaType.of(14f, color = AureaColors.Accent),
                    cursorBrush = SolidColor(AureaColors.Accent),
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = {}),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus).testTag("font.browser.search.field"),
                )
                if (query.isEmpty()) Text(stringResource(R.string.panel_buscar_fonte), modifier = Modifier.fillMaxWidth(),
                    style = AureaType.of(14f, color = AureaColors.Muted))
            } else {
                Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                    modifier = Modifier.semantics { heading() }, style = AureaType.of(14f, FontWeight.W600))
            }
        }
        if (searching) BarIcon(stringResource(R.string.common_cancel), "font.browser.search.cancel", onCancel) { CupertinoIcon(CupertinoGlyph.Xmark, 20.dp, AureaColors.Text) }
        else BarIcon(stringResource(R.string.om_fb_search_cd), "font.browser.search", onSearch) { CupertinoIcon(CupertinoGlyph.Search, 20.dp, AureaColors.Text) }
    }
}

@Composable
private fun BarIcon(label: String, tag: String, onClick: () -> Unit, content: @Composable () -> Unit) {
    Box(Modifier.size(44.dp, 42.dp).testTag(tag).semantics { contentDescription = label }.tocavel(onClick = onClick),
        contentAlignment = Alignment.Center) { content() }
}

@Composable
private fun FontCell(f: EditorStore.FontItem, query: String, favorite: Boolean, onPick: () -> Unit, onFavorite: () -> Unit, onImported: () -> Unit) {
    val name = "${f.family} ${f.style}".trim()
    val family = fontFamilyOf(f.path)
    val tag = "font.item.${FontPrefs.key(f)}"
    Box(
        Modifier
            .fillMaxWidth()
            .height(100.dp)
            .clip(RoundedCornerShape(6.dp))
            .border(1.dp, AureaColors.ContentBrowserLine, RoundedCornerShape(6.dp))
            .testTag(tag)
            .semantics { contentDescription = name }
            .tocavel(shrink = 1f, onClick = onPick),
    ) {
        Column(Modifier.fillMaxSize().padding(horizontal = 10.dp), verticalArrangement = Arrangement.Center) {
            Box(Modifier.fillMaxWidth().height(46.dp), contentAlignment = Alignment.CenterStart) {
                Text(
                    FontScripts.sample(f.scripts, long = true),
                    maxLines = 1,
                    overflow = TextOverflow.Clip,
                    // Sem a face carregada, o nome em texto secundário (P2-13).
                    style = TextStyle(fontSize = 20.sp, fontFamily = family ?: FontFamily.Default,
                        color = if (family != null) AureaColors.Text else AureaColors.Muted),
                )
            }
            Text(highlighted(name, query), maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(end = 30.dp),
                style = AureaType.of(12f, color = if (query.isBlank()) AureaColors.Text else AureaColors.Muted))
        }
        if (f.imported) {
            Box(Modifier.align(Alignment.TopStart).size(36.dp).testTag("$tag.imported")
                .semantics { contentDescription = name }
                .tocavel(shrink = 1f, onClick = onImported), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ArrowDownCircleFill, 18.dp, AureaColors.Text, Modifier.alpha(0.38f))
            }
        }
        val favLabel = stringResource(if (favorite) R.string.om_fb_fav_remove_cd else R.string.om_fb_fav_add_cd)
        Box(Modifier.align(Alignment.BottomEnd).size(40.dp).testTag("$tag.fav")
            .semantics { contentDescription = favLabel; selected = favorite }
            .tocavel(shrink = 1f, onClick = onFavorite), contentAlignment = Alignment.Center) {
            CupertinoIcon(if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 18.dp, if (favorite) AureaColors.Accent else AureaColors.Text)
        }
    }
}

/** A gaveta de filtros: categorias, ordenação, escrita e "Importar fontes". */
@Composable
private fun FilterDrawer(fonts: List<EditorStore.FontItem>, onImport: () -> Unit) {
    val hasImported = fonts.any { it.imported }
    val scripts = remember(fonts) { fonts.fold(0) { acc, f -> acc or f.scripts } }
    var sortOpen by remember { mutableStateOf(false) }
    var scriptOpen by remember { mutableStateOf(false) }
    Column(
        Modifier
            .width(280.dp)
            .fillMaxHeight()
            .background(AureaColors.ContentDrawer)
            .verticalScroll(rememberScrollState()),
    ) {
        Box(Modifier.fillMaxWidth().height(42.dp).background(AureaColors.ContentDrawerHeader).padding(horizontal = 16.dp), contentAlignment = Alignment.CenterStart) {
            Text(stringResource(R.string.om_fb_menu_cd), style = AureaType.of(14f, FontWeight.W600))
        }
        Spacer(Modifier.height(12.dp))
        FontScripts.categories.forEachIndexed { i, (bit, label) ->
            FilterBox(stringResource(label), "font.filter.${FontScripts.categoryTags[i]}", FontPrefs.filters and (1 shl bit) != 0) {
                FontPrefs.toggleFilter(1 shl bit)
            }
        }
        FilterBox(stringResource(R.string.om_fb_cat_favorite), "font.filter.favorite", FontPrefs.filters and FontScripts.FILTER_FAVORITE != 0) {
            FontPrefs.toggleFilter(FontScripts.FILTER_FAVORITE)
        }
        if (hasImported) {
            FilterBox(stringResource(R.string.om_fb_cat_imported), "font.filter.imported", FontPrefs.filters and FontScripts.FILTER_IMPORTED != 0) {
                FontPrefs.toggleFilter(FontScripts.FILTER_IMPORTED)
            }
        }
        DrawerDivider()
        Text(stringResource(R.string.om_fb_sort), Modifier.padding(start = 16.dp, top = 6.dp), style = AureaType.of(12f, color = AureaColors.Muted))
        Box(Modifier.padding(horizontal = 16.dp, vertical = 6.dp)) {
            DrawerMenuBox(stringResource(if (FontPrefs.sort == 0) R.string.om_fb_sort_used else R.string.om_fb_sort_alpha), "font.sort") { sortOpen = true }
            com.aurea.aurea.ui.ds.AnchoredMenu(
                expanded = sortOpen, onDismiss = { sortOpen = false }, matchAnchorWidth = true, tag = "font.sort.menu",
                entries = listOf(
                    com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.om_fb_sort_used), selected = FontPrefs.sort == 0) { FontPrefs.setSort(0) },
                    com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.om_fb_sort_alpha), selected = FontPrefs.sort == 1) { FontPrefs.setSort(1) },
                ),
            )
        }
        Text(stringResource(R.string.om_fb_script), Modifier.padding(start = 16.dp, top = 6.dp), style = AureaType.of(12f, color = AureaColors.Muted))
        Box(Modifier.padding(horizontal = 16.dp, vertical = 6.dp)) {
            val current = FontScripts.menu.firstOrNull { it.first == FontPrefs.script }?.second ?: R.string.om_fb_script_all
            DrawerMenuBox(stringResource(current), "font.script") { scriptOpen = true }
            val items = buildList {
                add(com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.om_fb_script_all), selected = FontPrefs.script == 0) { FontPrefs.setScript(0) })
                FontScripts.menu.filter { scripts and it.first != 0 }.forEach { (bit, label) ->
                    add(com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(label), selected = FontPrefs.script == bit) { FontPrefs.setScript(bit) })
                }
            }
            com.aurea.aurea.ui.ds.AnchoredMenu(expanded = scriptOpen, onDismiss = { scriptOpen = false }, matchAnchorWidth = true,
                tag = "font.script.menu", entries = items)
        }
        DrawerDivider()
        Row(
            Modifier.fillMaxWidth().height(48.dp).testTag("font.import").tocavel(onClick = onImport).padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.ArrowDownCircleFill, 18.dp, AureaColors.Text)
            Spacer(Modifier.width(10.dp))
            Text(stringResource(R.string.om_fb_import), style = AureaType.of(14f))
        }
    }
}

@Composable
private fun FilterBox(label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(40.dp).testTag(tag).semantics { selected = on }.tocavel(shrink = 1f, onClick = onClick).padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(20.dp).clip(RoundedCornerShape(4.dp))
                .background(if (on) AureaColors.Accent else AureaColors.ContentDrawer)
                .border(1.5.dp, if (on) AureaColors.Accent else AureaColors.Muted, RoundedCornerShape(4.dp)),
            contentAlignment = Alignment.Center,
        ) { if (on) CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 14.dp, AureaColors.OnAccent) }
        Spacer(Modifier.width(12.dp))
        Text(label, style = AureaType.of(14f))
    }
}

@Composable
private fun DrawerDivider() {
    Box(Modifier.padding(10.dp).fillMaxWidth().height(1.dp).background(AureaColors.ContentBrowserLine))
}

@Composable
private fun DrawerMenuBox(value: String, tag: String, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(40.dp).clip(RoundedCornerShape(6.dp)).background(AureaColors.ContentStrongLine)
            .testTag(tag).tocavel(shrink = 1f, onClick = onClick).padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(value, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.of(14f))
        CupertinoIcon(CupertinoGlyph.ChevronDown, 14.dp, AureaColors.Muted)
    }
}

/** Detalhes da fonte importada (P1-12): o nome, a data e "Apagar fonte" com confirmação. */
@Composable
private fun ImportedFontDetails(store: EditorStore, f: EditorStore.FontItem, onDismiss: () -> Unit) {
    var confirm by remember { mutableStateOf(false) }
    val date = remember(f.path) {
        val t = File(f.path).lastModified()
        if (t > 0L) DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(t)) else ""
    }
    val deleted = stringResource(R.string.om_fb_deleted)
    if (!confirm) {
        AureaDialog(
            title = "${f.family} ${f.style}".trim(),
            message = if (date.isNotEmpty()) stringResource(R.string.om_fb_imported_on, date) else null,
            onDismiss = onDismiss,
            tag = "font.details",
            actions = listOf(
                DialogAction(stringResource(R.string.common_close), DialogRole.Negative),
                DialogAction(stringResource(R.string.om_fb_delete_font), DialogRole.Neutral, destructive = true,
                    dismiss = false, tag = "font.item.${FontPrefs.key(f)}.delete") { confirm = true },
            ),
        )
    } else {
        AureaAlert(
            title = stringResource(R.string.om_fb_delete_confirm),
            confirmLabel = stringResource(R.string.om_fb_delete_font),
            destructive = true,
            onConfirm = { if (store.removeImportedFont(f)) store.showToast(deleted) },
            onDismiss = onDismiss,
        )
    }
}
