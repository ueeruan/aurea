package com.aurea.aurea.home

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
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
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Quantos projetos a Início mostra antes do "Ver todos". */
private const val RECENT_ON_HOME = 6

/**
 * A ABA INÍCIO (Fase 7.3 §3–§5): o lugar de chegar e começar.
 *
 * Só o que funciona: criar projeto, importar mídia, continuar o último e a
 * grade dos recentes com busca e ordenação. As fileiras de "Modelos",
 * "Comunidade" e "Aprender" da A.01 saíram — eram botões que só avisavam
 * "em breve" (§21, §70).
 *
 * A lista lê a ORDEM do store (mais novo primeiro) e não reordena nada: o
 * "Mais recentes" da Início é literalmente a ordem em que os projetos estão.
 */
@Composable
internal fun StartTab(
    store: EditorStore,
    vm: HomeViewModel,
    listState: LazyListState,
    bottomBar: Dp,
    onSelectTab: (Int) -> Unit,
) {
    val all = store.projects
    val loaded = store.projectsLoaded
    val query = vm.query
    val sort = vm.sort
    // A Início não tem seleção em lote: ela é a vitrine, não o gerenciador.
    LaunchedEffect(all) { vm.pruneSelection(all) }

    val arranged = remember(all, sort, query) { arrangeProjects(all, sort, query) }
    val filtering = query.isNotBlank() || sort != ProjectSort.Recent
    // O hero é o primeiro da lista e não se repete na grade.
    val hero = if (arranged.isEmpty() || filtering) null else arranged.first()
    val visible = remember(arranged, hero) {
        arranged.filter { it !== hero }.take(if (filtering) Int.MAX_VALUE else RECENT_ON_HOME)
    }
    val columns = gridColumns()
    val rows = (visible.size + columns - 1) / columns

    var dialog by remember { mutableStateOf(ProjectDialogState()) }
    var newSheet by rememberSaveable { mutableStateOf(false) }
    var searching by rememberSaveable { mutableStateOf(false) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) vm.createFromMedia(store, uri)
    }
    val openProject: (ProjectEntry) -> Unit = { e -> vm.afterEngine(store) { store.openProject(e.path) } }

    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(bottom = AureaDims.ListEndSpace + bottomBar - AureaDims.TabBarHeight),
        ) {
            item(key = "cabecalho") {
                HomeHeader(
                    onSearch = { searching = !searching },
                    onSettings = { onSelectTab(HomeViewModel.SETTINGS_TAB) },
                )
            }
            if (searching || filtering) {
                item(key = "busca") {
                    Row(Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = AureaDims.S1, end = AureaDims.S3, bottom = AureaDims.S3)) {
                        ProjectListBar(
                            count = arranged.size,
                            selecting = false,
                            selectedCount = 0,
                            searching = true,
                            query = query,
                            onQuery = { vm.query = it },
                            onOpenSearch = {},
                            onClearSearch = {
                                vm.query = ""
                                searching = false
                            },
                            onSort = { dialog.current = ProjectDialog.Sort },
                            onSelectAll = {},
                            onExitSelection = {},
                        )
                    }
                }
            }
            item(key = "criar") {
                Column(Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = AureaDims.S1, end = AureaDims.Gutter)) {
                    FillButton("Novo projeto", CupertinoGlyph.Plus) { newSheet = true }
                    Spacer(Modifier.height(AureaDims.S2))
                    Row(horizontalArrangement = Arrangement.spacedBy(AureaDims.S3)) {
                        QuickAction(CupertinoGlyph.PhotoOnRectangle, "Importar mídia", Modifier.weight(1f)) {
                            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                        }
                        QuickAction(CupertinoGlyph.ArrowUpArrowDown, "Ordenar", Modifier.weight(1f)) {
                            dialog.current = ProjectDialog.Sort
                        }
                    }
                }
            }
            if (hero != null && !searching) {
                item(key = "heroi") {
                    ContinueEditingCard(
                        hero, vm.thumbnails,
                        onOpen = { openProject(hero) },
                        onMenu = { dialog.current = ProjectDialog.Menu(hero) },
                    )
                }
            }
            if (arranged.isNotEmpty()) {
                item(key = "recentes") {
                    SectionHeader(
                        if (filtering) "${arranged.size} projeto(s)" else "Recentes",
                        actionLabel = if (!filtering && all.size > visible.size + (if (hero != null) 1 else 0)) "Ver todos" else null,
                        onAction = { onSelectTab(HomeViewModel.PROJECTS_TAB) },
                    )
                }
            }
            // Enquanto o disco não respondeu, nada: o estado vazio não pode
            // piscar na abertura.
            if (loaded && arranged.isEmpty()) {
                item(key = "vazio") {
                    ProjectsEmptyState(
                        if (query.isNotBlank()) "Nenhum projeto com esse nome."
                        else "Seus projetos aparecem aqui, com a miniatura do que você fez.",
                    )
                }
            }
            if (rows > 0) {
                items(count = rows, key = { "grade-$it" }) { r ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(start = AureaDims.Gutter, end = AureaDims.Gutter, top = if (r == 0) 0.dp else AureaDims.S4),
                        horizontalArrangement = Arrangement.spacedBy(AureaDims.S3),
                    ) {
                        for (c in 0 until columns) {
                            val i = r * columns + c
                            if (i < visible.size) {
                                val e = visible[i]
                                ProjectGridCard(
                                    entry = e,
                                    thumbs = vm.thumbnails,
                                    selecting = false,
                                    marked = false,
                                    onOpen = { openProject(e) },
                                    onMenu = { dialog.current = ProjectDialog.Menu(e) },
                                    onMark = {},
                                    modifier = Modifier.weight(1f),
                                )
                            } else {
                                Spacer(Modifier.weight(1f))
                            }
                        }
                    }
                }
            }
        }
    }

    if (newSheet) NewProjectSheetFor(store, vm, all, onDismiss = { newSheet = false })
    ProjectDialogs(store, vm, all, dialog, openProject)
}

/** Ação secundária: cartão com ícone e rótulo, largura dividida. */
@Composable
private fun QuickAction(glyph: Char, label: String, modifier: Modifier, onClick: () -> Unit) {
    Row(
        modifier
            .height(46.dp)
            .clip(AureaShape.Md)
            .background(AureaColors.Surface)
            .tocavel(shrink = 1f, onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(glyph, AureaDims.IconMd, AureaColors.Accent)
        Spacer(Modifier.width(AureaDims.S2))
        Text(label, style = AureaType.FeatureTitle, textAlign = TextAlign.Center)
    }
}

/** Duas colunas em celular, mais em tela larga. */
@Composable
internal fun gridColumns(): Int {
    val width = LocalConfiguration.current.screenWidthDp
    return if (width >= 840) 4 else if (width >= 600) 3 else 2
}
