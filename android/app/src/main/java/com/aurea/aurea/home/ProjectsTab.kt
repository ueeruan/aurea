package com.aurea.aurea.home

import androidx.compose.foundation.background
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType

/**
 * A ABA PROJETOS (Fase 7.3 §5–§6): todos os projetos, com busca, ordenação e
 * seleção em lote.
 *
 * A grade é uma LazyColumn de LINHAS (não de cartões): cada item compõe uma
 * linha inteira, então uma lista de 300 projetos custa o mesmo que uma de 12
 * — só as linhas visíveis existem. As miniaturas entram por
 * `produceState` (fora da thread principal) e o cache é por bytes.
 */
@Composable
internal fun ProjectsTab(
    store: EditorStore,
    vm: HomeViewModel,
    listState: LazyListState,
    bottomBar: Dp,
) {
    val all = store.projects
    val loaded = store.projectsLoaded
    val selection = vm.selection
    val selecting = selection.isNotEmpty()
    val query = vm.query
    val sort = vm.sort
    LaunchedEffect(all) { vm.pruneSelection(all) }

    val arranged = remember(all, sort, query) { arrangeProjects(all, sort, query) }
    val columns = gridColumns()
    val rows = (arranged.size + columns - 1) / columns

    var dialog by remember { mutableStateOf(ProjectDialogState()) }
    val listBackdrop = rememberBackdrop()
    val openProject: (ProjectEntry) -> Unit = { e -> vm.afterEngine(store) { store.openProject(e.path) } }

    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize().backdropSource(listBackdrop),
            contentPadding = PaddingValues(bottom = AureaDims.ListEndSpace + bottomBar - AureaDims.TabBarHeight),
        ) {
            item(key = "titulo") {
                Column {
                    Text(
                        stringResource(R.string.home_title_projects),
                        style = AureaType.HeadlineLarge,
                        modifier = Modifier.padding(start = AureaDims.Gutter, top = AureaDims.S5, end = AureaDims.Gutter),
                    )
                    ProjectListBar(
                        count = arranged.size,
                        selecting = selecting,
                        selectedCount = selection.size,
                        searching = vm.searching || query.isNotBlank(),
                        query = query,
                        onQuery = { vm.query = it },
                        onOpenSearch = { vm.searching = true },
                        onClearSearch = {
                            vm.query = ""
                            vm.searching = false
                        },
                        onSort = { dialog.current = ProjectDialog.Sort },
                        onSelectAll = { vm.selectAll(arranged.map { it.path }) },
                        onExitSelection = { vm.clearSelection() },
                    )
                }
            }
            if (loaded && arranged.isEmpty()) {
                item(key = "vazio") {
                    ProjectsEmptyState(
                        if (query.isNotBlank()) stringResource(R.string.home_no_results)
                        else stringResource(R.string.home_empty_hint),
                    )
                }
            }
            items(count = rows, key = { "grade-$it" }) { r ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(start = AureaDims.Gutter, end = AureaDims.Gutter, top = if (r == 0) 0.dp else AureaDims.S4),
                    horizontalArrangement = Arrangement.spacedBy(AureaDims.S3),
                ) {
                    for (c in 0 until columns) {
                        val i = r * columns + c
                        if (i < arranged.size) {
                            val e = arranged[i]
                            ProjectGridCard(
                                entry = e,
                                thumbs = vm.thumbnails,
                                selecting = selecting,
                                marked = e.path in selection,
                                onOpen = { openProject(e) },
                                onMenu = { dialog.current = ProjectDialog.Menu(e) },
                                onMark = { vm.toggle(e.path) },
                                modifier = Modifier.weight(1f),
                            )
                        } else {
                            Spacer(Modifier.weight(1f))
                        }
                    }
                }
            }
        }

        if (selecting) {
            BatchActionsBar(
                count = selection.size,
                backdrop = listBackdrop,
                onDuplicate = {
                    all.forEach { if (it.path in selection) store.duplicateProject(it.path) }
                    vm.clearSelection()
                },
                onDelete = { dialog.current = ProjectDialog.BatchDelete(selection) },
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = bottomBar),
            )
        }
    }

    ProjectDialogs(store, vm, all, dialog, openProject)
}
