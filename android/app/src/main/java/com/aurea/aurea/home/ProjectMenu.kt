package com.aurea.aurea.home

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.TextFieldValue
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole

/**
 * Os diálogos de um projeto (Omega home): um por vez, encadeados a partir do
 * menu rápido (P1-2) e da barra de seleção (P0-6). Todos claros (fundação F4).
 * O menu em si é o [ProjectQuickMenu]; a ordenação é um menu ancorado na aba.
 */
internal sealed interface ProjectDialog {
    data class ConfirmDelete(val entry: ProjectEntry) : ProjectDialog
    data class Rename(val entry: ProjectEntry) : ProjectDialog
    /** "Exportar arquivo do projeto": com ou sem a mídia. */
    data class ExportFile(val entry: ProjectEntry) : ProjectDialog
    data class BatchDelete(val paths: Set<String>) : ProjectDialog
}

/** Quem está aberto agora (um só para a Home inteira). */
internal class ProjectDialogState {
    var current by mutableStateOf<ProjectDialog?>(null)
}

/**
 * Desenha o diálogo aberto e age. [all] é a lista inteira (a poda da seleção
 * olha para ela, não para o que está filtrado na tela).
 */
@Composable
internal fun ProjectDialogs(
    store: EditorStore,
    vm: HomeViewModel,
    all: List<ProjectEntry>,
    state: ProjectDialogState,
    @Suppress("UNUSED_PARAMETER") onOpen: (ProjectEntry) -> Unit,
) {
    val close = { state.current = null }
    // Destino do arquivo do projeto: o documento que a pessoa escolhe (o
    // lançador vive aqui, fora do diálogo, para o retorno achar quem pediu).
    var pendingExport by remember { mutableStateOf<Pair<String, Boolean>?>(null) }
    val exportTarget = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val p = pendingExport
        pendingExport = null
        if (uri != null && p != null) store.exportProjectFile(p.first, p.second, uri)
    }
    LaunchedEffect(all) { vm.pruneSelection(all) }
    when (val d = state.current) {
        null -> Unit
        is ProjectDialog.ConfirmDelete -> AureaAlert(
            title = d.entry.title,
            message = stringResource(R.string.common_irreversible),
            confirmLabel = stringResource(R.string.project_delete),
            destructive = true,
            onConfirm = { store.deleteProjects(listOf(d.entry.path)) },
            onDismiss = close,
        )
        is ProjectDialog.ExportFile -> AureaDialog(
            title = stringResource(R.string.project_file_export),
            message = stringResource(R.string.project_file_export_message),
            onDismiss = close,
            tag = "home.exportFile",
            actions = listOf(
                DialogAction(stringResource(R.string.project_file_include_media), DialogRole.Positive, tag = "home.exportFile.withMedia") {
                    pendingExport = d.entry.path to true
                    vm.clearSelection()
                    exportTarget.launch(store.projectFileName(d.entry.path))
                },
                DialogAction(stringResource(R.string.project_file_without_media), DialogRole.Neutral, tag = "home.exportFile.withoutMedia") {
                    pendingExport = d.entry.path to false
                    vm.clearSelection()
                    exportTarget.launch(store.projectFileName(d.entry.path))
                },
                DialogAction(stringResource(R.string.common_cancel), DialogRole.Negative),
            ),
        )
        is ProjectDialog.Rename -> RenameProjectDialog(
            initial = d.entry.title,
            onSave = { t -> if (t != d.entry.title) store.renameProjectFile(d.entry.path, t) },
            onDismiss = close,
        )
        is ProjectDialog.BatchDelete -> AureaAlert(
            title = stringResource(R.string.project_delete_many_title, d.paths.size),
            message = stringResource(R.string.common_irreversible),
            confirmLabel = stringResource(R.string.common_delete),
            destructive = true,
            onConfirm = {
                store.deleteProjects(d.paths)
                vm.clearSelection()
            },
            onDismiss = close,
        )
    }
}

/**
 * "Renomear" com o campo em foco (texto selecionado para trocar de uma vez),
 * capitalização de frases, Enter salva. Vazio → nada.
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
        title = stringResource(R.string.common_rename),
        confirmLabel = stringResource(R.string.common_save),
        onConfirm = save,
        onDismiss = onDismiss,
        extra = {
            // Omega fundação: o campo do diálogo claro (F4), com ✕ de limpar.
            com.aurea.aurea.ui.ds.AureaDialogField(
                value = value,
                onValueChange = { value = it },
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    onDismiss()
                    save()
                }),
                tag = "dialog.field",
                focusRequester = focus,
            )
        },
    )
}

/**
 * Nome sugerido da folha: o primeiro "Projeto N" ainda não usado (a
 * referência procura o primeiro livre; antes era total + 1).
 */
internal fun suggestedProjectName(titles: Collection<String>, nameFor: (Int) -> String): String {
    val used = titles.mapTo(HashSet()) { it.trim().lowercase() }
    var n = 1
    while (n < 10_000 && nameFor(n).lowercase() in used) n++
    return nameFor(n)
}

/** A folha "Novo projeto", com o que ela lembra (ou os padrões dos Ajustes). */
@Composable
internal fun NewProjectSheetFor(store: EditorStore, vm: HomeViewModel, all: List<ProjectEntry>, onDismiss: () -> Unit) {
    val template = stringResource(R.string.project_new, 0)
    val context = androidx.compose.ui.platform.LocalContext.current
    val suggested = remember(all, template) {
        suggestedProjectName(all.map { it.title }) { n -> context.getString(R.string.project_new, n) }
    }
    NewProjectSheet(
        suggestedName = suggested,
        defaults = NewProjectDefaults(
            aspectKey = vm.defaultAspectKey,
            resolution = vm.defaultResolution,
            fps = (vm.lastFps ?: vm.defaultFps.toFloat()).toDouble(),
            free = vm.lastFree,
            freeWidth = vm.lastFreeWidth,
            freeHeight = vm.lastFreeHeight,
            keepRatio = vm.lastKeepRatio,
            background = vm.lastBackground,
        ),
        onCreate = { spec ->
            vm.rememberNewProject(
                aspectKey = spec.aspectKey, resolution = spec.resolution, free = spec.free,
                freeWidth = spec.width, freeHeight = spec.height, keepRatio = spec.keepRatio,
                fps = spec.fps, background = spec.background, transparent = spec.transparent,
            )
            // fps livre e fundo escolhidos na folha vão direto para o motor (fora do histórico).
            // Alfa 0 = "Transparente" (o motor decide: Composition::set_background_choice).
            val background = floatArrayOf(spec.background[0], spec.background[1], spec.background[2], if (spec.transparent) 0f else 1f)
            vm.afterEngine(store) {
                store.newProject(spec.width, spec.height, spec.fps, spec.title, background, spec.uiMode)
                if (vm.lightPreview) store.setPreviewScale(false, 1, 2)
            }
        },
        onDismiss = onDismiss,
        device = store.deviceReport,
    )
}
