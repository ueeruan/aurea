package com.aurea.aurea.ui.ds

import com.aurea.aurea.ui.i18n.KeepLtr
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * O que o teclado precisa para abrir: título, valor de partida, faixa e casas.
 * [min]..[max] é a faixa que o teclado ACEITA (num parâmetro de efeito, a faixa
 * digitada, que pode passar da régua). [percentBase] é o 100 % de "50%" quando
 * a unidade não é "%" (padrão: [max]; o efeito passa o fim da régua).
 */
class KeypadRequest(
    val title: String,
    val initial: Float,
    val unit: String,
    val min: Float,
    val max: Float,
    val decimals: Int,
    val percentBase: Float = max,
    /**
     * Omega efeitos (P2-2): VÁRIOS CAMPOS no mesmo teclado (X/Y, X/Y/Z, α/β/γ),
     * uma aba por campo no visor; [focus] é o campo que abre selecionado. Vazio
     * = um campo só (o de sempre). Com campos, o OK chama [onConfirmFields] com
     * o valor novo de cada campo (nulo = não mexido), num gesto só de quem chama.
     */
    val fields: List<KeypadField> = emptyList(),
    val focus: Int = 0,
    val onConfirmFields: ((List<Float?>) -> Unit)? = null,
    val onConfirm: (Float) -> Unit,
)

/** Um campo do teclado de vários campos: rótulo (X, Y, α…), valor, padrão e faixa aceita. */
class KeypadField(
    val label: String,
    val initial: Float,
    val default: Float,
    val min: Float,
    val max: Float,
)

/**
 * A CONTA DO TECLADO (`domain/expr.dart` da A.01): × ÷ antes de + −; "1:30" = 90
 * (tempo com dois-pontos; "1:02:03,5" = 3723,5); "50%" = 50 % de [percentOf].
 * Vírgula é o decimal; ponto só é aceito como milhar quando há vírgula ("1.234,56").
 * Devolve NULO quando a conta não fecha (termina em operador, vazio, ÷ 0).
 */
object ValueExpression {
    const val MINUS = '−'
    const val TIMES = '×'
    const val DIVIDE = '÷'

    fun isOperator(c: Char) = c == '+' || c == MINUS || c == '-' || c == TIMES || c == DIVIDE

    fun evaluate(text: String, percentOf: Double): Double? {
        val s = text.trim()
        if (s.isEmpty()) return null
        val numbers = ArrayList<Double>()
        val ops = ArrayList<Char>()
        var i = 0
        var expectNumber = true
        var sign = 1.0
        while (i < s.length) {
            val c = s[i]
            if (expectNumber) {
                if (c == MINUS || c == '-') { sign = -sign; i++; continue }
                if (c == '+') { i++; continue }
                val start = i
                while (i < s.length && (s[i].isDigit() || s[i] == ',' || s[i] == '.' || s[i] == ':')) i++
                if (start == i) return null
                var v = parseNumber(s.substring(start, i)) ?: return null
                if (i < s.length && s[i] == '%') { v = v / 100.0 * percentOf; i++ }
                numbers.add(sign * v)
                sign = 1.0
                expectNumber = false
            } else {
                if (!isOperator(c)) return null
                ops.add(if (c == '-') MINUS else c)
                i++
                expectNumber = true
            }
        }
        if (expectNumber) return null
        // × ÷ primeiro, depois + −.
        val n2 = ArrayList<Double>().apply { add(numbers[0]) }
        val o2 = ArrayList<Char>()
        for (k in ops.indices) {
            val b = numbers[k + 1]
            when (ops[k]) {
                TIMES -> n2[n2.size - 1] = n2.last() * b
                DIVIDE -> { if (b == 0.0) return null; n2[n2.size - 1] = n2.last() / b }
                else -> { o2.add(ops[k]); n2.add(b) }
            }
        }
        var r = n2[0]
        for (k in o2.indices) r = if (o2[k] == '+') r + n2[k + 1] else r - n2[k + 1]
        return if (r.isFinite()) r else null
    }

    private fun parseNumber(raw: String): Double? {
        if (raw.contains(':')) {
            var total = 0.0
            for (part in raw.split(':')) {
                val v = parsePlain(part) ?: return null
                total = total * 60.0 + v
            }
            return total
        }
        return parsePlain(raw)
    }

    private fun parsePlain(raw: String): Double? {
        if (raw.isEmpty()) return null
        val t = if (raw.contains(',')) raw.replace(".", "").replace(',', '.') else raw
        if (t == ".") return null
        return t.toDoubleOrNull()
    }

    fun hasOperation(text: String): Boolean =
        text.drop(1).any { isOperator(it) } || text.contains('%') || text.contains(':')
}

/**
 * O TECLADO NUMÉRICO (`aurea_teclado_numerico.dart`): visor com a conta, dica
 * ("Conta incompleta" · "Fica em …" · "= …"), 5 × 4 teclas e Cancelar / OK.
 * O visor nasce com o valor atual TODO selecionado (a primeira tecla troca o
 * número em vez de emendar) e com VÍRGULA — o teclado só tem vírgula (B-44).
 * O resultado é preso em [KeypadRequest.min]..[KeypadRequest.max].
 */
@Composable
fun NumericKeypadSheet(request: KeypadRequest, onDismiss: () -> Unit) {
    // Omega efeitos (P2-2): vários campos (X/Y/Z, α/β/γ) — uma aba por campo.
    val multi = request.fields.size >= 2 && request.onConfirmFields != null
    var field by remember { mutableStateOf(if (multi) request.focus.coerceIn(0, request.fields.lastIndex) else 0) }
    val texts = remember { request.fields.map { visor(it.initial, request.decimals) }.toMutableList() }
    val edited = remember { MutableList(request.fields.size) { false } }
    var text by remember { mutableStateOf(if (multi) texts[field] else visor(request.initial, request.decimals)) }
    var selectedAll by remember { mutableStateOf(true) }
    val fieldMin = if (multi) request.fields[field].min else request.min
    val fieldMax = if (multi) request.fields[field].max else request.max
    val percentOf = when {
        request.unit == "%" -> 100.0
        request.percentBase.isFinite() -> request.percentBase.toDouble()
        else -> 100.0
    }
    val result = ValueExpression.evaluate(text, percentOf)
    val clamped = result?.toFloat()?.coerceIn(
        if (fieldMin.isNaN()) Float.NEGATIVE_INFINITY else fieldMin,
        if (fieldMax.isNaN()) Float.POSITIVE_INFINITY else fieldMax,
    )
    /** Valor de cada campo como o OK gravaria (nulo = não mexido); `false` = algum campo mexido não fecha a conta. */
    fun fieldValues(): Pair<List<Float?>, Boolean> {
        if (!multi) return emptyList<Float?>() to true
        var ok = true
        val out = request.fields.mapIndexed { i, f ->
            val t = if (i == field) text else texts[i]
            val touched = edited[i] || (i == field && !selectedAll)
            if (!touched) {
                null
            } else {
                val v = ValueExpression.evaluate(t, percentOf)?.toFloat()?.coerceIn(
                    if (f.min.isNaN()) Float.NEGATIVE_INFINITY else f.min,
                    if (f.max.isNaN()) Float.POSITIVE_INFINITY else f.max,
                )
                if (v == null) ok = false
                v
            }
        }
        return out to ok
    }
    fun switchTo(i: Int) {
        if (!multi || i == field) return
        texts[field] = text
        if (!selectedAll) edited[field] = true
        field = i
        text = texts[i]
        selectedAll = true
    }
    fun fmt(v: Float) = comUnidade(numeroPtBr(v, request.decimals), request.unit)
    val hint = when {
        text.isNotEmpty() && result == null -> stringResource(R.string.ds_conta_incompleta)
        result != null && clamped != null && clamped.toDouble() != result -> stringResource(R.string.app_keypad_clamped, fmt(clamped))
        result != null && !selectedAll && ValueExpression.hasOperation(text) -> "= ${fmt(result.toFloat())}"
        else -> ""
    }

    fun press(key: String) {
        val last = text.lastOrNull()
        when (key) {
            "⌫" -> { text = if (selectedAll) "" else text.dropLast(1); selectedAll = false }
            "±" -> { text = toggleSign(text); selectedAll = false }
            "=" -> { result?.let { text = visor(it.toFloat(), request.decimals) }; selectedAll = false }
            "%" -> { if (last != null && last.isDigit()) text += "%"; selectedAll = false }
            "÷", "×", "−", "+" -> {
                val op = key[0]
                text = when {
                    text.isEmpty() -> if (op == ValueExpression.MINUS) "−" else text
                    last != null && ValueExpression.isOperator(last) && !(op == ValueExpression.MINUS && (last == ValueExpression.TIMES || last == ValueExpression.DIVIDE)) ->
                        text.dropLast(1) + op
                    else -> text + op
                }
                selectedAll = false
            }
            "," -> {
                val base = if (selectedAll) "" else text
                val tail = base.takeLastWhile { it.isDigit() || it == ',' || it == ':' }
                if (!tail.substringAfterLast(':').contains(',')) text = base + (if (tail.isEmpty() || tail.last() == ':') "0," else ",")
                selectedAll = false
            }
            ":" -> {
                if (!selectedAll && last != null && last.isDigit()) text += ":"
                selectedAll = false
            }
            else -> { text = (if (selectedAll) "" else text) + key; selectedAll = false }
        }
    }

    AureaAdjustSheet(onDismiss = onDismiss, topRadius = 13.5.dp) { sheet ->
        BoxWithConstraints(Modifier.fillMaxWidth()) {
        Column(Modifier.heightIn(max = maxHeight).padding(start = 16.dp, top = 14.dp, end = 16.dp, bottom = 12.dp)) {
            Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState())) {
            Text(
                request.title.ifEmpty { stringResource(R.string.i18n_keypad_exact, request.unit) },
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700)),
            )
            Spacer(Modifier.height(10.dp))
            if (multi) {
                // Omega efeitos (P2-2): uma aba por campo (X/Y/Z, α/β/γ) com o valor dele.
                KeepLtr { Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    request.fields.forEachIndexed { i, f ->
                        val on = i == field
                        val shown = if (on) text else texts[i]
                        Column(
                            Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                                .testTag("keypad.field.$i")
                                .semantics { selected = on; contentDescription = "${f.label} $shown" }
                                .tocavel(shrink = 1f) { switchTo(i) }
                                .padding(horizontal = 8.dp, vertical = 6.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            Text(f.label, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = if (on) AureaColors.Accent else AureaColors.Muted)))
                            Text(
                                shown,
                                maxLines = 1,
                                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, fontFeatureSettings = "tnum")),
                            )
                        }
                    }
                } }
            }
            // Visor: fundo `palco`, 26 sp w700 tabular à direita + unidade.
            // Visor e teclas em LTR também em árabe (como a calculadora e o
            // discador do sistema): o número cresce para a direita e ⌫ apaga ali.
            KeepLtr { Row(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(AureaColors.Stage)
                    .padding(horizontal = 14.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.End,
            ) {
                Text(
                    text,
                    maxLines = 1,
                    modifier = Modifier
                        .weight(1f, fill = false)
                        .clip(RoundedCornerShape(4.dp))
                        .background(if (selectedAll && text.isNotEmpty()) AureaColors.AccentDim else Color.Transparent),
                    style = AureaType.Base.merge(
                        TextStyle(fontSize = 26.sp, fontWeight = FontWeight.W700, fontFeatureSettings = "tnum", textAlign = TextAlign.End),
                    ),
                )
                if (request.unit.isNotEmpty()) {
                    Spacer(Modifier.width(6.dp))
                    Text(request.unit, style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, color = AureaColors.Muted)))
                }
            } }
            Box(Modifier.fillMaxWidth().height(22.dp), contentAlignment = Alignment.CenterEnd) {
                Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
            val rows = listOf(
                listOf("7", "8", "9", "⌫"),
                listOf("4", "5", "6", "÷"),
                listOf("1", "2", "3", "×"),
                listOf(",", "0", "±", "−"),
                listOf(":", "%", "=", "+"),
            )
            KeepLtr { rows.forEach { row ->
                Row(Modifier.fillMaxWidth().padding(bottom = 6.dp)) {
                    row.forEach { k ->
                        val op = k == "÷" || k == "×" || k == "−" || k == "+" || k == "="
                        Box(
                            Modifier
                                .weight(1f)
                                .padding(horizontal = 3.dp)
                                .height(48.dp)
                                .clip(RoundedCornerShape(10.dp))
                                .background(if (op) AureaColors.AccentDim else AureaColors.Chip)
                                .tocavel(
                                    shrink = 1f,
                                    haptic = true,
                                    onLongClick = if (k == "⌫") ({ text = ""; selectedAll = false }) else null,
                                ) { press(k) },
                            contentAlignment = Alignment.Center,
                        ) {
                            if (k == "⌫") {
                                CupertinoIcon(CupertinoGlyph.DeleteLeft, 22.dp, AureaColors.Text)
                            } else {
                                Text(
                                    k,
                                    style = AureaType.Base.merge(
                                        TextStyle(fontSize = 21.sp, fontWeight = FontWeight.W600, color = if (op) AureaColors.Accent else AureaColors.Text),
                                    ),
                                )
                            }
                        }
                    }
                }
            } }
            }
            Spacer(Modifier.height(4.dp))
            Row(Modifier.fillMaxWidth()) {
                Box(
                    Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(8.dp))
                        .background(AureaColors.Chip)
                        .tocavel(shrink = 1f) { sheet.dismiss() }
                        .padding(vertical = 12.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(stringResource(R.string.ds_cancelar), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp)))
                }
                Spacer(Modifier.width(10.dp))
                val (values, allOk) = fieldValues()
                val ok = if (multi) allOk && (clamped != null || selectedAll) else clamped != null
                Box(
                    Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (ok) AureaColors.Accent else AureaColors.ChipHigh)
                        .tocavel(enabled = ok, shrink = 1f) {
                            if (multi) request.onConfirmFields?.invoke(values) else clamped?.let(request.onConfirm)
                            sheet.dismiss()
                        }
                        .padding(vertical = 12.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "OK",
                        style = AureaType.Base.merge(
                            TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700, color = if (ok) AureaColors.OnAction else AureaColors.Disabled),
                        ),
                    )
                }
            }
        }
        }
    }
}

/** O número no visor: pt-BR com o sinal de menos DA TECLA (−), para ± e a conta concordarem. */
private fun visor(v: Float, casas: Int) = numeroPtBr(v, casas).replace('-', ValueExpression.MINUS)

/** ± troca o sinal do ÚLTIMO número da conta (o que o dedo está digitando). */
private fun toggleSign(text: String): String {
    if (text.isEmpty()) return "−"
    var i = text.length
    while (i > 0 && (text[i - 1].isDigit() || text[i - 1] == ',' || text[i - 1] == ':' || text[i - 1] == '%')) i--
    val before = text.substring(0, i)
    val num = text.substring(i)
    val unary = before.isNotEmpty() && before.last() == ValueExpression.MINUS &&
        (before.length == 1 || ValueExpression.isOperator(before[before.length - 2]))
    return if (unary) before.dropLast(1) + num else before + ValueExpression.MINUS + num
}
