package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.presets.PresetEntry
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaNamePrompt
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date

/** O glifo de cada tipo de preset (o mesmo no iOS). */
internal val PresetKind.glyph: Char
    get() = when (this) {
        PresetKind.Effects -> CupertinoGlyph.Sparkles
        PresetKind.Text -> CupertinoGlyph.Textformat
        PresetKind.Animation -> CupertinoGlyph.Move
        PresetKind.Caption -> CupertinoGlyph.CaptionsBubble
        PresetKind.Curve -> CupertinoGlyph.Scribble
    }

/** Ordem da aba Presets: o índice é o que fica gravado. */
internal enum class PresetSort { Name, Recent }

/**
 * Omega home · P1-5: a ABA PRESETS no lugar da aba Modelos da referência (D3).
 * Mesma anatomia da aba Projetos: pílulas **Meus presets** · **Comunidade**
 * (deslizar troca a subaba), a linha "Meus presets" com a ordem e linhas de
 * 96 dp com o glifo do tipo e a ficha (tipo, data). Presets da pessoa têm o
 * menu rápido (Renomear · Excluir); os nativos do AUREA, não.
 */
@Composable
internal fun PresetsTab(store: EditorStore) {
    val pager = rememberPagerState { 2 }
    val scope = rememberCoroutineScope()
    Column(Modifier.fillMaxSize().testTag("home.presets.tab")) {
        Spacer(Modifier.height(18.dp))
        HomeSubtabs(
            listOf(
                stringResource(R.string.om_home_my_presets) to "home.presets.subtab.mine",
                stringResource(R.string.social_community) to "home.presets.subtab.community",
            ),
            pager.currentPage,
        ) { scope.launch { pager.animateScrollToPage(it) } }
        Spacer(Modifier.height(8.dp))
        HorizontalPager(pager, Modifier.weight(1f).fillMaxWidth()) { page ->
            if (page == 0) MyPresets(store) else CommunityPresetsTab()
        }
    }
}

@Composable
private fun MyPresets(store: EditorStore) {
    val library = store.presets
    val context = LocalContext.current
    var sortIndex by rememberSaveable { mutableIntStateOf(PresetSort.Name.ordinal) }
    val sort = PresetSort.entries[sortIndex]
    var sortOpen by remember { mutableStateOf(false) }
    var quick by remember { mutableStateOf<Pair<PresetEntry, QuickMenuAnchor>?>(null) }
    var renaming by remember { mutableStateOf<PresetEntry?>(null) }
    var deleting by remember { mutableStateOf<PresetEntry?>(null) }
    var areaBottom by remember { mutableFloatStateOf(Float.MAX_VALUE) }
    // `library.user` é estado: salvar, renomear e excluir redesenham a lista.
    val user = library.user
    val entries = remember(user, sort) {
        val mine = PresetKind.entries.flatMap { user[it].orEmpty() }
        val builtin = PresetKind.entries.flatMap { library.entries(it).filter { e -> e.builtin } }
        when (sort) {
            PresetSort.Name -> (mine + builtin).sortedBy { it.name.lowercase() }
            PresetSort.Recent -> mine.sortedByDescending { it.file?.lastModified() ?: 0L } + builtin.sortedBy { it.name.lowercase() }
        }
    }
    val listState = rememberLazyListState()
    val scrolled by remember(listState) {
        derivedStateOf { listState.firstVisibleItemIndex != 0 || listState.firstVisibleItemScrollOffset > 0 }
    }
    val sortLabels = mapOf(PresetSort.Name to stringResource(R.string.home_sort_name), PresetSort.Recent to stringResource(R.string.home_sort_recent))
    Column(Modifier.fillMaxSize().onGloballyPositioned { areaBottom = it.boundsInWindow().bottom }) {
        HomeListHeader(
            title = stringResource(R.string.om_home_my_presets),
            sortLabel = stringResource(R.string.om_home_sort_current, sortLabels.getValue(sort)),
            onSort = { sortOpen = true },
            tag = "home.presets.header",
            sortTag = "home.presets.sort",
            sortAnchor = {
                AnchoredMenu(
                    expanded = sortOpen,
                    onDismiss = { sortOpen = false },
                    width = (LocalConfiguration.current.screenWidthDp * 0.6f).dp,
                    alignEnd = true,
                    tag = "home.presets.sortMenu",
                    entries = PresetSort.entries.map { s ->
                        MenuEntry.Item(sortLabels.getValue(s), checked = s == sort, selected = s == sort,
                            tag = "home.presets.sort.${s.name.lowercase()}") { sortIndex = s.ordinal }
                    },
                )
            },
        )
        Spacer(Modifier.height(2.dp))
        Box(Modifier.weight(1f).fillMaxWidth()) {
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxSize().testTag("home.presets.list"),
                contentPadding = PaddingValues(start = OmegaHomeDims.Gutter, end = OmegaHomeDims.Gutter, bottom = 16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                if (user.values.all { it.isEmpty() }) item(key = "vazio") {
                    Text(stringResource(R.string.om_home_presets_empty), style = OmegaHomeType.Body,
                        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp).testTag("home.presets.empty"))
                }
                items(entries, key = { it.key }) { e ->
                    PresetRowCard(e, onMenu = { coords ->
                        val bounds = coords?.takeIf { it.isAttached }?.boundsInWindow()
                        if (bounds != null && !e.builtin) quick = e to QuickMenuAnchor(bounds, areaBottom)
                    })
                }
            }
            HomeScrollShadow(scrolled, Modifier.align(Alignment.TopCenter))
        }
    }

    quick?.let { (entry, anchor) ->
        ProjectQuickMenu(
            anchor = anchor,
            onDismiss = { quick = null },
            tag = "home.presetMenu",
            options = listOf(
                QuickMenuOption(stringResource(R.string.common_rename), CupertinoGlyph.Pencil, "home.presetMenu.rename") { renaming = entry },
                QuickMenuOption(stringResource(R.string.common_delete), CupertinoGlyph.Trash, "home.presetMenu.delete", destructive = true) {
                    deleting = entry
                },
            ),
        ) { PresetRowCard(entry, onMenu = {}, interactive = false) }
    }
    renaming?.let { e ->
        AureaNamePrompt(
            title = stringResource(R.string.common_rename),
            initial = e.name,
            onConfirm = { name ->
                val clean = name.trim()
                if (clean.isNotEmpty() && clean != e.name) {
                    val json = library.jsonOf(e)
                    when {
                        json == null -> store.showToast(context.getString(R.string.import_failed))
                        library.exists(e.kind, clean) -> store.showToast(context.getString(R.string.om_home_preset_name_taken))
                        library.save(e.kind, clean, json) != null -> library.delete(e)
                        else -> store.showToast(context.getString(R.string.import_failed))
                    }
                }
            },
            onDismiss = { renaming = null },
        )
    }
    deleting?.let { e ->
        AureaAlert(
            title = stringResource(R.string.om_home_preset_delete_q, e.name),
            message = stringResource(R.string.common_irreversible),
            confirmLabel = stringResource(R.string.common_delete),
            destructive = true,
            onConfirm = { library.delete(e) },
            onDismiss = { deleting = null },
        )
    }
}

/**
 * A linha de 96 dp de um preset: no lugar da miniatura, o glifo do tipo (32
 * dp) sobre `HomeThumb`; o nome; a ficha (tipo · data, ou "Do AUREA"); o ⋯ só
 * nos presets da pessoa.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun PresetRowCard(entry: PresetEntry, onMenu: (LayoutCoordinates?) -> Unit, interactive: Boolean = true) {
    val holder = remember { arrayOfNulls<LayoutCoordinates>(1) }
    val shape = RoundedCornerShape(12.dp)
    val kind = stringResource(entry.kind.label)
    val date = remember(entry.file) {
        entry.file?.lastModified()?.takeIf { it > 0L }?.let { DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(it)) }
    }
    Row(
        Modifier
            .fillMaxWidth()
            .height(OmegaHomeDims.Row)
            .testTag("home.preset.row")
            .homeShadow(12.dp, shape, AureaColors.HomeShadowSoft)
            .clip(shape)
            .background(AureaColors.LightSurface)
            .onGloballyPositioned { holder[0] = it }
            .then(if (interactive && !entry.builtin) Modifier.combinedClickable(onClick = {}, onLongClick = { onMenu(holder[0]) }) else Modifier)
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(OmegaHomeDims.Thumb).clip(RoundedCornerShape(8.dp)).background(AureaColors.HomeThumb), contentAlignment = Alignment.Center) {
            CupertinoIcon(entry.kind.glyph, 32.dp, AureaColors.LightText)
        }
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(entry.name, style = OmegaHomeType.RowTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(8.dp))
            Row(Modifier.height(16.dp), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(entry.kind.glyph, 14.dp, AureaColors.LightTextMuted)
                Spacer(Modifier.width(6.dp))
                Text(kind, style = OmegaHomeType.Meta, maxLines = 1)
            }
            Spacer(Modifier.height(4.dp))
            Row(Modifier.height(16.dp), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(if (entry.builtin) CupertinoGlyph.CheckmarkSeal else CupertinoGlyph.Clock, 14.dp, AureaColors.LightTextMuted)
                Spacer(Modifier.width(6.dp))
                Text(if (entry.builtin) stringResource(R.string.om_home_preset_builtin) else date.orEmpty(), style = OmegaHomeType.Meta, maxLines = 1)
            }
        }
        if (!entry.builtin) {
            val menuLabel = stringResource(R.string.om_home_preset_menu)
            Box(
                Modifier.size(44.dp).testTag("home.preset.more").semantics { contentDescription = menuLabel }
                    .tocavel(enabled = interactive) { onMenu(holder[0]) },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, AureaColors.LightTextMuted)
            }
            Spacer(Modifier.width(4.dp))
        }
    }
}
