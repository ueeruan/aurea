package com.aurea.aurea.ui.ds

import com.aurea.aurea.engine.ExpressionLook
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitHorizontalTouchSlopOrCancellation
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.graphics.lerp
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.min

// =============================================================================
// Número pt-BR
// =============================================================================

/**
 * O número em pt-BR, com casas FIXAS (decisão D-1 = [A]): vírgula decimal e
 * sempre as mesmas casas ("0,82", "100,0%"). Casas variáveis fazem a caixa
 * "dançar" de largura durante o arrasto (bug B-03); o "-0,0" que o
 * arredondamento de um negativo minúsculo produz vira "0,0" para o campo não
 * piscar o sinal.
 */
fun numeroPtBr(v: Float, casas: Int = 1): String {
    val n = if (v.isFinite()) v else 0f
    val c = casas.coerceIn(0, 6)
    var s = String.format(Locale.ROOT, "%.${c}f", n)
    if (s.startsWith("-") && s.drop(1).all { it == '0' || it == '.' }) s = s.drop(1)
    // Vírgula só nos idiomas que usam vírgula decimal (pt, es, ru, id); em inglês "1.5".
    return if (java.text.DecimalFormatSymbols.getInstance(Locale.getDefault()).decimalSeparator == ',') s.replace('.', ',') else s
}

/**
 * Número + unidade como a A.01 escrevia: símbolos colados ("100,0%", "45°",
 * "30,0px"); palavra com espaço ("0,00 stops"), senão a unidade gruda no
 * número e vira ruído.
 */
fun comUnidade(texto: String, unit: String): String = when {
    unit.isEmpty() -> texto
    unit.length <= 2 || !unit.all { it.isLetter() } -> texto + unit
    else -> "$texto $unit"
}

// =============================================================================
// Estado do keyframe (três estados, calculados dos dados do store)
// =============================================================================

/** O que o losango mostra: parado, animado sem marca aqui, marca no cabeçote. */
enum class KeyframeLook { None, Animated, KeyHere }

// =============================================================================
// Régua de riscos (fita)
// =============================================================================

/** Passo entre riscos (redesenho 2026-09-29: 14 riscos na régua da linha de 40 dp). */
val TickStep: Dp = 11.dp

/**
 * Cores e medidas da LINHA DE PARÂMETRO do redesenho aprovado em 2026-09-29
 * (`docs/design/redesenho-2026-09-29/Efeitos.dc.html`): painel, cartão do
 * efeito, rótulo, caixa de valor e riscos. O destaque vem do tema (#6FAED9).
 */
object ParamRowColors {
    val Panel = Color(0xFF121826)
    val Card = Color(0xFF252F43)
    val LabelOn = Color(0xFF121826)
    val LabelOff = Color(0xFF2B364B)
    val LabelOffText = Color(0xFFAAB6C3)
    val ValueBox = Color(0xFF1B2333)
    val RailLine = Color(0xFF222B3B)
    val RailDisabled = Color(0xFF3E4859)
    val TickEdge = Color(0xFF3C475C)
    val TickCenter = Color(0xFF97A3BA)
    val RgbText = Color(0xFF6F8A9E)
    val SwatchBorder = Color(0xFF3C475C)
}

object ParamRowDims {
    val Row = 40.dp
    val Gap = 6.dp
    val LabelW = 70.dp
    val LabelH = 36.dp
    val ValueW = 62.dp
    val Radius = 6.dp
}

/**
 * A RÉGUA DE RISCOS, pintada num Canvas só (trinta riscos como widgets seriam
 * trinta nós refeitos a cada quadro do arrasto).
 *
 * Os riscos SEGUEM O DEDO: a posição de cada um é `centro + valor/porDp`, então
 * arrastar para a direita aumenta o valor e os riscos andam para a direita —
 * a conta, os riscos e o número concordam. [value] é lido DENTRO do desenho:
 * mudar o valor só repinta, não recompõe. Os riscos acendem perto do centro
 * ([ParamRowMath.tickBrightness]) e a linha do meio é a do destaque.
 *
 * @param active centro aceso (a régua em edição); a outra do par fica branca.
 */
@Composable
fun TickRuler(
    value: () -> Float,
    unitsPerDp: Float,
    active: Boolean,
    modifier: Modifier = Modifier,
    verticalPadding: Dp = 8.dp,
) {
    Canvas(modifier) { drawTicks(value(), unitsPerDp, active, verticalPadding.toPx()) }
}

private fun DrawScope.drawTicks(value: Float, unitsPerDp: Float, active: Boolean, pad: Float) {
    val top = pad
    val bottom = size.height - pad
    if (bottom <= top || size.width <= 0f) return
    val step = TickStep.toPx()
    // Os riscos têm ~70 % da altura da linha do meio (22 de 32 na referência).
    val inset = (bottom - top) * 0.155f
    val w = 1.dp.toPx()
    val center = size.width / 2f
    // Onde o valor ZERO cai no papel, em px a partir da borda esquerda.
    val perPx = if (unitsPerDp > 0f) unitsPerDp / density else 1f
    val base = (if (value.isFinite()) value / perPx else 0f) + center
    val whole = floor(base / step)
    var x = base - whole * step - step
    while (x <= size.width) {
        val b = ParamRowMath.tickBrightness(x, size.width)
        if (b > 0f) {
            drawLine(
                color = lerp(ParamRowColors.TickEdge, ParamRowColors.TickCenter, b * b).copy(alpha = min(1f, b * 4f)),
                start = Offset(x, top + inset),
                end = Offset(x, bottom - inset),
                strokeWidth = w,
            )
        }
        x += step
    }
    drawLine(
        color = if (active) AureaColors.Accent else AureaColors.Playhead,
        start = Offset(center, top),
        end = Offset(center, bottom),
        strokeWidth = 2.dp.toPx(),
    )
}

/**
 * O GESTO de um número: arrasto horizontal que ACUMULA desde o início
 * (`novo = início + andado × porDp`, preso na faixa). Somar delta a delta
 * sobre o valor que volta do motor acumularia o arredondamento dele e o
 * desenho descolaria do dedo. Direita aumenta.
 *
 * O início e o fim do gesto são avisados para quem abre/fecha o passo de
 * desfazer (um arrasto = um desfazer). Cancelamento também fecha.
 *
 * Valor já FORA da faixa da régua (digitado além do slider): a faixa do gesto
 * se estende até ele ([dragBounds]) — o primeiro toque não o puxa de volta, e o
 * arrasto continua do valor mostrado, sem salto.
 *
 * CONTROLE FINO ([ParamRowMath.scrubGain]): com dois dedos na régua o passo cai
 * a 1/10; arrastando devagar, a 1/4 (e volta a 1:1 com a velocidade).
 */
fun Modifier.valueDrag(
    enabled: Boolean,
    start: () -> Float,
    unitsPerDp: () -> Float,
    min: Float,
    max: Float,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
): Modifier = if (!enabled) this else composed {
    val readStart by rememberUpdatedState(start)
    val readUnits by rememberUpdatedState(unitsPerDp)
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    this.pointerInput(min, max) {
        val lo = if (min.isNaN()) Float.NEGATIVE_INFINITY else min
        val hi = if (max.isNaN()) Float.POSITIVE_INFINITY else max
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = false)
            var over = 0f
            // Só o arrasto HORIZONTAL é da régua; o vertical fica para a rolagem.
            val first = awaitHorizontalTouchSlopOrCancellation(down.id) { change, amount ->
                change.consume()
                over = amount
            } ?: return@awaitEachGesture
            val s = readStart()
            val from = if (s.isFinite()) s else 0f
            var walked = 0f
            var pointer = first.id
            var last = first.uptimeMillis
            // Controle fino (ParamRowMath): dois dedos = 1/10; devagar = 1/4.
            var speed = ParamRowMath.smoothSpeed(
                ParamRowMath.FAST_SPEED,
                (first.position.x - down.position.x) / density,
                (first.uptimeMillis - down.uptimeMillis).toFloat(),
            )
            fun step(dxPx: Float, dtMs: Float, pointers: Int) {
                val dxDp = dxPx / density
                speed = ParamRowMath.smoothSpeed(speed, dxDp, dtMs)
                walked += dxDp * ParamRowMath.scrubGain(pointers, speed)
                val v = ParamRowMath.scrubValue(from, walked, readUnits(), lo, hi)
                if (v.isFinite()) send(v)
            }
            begin()
            try {
                step(over, 16f, 1)
                while (true) {
                    val event = awaitPointerEvent()
                    val pressed = event.changes.filter { it.pressed }
                    if (pressed.isEmpty()) break
                    val main = pressed.firstOrNull { it.id == pointer }
                    if (main == null) {
                        // O dedo que arrastava saiu: segue com outro, sem salto.
                        pointer = pressed.first().id
                        last = pressed.first().uptimeMillis
                        continue
                    }
                    val dx = main.position.x - main.previousPosition.x
                    val dt = (main.uptimeMillis - last).toFloat()
                    last = main.uptimeMillis
                    event.changes.forEach { if (it.positionChange() != Offset.Zero) it.consume() }
                    if (dx != 0f) step(dx, dt, pressed.size)
                }
            } finally {
                finish()
            }
        }
    }
}

/**
 * Faixa de UM arrasto da régua: [min]..[max], estendida até o valor de partida
 * [from] quando ele está fora (valor digitado além do slider). Dentro da faixa,
 * é a própria faixa. Nunca troca os lados (min > max vira a faixa ordenada).
 */
internal fun dragBounds(min: Float, max: Float, from: Float): Pair<Float, Float> {
    val lo = kotlin.math.min(min, max)
    val hi = kotlin.math.max(min, max)
    if (!from.isFinite()) return lo to hi
    return kotlin.math.min(lo, from) to kotlin.math.max(hi, from)
}

// =============================================================================
// Caixa de valor ("pílula")
// =============================================================================

/**
 * A CAIXA DE VALOR [A] (`CampoDeValor`): altura 24, raio 8, fundo `campo`,
 * número 13 sp w600 em `destaque` SUBLINHADO (o sublinhado é a promessa de que
 * dá para digitar; sem [onTap] não há sublinhado). O número encolhe antes de
 * vazar (a escala não tem teto). Rótulo opcional embaixo, 9 sp muted.
 */
@Composable
fun ValueBox(
    text: String,
    modifier: Modifier = Modifier,
    width: Dp = 68.dp,
    color: Color = AureaColors.Accent,
    label: String? = null,
    enabled: Boolean = true,
    onLongPress: (() -> Unit)? = null,
    onTap: (() -> Unit)?,
) {
    val tappable = onTap != null && enabled
    Column(
        modifier
            .width(width)
            .heightIn(min = 48.dp)
            .then(
                if (onTap != null || onLongPress != null) {
                    Modifier.tocavel(enabled = enabled, shrink = 1f, onLongClick = onLongPress, onClick = { onTap?.invoke() })
                } else {
                    Modifier
                },
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(24.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.Chip)
                .padding(horizontal = 4.dp),
            contentAlignment = Alignment.Center,
        ) {
            val c = if (enabled) color else AureaColors.Muted
            BasicText(
                text = text,
                maxLines = 1,
                autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 13.sp, stepSize = 0.5.sp),
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = 13.sp,
                        fontWeight = FontWeight.W600,
                        color = c,
                        textAlign = TextAlign.Center,
                        textDecoration = if (tappable) TextDecoration.Underline else TextDecoration.None,
                        fontFeatureSettings = "tnum",
                    ),
                ),
            )
        }
        if (!label.isNullOrEmpty()) {
            Spacer(Modifier.height(3.dp))
            Text(
                label,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 9.sp, color = AureaColors.Muted, textAlign = TextAlign.Center)),
            )
        }
    }
}

// =============================================================================
// Chip do rótulo e linha de propriedade [A]
// =============================================================================

/**
 * O RÓTULO DA LINHA (redesenho 2026-09-29): botão 70 × 36, raio 6, SEMPRE
 * sublinhado (a promessa de que toca). Escolhido = fundo escuro #121826 e texto
 * no destaque em negrito; os outros #2B364B com texto apagado. Rótulo longo
 * encolhe para 11 sp e quebra em duas linhas ([ParamRowMath.labelFontSp]).
 *
 * [keyframe] desenha um losango pequeno no canto quando a trilha anima (cheio =
 * marca no cabeçote) — é o que diz, linha a linha, o que o losango do trilho
 * esquerdo vai fazer, sem ocupar largura (a linha não pula ao nascer a 1ª marca,
 * bug B-02).
 *
 * [expression] desenha um "=" no canto direito quando a trilha tem expressão
 * (destaque = ligada, apagado = desligada, vermelho = com erro); segurar o
 * rótulo ([onLongClick]) abre o menu do parâmetro.
 */
@Composable
fun PropertyLabelChip(
    label: String,
    selected: Boolean,
    modifier: Modifier = Modifier,
    keyframe: KeyframeLook = KeyframeLook.None,
    expression: ExpressionLook = ExpressionLook.None,
    onLongClick: (() -> Unit)? = null,
    onClick: (() -> Unit)? = null,
) {
    Box(
        modifier
            .size(width = ParamRowDims.LabelW, height = ParamRowDims.LabelH)
            .clip(RoundedCornerShape(ParamRowDims.Radius))
            .background(if (selected) ParamRowColors.LabelOn else ParamRowColors.LabelOff)
            .then(
                if (onClick != null || onLongClick != null) {
                    Modifier.tocavel(shrink = 1f, onLongClick = onLongClick, onClick = { onClick?.invoke() })
                } else {
                    Modifier
                },
            )
            .padding(horizontal = 4.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            maxLines = ParamRowMath.labelMaxLines(label),
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = ParamRowMath.labelFontSp(label).sp,
                    lineHeight = 1.1.em,
                    fontWeight = if (selected) FontWeight.W700 else FontWeight.W400,
                    textAlign = TextAlign.Center,
                    color = if (selected) AureaColors.Accent else ParamRowColors.LabelOffText,
                    textDecoration = TextDecoration.Underline,
                ),
            ),
        )
        if (keyframe != KeyframeLook.None) {
            Canvas(
                Modifier
                    .align(Alignment.TopStart)
                    .offset(x = (-1).dp, y = 3.dp)
                    .size(7.dp),
            ) {
                val p = Path().apply {
                    moveTo(size.width / 2f, 0f)
                    lineTo(size.width, size.height / 2f)
                    lineTo(size.width / 2f, size.height)
                    lineTo(0f, size.height / 2f)
                    close()
                }
                if (keyframe == KeyframeLook.KeyHere) {
                    drawPath(p, AureaColors.Keyframe)
                } else {
                    drawPath(p, AureaColors.Keyframe, style = Stroke(1.dp.toPx()))
                }
            }
        }
        if (expression != ExpressionLook.None) ExpressionBadge(expression, Modifier.align(Alignment.TopEnd).offset(x = 2.dp))
    }
}

/**
 * A CAIXA DE VALOR da linha (redesenho 2026-09-29): 62 × 36, raio 6, #1B2333,
 * número 13 sp alinhado à DIREITA com algarismos tabulares (não dança durante o
 * arrasto); encolhe antes de vazar. Tocar abre o teclado.
 */
@Composable
fun ParamValueBox(
    text: String,
    modifier: Modifier = Modifier,
    width: Dp = ParamRowDims.ValueW,
    enabled: Boolean = true,
    onTap: (() -> Unit)?,
) {
    Box(
        modifier
            .size(width = width, height = ParamRowDims.LabelH)
            .clip(RoundedCornerShape(ParamRowDims.Radius))
            .background(ParamRowColors.ValueBox)
            .then(if (onTap != null) Modifier.tocavel(enabled = enabled, shrink = 1f, onClick = onTap) else Modifier)
            .padding(start = 4.dp, end = 8.dp),
        contentAlignment = Alignment.CenterEnd,
    ) {
        BasicText(
            text = text,
            maxLines = 1,
            autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 13.sp, stepSize = 0.5.sp),
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 13.sp,
                    color = if (enabled) AureaColors.Text else AureaColors.Muted,
                    textAlign = TextAlign.End,
                    fontFeatureSettings = "tnum",
                ),
            ),
        )
    }
}

/** Cor do "=" de expressão. */
fun expressionColor(look: ExpressionLook): Color = when (look) {
    ExpressionLook.Error -> Color(0xFFFF6B5E)
    ExpressionLook.On -> AureaColors.Accent
    else -> AureaColors.Muted
}

/** O "=" pequeno das linhas com expressão. */
@Composable
fun ExpressionBadge(look: ExpressionLook, modifier: Modifier = Modifier) {
    Text(
        "=",
        modifier = modifier,
        style = AureaType.Base.merge(
            TextStyle(fontSize = 11.sp, lineHeight = 1.em, fontWeight = FontWeight.W800, color = expressionColor(look)),
        ),
    )
}

/**
 * A LINHA DE PARÂMETRO (redesenho 2026-09-29, Efeitos.dc.html): 40 dp =
 * `[rótulo 70×36] 6 [régua] 6 [valor 62×36]`.
 *
 * O valor mostrado é o do MOTOR ([value]); enquanto o dedo arrasta, a linha
 * mostra o valor em voo (o motor recebe cada passo ao vivo) para a régua não
 * esperar a volta. Começar a arrastar também escolhe a linha. A régua aceita
 * controle fino (dois dedos ou arrasto lento, ver [valueDrag]).
 */
@Composable
fun PropertyRow(
    label: String,
    value: Float,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    format: (Float) -> String,
    selected: Boolean,
    onSelect: () -> Unit,
    onGestureStart: () -> Unit,
    onValue: (Float) -> Unit,
    onGestureEnd: () -> Unit,
    onTapValue: () -> Unit,
    modifier: Modifier = Modifier,
    keyframe: KeyframeLook = KeyframeLook.None,
    enabled: Boolean = true,
    expression: ExpressionLook = ExpressionLook.None,
    onExpression: (() -> Unit)? = null,
) {
    var dragging by remember { mutableStateOf(false) }
    var live by remember { mutableFloatStateOf(value) }
    // O gesto vive mais que uma composição: lê SEMPRE os callbacks e o valor
    // atuais (um callback velho escreveria com o parâmetro de antes da 1ª marca).
    val current by rememberUpdatedState(value)
    val step by rememberUpdatedState(unitsPerDp)
    val select by rememberUpdatedState(onSelect)
    val begin by rememberUpdatedState(onGestureStart)
    val send by rememberUpdatedState(onValue)
    val end by rememberUpdatedState(onGestureEnd)
    val shown = if (dragging) live else value
    Row(
        modifier
            .fillMaxWidth()
            .height(ParamRowDims.Row)
            .alpha(if (enabled) 1f else 0.45f),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PropertyLabelChip(label, selected, keyframe = keyframe, expression = expression, onLongClick = onExpression, onClick = onSelect)
        Spacer(Modifier.width(ParamRowDims.Gap))
        TickRuler(
            value = { if (dragging) live else current },
            unitsPerDp = unitsPerDp,
            active = true,
            verticalPadding = 2.dp,
            modifier = Modifier
                .weight(1f)
                .height(ParamRowDims.LabelH)
                .valueDrag(
                    enabled = enabled,
                    start = { current },
                    unitsPerDp = { step },
                    min = min,
                    max = max,
                    onStart = {
                        live = current
                        dragging = true
                        select()
                        begin()
                    },
                    onValue = { v ->
                        live = v
                        send(v)
                    },
                    onEnd = {
                        dragging = false
                        end()
                    },
                ),
        )
        Spacer(Modifier.width(ParamRowDims.Gap))
        ParamValueBox(format(shown), enabled = enabled, onTap = onTapValue)
    }
}

/**
 * Linha "rótulo + controle livre" (interruptor, escolha, cor): o mesmo rótulo
 * de 70 à esquerda para a coluna dos nomes não pular entre tipos.
 */
@Composable
fun PropertyCustomRow(
    label: String,
    selected: Boolean,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier,
    keyframe: KeyframeLook = KeyframeLook.None,
    minHeight: Dp = ParamRowDims.Row,
    expression: ExpressionLook = ExpressionLook.None,
    onExpression: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    Row(
        modifier
            .fillMaxWidth()
            .heightIn(min = minHeight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PropertyLabelChip(label, selected, keyframe = keyframe, expression = expression, onLongClick = onExpression, onClick = onSelect)
        Spacer(Modifier.width(ParamRowDims.Gap))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) { content() }
    }
}

// =============================================================================
// Interruptor e escolha
// =============================================================================

/**
 * O INTERRUPTOR (CupertinoSwitch da A.01): 51 × 31, ligado em `acao` #245D8C,
 * desligado em `campoAlto`; bola branca de 27 que desliza em 200 ms.
 */
@Composable
fun AureaToggle(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val x by animateDpAsState(if (checked) 22.dp else 2.dp, tween(200), label = "toggle")
    Box(
        modifier
            .size(width = 51.dp, height = 31.dp)
            .alpha(if (enabled) 1f else 0.45f)
            .clip(RoundedCornerShape(15.5.dp))
            .background(if (checked) AureaColors.Action else AureaColors.ChipHigh)
            .tocavel(enabled = enabled, shrink = 1f) { onCheckedChange(!checked) },
    ) {
        Box(
            Modifier
                .offset(x = x, y = 2.dp)
                .size(27.dp)
                .shadow(2.dp, CircleShape)
                .background(Color.White, CircleShape),
        )
    }
}

/**
 * A ESCOLHA COM TUDO À VISTA (`_LinhaDeEscolha` da A.01): chips em fileira que
 * quebra linha, vão 6; aceso = `destaqueApagado` + texto `destaque`.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ChoiceChips(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    FlowRow(
        modifier.padding(vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            Box(
                Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(shrink = 1f) { onSelect(i) }
                    .padding(horizontal = 11.dp, vertical = 6.dp),
            ) {
                Text(o, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}

// =============================================================================
// Ícones pintados do trilho (losango de keyframe e curva)
// =============================================================================

/**
 * O LOSANGO DO TRILHO [A] (`_DiamondKeyframePainter`): losango vazado com "+"
 * (sem marca aqui) ou "−" (há marca aqui). Três estados de cor, lidos do store:
 * parado = branco, animado sem marca aqui = `keyframe`, marca aqui = `destaque`.
 * Sem alvo = `#434956`.
 */
@Composable
fun KeyframeDiamondIcon(look: KeyframeLook, enabled: Boolean, modifier: Modifier = Modifier) {
    Canvas(modifier.size(22.dp)) {
        val color = when {
            !enabled -> AureaColors.RailDisabled
            look == KeyframeLook.KeyHere -> AureaColors.Accent
            look == KeyframeLook.Animated -> AureaColors.Keyframe
            else -> Color.White
        }
        val inset = 2.dp.toPx()
        val c = Offset(size.width / 2f, size.height / 2f)
        val p = Path().apply {
            moveTo(c.x, inset)
            lineTo(size.width - inset, c.y)
            lineTo(c.x, size.height - inset)
            lineTo(inset, c.y)
            close()
        }
        drawPath(p, color, style = Stroke(1.6.dp.toPx()))
        val arm = 3.5.dp.toPx()
        val w = 1.5.dp.toPx()
        drawLine(color, Offset(c.x - arm, c.y), Offset(c.x + arm, c.y), w)
        if (look != KeyframeLook.KeyHere) drawLine(color, Offset(c.x, c.y - arm), Offset(c.x, c.y + arm), w)
    }
}

/**
 * O ÍCONE DE CURVA DO TRILHO [A] (`_CurveIconPainter`): caixa arredondada a 50 %
 * + curva em S. Inativo `#434956`; animado `destaque`; senão muted.
 */
@Composable
fun CurveRailIcon(enabled: Boolean, animated: Boolean, modifier: Modifier = Modifier) {
    Canvas(modifier.size(20.dp)) {
        val color = when {
            !enabled -> AureaColors.RailDisabled
            animated -> AureaColors.Accent
            else -> AureaColors.Muted
        }
        val one = 1.dp.toPx()
        drawRoundRect(
            color = color.copy(alpha = 0.5f),
            topLeft = Offset(one, one),
            size = Size(size.width - 2 * one, size.height - 2 * one),
            cornerRadius = CornerRadius(4.dp.toPx()),
            style = Stroke(1.3.dp.toPx()),
        )
        val p = Path().apply {
            moveTo(4.dp.toPx(), size.height - 5.dp.toPx())
            cubicTo(
                size.width * 0.45f, size.height - 5.dp.toPx(),
                size.width * 0.55f, 5.dp.toPx(),
                size.width - 4.dp.toPx(), 5.dp.toPx(),
            )
        }
        drawPath(p, color, style = Stroke(1.5.dp.toPx(), cap = StrokeCap.Round))
    }
}
