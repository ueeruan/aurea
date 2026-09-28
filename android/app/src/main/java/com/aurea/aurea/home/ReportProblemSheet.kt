package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.conta.SessaoGuardada
import com.aurea.aurea.diagnostics.ProblemReport
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * "Relatar um problema": o que você fez, o que esperava / o que aconteceu
 * (obrigatório) e os passos para repetir, com a ficha do aparelho à vista.
 * Vai para o mesmo servidor dos relatórios de crash.
 */
@Composable
internal fun ReportProblemSheet(sessao: () -> SessaoGuardada?, onResult: (String) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var whatDid by rememberSaveable { mutableStateOf("") }
    var whatHappened by rememberSaveable { mutableStateOf("") }
    var steps by rememberSaveable { mutableStateOf("") }
    var sending by rememberSaveable { mutableStateOf(false) }
    val canSend = whatHappened.trim().length >= 3 && !sending
    val msgSent = stringResource(R.string.report_sent)
    val msgOffline = stringResource(R.string.report_offline)
    val msgLimit = stringResource(R.string.report_rate_limited)
    val msgFailed = stringResource(R.string.report_failed)
    AureaModalSheet(onDismiss = { if (!sending) onDismiss() }) {
        Column(
            Modifier.fillMaxWidth().imePadding().verticalScroll(rememberScrollState())
                .padding(start = 20.dp, top = 12.dp, end = 20.dp, bottom = 16.dp),
        ) {
            Text(stringResource(R.string.report_title), style = AureaType.TitleLarge)
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.report_subtitle), style = AureaType.Note)
            Spacer(Modifier.height(16.dp))
            ReportField(stringResource(R.string.report_what_did), whatDid, ProblemReport.WHAT_DID_MAX) { whatDid = it }
            ReportField(stringResource(R.string.report_what_happened), whatHappened, ProblemReport.WHAT_HAPPENED_MAX) { whatHappened = it }
            ReportField(stringResource(R.string.report_steps), steps, ProblemReport.STEPS_MAX) { steps = it }
            CapsLabel(stringResource(R.string.report_device))
            Spacer(Modifier.height(4.dp))
            Text(ProblemReport.deviceSummary(), style = AureaType.Note)
            Spacer(Modifier.height(20.dp))
            Box(
                Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(14.dp))
                    .background(AureaColors.Accent).alpha(if (canSend) 1f else 0.45f)
                    .pressHighlight {
                        if (!canSend) return@pressHighlight
                        sending = true
                        scope.launch {
                            val r = withContext(Dispatchers.IO) {
                                ProblemReport.send(context.applicationContext, sessao(), whatDid, whatHappened, steps)
                            }
                            sending = false
                            when (r) {
                                ProblemReport.Result.SENT -> { onResult(msgSent); onDismiss() }
                                ProblemReport.Result.OFFLINE -> onResult(msgOffline)
                                ProblemReport.Result.RATE_LIMITED -> onResult(msgLimit)
                                else -> onResult(msgFailed)
                            }
                        }
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text(stringResource(if (sending) R.string.report_sending else R.string.report_send), style = AureaType.Button)
            }
        }
    }
}

@Composable
private fun ReportField(label: String, value: String, max: Int, onChange: (String) -> Unit) {
    CapsLabel(label)
    Spacer(Modifier.height(6.dp))
    BasicTextField(
        value = value,
        onValueChange = { onChange(it.take(max)) },
        textStyle = AureaType.DialogField,
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences),
        modifier = Modifier.fillMaxWidth().heightIn(min = 72.dp).clip(RoundedCornerShape(10.dp))
            .background(AureaColors.FieldDialog).padding(horizontal = 12.dp, vertical = 10.dp),
    )
    Spacer(Modifier.height(14.dp))
}
