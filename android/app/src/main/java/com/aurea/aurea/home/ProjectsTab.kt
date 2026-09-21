package com.aurea.aurea.home

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.EaseIn
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph

/** Menus e diálogos da Inicio: um por vez, encadeados (menu → renomear, menu → excluir). */
private sealed interface HomeDialog {
    data class Menu(val entry: ProjectEntry) : HomeDialog
    data class ConfirmDelete(val entry: ProjectEntry) : HomeDialog
    data class Rename(val entry: ProjectEntry) : HomeDialog
    data object DeleteAll : HomeDialog
    data class BatchDelete(val paths: Set<String>) : HomeDialog
    data object Sort : HomeDialog
}

/**
 * A aba Inicio (`ProjectsTab@762dbfe`, spec §5): cabeçalho, "Novo projeto",
 * atalhos, o hero "Continuar editando", a grade, "Mostrar todos", Modelos,
 * Comunidade e Aprender — tudo numa LazyColumn só (o CustomScrollView da A.01).
 *
 * A lista vem do [EditorStore]; o que é só apresentação (busca, ordem,
 * seleção, "mostrar todos") vem do [HomeViewModel].
 */
@Composable
internal fun ProjectsTab(
    store: EditorStore,
    vm: HomeViewModel,
    listState: LazyListState,
    bottomBar: Dp,
    onSelectTab: (Int) -> Unit,
) {
    val all = store.projects
    val loaded = store.projectsLoaded
    val selection = vm.selection
    val selecting = selection.isNotEmpty()
    val sort = vm.sort
    val query = vm.query
    LaunchedEffect(all) { vm.pruneSelection(all) }

    val arranged = remember(all, sort, query) { arrangeProjects(all, sort, query) }
    val searchOrSort = query.isNotBlank() || sort != ProjectSort.Recent
    // O hero é o 1º da lista arrumada e não se repete na grade. Procurando,
    // ordenando por outra coisa ou escolhendo vários, não há hero.
    val hero = if (arranged.isEmpty() || selecting || searchOrSort) null else arranged.first()
    val open = vm.showAll || selecting || searchOrSort
    val visible = remember(arranged, open, hero) {
        (if (open) arranged else arranged.take(HomeDims.RECENT_ON_HOME)).filter { it !== hero }
    }
    val width = LocalConfiguration.current.screenWidthDp
    val columns = if (width >= 700) 4 else if (width >= 520) 3 else 2
    val rows = (visible.size + columns - 1) / columns
    // A A.01 só mostrava "Mostrar todos" (o "Mostrar menos" nunca aparecia, §8.7).
    val showAllRow = !selecting && !searchOrSort && arranged.size > HomeDims.RECENT_ON_HOME

    var dialog by remember { mutableStateOf<HomeDialog?>(null) }
    var newSheet by rememberSaveable { mutableStateOf(false) }
    val listBackdrop = rememberBackdrop()
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) vm.createFromMedia(store, uri)
    }

    val thresholdPx = with(LocalDensity.current) { HomeDims.CompactBarThreshold.toPx() }
    val compact by remember(listState) {
        derivedStateOf { listState.firstVisibleItemIndex > 0 || listState.firstVisibleItemScrollOffset > thresholdPx }
    }

    val openProject: (ProjectEntry) -> Unit = { e -> vm.afterEngine(store) { store.openProject(e.path) } }
    val templates = { store.comingSoon("Templates") }
    val profile = { onSelectTab(PROFILE_TAB) }

    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxSize().backdropSource(listBackdrop),
            contentPadding = PaddingValues(bottom = HomeDims.ListEndSpace + bottomBar - HomeDims.TabBarHeight),
        ) {
            item(key = "cabecalho") { HomeHeader(onTemplate = templates, onProfile = profile) }
            item(key = "novo") { NewProjectButton { newSheet = true } }
            item(key = "atalhos") {
                HomeShortcuts(
                    onMedia = { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)) },
                    onTemplate = templates,
                    onScene = { store.comingSoon("Cena 3D") },
                )
            }
            if (hero != null) {
                item(key = "heroi") {
                    ContinueEditingCard(hero, vm.thumbnails, onOpen = { openProject(hero) }, onMenu = { dialog = HomeDialog.Menu(hero) })
                }
            }
            if (all.size > 1) {
                item(key = "barra") {
                    ProjectListBar(
                        count = arranged.size,
                        selecting = selecting,
                        selectedCount = selection.size,
                        searching = vm.searching,
                        query = query,
                        onQuery = { vm.query = it },
                        onOpenSearch = { vm.searching = true },
                        onClearSearch = {
                            vm.query = ""
                            vm.searching = false
                        },
                        onSort = { dialog = HomeDialog.Sort },
                        onSelect = { arranged.firstOrNull()?.let { vm.selectOnly(it.path) } },
                        onMarkAll = { vm.selectAll(arranged.map { it.path }) },
                        onExitSelection = { vm.clearSelection() },
                    )
                }
            }
            // Enquanto o disco não respondeu, nada: o estado vazio não pode
            // piscar na abertura (a A.01 começava com [] e piscava, §8.5).
            if (loaded && arranged.isEmpty()) {
                item(key = "vazio") { ProjectsEmptyState() }
            } else if (rows > 0) {
                items(count = rows, key = { "grade-$it" }) { r ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(start = HomeDims.Gutter, end = HomeDims.Gutter, top = if (r == 0) 0.dp else 16.dp),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        for (c in 0 until columns) {
                            val i = r * columns + c
                            if (i < visible.size) {
                                val e = visible[i]
                                ProjectGridCard(
                                    entry = e,
                                    thumbs = vm.thumbnails,
                                    selecting = selecting,
                                    marked = e.path in selection,
                                    onOpen = { openProject(e) },
                                    onMenu = { dialog = HomeDialog.Menu(e) },
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
            if (showAllRow) {
                item(key = "todos") {
                    HomeLinkRow(
                        if (vm.showAll) CupertinoGlyph.ChevronUp else CupertinoGlyph.SquareGrid2x2,
                        if (vm.showAll) "Mostrar menos" else "Mostrar todos os ${arranged.size} projetos",
                    ) { vm.showAll = !vm.showAll }
                }
            }
            item(key = "t-modelos") { SectionTitle("Modelos") }
            item(key = "modelos") {
                LazyRow(
                    Modifier.fillMaxWidth().height(196.dp),
                    contentPadding = PaddingValues(horizontal = HomeDims.Gutter),
                ) {
                    items(TemplateModels) { m -> TemplateCard(m, vm.thumbnails) { store.comingSoon("Modelos") } }
                }
            }
            item(key = "t-comunidade") { SectionTitle("Comunidade") }
            item(key = "comunidade") {
                HomeFeatureRow(
                    CupertinoGlyph.Person2Fill,
                    "Veja o que a galera está criando",
                    "Poste o seu projeto, responda e reposte",
                ) { onSelectTab(COMMUNITY_TAB) }
            }
            item(key = "t-aprender") { SectionTitle("Aprender") }
            item(key = "aprender") {
                Column {
                    HomeLinkRow(CupertinoGlyph.PlayRectangle, "Tutorial em vídeo: sua primeira cena 3D") { store.comingSoon("Tutoriais") }
                    HomeLinkRow(CupertinoGlyph.CubeBox, "Tutorial em vídeo: cena 3D com modelos e câmeras") { store.comingSoon("Tutoriais") }
                    HomeLinkRow(CupertinoGlyph.Textformat, "Tutorial em vídeo: texto que quica, do seu jeito") { store.comingSoon("Tutoriais") }
                    HomeLinkRow(CupertinoGlyph.Sparkles, "O que ha de novo nesta versao") { store.comingSoon("Novidades") }
                    HomeLinkRow(CupertinoGlyph.ExclamationmarkBubble, "Versao beta: achou um problema? Conte pra gente") { store.comingSoon("Reportar") }
                }
            }
        }

        // Barra compacta: some da árvore quando escondida (nada de alvo invisível).
        AnimatedVisibility(
            visible = compact,
            enter = fadeIn(tween(180, easing = EaseOut)),
            exit = fadeOut(tween(180, easing = EaseIn)),
            modifier = Modifier.align(Alignment.TopCenter),
        ) {
            CompactHomeBar(listBackdrop, onTemplate = templates, onProfile = profile)
        }

        if (selecting) {
            BatchActionsBar(
                count = selection.size,
                backdrop = listBackdrop,
                onDuplicate = {
                    all.forEach { if (it.path in selection) store.duplicateProject(it.path) }
                    vm.clearSelection()
                },
                onDelete = { dialog = HomeDialog.BatchDelete(selection) },
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = bottomBar),
            )
        }
    }

    if (newSheet) {
        NewProjectSheet(
            suggestedName = "Projeto ${all.size + 1}",
            defaultAspectKey = vm.defaultAspectKey,
            defaultResolution = vm.defaultResolution,
            defaultFps = vm.defaultFps,
            onCreate = { spec ->
                vm.afterEngine(store) { store.newProject(spec.width, spec.height, spec.fps.toFloat(), spec.title) }
            },
            onDismiss = { newSheet = false },
        )
    }

    val close = { dialog = null }
    when (val d = dialog) {
        null -> Unit
        is HomeDialog.Menu -> AureaActionSheet(
            title = d.entry.title,
            message = projectSpec(d.entry),
            actions = listOf(
                SheetAction("Abrir") { openProject(d.entry) },
                SheetAction("Duplicar") { store.duplicateProject(d.entry.path) },
                SheetAction("Renomear") { dialog = HomeDialog.Rename(d.entry) },
                SheetAction("Excluir projeto", destructive = true) { dialog = HomeDialog.ConfirmDelete(d.entry) },
                SheetAction("Apagar todos os projetos", destructive = true) { if (all.isNotEmpty()) dialog = HomeDialog.DeleteAll },
            ),
            onDismiss = close,
        )
        // Na A.01 esta confirmação também oferecia "Apagar todos" ao lado da
        // exclusão unitária (§8.3) — risco de apagar tudo por engano; só no menu.
        is HomeDialog.ConfirmDelete -> AureaActionSheet(
            title = d.entry.title,
            actions = listOf(SheetAction("Excluir projeto", destructive = true) { store.deleteProjects(listOf(d.entry.path)) }),
            onDismiss = close,
        )
        is HomeDialog.Rename -> RenameProjectDialog(
            initial = d.entry.title,
            onSave = { t -> if (t != d.entry.title) store.renameProjectFile(d.entry.path, t) },
            onDismiss = close,
        )
        HomeDialog.DeleteAll -> AureaAlert(
            title = "Apagar todos os projetos?",
            message = "${all.size} projeto(s) serao apagados. Isso nao pode ser desfeito.",
            confirmLabel = "Apagar todos",
            cancelLabel = "Cancelar",
            destructive = true,
            onConfirm = {
                store.deleteProjects(all.map { it.path })
                vm.clearSelection()
            },
            onDismiss = close,
        )
        is HomeDialog.BatchDelete -> AureaActionSheet(
            title = "Excluir ${d.paths.size} projetos?",
            message = "Nao da para desfazer.",
            actions = listOf(
                SheetAction("Excluir", destructive = true) {
                    store.deleteProjects(d.paths)
                    vm.clearSelection()
                },
            ),
            onDismiss = close,
        )
        HomeDialog.Sort -> AureaActionSheet(
            title = "Ordenar os projetos",
            actions = ProjectSort.entries.map { s -> SheetAction(s.label) { vm.changeSort(s) } },
            onDismiss = close,
        )
    }
}

/**
 * `_DialogoDeNome`: "Renomear" com o campo em foco (texto selecionado para
 * trocar de uma vez), capitalização de frases, Enter salva. Vazio → nada.
 */
@Composable
private fun RenameProjectDialog(initial: String, onSave: (String) -> Unit, onDismiss: () -> Unit) {
    var value by remember { mutableStateOf(TextFieldValue(initial, TextRange(0, initial.length))) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    val save = {
        val t = value.text.trim()
        if (t.isNotEmpty()) onSave(t)
    }
    AureaAlert(
        title = "Renomear",
        confirmLabel = "Salvar",
        cancelLabel = "Cancelar",
        onConfirm = save,
        onDismiss = onDismiss,
        extra = {
            BasicTextField(
                value = value,
                onValueChange = { value = it },
                singleLine = true,
                textStyle = HomeType.DialogField,
                cursorBrush = SolidColor(AureaColors.Accent),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    onDismiss()
                    save()
                }),
                modifier = Modifier
                    .padding(top = 12.dp)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(7.dp))
                    .background(HomeColors.DialogField)
                    .padding(horizontal = 8.dp, vertical = 7.dp)
                    .focusRequester(focus),
            )
        },
    )
}

internal const val COMMUNITY_TAB = 1
internal const val PROFILE_TAB = 3
