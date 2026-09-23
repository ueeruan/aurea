package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.ExpressionDiag
import com.aurea.aurea.engine.ExpressionInfo
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay

/**
 * Atalhos que entram no cursor. São expressões inteiras do motor (nenhuma é
 * enfeite): tocar insere o texto; "Aplicar" grava.
 */
private val Snippets = listOf(
    "wiggle(2, 30)",
    "loopOut(\"cycle\")",
    "time * 90",
    "value",
    "loopOut(\"pingpong\")",
    "linear(time, 0, 1, 0, 100)",
    "effect(\"Slider Control\")(\"Slider\")",
    "thisComp.layer(1).transform.position",
    "random(0, 100)",
)

private val Mono = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 14.sp, lineHeight = 20.sp)

/**
 * O EDITOR DE EXPRESSÃO de uma propriedade: texto em fonte fixa com a coluna de
 * linhas (a linha do erro fica vermelha), validação de sintaxe enquanto digita
 * (o motor compila, não o Kotlin), atalhos, liga/desliga e o resultado no
 * cabeçote depois de aplicar. Erro de execução aparece aqui também — a
 * propriedade nunca quebra: sem expressão válida, vale o keyframe.
 */
@Composable
internal fun ExpressionSheet(store: EditorStore, target: EditorStore.ExpressionTarget) {
    var info by remember(target) { mutableStateOf<ExpressionInfo?>(store.expressionInfo(target)) }
    var field by remember(target) {
        val src = info?.takeIf { it.exists }?.source ?: ""
        mutableStateOf(TextFieldValue(src, TextRange(src.length)))
    }
    var syntax by remember(target) { mutableStateOf(ExpressionDiag.OK) }
    var values by remember(target) { mutableStateOf(if (info?.exists == true) store.expressionValues(target) else emptyList()) }
    var refused by remember(target) { mutableStateOf(false) }

    // Validação enquanto digita: 250 ms depois da última tecla.
    LaunchedEffect(field.text) {
        delay(250)
        syntax = if (field.text.isBlank()) ExpressionDiag.OK else store.checkExpressionSyntax(field.text)
    }

    fun reread() {
        info = store.expressionInfo(target)
        values = if (info?.exists == true) store.expressionValues(target) else emptyList()
    }

    val applied = info?.exists == true
    val dirty = field.text != (info?.source ?: "")
    // Erro mostrado: sintaxe do texto em edição; sem edição pendente, o do motor (execução).
    val shownError: ExpressionDiag? = when {
        !syntax.ok -> syntax
        !dirty && applied && info?.error?.ok == false -> info?.error
        else -> null
    }

    AureaModalSheet(onDismiss = { store.closeExpression() }) {
        Column(Modifier.fillMaxWidth().imePadding().padding(horizontal = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.panel_expressao), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
                    Text(target.label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                }
                if (applied) {
                    Text(
                        if (info?.enabled == true) stringResource(R.string.panel_ligada) else stringResource(R.string.panel_desligada),
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                    )
                    Spacer(Modifier.width(8.dp))
                    AureaToggle(checked = info?.enabled == true, onCheckedChange = { on ->
                        store.setExpressionEnabled(target, on)
                        reread()
                    })
                }
            }
            Spacer(Modifier.height(12.dp))
            CodeEditor(field, errorLine = shownError?.line ?: 0, onChange = { field = it })
            Spacer(Modifier.height(8.dp))
            // Linha de estado: erro (com linha e coluna) ou o resultado no cabeçote.
            val status = when {
                refused -> stringResource(R.string.panel_motor_recusou_esta_propriedade) to AureaColors.Danger
                shownError != null -> {
                    val where = if (shownError.line > 0) "Linha ${shownError.line}, coluna ${shownError.column}: " else ""
                    val tail = if (!dirty && applied && syntax.ok) stringResource(R.string.panel_usando_valor_keyframes) else ""
                    (where + shownError.message + tail) to AureaColors.Danger
                }
                dirty -> (if (field.text.isBlank()) stringResource(R.string.panel_aplicar_sem_texto_remove_expressao) else stringResource(R.string.panel_sintaxe_ok_toque_aplicar)) to AureaColors.Muted
                applied && info?.enabled == false -> stringResource(R.string.panel_desligada_propriedade_usa_keyframes) to AureaColors.Muted
                applied -> stringResource(R.string.panel_resultado_agora) + values.joinToString(" · ") { numeroPtBr(it, 2) + target.unit } to AureaColors.Keyframe
                else -> stringResource(R.string.panel_escreva_expressao_ou_toque_num_atalho) to AureaColors.Muted
            }
            Text(status.first, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = status.second)))
            Spacer(Modifier.height(10.dp))
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Snippets.forEach { snip ->
                    Box(
                        Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(AureaColors.Chip)
                            .tocavel {
                                val t = field.text
                                val a = field.selection.min.coerceIn(0, t.length)
                                val b = field.selection.max.coerceIn(0, t.length)
                                val nt = t.substring(0, a) + snip + t.substring(b)
                                field = TextFieldValue(nt, TextRange(a + snip.length))
                            }
                            .padding(horizontal = 10.dp, vertical = 7.dp),
                    ) {
                        Text(snip, style = Mono.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Keyframe)))
                    }
                }
            }
            Spacer(Modifier.height(14.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (applied) {
                    SheetButton(stringResource(R.string.panel_remover), AureaColors.Danger, Modifier.weight(1f)) {
                        refused = store.applyExpression(target, "") == null
                        field = TextFieldValue("")
                        reread()
                    }
                }
                SheetButton(stringResource(R.string.panel_fechar), AureaColors.Text, Modifier.weight(1f)) { store.closeExpression() }
                SheetButton(stringResource(R.string.panel_aplicar), AureaColors.Accent, Modifier.weight(1f), enabled = dirty) {
                    val d = store.applyExpression(target, field.text)
                    refused = d == null
                    if (d != null) syntax = d
                    reread()
                }
            }
            Spacer(Modifier.height(12.dp))
        }
    }
}

/** Texto com a coluna de números de linha; a linha do erro em vermelho. */
@Composable
private fun CodeEditor(value: TextFieldValue, errorLine: Int, onChange: (TextFieldValue) -> Unit) {
    val lines = value.text.count { it == '\n' } + 1
    val scroll = rememberScrollState()
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 120.dp, max = 220.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(Color(0xFF0B1016))
            .verticalScroll(scroll)
            .padding(vertical = 10.dp),
    ) {
        Column(Modifier.width(34.dp)) {
            for (i in 1..lines) {
                Text(
                    "$i",
                    modifier = Modifier.fillMaxWidth().padding(end = 6.dp),
                    style = Mono.merge(
                        TextStyle(
                            textAlign = TextAlign.End,
                            color = if (i == errorLine) AureaColors.Danger else AureaColors.Muted.copy(alpha = 0.6f),
                            fontWeight = if (i == errorLine) FontWeight.W700 else FontWeight.Normal,
                        ),
                    ),
                )
            }
        }
        BasicTextField(
            value = value,
            onValueChange = onChange,
            textStyle = Mono.merge(TextStyle(color = AureaColors.Text)),
            cursorBrush = SolidColor(AureaColors.Accent),
            keyboardOptions = KeyboardOptions(
                capitalization = KeyboardCapitalization.None,
                autoCorrectEnabled = false,
                keyboardType = KeyboardType.Ascii,
            ),
            modifier = Modifier.weight(1f).padding(end = 10.dp),
        )
    }
}

@Composable
private fun SheetButton(label: String, color: Color, modifier: Modifier, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        modifier
            .height(44.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .tocavel(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            style = AureaType.Base.merge(
                TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = if (enabled) color else AureaColors.Disabled),
            ),
        )
    }
}
