package com.aurea.aurea.home

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.*
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.BuildConfig
import androidx.compose.ui.res.pluralStringResource
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaEstado
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.*

@Composable
fun HomeScreen(store: EditorStore) {
    val vm: HomeViewModel = viewModel()
    // O mesmo ViewModel da MainActivity (mesmo dono, mesma chave).
    val conta: ContaViewModel = viewModel()
    var confirmLogout by remember { mutableStateOf(false) }
    val projects = rememberTabListState(vm, HomeViewModel.PROJECTS_TAB)
    val settings = rememberTabListState(vm, HomeViewModel.SETTINGS_TAB)
    var menu by remember { mutableStateOf(false) }
    var about by remember { mutableStateOf(false) }
    var licenses by remember { mutableStateOf(false) }
    var newProject by rememberSaveable { mutableStateOf(false) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) vm.createFromMedia(store, uri)
    }
    // "Importar arquivo do projeto": qualquer tipo (o .aureaproj não tem MIME
    // registrado); o motor diz se é projeto do Aurea.
    val projectFilePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.afterEngine(store) { store.importProjectFile(uri) }
    }
    var reportProblem by rememberSaveable { mutableStateOf(false) }
    BackHandler(vm.tab != HomeViewModel.PROJECTS_TAB) { vm.selectTab(HomeViewModel.PROJECTS_TAB) }
    Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
        LiveNoticeBanners(vm.notices)
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("aurea", style = AureaType.HeadlineLarge.copy(fontWeight = FontWeight.Bold, letterSpacing = (-1).sp))
                RegisteredUsersLine(conta.usuarios)
            }
            ReleaseNotesEntry()
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (vm.tab) {
                HomeViewModel.HOME_TAB -> CommunityPresetsTab()
                HomeViewModel.SETTINGS_TAB -> SettingsTab(store, vm, settings, AureaDims.TabBarHeight)
                else -> ProjectsTab(store, vm, projects, AureaDims.TabBarHeight)
            }
        }
        HomeDock(selected = vm.tab, onProjects = { vm.selectTab(HomeViewModel.PROJECTS_TAB) },
            onCommunity = { vm.selectTab(HomeViewModel.HOME_TAB) }, onCreate = { newProject = true },
            onMenu = { menu = true }, onImport = {
                picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
            })
    }
    if (newProject) NewProjectSheetFor(store, vm, store.projects) { newProject = false }
    val signedIn = (conta.estado as? ContaEstado.Dentro)?.email
    if (menu) AureaActionSheet(title = "Aurea",
        message = signedIn?.let { stringResource(R.string.conta_conectado, it) },
        onDismiss = { menu = false }, actions = listOf(
        SheetAction(stringResource(R.string.home_tab_settings)) { vm.selectTab(HomeViewModel.SETTINGS_TAB) },
        SheetAction(stringResource(R.string.project_file_import)) { projectFilePicker.launch(arrayOf("*/*")) },
        SheetAction(stringResource(R.string.report_title)) { reportProblem = true },
        SheetAction(stringResource(R.string.settings_group_about)) { about = true },
        SheetAction(stringResource(R.string.licenses_title)) { licenses = true },
        SheetAction(stringResource(R.string.conta_sair), destructive = true) { confirmLogout = true },
    ))
    if (reportProblem) ReportProblemSheet(sessao = { conta.sessao() }, onResult = { store.showToast(it) }) { reportProblem = false }
    if (confirmLogout) AureaAlert(
        title = stringResource(R.string.conta_sair),
        message = stringResource(R.string.conta_sair_mensagem),
        confirmLabel = stringResource(R.string.conta_sair),
        destructive = true,
        onConfirm = { confirmLogout = false; conta.sair() },
        onDismiss = { confirmLogout = false },
    )
    if (about || licenses) AlertDialog(onDismissRequest = { about = false; licenses = false },
        title = { Text(stringResource(if (licenses) R.string.licenses_title else R.string.settings_group_about)) },
        text = { Text(if (licenses) stringResource(R.string.licenses_ai_body) else
            "Aurea ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})\n\n" + stringResource(R.string.settings_technology_value) + "\n\n" + stringResource(R.string.settings_made_by),
            modifier = Modifier.heightIn(max = 400.dp).verticalScroll(rememberScrollState())) },
        confirmButton = { TextButton(onClick = { about = false; licenses = false }) { Text(stringResource(R.string.editor_fechar)) } })
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

/** Equal side groups keep Create precisely centered at every phone width. */
@Composable
internal fun HomeDock(selected: Int, onProjects: () -> Unit, onCommunity: () -> Unit,
                      onCreate: () -> Unit, onMenu: () -> Unit, onImport: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)
        .clip(RoundedCornerShape(28.dp)).background(AureaColors.Surface)
        .border(1.dp, AureaColors.Border, RoundedCornerShape(28.dp)).padding(horizontal = 4.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically) {
        DockUtility(CupertinoGlyph.SliderHorizontal3, stringResource(R.string.home_tab_settings), "home.menu", onMenu)
        DockTab(CupertinoGlyph.RectangleStack, stringResource(R.string.home_tab_projects), selected == HomeViewModel.PROJECTS_TAB,
            Modifier.weight(1f).testTag("home.projects"), onProjects)
        val createLabel = stringResource(R.string.home_new_project)
        Box(Modifier.size(64.dp).clip(CircleShape).background(AureaColors.Accent)
            .testTag("home.create").semantics { contentDescription = createLabel }
            .clickable(role = Role.Button, onClick = onCreate), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Plus, 30.dp, AureaColors.OnAccent)
        }
        DockTab(CupertinoGlyph.Sparkles, stringResource(R.string.home_presets_short), selected == HomeViewModel.HOME_TAB,
            Modifier.weight(1f).testTag("home.community"), onCommunity)
        DockUtility(CupertinoGlyph.PhotoOnRectangle, stringResource(R.string.home_import_media), "home.import", onImport)
    }
}

@Composable
private fun DockUtility(icon: Char, label: String, tag: String, onClick: () -> Unit) {
    Box(Modifier.size(48.dp).clip(CircleShape).testTag(tag).semantics { contentDescription = label }
        .clickable(role = Role.Button, onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(icon, 21.dp, AureaColors.Muted)
    }
}

@Composable
private fun DockTab(icon: Char, label: String, active: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Column(modifier.heightIn(min = 64.dp).clip(RoundedCornerShape(16.dp))
        .semantics { selected = active }.clickable(role = Role.Tab, onClick = onClick).padding(vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterVertically)) {
        CupertinoIcon(icon, 23.dp, if (active) AureaColors.Accent else AureaColors.Muted)
        Text(label, style = AureaType.BodySmall.copy(fontSize = 12.sp, fontWeight = if (active) FontWeight.SemiBold else FontWeight.Normal),
            color = if (active) AureaColors.Text else AureaColors.Muted, textAlign = TextAlign.Center)
        Box(Modifier.size(width = 16.dp, height = 2.dp).background(if (active) AureaColors.Accent else AureaColors.Surface, CircleShape))
    }
}

@Composable
private fun rememberTabListState(vm: HomeViewModel, tab: Int): LazyListState {
    val state = remember { LazyListState(vm.scrollIndex(tab), vm.scrollOffset(tab)) }
    DisposableEffect(state) { onDispose { vm.saveScroll(tab, state.firstVisibleItemIndex, state.firstVisibleItemScrollOffset) } }
    return state
}
