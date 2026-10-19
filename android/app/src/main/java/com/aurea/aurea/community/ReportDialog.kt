package com.aurea.aurea.community

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionPresetStore
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException

private val reasonLabels = mapOf("spam" to R.string.mod_reason_spam, "nudity" to R.string.mod_reason_nudity,
    "violence" to R.string.mod_reason_violence, "hate" to R.string.mod_reason_hate, "harassment" to R.string.mod_reason_harassment,
    "illegal" to R.string.mod_reason_illegal, "copyright" to R.string.mod_reason_copyright, "other" to R.string.mod_reason_other)

/** Erro da comunidade em texto de gente; o resto vira a mensagem genérica. */
@StringRes internal fun reportErrorRes(e: Exception): Int = when ((e as? CommunityFailure)?.code) {
    "rate_limited" -> R.string.app_community_limit
    "unauthorized" -> R.string.social_session_error
    else -> R.string.social_error
}

/**
 * A denúncia de sempre: post, comentário, perfil, vídeo de IA e preset de legenda.
 * Motivo obrigatório, detalhe opcional (até 500). [onSend] roda fora da thread da
 * interface; se falhar, o diálogo fica aberto com o erro. Quem chama agradece em [onSent].
 */
@Composable
internal fun ReportDialog(onDismiss: () -> Unit, onSent: () -> Unit, errorText: (Exception) -> String? = { null },
    onSend: suspend (reason: String, details: String) -> Unit) {
    val context = LocalContext.current; val scope = rememberCoroutineScope()
    var reason by remember { mutableStateOf<String?>(null) }; var details by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }; var error by remember { mutableStateOf<String?>(null) }
    AlertDialog(onDismissRequest = { if (!busy) onDismiss() }, modifier = Modifier.testTag("mod.report"),
        title = { Text(stringResource(R.string.mod_report_title)) },
        text = { Column(Modifier.verticalScroll(rememberScrollState())) {
            Column(Modifier.selectableGroup()) { for (r in reportReasons) {
                Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).selectable(reason == r, enabled = !busy, role = Role.RadioButton) { reason = r }
                    .testTag("mod.report.reason.$r"), verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(reason == r, onClick = null, enabled = !busy)
                    Text(stringResource(reasonLabels.getValue(r)), style = AureaType.Body, modifier = Modifier.padding(start = 12.dp))
                }
            } }
            OutlinedTextField(details, { details = it.clipReportDetails() }, enabled = !busy, minLines = 2, maxLines = 4,
                label = { Text(stringResource(R.string.mod_report_details)) }, supportingText = { Text("${details.length}/$REPORT_DETAILS_MAX") },
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp).testTag("mod.report.details"))
            error?.let { Text(it, color = AureaColors.Danger, modifier = Modifier.padding(top = 4.dp).testTag("mod.report.error")) }
        } },
        confirmButton = { TextButton(enabled = !busy && reason != null, modifier = Modifier.heightIn(min = 48.dp).testTag("mod.report.send"), onClick = {
            val chosen = reason ?: return@TextButton
            scope.launch { busy = true; error = null
                try { withContext(Dispatchers.IO) { onSend(chosen, details) }; onSent() }
                catch (e: CancellationException) { throw e } catch (e: Exception) { error = errorText(e) ?: context.getString(reportErrorRes(e)) }
                finally { busy = false }
            }
        }) { if (busy) CircularProgressIndicator(Modifier.size(20.dp)) else Text(stringResource(R.string.mod_report_send)) } },
        dismissButton = { TextButton(enabled = !busy, onClick = onDismiss, modifier = Modifier.heightIn(min = 48.dp).testTag("mod.report.cancel")) {
            Text(stringResource(R.string.editor_fechar)) } })
}

/**
 * "Denunciar preset" de um item da comunidade de legendas. Vai com a sessão
 * anônima do [CaptionPresetStore]; com 3 sessões o servidor tira o preset da lista.
 */
@Composable
internal fun CaptionPresetReportAction(entry: JSONObject, library: CaptionPresetStore, enabled: Boolean = true, onReported: () -> Unit) {
    var open by remember { mutableStateOf(false) }
    TextButton(enabled = enabled, onClick = { open = true }, modifier = Modifier.heightIn(min = 48.dp).testTag("mod.preset.report.${entry.optString("id")}")) {
        Text(stringResource(R.string.mod_report_preset))
    }
    // O store só lança IOException "puro" com texto já traduzido (limite, servidor fora);
    // subclasses (sem rede, tempo esgotado) ficam com a mensagem genérica.
    if (open) ReportDialog(onDismiss = { open = false }, onSent = { open = false; onReported() },
        errorText = { e -> e.message?.takeIf { e.javaClass == IOException::class.java } }) { reason, details -> library.report(entry, reason, details) }
}
