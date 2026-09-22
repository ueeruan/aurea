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
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
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
    return s.replace('.', ',')
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

/** Passo entre riscos, medido na referência (9 dp) — igual em toda régua. */
val TickStep: Dp = 9.dp

/** Um risco forte a cada 5 (índice ABSOLUTO no papel: anda junto com os fracos). */
private const val TICKS_PER_STRONG = 5

/** Onde os riscos começam a sumir, contado de cada borda (a fita não termina: some). */
private val TickFade: Dp = 24.dp

/**
 * A RÉGUA DE RISCOS, pintada num Canvas só (trinta riscos como widgets seriam
 * trinta nós refeitos a cada quadro do arrasto).
 *
 * Os riscos SEGUEM O DEDO: a posição de cada um é `centro + valor/porDp`, então
 * arrastar para a direita aumenta o valor e os riscos andam para a direita —
 * a conta, os riscos e o número concordam. [value] é lido DENTRO do desenho:
 * mudar o valor só repinta, não recompõe.
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
    val fade = TickFade.toPx()
    val center = size.width / 2f
    val weak = AureaColors.Muted.copy(alpha = 0.25f)
    val strong = AureaColors.Muted.copy(alpha = 0.60f)
    val weakW = 1.dp.toPx()
    val strongW = 1.5.dp.toPx()
    // Onde o valor ZERO cai no papel, em px a partir da borda esquerda.
    val perPx = if (unitsPerDp > 0f) unitsPerDp / density else 1f
    val base = (if (value.isFinite()) value / perPx else 0f) + center
    val whole = floor(base / step)
    val phase = base - whole * step
    var x = phase - step
    var k = 0
    while (x <= size.width) {
        val fromEdge = min(x, size.width - x)
        if (fromEdge > 0f) {
            val f = if (fromEdge >= fade) 1f else fromEdge / fade
            val index = k - 1 - whole.toInt()
            val isStrong = Math.floorMod(index, TICKS_PER_STRONG) == 0
            val c = if (isStrong) strong else weak
            drawLine(
                color = c.copy(alpha = c.alpha * f),
                start = Offset(x, top),
                end = Offset(x, bottom),
                strokeWidth = if (isStrong) strongW else weakW,
            )
        }
        x += step
        k++
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
): Modifier = if (!enabled) this else pointerInput(min, max) {
    var from = 0f
    var walked = 0f
    var active = false
    val lo = if (min.isNaN()) Float.NEGATIVE_INFINITY else min
    val hi = if (max.isNaN()) Float.POSITIVE_INFINITY else max
    detectHorizontalDragGestures(
        onDragStart = {
            val s = start()
            from = if (s.isFinite()) s else 0f
            walked = 0f
            active = true
            onStart()
        },
        onDragEnd = {
            if (active) {
                active = false
                onEnd()
            }
        },
        onDragCancel = {
            if (active) {
                active = false
                onEnd()
            }
        },
        onHorizontalDrag = { change, dx ->
            change.consume()
            walked += dx / density
            val v = (from + walked * unitsPerDp()).coerceIn(lo, hi)
            if (v.isFinite()) onValue(v)
        },
    )
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
            .then(
                if (onTap != null || onLongPress != null) {
                    Modifier.tocavel(enabled = enabled, shrink = 1f, onLongClick = onLongPress, onClick = { onTap?.invoke() })
                } else {
                    Modifier
                },
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
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
 * O CHIP DO RÓTULO [A] (94 × 32): nome em ATÉ DUAS LINHAS, 12 sp w600, sem
 * encolher a fonte (o bug B-01 era o rótulo de uma linha que encolhia até ficar
 * ilegível). Escolhido = fundo `campo` e texto `destaque` sublinhado.
 *
 * [keyframe] desenha um losango pequeno no canto quando a trilha anima (cheio =
 * marca no cabeçote) — é o que diz, linha a linha, o que o losango do trilho
 * esquerdo vai fazer, sem ocupar largura (a linha não pula ao nascer a 1ª marca,
 * bug B-02).
 *
 * [expression] desenha um "=" no canto direito quando a trilha tem expressão
 * (destaque = ligada, apagado = desligada, vermelho = com erro); segurar o chip
 * ([onLongClick]) abre o editor de expressão da propriedade.
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
            .size(width = 94.dp, height = 32.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) AureaColors.Chip else Color.Transparent)
            .then(
                if (onClick != null || onLongClick != null) {
                    Modifier.tocavel(shrink = 1f, onLongClick = onLongClick, onClick = { onClick?.invoke() })
                } else {
                    Modifier
                },
            )
            .padding(horizontal = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 12.sp,
                    lineHeight = 1.05.em,
                    fontWeight = FontWeight.W600,
                    textAlign = TextAlign.Center,
                    color = if (selected) AureaColors.Accent else AureaColors.Muted,
                    textDecoration = if (selected) TextDecoration.Underline else TextDecoration.None,
                ),
            ),
        )
        if (keyframe != KeyframeLook.None) {
            Canvas(
                Modifier
                    .align(Alignment.TopStart)
                    .offset(x = (-3).dp, y = 3.dp)
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
        if (expression != ExpressionLook.None) ExpressionBadge(expression, Modifier.align(Alignment.TopEnd).offset(x = 4.dp))
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
 * A LINHA DE PROPRIEDADE [A] (`LinhaDeParametro`, t2): 48 dp =
 * `[chip 94] 8 [régua] 8 [caixa 68 × 24]`.
 *
 * O valor mostrado é o do MOTOR ([value]); enquanto o dedo arrasta, a linha
 * mostra o valor em voo (o motor recebe cada passo ao vivo) para a régua não
 * esperar a volta. Começar a arrastar também escolhe a linha.
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
            .height(48.dp)
            .alpha(if (enabled) 1f else 0.45f),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PropertyLabelChip(label, selected, keyframe = keyframe, expression = expression, onLongClick = onExpression, onClick = onSelect)
        Spacer(Modifier.width(8.dp))
        TickRuler(
            value = { if (dragging) live else current },
            unitsPerDp = unitsPerDp,
            active = selected,
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .padding(vertical = 4.dp)
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
        Spacer(Modifier.width(8.dp))
        ValueBox(format(shown), enabled = enabled, onTap = onTapValue)
    }
}

/**
 * Linha "rótulo + controle livre" (interruptor, escolha, cor): o mesmo chip de
 * 94 à esquerda para a coluna dos nomes não pular entre tipos.
 */
@Composable
fun PropertyCustomRow(
    label: String,
    selected: Boolean,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier,
    keyframe: KeyframeLook = KeyframeLook.None,
    minHeight: Dp = 48.dp,
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
        Spacer(Modifier.width(8.dp))
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
