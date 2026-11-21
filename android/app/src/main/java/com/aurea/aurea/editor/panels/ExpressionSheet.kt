package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.i18n.KeepLtr
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.safeDrawingPadding
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
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.testTag
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
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.engine.ExpressionDiag
import com.aurea.aurea.engine.ExpressionInfo
import com.aurea.aurea.state.EditorStore
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
    "shake(30, 8)",
    "shake(80, 14, 0.6, 0.6, inPoint)",
    "ease(timeSinceMarker(), 0, 0.3, 120, 100)",
    "posterizeTime(12); wiggle(2, 30)",
    "loopOut(\"cycle\")",
    "time * 90",
    "value",
    "loopOut(\"pingpong\")",
    "linear(time, 0, 1, 0, 100)",
    "effect(\"Slider Control\")(\"Slider\")",
    "thisComp.layer(1).transform.position",
    "random(0, 100)",
    // AE animation: espaço da camada, mira, caixa do conteúdo e outra composição.
    "thisComp.layer(1).toComp([0, 0])",
    "fromComp(thisComp.layer(1).toComp([0, 0]))",
    "thisComp.layer(1).toWorld(thisComp.layer(1).anchorPoint)",
    "lookAt(position, thisComp.layer(1).position)",
    "var r = thisLayer.sourceRectAtTime(); [r.left + r.width / 2, r.top + r.height / 2]",
    "comp(\"Comp 1\").layer(1).transform.position",
)

/**
 * AE expressions (lote E), só no Aurea PC (PlatformFeatures.desktopAe): atalhos
 * com nome para o que entrou nas expressões — conversão de cor, seguir um
 * caminho, trocar de valor por um Menu suspenso e função própria. Como os
 * bounces, cada um troca o texto inteiro.
 */
internal const val AeSnipColor = """c = hexToRgb("FF8800");
hsl = rgbToHsl(c);
hslToRgb([(hsl[0] + time * 0.1) % 1, hsl[1], hsl[2], 1]);"""

internal const val AeSnipPathFollow = """L = thisComp.layer(1);
p = L.mask(1).maskPath;
t = (time * 0.25) % 1;
L.toComp(p.pointOnPath(t));"""

internal const val AeSnipDropdown = """m = effect("Dropdown Menu Control")("Menu").value;
switch (m) {
  case 1: value; break;
  case 2: value * 1.5; break;
  default: value * 2;
}"""

internal const val AeSnipFunction = """function pulse(t, freq, amount) {
  return 1 + Math.sin(t * freq * 2 * Math.PI) * amount;
}
value * pulse(time, 2, 0.1);"""

/** Para a expressão do CAMINHO de uma máscara (o painel Máscara do Aurea PC abre a folha nela). */
internal const val AeSnipCreatePath = """p = thisProperty.points();
s = 1 + Math.sin(time * 2) * 0.1;
createPath(p.map(q => q * s), thisProperty.inTangents(), thisProperty.outTangents(), thisProperty.isClosed());"""

/**
 * v70: os bounces do After Effects, inteiros, com nome no atalho (o código é
 * longo demais para o botão). Funcionam em qualquer propriedade com keyframes.
 */
internal const val BounceInertia = """amp = .06;
freq = 2.5;
decay = 5.0;
n = 0;
if (numKeys > 0) {
  n = nearestKey(time).index;
  if (key(n).time > time) n--;
}
t = n == 0 ? 0 : time - key(n).time;
if (n > 0 && t < 1) {
  v = velocityAtTime(key(n).time - thisComp.frameDuration / 10);
  value + v * amp * Math.sin(freq * t * 2 * Math.PI) / Math.exp(decay * t);
} else {
  value;
}"""

internal const val BounceDrop = """e = .7;
g = 5000;
nMax = 9;
n = 0;
if (numKeys > 0) {
  n = nearestKey(time).index;
  if (key(n).time > time) n--;
}
if (n > 0) {
  t = time - key(n).time;
  v = -velocityAtTime(key(n).time - .001) * e;
  vl = length(v);
  vu = vl > 0 ? v / vl : v * 0;
  tCur = 0;
  segDur = 2 * vl / g;
  tNext = segDur;
  nb = 1;
  while (tNext < t && nb <= nMax) {
    vl *= e;
    segDur *= e;
    tCur = tNext;
    tNext += segDur;
    nb++;
  }
  if (nb <= nMax) {
    delta = t - tCur;
    value + vu * delta * (vl - g * delta / 2);
  } else {
    value;
  }
} else {
  value;
}"""

/** v70: atalhos da expressão do texto-fonte (o resultado vira o texto). */
private val TextSnippets = listOf(
    "value.substr(0, Math.floor(time * 12))",
    "Math.round(linear(time, 0, 2, 0, 1000))",
    "timeToTimecode()",
    "value.toUpperCase()",
    "\"Pontos: \" + Math.floor(time * 10)",
    "time.toFixed(1) + \"s\"",
    "thisComp.layer(1).name",
    "value + \" \" + Math.floor(time)",
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
    // v70: no texto-fonte o resultado é texto (e os atalhos são de texto).
    val textSource = target.tracks.firstOrNull()?.property == com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE
    // Erro mostrado: sintaxe do texto em edição; sem edição pendente, o do motor (execução).
    val shownError: ExpressionDiag? = when {
        !syntax.ok -> syntax
        !dirty && applied && info?.error?.ok == false -> info?.error
        else -> null
    }

    Dialog(
        onDismissRequest = store::closeExpression,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        val focus = LocalFocusManager.current
        Column(Modifier.fillMaxSize().background(AureaColors.Surface).safeDrawingPadding()
            .imePadding().padding(horizontal = 16.dp).testTag("expression.sheet")) {
            Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SheetButton(stringResource(R.string.panel_fechar), AureaColors.Text,
                    Modifier.weight(1f).testTag("expression.close"), onClick = store::closeExpression)
                SheetButton(stringResource(R.string.panel_aplicar), AureaColors.Accent,
                    Modifier.weight(1f).testTag("expression.apply"), enabled = dirty) {
                    val diagnostic = store.applyExpression(target, field.text)
                    refused = diagnostic == null
                    if (diagnostic != null) {
                        syntax = diagnostic
                        if (diagnostic.ok) focus.clearFocus()
                    }
                    reread()
                }
            }
            Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState())) {
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
                        val msg = if (shownError.line > 0) stringResource(R.string.pn_expr_error_at, shownError.line, shownError.column, shownError.message) else shownError.message
                        val tail = if (!dirty && applied && syntax.ok) stringResource(R.string.panel_usando_valor_keyframes) else ""
                        (msg + tail) to AureaColors.Danger
                    }
                    dirty -> (if (field.text.isBlank()) stringResource(R.string.panel_aplicar_sem_texto_remove_expressao) else stringResource(R.string.panel_sintaxe_ok_toque_aplicar)) to AureaColors.Muted
                    applied && info?.enabled == false -> stringResource(R.string.panel_desligada_propriedade_usa_keyframes) to AureaColors.Muted
                    applied && textSource -> stringResource(R.string.panel_resultado_agora) + "\u201C" + (info?.text ?: "") + "\u201D" to AureaColors.Keyframe
                    applied -> stringResource(R.string.panel_resultado_agora) + values.joinToString(" · ") { numeroPtBr(it, 2) + target.unit } to AureaColors.Keyframe
                    else -> stringResource(R.string.panel_escreva_expressao_ou_toque_num_atalho) to AureaColors.Muted
                }
                Text(status.first, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = status.second)))
                Spacer(Modifier.height(10.dp))
                // Atalho: o rótulo (nulo = o próprio código) e o código que entra no cursor.
                val bounceInertia = stringResource(R.string.expr_snip_bounce)
                val bounceDrop = stringResource(R.string.expr_snip_bounce_drop)
                // AE expressions (lote E): só no Aurea PC; no celular a lista fica como estava.
                val aeShortcuts: List<Pair<String?, String>> =
                    if (com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe) listOf(
                        stringResource(R.string.expr_snip_ae_color) to AeSnipColor,
                        stringResource(R.string.expr_snip_ae_path) to AeSnipPathFollow,
                        stringResource(R.string.expr_snip_ae_dropdown) to AeSnipDropdown,
                        stringResource(R.string.expr_snip_ae_function) to AeSnipFunction,
                        stringResource(R.string.expr_snip_ae_create_path) to AeSnipCreatePath,
                    ) else emptyList()
                val shortcuts: List<Pair<String?, String>> =
                    if (textSource) TextSnippets.map { null to it }
                    else listOf(bounceInertia to BounceInertia, bounceDrop to BounceDrop) + aeShortcuts + Snippets.map { null to it }
                Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    shortcuts.forEach { (name, snip) ->
                        Box(
                            Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(AureaColors.Chip)
                                .tocavel {
                                    val t = field.text
                                    val a = field.selection.min.coerceIn(0, t.length)
                                    val b = field.selection.max.coerceIn(0, t.length)
                                    // Um bounce inteiro troca o texto todo (ele já usa `value`).
                                    val whole = name != null
                                    val nt = if (whole) snip else t.substring(0, a) + snip + t.substring(b)
                                    field = TextFieldValue(nt, TextRange(if (whole) nt.length else a + snip.length))
                                }
                                .padding(horizontal = 10.dp, vertical = 7.dp),
                        ) {
                            if (name != null) {
                                Text(name, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Keyframe)))
                            } else {
                                Text(snip, style = Mono.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Keyframe)))
                            }
                        }
                    }
                }
                if (applied) {
                    SheetButton(stringResource(R.string.panel_remover), AureaColors.Danger,
                        Modifier.fillMaxWidth().padding(top = 12.dp).testTag("expression.remove")) {
                        refused = store.applyExpression(target, "") == null
                        if (!refused) {
                            field = TextFieldValue("")
                            reread()
                        }
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
        }
    }
}

/** Texto com a coluna de números de linha; a linha do erro em vermelho. */
@Composable
private fun CodeEditor(value: TextFieldValue, errorLine: Int, onChange: (TextFieldValue) -> Unit) = KeepLtr {
    // Código é LTR em qualquer idioma: números de linha à esquerda, texto da esquerda.
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
            textStyle = Mono.merge(TextStyle(color = AureaColors.Text, textDirection = androidx.compose.ui.text.style.TextDirection.Ltr)),
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
