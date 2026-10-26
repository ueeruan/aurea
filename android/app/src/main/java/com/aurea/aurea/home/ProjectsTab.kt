package com.aurea.aurea.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle

/**
 * Omega home · P0-5: a ABA PROJETOS como a da referência. 24 dp · pílulas de
 * subaba (por enquanto só "Projetos"; "Elementos" é o P2-6) · 24 dp · a linha
 * "Meus projetos" com a busca (extra D8) e a ordem · 12 dp · a lista de
 * cartões de 96 dp (16 dp nas laterais, 12 entre eles), com o cabeçalho de
 * data na ordem "Mais recentes" (P1-3) e a sombra de 25 dp quando a lista sai
 * do topo. Em tela larga, colunas de linhas (mínimo 360 dp).
 *
 * Só as linhas visíveis existem (LazyColumn); as miniaturas entram fora da
 * thread principal com o cache por bytes da Home.
 */
@Composable
internal fun ProjectsTab(
    store: EditorStore,
    vm: HomeViewModel,
    listState: LazyListState,
    dialogs: ProjectDialogState,
    openProject: (ProjectEntry) -> Unit,
) {
    val all = store.projects
    val loaded = store.projectsLoaded
    val selection = vm.selection
    val selecting = selection.isNotEmpty()
    val query = vm.query
    val sort = vm.sort
    LaunchedEffect(all) { vm.pruneSelection(all) }
    val arranged = remember(all, sort, query) { arrangeProjects(all, sort, query) }
    var sortOpen by remember { mutableStateOf(false) }
    var quick by remember { mutableStateOf<Pair<ProjectEntry, QuickMenuAnchor>?>(null) }
    var areaBottom by remember { mutableFloatStateOf(Float.MAX_VALUE) }
    val searching = vm.searching || query.isNotBlank()

    Column(
        Modifier
            .fillMaxSize()
            .onGloballyPositioned { areaBottom = it.boundsInWindow().bottom }
            .testTag("home.projects.tab"),
    ) {
        Spacer(Modifier.height(18.dp))
        HomeSubtabs(listOf(stringResource(R.string.home_tab_projects) to "home.projects.subtab.projects"), 0) {}
        Spacer(Modifier.height(8.dp))
        if (loaded && all.isEmpty()) {
            // P0-7: o vazio fica no meio do espaço da página.
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) { ProjectsEmptyState() }
        } else if (loaded) {
        if (searching) {
            Row(Modifier.fillMaxWidth().padding(horizontal = OmegaHomeDims.Gutter), verticalAlignment = Alignment.CenterVertically) {
                HomeSearchField(query, { vm.query = it }, onClear = { vm.query = ""; vm.searching = false }, modifier = Modifier.weight(1f))
                Spacer(Modifier.width(8.dp))
                SortButton(sort, onOpen = { sortOpen = true }, open = sortOpen, onPick = { vm.changeSort(it) }, onDismiss = { sortOpen = false })
            }
        } else {
            HomeListHeader(
                title = stringResource(R.string.om_home_my_projects),
                sortLabel = stringResource(R.string.om_home_sort_current, stringResource(sort.label)),
                onSort = { sortOpen = true },
                tag = "home.projects.header",
                sortTag = "home.projects.sort",
                searchLabel = stringResource(R.string.common_search),
                searchTag = "home.projects.search",
                onSearch = { vm.searching = true },
                sortAnchor = { SortMenu(sortOpen, sort, onPick = { vm.changeSort(it) }, onDismiss = { sortOpen = false }) },
            )
        }
        Spacer(Modifier.height(2.dp))
        BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
            val columns = projectColumns(maxWidth.value)
            val zone = remember { ZoneId.systemDefault() }
            val items = remember(arranged, sort, columns) { projectListItems(arranged, sort, columns, zone) }
            val scrolled by remember(listState) {
                derivedStateOf { listState.firstVisibleItemIndex != 0 || listState.firstVisibleItemScrollOffset > 0 }
            }
            val locale = LocalConfiguration.current.locales[0]
            val dayFormat = remember(locale) { DateTimeFormatter.ofLocalizedDate(FormatStyle.LONG).withLocale(locale) }
            if (arranged.isEmpty()) {
                ProjectsNoResults()
            } else {
                LazyColumn(
                    state = listState,
                    modifier = Modifier.fillMaxSize().testTag("home.projects.list"),
                    contentPadding = PaddingValues(start = OmegaHomeDims.Gutter, end = OmegaHomeDims.Gutter, bottom = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    // Com o menu de ordenação aberto, a lista não responde (referência).
                    userScrollEnabled = !sortOpen,
                ) {
                    items(items, key = { it.key }) { item ->
                        when (item) {
                            is ProjectListItem.DateHeader -> Text(
                                item.day.format(dayFormat),
                                style = OmegaHomeType.Small,
                                modifier = Modifier.fillMaxWidth().padding(top = 12.dp).testTag("home.projects.dateHeader")
                                    .semantics { heading() },
                            )
                            is ProjectListItem.Cards -> Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                item.entries.forEach { e ->
                                    val marked = e.path in selection
                                    ProjectRowCard(
                                        entry = e,
                                        thumbs = vm.thumbnails,
                                        selecting = selecting,
                                        marked = marked,
                                        onOpen = { if (selecting) vm.toggle(e.path) else openProject(e) },
                                        onLongPress = { if (selecting) vm.toggle(e.path) else vm.startSelection(e.path) },
                                        onMenu = { coords ->
                                            val bounds = coords?.takeIf { it.isAttached }?.boundsInWindow()
                                            if (bounds != null) quick = e to QuickMenuAnchor(bounds, areaBottom)
                                        },
                                        modifier = Modifier.weight(1f),
                                    )
                                }
                                repeat(columns - item.entries.size) { Spacer(Modifier.weight(1f)) }
                            }
                        }
                    }
                }
                HomeScrollShadow(scrolled, Modifier.align(Alignment.TopCenter))
            }
        }
        }
    }

    quick?.let { (entry, anchor) ->
        val close = { quick = null }
        ProjectQuickMenu(
            anchor = anchor,
            onDismiss = close,
            options = listOf(
                QuickMenuOption(stringResource(R.string.home_select), CupertinoGlyph.CheckmarkCircle, "home.project.select") {
                    vm.startSelection(entry.path)
                },
                QuickMenuOption(stringResource(R.string.om_home_action_export), CupertinoGlyph.SquareArrowUp, "home.projectMenu.export") {
                    dialogs.current = ProjectDialog.ExportFile(entry)
                },
                QuickMenuOption(stringResource(R.string.common_rename), CupertinoGlyph.Pencil, "home.projectMenu.rename") {
                    dialogs.current = ProjectDialog.Rename(entry)
                },
                QuickMenuOption(stringResource(R.string.common_duplicate), CupertinoGlyph.PlusSquareOnSquare, "home.projectMenu.duplicate") {
                    store.duplicateProject(entry.path)
                },
                QuickMenuOption(stringResource(R.string.common_delete), CupertinoGlyph.Trash, "home.projectMenu.delete", destructive = true) {
                    dialogs.current = ProjectDialog.ConfirmDelete(entry)
                },
            ),
        ) {
            ProjectRowCard(entry, vm.thumbnails, selecting = false, marked = false, onOpen = {}, onLongPress = {},
                onMenu = {}, interactive = false)
        }
    }
}

/**
 * P1-3: a ordem num menu ancorado da fundação (60 % da largura, alinhado ao
 * fim, logo abaixo da linha): Nome · Mais recentes · Mais longos (extra D8).
 * Tocar fora fecha.
 */
@Composable
private fun SortMenu(open: Boolean, current: ProjectSort, onPick: (ProjectSort) -> Unit, onDismiss: () -> Unit) {
    val width = (LocalConfiguration.current.screenWidthDp * 0.6f).dp
    val order = listOf(
        ProjectSort.Name to "home.projects.sort.name",
        ProjectSort.Recent to "home.projects.sort.recent",
        ProjectSort.Longest to "home.projects.sort.longest",
    )
    AnchoredMenu(
        expanded = open,
        onDismiss = onDismiss,
        width = width,
        alignEnd = true,
        tag = "home.projects.sortMenu",
        entries = order.map { (s, tag) ->
            MenuEntry.Item(stringResource(s.label), checked = s == current, selected = s == current, tag = tag) { onPick(s) }
        },
    )
}

/** O botão de ordem ao lado do campo de busca (a linha "Meus projetos" some enquanto se busca). */
@Composable
private fun SortButton(current: ProjectSort, onOpen: () -> Unit, open: Boolean, onPick: (ProjectSort) -> Unit, onDismiss: () -> Unit) {
    Box {
        HomeIconButton(CupertinoGlyph.ArrowUpArrowDown, stringResource(R.string.home_sort_title), "home.projects.sort", onOpen)
        SortMenu(open, current, onPick, onDismiss)
    }
}

/** Botão de ícone de 44 dp (ícone 20 no texto claro). */
@Composable
internal fun HomeIconButton(glyph: Char, description: String, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .size(44.dp)
            .testTag(tag)
            .semantics { contentDescription = description }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 20.dp, AureaColors.LightText)
    }
}
