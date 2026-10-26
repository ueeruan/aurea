package com.aurea.aurea.home

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaEstado
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.launch

/**
 * A HOME (Omega 0.3.2, área 01): barra superior de 57 dp (☰ · AUREA · ✦ ·
 * avatar), as abas Início (Comunidade) · Perfil · [+] · Projetos · Presets no
 * dock de 72 dp e a gaveta de ajustes. Selecionando projetos, o cabeçalho de
 * seleção cobre a barra superior e a barra de seleção cobre o dock.
 *
 * Clara no Omega (D6): pinta só com os papéis claros e os da Home.
 */
@Composable
fun HomeScreen(store: EditorStore) {
    val vm: HomeViewModel = viewModel()
    // O mesmo ViewModel da MainActivity (mesmo dono, mesma chave).
    val conta: ContaViewModel = viewModel()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var confirmLogout by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    var showAccount by rememberSaveable { mutableStateOf(false) }
    var accountMenu by remember { mutableStateOf(false) }
    LaunchedEffect(conta.logado) { if (conta.logado) showAccount = false }
    val projects = rememberTabListState(vm, HomeViewModel.PROJECTS_TAB)
    var about by rememberSaveable { mutableStateOf(false) }
    var licenses by remember { mutableStateOf(false) }
    var newProject by rememberSaveable { mutableStateOf(false) }
    var reportProblem by rememberSaveable { mutableStateOf(false) }
    val dialogs = remember { ProjectDialogState() }
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) vm.createFromMedia(store, uri)
    }
    // "Importar arquivo do projeto": qualquer tipo (o .aureaproj não tem MIME
    // registrado); o motor diz se é projeto do Aurea.
    val projectFilePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.afterEngine(store) { store.importProjectFile(uri) }
    }
    // Omega home · P2-1: "Prévia leve" abre o editor com a prévia em 1/2.
    val openProject: (ProjectEntry) -> Unit = { e ->
        vm.afterEngine(store) {
            store.openProject(e.path)
            if (vm.lightPreview) store.setPreviewScale(false, 1, 2)
        }
    }
    val selection = vm.selection
    val selecting = selection.isNotEmpty() && vm.tab == HomeViewModel.PROJECTS_TAB
    val closeDrawer: () -> Unit = { scope.launch { drawer.close() } }
    BackHandler(drawer.isOpen) { closeDrawer() }
    BackHandler(!drawer.isOpen && selecting) { vm.clearSelection() }
    LightSystemBars(AureaColors.LightIsLight)

    val screenWidth = LocalConfiguration.current.screenWidthDp.dp
    ModalNavigationDrawer(
        drawerState = drawer,
        scrimColor = AureaColors.HomeCover,
        gesturesEnabled = !selecting,
        drawerContent = {
            val drawerTitle = stringResource(R.string.home_title_settings)
            ModalDrawerSheet(
                // O leitor de tela anuncia "Ajustes" ao abrir (P0-3).
                modifier = Modifier.width(minOf(OmegaHomeDims.Drawer, screenWidth * 0.85f)).testTag("home.drawer")
                    .semantics { paneTitle = drawerTitle },
                drawerShape = RectangleShape,
                drawerContainerColor = AureaColors.HomeDrawer,
                drawerContentColor = AureaColors.LightText,
            ) {
                SettingsDrawer(
                    store = store,
                    vm = vm,
                    onImportMedia = {
                        closeDrawer()
                        picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
                    },
                    onImportProject = { closeDrawer(); projectFilePicker.launch(arrayOf("*/*")) },
                    onHyper = { closeDrawer(); store.openHyper() },
                    onAbout = { closeDrawer(); about = true },
                    onSupport = { closeDrawer(); reportProblem = true },
                    onLicenses = { licenses = true },
                )
            }
        },
    ) {
        Column(
            Modifier
                .fillMaxSize()
                .background(AureaColors.LightBackground)
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)),
        ) {
            LiveNoticeBanners(vm.notices)
            com.aurea.aurea.config.AppConfigBanner()
            val signedIn = (conta.estado as? ContaEstado.Dentro)?.email
            Crossfade(selecting, animationSpec = tween(300), label = "topo") { picking ->
                if (picking) {
                    HomeSelectionHeader(
                        count = selection.size,
                        onSelectAll = { vm.selectAll(arrangeProjects(store.projects, vm.sort, vm.query).map { it.path }) },
                        onCancel = { vm.clearSelection() },
                    )
                } else {
                    HomeTopBar(
                        onMenu = { scope.launch { drawer.open() } },
                        signedInEmail = signedIn,
                        onAccount = {
                            if (signedIn == null) {
                                conta.entrando = true
                                showAccount = true
                            } else accountMenu = true
                        },
                        accountMenuOpen = accountMenu,
                        onDismissAccountMenu = { accountMenu = false },
                        accountMenu = if (signedIn == null) emptyList() else listOf(
                            MenuEntry.Header(stringResource(R.string.conta_conectado, signedIn), tag = "home.account.email"),
                            MenuEntry.Item(stringResource(R.string.conta_sair), glyph = CupertinoGlyph.ArrowUturnLeft,
                                tag = "home.account.logout") { confirmLogout = true },
                            MenuEntry.Item(stringResource(R.string.conta_excluir), glyph = CupertinoGlyph.Trash,
                                destructive = true, tag = "home.account.delete") { confirmDelete = true },
                        ),
                    )
                }
            }
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when (vm.tab) {
                    HomeViewModel.HOME_TAB -> com.aurea.aurea.community.CommunityScreen(store, conta, false)
                    HomeViewModel.PROFILE_TAB -> com.aurea.aurea.community.CommunityScreen(store, conta, true)
                    HomeViewModel.PRESETS_TAB -> PresetsTab(store)
                    else -> ProjectsTab(store, vm, projects, dialogs, openProject)
                }
            }
            Crossfade(selecting, animationSpec = tween(300), label = "base") { picking ->
                if (picking) {
                    val chosen = store.projects.filter { it.path in selection }
                    HomeSelectionBar(
                        count = selection.size,
                        onExport = { chosen.singleOrNull()?.let { dialogs.current = ProjectDialog.ExportFile(it) } },
                        onDuplicate = {
                            chosen.forEach { store.duplicateProject(it.path) }
                            vm.clearSelection()
                        },
                        onDelete = { dialogs.current = ProjectDialog.BatchDelete(selection) },
                    )
                } else {
                    HomeDock(selected = vm.tab, onSelect = vm::selectTab, onCreate = { newProject = true })
                }
            }
        }
    }

    ProjectDialogs(store, vm, store.projects, dialogs, openProject)
    if (newProject) NewProjectSheetFor(store, vm, store.projects) { newProject = false }
    if (showAccount) androidx.compose.ui.window.Dialog(
        onDismissRequest = { showAccount = false },
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
            TextButton(onClick = { showAccount = false }, modifier = Modifier.align(Alignment.End).testTag("conta.fechar")) {
                Text(stringResource(R.string.editor_fechar))
            }
            Box(Modifier.weight(1f)) { com.aurea.aurea.conta.ContaScreen(conta) }
        }
    }
    if (reportProblem) ReportProblemSheet(sessao = { conta.sessao() }, onResult = { store.showToast(it) }) { reportProblem = false }
    store.quarantinePrompt?.let { q -> QuarantineDialog(store, q) }
    if (confirmLogout) AureaAlert(
        title = stringResource(R.string.conta_sair),
        message = stringResource(R.string.conta_sair_mensagem),
        confirmLabel = stringResource(R.string.conta_sair),
        destructive = true,
        onConfirm = { confirmLogout = false; conta.sair() },
        onDismiss = { confirmLogout = false },
    )
    if (confirmDelete) {
        AureaAlert(
            title = stringResource(R.string.conta_excluir),
            message = stringResource(R.string.conta_excluir_mensagem),
            confirmLabel = stringResource(R.string.conta_excluir),
            destructive = true,
            onConfirm = {
                confirmDelete = false
                conta.excluirConta { ok ->
                    store.showToast(context.getString(if (ok) R.string.conta_excluida else R.string.conta_excluir_falhou))
                }
            },
            onDismiss = { confirmDelete = false },
        )
    }
    if (about) AboutScreen(store, onLicenses = { licenses = true }) { about = false }
    // Depois do Sobre: as licenças abertas de lá ficam por cima dele.
    if (licenses) LicensesScreen { licenses = false }
}

/**
 * "Projeto com problema": o app fechou com o motor neste projeto (abrir,
 * importar, exportar). Ele não é aberto sozinho; a pessoa escolhe.
 */
@Composable
private fun QuarantineDialog(store: EditorStore, q: com.aurea.aurea.diagnostics.ProjectGuard.Quarantined) {
    val title = store.quarantineTitle(q)
    val body = when (q.stage) {
        com.aurea.aurea.diagnostics.ProjectGuard.Stage.OPEN -> R.string.quarantine_body_open
        com.aurea.aurea.diagnostics.ProjectGuard.Stage.IMPORT -> R.string.quarantine_body_import
        com.aurea.aurea.diagnostics.ProjectGuard.Stage.EXPORT_VIDEO -> R.string.quarantine_body_export
        com.aurea.aurea.diagnostics.ProjectGuard.Stage.EXPORT_FILE -> R.string.quarantine_body_package
    }
    // Omega home: diálogo claro da fundação (três ações: pilha).
    AureaDialog(
        title = stringResource(R.string.quarantine_title),
        message = stringResource(body, title),
        onDismiss = { store.dismissQuarantinePrompt() },
        tag = "home.quarantine",
        actions = listOf(
            DialogAction(stringResource(R.string.quarantine_recover), DialogRole.Positive, dismiss = false) { store.recoverQuarantined(q.path) },
            DialogAction(stringResource(R.string.quarantine_delete), DialogRole.Neutral, destructive = true, dismiss = false) { store.deleteQuarantined(q.path) },
            DialogAction(stringResource(R.string.quarantine_later), DialogRole.Negative),
        ),
    )
}

/** "N pessoas cadastradas" sob o título (do Worker; guardado para abrir offline). */
@Composable
internal fun RegisteredUsersLine(count: Int?) {
    if (count == null) return
    val formatted = remember(count) { java.text.NumberFormat.getIntegerInstance().format(count) }
    Row(Modifier.testTag("home.registeredUsers").padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(Modifier.size(6.dp).background(AureaColors.Success, CircleShape))
        Text(pluralStringResource(R.plurals.conta_usuarios, count, formatted), style = AureaType.Note)
    }
}

/**
 * Omega home · P0-2: o dock de 72 dp (branco, sombra de 30 dp para cima):
 * Início · Perfil · [+] · Projetos · Presets. O "+" é o do AUREA (D4), no centro
 * exato: os dois grupos laterais têm o mesmo peso em qualquer largura.
 */
@Composable
internal fun HomeDock(selected: Int, onSelect: (Int) -> Unit, onCreate: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .homeShadow(30.dp, RectangleShape, AureaColors.LightShadow)
            .background(AureaColors.LightSurface)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom))
            .testTag("home.dock"),
    ) {
        Row(
            Modifier.fillMaxWidth().height(OmegaHomeDims.Dock).padding(horizontal = 8.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            DockTab(CupertinoGlyph.House, CupertinoGlyph.HouseFill, stringResource(R.string.home_tab_start),
                selected == HomeViewModel.HOME_TAB, "home.community") { onSelect(HomeViewModel.HOME_TAB) }
            DockTab(CupertinoGlyph.PersonCropCircle, CupertinoGlyph.PersonCropCircle, stringResource(R.string.social_profile),
                selected == HomeViewModel.PROFILE_TAB, "home.profile") { onSelect(HomeViewModel.PROFILE_TAB) }
            val createLabel = stringResource(R.string.home_new_project)
            Box(
                Modifier
                    .padding(horizontal = 4.dp)
                    .size(56.dp)
                    .clip(CircleShape)
                    .background(AureaColors.Accent)
                    .testTag("home.create")
                    .semantics { contentDescription = createLabel }
                    .clickable(role = Role.Button, onClick = onCreate),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Plus, 30.dp, AureaColors.OnAccent)
            }
            DockTab(CupertinoGlyph.RectangleStack, CupertinoGlyph.RectangleStack, stringResource(R.string.home_tab_projects),
                selected == HomeViewModel.PROJECTS_TAB, "home.projects") { onSelect(HomeViewModel.PROJECTS_TAB) }
            DockTab(CupertinoGlyph.WandStars, CupertinoGlyph.WandStars, stringResource(R.string.home_presets_short),
                selected == HomeViewModel.PRESETS_TAB, "home.presets") { onSelect(HomeViewModel.PRESETS_TAB) }
        }
    }
}

/**
 * Uma aba do dock: ícone de 24 dp sobre o rótulo de 12 médio. A acesa usa o
 * glifo cheio (quando o AUREA tem) e a cor plena; a apagada, o tom secundário.
 * A troca é um crossfade de 300 ms (a fileira da referência).
 */
@Composable
private fun RowScope.DockTab(icon: Char, iconOn: Char, label: String, active: Boolean, tag: String, onClick: () -> Unit) {
    val ink by animateColorAsState(if (active) AureaColors.LightText else AureaColors.LightTextMuted, tween(300), label = "aba-cor")
    Column(
        Modifier
            .weight(1f)
            .widthIn(max = 158.dp)
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(12.dp))
            .testTag(tag)
            .semantics { selected = active; contentDescription = label }
            .clickable(role = Role.Tab, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically),
    ) {
        CupertinoIcon(if (active) iconOn else icon, 24.dp, ink)
        Text(
            label,
            style = OmegaHomeType.TabLabel.copy(color = ink, fontWeight = if (active) FontWeight.W600 else FontWeight.W500),
            textAlign = TextAlign.Center,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * Omega home · P0-6: a barra de seleção (72 dp) no lugar do dock: Exportar ·
 * Duplicar · Excluir (ícone + rótulo claro). Exportar só com UM projeto (o
 * arquivo do projeto é um por vez).
 */
@Composable
internal fun HomeSelectionBar(count: Int, onExport: () -> Unit, onDuplicate: () -> Unit, onDelete: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .background(AureaColors.HomeSelectionBar)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom))
            .testTag("home.selection.bar"),
    ) {
        Row(
            Modifier.fillMaxWidth().height(OmegaHomeDims.SelectionBar).padding(horizontal = 8.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SelectionAction(CupertinoGlyph.SquareArrowUp, stringResource(R.string.om_home_action_export), "home.selection.export",
                enabled = count == 1, onClick = onExport)
            SelectionAction(CupertinoGlyph.PlusSquareOnSquare, stringResource(R.string.common_duplicate), "home.selection.duplicate",
                onClick = onDuplicate)
            SelectionAction(CupertinoGlyph.Trash, stringResource(R.string.common_delete), "home.selection.delete", onClick = onDelete)
        }
    }
}

@Composable
private fun RowScope.SelectionAction(glyph: Char, label: String, tag: String, enabled: Boolean = true, onClick: () -> Unit) {
    val ink = AureaColors.HomeOnSelectionBar
    Column(
        Modifier
            .weight(1f)
            .height(48.dp)
            .clip(RoundedCornerShape(12.dp))
            .testTag(tag)
            .semantics(mergeDescendants = true) { if (!enabled) disabled() }
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .alpha(if (enabled) 1f else 0.4f),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically),
    ) {
        CupertinoIcon(glyph, 22.dp, ink)
        Text(label, style = OmegaHomeType.TabLabel.copy(color = ink), maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun rememberTabListState(vm: HomeViewModel, tab: Int): LazyListState {
    val state = remember { LazyListState(vm.scrollIndex(tab), vm.scrollOffset(tab)) }
    DisposableEffect(state) { onDispose { vm.saveScroll(tab, state.firstVisibleItemIndex, state.firstVisibleItemScrollOffset) } }
    return state
}
