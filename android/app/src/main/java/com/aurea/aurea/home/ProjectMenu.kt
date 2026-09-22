package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaType

/**
 * Os menus e diálogos de um projeto: um por vez, encadeados (menu → renomear,
 * menu → excluir). Vive fora das abas porque Início e Projetos usam os MESMOS —
 * o cartão do hero, um cartão da grade e a barra de lote abrem todos aqui.
 */
internal sealed interface ProjectDialog {
    data class Menu(val entry: ProjectEntry) : ProjectDialog
    data class ConfirmDelete(val entry: ProjectEntry) : ProjectDialog
    data class Rename(val entry: ProjectEntry) : ProjectDialog
    data object DeleteAll : ProjectDialog
    data class BatchDelete(val paths: Set<String>) : ProjectDialog
    data object Sort : ProjectDialog
}

/** Quem está aberto agora. Um por aba, para as duas não brigarem pelo mesmo menu. */
internal class ProjectDialogState {
    var current by mutableStateOf<ProjectDialog?>(null)
}

/**
 * Desenha o diálogo aberto e age. [all] é a lista inteira (o "apagar todos" e a
 * poda da seleção olham para ela, não para o que está filtrado na tela).
 */
@Composable
internal fun ProjectDialogs(
    store: EditorStore,
    vm: HomeViewModel,
    all: List<ProjectEntry>,
    state: ProjectDialogState,
    onOpen: (ProjectEntry) -> Unit,
) {
    val close = { state.current = null }
    when (val d = state.current) {
        null -> Unit
        is ProjectDialog.Menu -> AureaActionSheet(
            title = d.entry.title,
            message = projectSpec(d.entry),
            actions = listOf(
                SheetAction(stringResource(R.string.common_open)) { onOpen(d.entry) },
                SheetAction(stringResource(R.string.common_duplicate)) { store.duplicateProject(d.entry.path) },
                SheetAction(stringResource(R.string.common_rename)) { state.current = ProjectDialog.Rename(d.entry) },
                SheetAction(stringResource(R.string.project_delete), destructive = true) { state.current = ProjectDialog.ConfirmDelete(d.entry) },
                SheetAction(stringResource(R.string.project_delete_all), destructive = true) { if (all.isNotEmpty()) state.current = ProjectDialog.DeleteAll },
            ),
            onDismiss = close,
        )
        // "Apagar todos" fica só no menu: ao lado da exclusão unitária o risco
        // de apagar tudo por engano não se paga.
        is ProjectDialog.ConfirmDelete -> AureaActionSheet(
            title = d.entry.title,
            actions = listOf(SheetAction(stringResource(R.string.project_delete), destructive = true) { store.deleteProjects(listOf(d.entry.path)) }),
            onDismiss = close,
        )
        is ProjectDialog.Rename -> RenameProjectDialog(
            initial = d.entry.title,
            onSave = { t -> if (t != d.entry.title) store.renameProjectFile(d.entry.path, t) },
            onDismiss = close,
        )
        ProjectDialog.DeleteAll -> AureaAlert(
            title = stringResource(R.string.project_delete_all_title),
            message = stringResource(R.string.project_delete_all_message, all.size),
            confirmLabel = stringResource(R.string.project_delete_all_confirm),
            destructive = true,
            onConfirm = {
                store.deleteProjects(all.map { it.path })
                vm.clearSelection()
            },
            onDismiss = close,
        )
        is ProjectDialog.BatchDelete -> AureaActionSheet(
            title = stringResource(R.string.project_delete_many_title, d.paths.size),
            message = stringResource(R.string.common_irreversible),
            actions = listOf(
                SheetAction(stringResource(R.string.common_delete), destructive = true) {
                    store.deleteProjects(d.paths)
                    vm.clearSelection()
                },
            ),
            onDismiss = close,
        )
        ProjectDialog.Sort -> AureaActionSheet(
            title = stringResource(R.string.home_sort_title),
            actions = ProjectSort.entries.map { s -> SheetAction(stringResource(s.label)) { vm.changeSort(s) } },
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
            BasicTextField(
                value = value,
                onValueChange = { value = it },
                singleLine = true,
                textStyle = AureaType.DialogField,
                cursorBrush = SolidColor(AureaColors.Accent),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    onDismiss()
                    save()
                }),
                modifier = Modifier
                    .padding(top = AureaDims.S3)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(7.dp))
                    .background(AureaColors.FieldDialog)
                    .padding(horizontal = AureaDims.S2, vertical = 7.dp)
                    .focusRequester(focus),
            )
        },
    )
}

/** A folha "Novo projeto", com os padrões dos Ajustes já preenchidos. */
@Composable
internal fun NewProjectSheetFor(store: EditorStore, vm: HomeViewModel, all: List<ProjectEntry>, onDismiss: () -> Unit) {
    NewProjectSheet(
        suggestedName = stringResource(R.string.project_new, all.size + 1),
        defaultAspectKey = vm.defaultAspectKey,
        defaultResolution = vm.defaultResolution,
        defaultFps = vm.defaultFps,
        onCreate = { spec -> vm.afterEngine(store) { store.newProject(spec.width, spec.height, spec.fps.toFloat(), spec.title) } },
        onDismiss = onDismiss,
        device = store.deviceReport,
    )
}
