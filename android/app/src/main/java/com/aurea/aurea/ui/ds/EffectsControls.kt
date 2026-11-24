package com.aurea.aurea.ui.ds

import android.view.HapticFeedbackConstants
import androidx.compose.animation.core.AnimationState
import androidx.compose.animation.core.animateDecay
import androidx.compose.animation.splineBasedDecay
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Job
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor

// =============================================================================
//  Componentes da frente EFEITOS.
//
//  Omega efeitos (0.3.2, área 07, P0-4 e P1-1): as peças das LINHAS DE PARÂMETRO
//  da referência — rótulo 75 × (alt − 6) e caixa de valor 56, o slider próprio
//  (trilho 2,5 com origem e pista de encaixe) e a régua de riscos (5,5 dp por
//  risco, 1 risco = 1 passo, arremesso, encaixe e toque de vibração), mais a
//  escolha segmentada. A conta mora em [FxParamMath] (ParamRowMath.kt). Os
//  componentes antigos (`PropertyRow`, `TickRuler`) ficam para os outros painéis.
//  Par do iOS: DesignSystem.swift (mesmas medidas, cores e ids).
// =============================================================================

/** Medidas das linhas da referência (`effect_setting_*`). */
object FxRow {
    val Label: Dp = FxParamMath.LABEL_W.dp
    val Value: Dp = FxParamMath.VALUE_W.dp
    val Margin: Dp = FxParamMath.LABEL_MARGIN.dp
    val Radius: Dp = 4.dp
    /** 1.0.8 visual minimalista: raio do realce do rótulo, da caixa de valor e das escolhas. */
    val Soft: Dp = 8.dp
    val Slider: Dp = FxParamMath.ROW_SLIDER.dp
    val Ruler: Dp = FxParamMath.ROW_RULER.dp
    val Small: Dp = FxParamMath.ROW_SMALL.dp
    val Section: Dp = FxParamMath.ROW_SECTION.dp
    val List: Dp = FxParamMath.ROW_LIST.dp
    val Gap: Dp = FxParamMath.LIST_GAP.dp
    /** Coluna X/Y do ponto e caixa de XYZ. */
    val AxisColumn: Dp = 60.dp
    val AxisBox: Dp = 36.dp
    /** Botão da escolha segmentada (0.3.4 painéis: 36, antes 27,5). */
    val Segment: Dp = 36.dp
    // 0.3.4 painéis: textos das linhas (par do `FxParamLabel`/`FxValueBox` do iOS).
    /** Rótulo da linha: até 13 sp (antes 12), encolhe até 8. */
    val LabelText = 13.sp
    /** Número da caixa de valor: até 15 sp (antes 14), encolhe até 8. */
    val ValueText = 15.sp
    /** Linha auxiliar e rótulos de eixo (antes 10). */
    val SmallText = 12.sp
    /** Toque de vibração ao cruzar um risco de encaixe. */
    fun tick(view: android.view.View) {
        // A referência vibra 5 ms com amplitude 50 (Vibrator); sem a permissão
        // VIBRATE no app, o toque de relógio do sistema é o equivalente leve.
        view.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
    }
}

/**
 * O RÓTULO DA LINHA (`propertyLabel`): [width] × [height]. 1.0.8 visual
 * minimalista: sem caixa em repouso (texto a 85 %, alinhado ao início); ATIVO
 * ganha um realce suave de raio 8 em destaque a 14 % e texto em destaque;
 * pressionado, «afundado». Texto 8–13 sp autoajustável, até 2 linhas. O ◆ de 9×8
 * no canto superior inicial aparece SÓ com keyframe; o "=" de expressão (extra
 * do AUREA) no canto superior final.
 */
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun FxParamLabel(
    label: String,
    height: Dp,
    modifier: Modifier = Modifier,
    width: Dp = FxRow.Label,
    active: Boolean = false,
    enabled: Boolean = true,
    keyframe: KeyframeLook = KeyframeLook.None,
    expression: ExpressionLook = ExpressionLook.None,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val textColor = when {
        !enabled -> AureaColors.FxDisabledText
        active -> AureaColors.Accent
        else -> AureaColors.FxLabelInk
    }
    val fill = when {
        !enabled -> Color.Transparent
        active -> AureaColors.FxLabelLit
        pressed -> AureaColors.FxSunken
        else -> Color.Transparent
    }
    Box(
        modifier
            .size(width, height)
            .clip(RoundedCornerShape(FxRow.Soft))
            .background(fill)
            .then(
                if (enabled && (onClick != null || onLongClick != null)) {
                    Modifier.combinedClickable(
                        interactionSource = interaction,
                        indication = null,
                        role = Role.Button,
                        onLongClick = onLongClick,
                        onClick = { onClick?.invoke() },
                    )
                } else {
                    Modifier
                },
            )
            .padding(start = 8.dp, end = 4.dp, top = 3.dp, bottom = 3.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        BasicText(
            text = label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = FxRow.LabelText, stepSize = 0.5.sp),
            style = AureaType.Base.merge(
                TextStyle(fontSize = FxRow.LabelText, lineHeight = 1.1.em, color = textColor, textAlign = TextAlign.Start),
            ),
        )
        if (keyframe != KeyframeLook.None) {
            val tint = if (active && enabled) AureaColors.Accent else AureaColors.Text.copy(alpha = 0.5f)
            Canvas(Modifier.align(Alignment.TopStart).offset(x = (-6).dp, y = (-1).dp).size(9.dp, 8.dp)) {
                val p = Path().apply {
                    moveTo(size.width / 2f, 0f)
                    lineTo(size.width, size.height / 2f)
                    lineTo(size.width / 2f, size.height)
                    lineTo(0f, size.height / 2f)
                    close()
                }
                if (keyframe == KeyframeLook.KeyHere) drawPath(p, tint) else drawPath(p, tint, style = Stroke(1.dp.toPx()))
            }
        }
        if (expression != ExpressionLook.None) {
            ExpressionBadge(expression, Modifier.align(Alignment.TopEnd).offset(x = 1.dp, y = (-2).dp))
        }
    }
}

/**
 * A CAIXA DE VALOR (`propertyValue`): [width] × [height]. 1.0.8 visual
 * minimalista: sem véu — só um fio de 1 dp (texto a 12 %) sob o número;
 * pressionada, «afundado» com raio 8. Número 8–15 sp autoajustável alinhado ao
 * FIM, tabular, sempre LTR. [aux] é a linha auxiliar acima do número (as voltas
 * "N× +" do ângulo), em «texto desabilitado». Desabilitada: texto apagado e sem toque.
 */
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun FxValueBox(
    text: String,
    height: Dp,
    modifier: Modifier = Modifier,
    width: Dp = FxRow.Value,
    enabled: Boolean = true,
    color: Color = AureaColors.Text,
    aux: String? = null,
    background: Color = Color.Transparent,
    pressedBackground: Color = AureaColors.FxSunken,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Box(
        modifier
            .size(width, height)
            .clip(RoundedCornerShape(FxRow.Soft))
            .background(if (pressed && enabled) pressedBackground else background)
            .then(
                if (enabled && (onClick != null || onLongClick != null)) {
                    Modifier.combinedClickable(
                        interactionSource = interaction,
                        indication = null,
                        role = Role.Button,
                        onLongClick = onLongClick,
                        onClick = { onClick?.invoke() },
                    )
                } else {
                    Modifier
                },
            )
            .padding(horizontal = 6.dp),
        contentAlignment = Alignment.CenterEnd,
    ) {
        Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.Center) {
            if (!aux.isNullOrEmpty()) {
                BasicText(
                    text = aux,
                    maxLines = 1,
                    autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = FxRow.SmallText, stepSize = 0.5.sp),
                    style = AureaType.Base.merge(
                        TextStyle(fontSize = FxRow.SmallText, color = AureaColors.FxDisabledText, textAlign = TextAlign.End,
                            textDirection = TextDirection.Ltr),
                    ),
                )
            }
            BasicText(
                text = text,
                maxLines = 1,
                autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = FxRow.ValueText, stepSize = 0.5.sp),
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = FxRow.ValueText,
                        color = if (enabled) color else AureaColors.FxDisabledText,
                        textAlign = TextAlign.End,
                        fontFeatureSettings = "tnum",
                        // Número com sinal/unidade sempre LTR: em RTL "-12,5" viraria "12,5-".
                        textDirection = TextDirection.Ltr,
                    ),
                ),
            )
            // O fio sob o número (no lugar do véu).
            Spacer(Modifier.height(3.dp))
            Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.FxHairline))
        }
    }
}

// =============================================================================
// Slider = régua (1.0.8: "eu prefiro TUDO com isso aqui")
// =============================================================================

/**
 * O SLIDER do AUREA agora é a RÉGUA ([FxRuler]) com faixa FECHADA: a mesma API
 * de antes (nenhuma tela muda), o mesmo gesto da régua (arrasto relativo,
 * arremesso, dois dedos = 1/10, slop e dominância do [FxParamMath]) e o valor
 * preso em [min]..[max] — os riscos param no fim da faixa. 1 risco =
 * [FxParamMath.sliderRulerStep] (o [step], com a faixa inteira em ~120 riscos).
 * [snap] e [origin] viram riscos de encaixe (maiores, com o toque de vibração).
 * Acessibilidade igual à do slider: rótulo, valor, faixa e `setProgress`.
 * [onStart]/[onEnd] abrem e fecham o passo de desfazer (o arremesso fica dentro).
 */
@Composable
fun AureaSlider(
    value: Float,
    min: Float,
    max: Float,
    step: Float,
    origin: Float,
    snap: Float?,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    a11yLabel: String = "",
    a11yValue: String = "",
) {
    val lo = kotlin.math.min(min, max)
    val hi = kotlin.math.max(min, max)
    val tick = FxParamMath.sliderRulerStep(lo, hi, step)
    val marks = listOfNotNull(snap, origin).filter { it.isFinite() && it >= lo && it <= hi }.distinct()
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    val shown = if (value.isFinite()) value.coerceIn(lo, hi) else lo
    FxRuler(
        value = shown,
        step = tick,
        min = lo,
        max = hi,
        onStart = { begin() },
        onValue = { send(it.coerceIn(lo, hi)) },
        onEnd = { finish() },
        modifier = modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = FxRow.Ruler - 12.dp)
            .semantics {
                contentDescription = a11yLabel
                stateDescription = a11yValue
                if (lo < hi && !value.isNaN()) progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(lo, hi), lo..hi)
                if (!enabled) disabled() else setProgress { next ->
                    if (!next.isFinite()) return@setProgress false
                    begin(); send(next.coerceIn(lo, hi)); finish(); true
                }
            },
        enabled = enabled,
        snaps = marks,
        limits = true,
    )
}

// =============================================================================
// Régua (ValueSpinner da referência)
// =============================================================================

/**
 * Estado de uma [FxRuler]: o valor EM VOO (null parado) — a linha mostra este
 * número enquanto o dedo arrasta ou a régua arremessa — e o passo de desfazer
 * aberto (o arremesso continua o gesto e fecha só ao parar).
 */
@Stable
class FxRulerState {
    var live by mutableStateOf<Float?>(null)
    internal var fling: Job? = null
    internal var open = false
}

@Composable
fun rememberFxRulerState(): FxRulerState = remember { FxRulerState() }

/** A régua que está arremessando agora: toque noutra régua é ignorado (referência). */
internal object FxRulerFling {
    var owner: FxRulerState? = null
}

/**
 * A RÉGUA DE RISCOS. 1.0.8 visual minimalista: risco a cada 5,5 dp, 1,5 dp de
 * largura com pontas redondas, centrados na altura; menores de 10 dp (texto a
 * 35 %), o maior a cada 5 riscos de 16 dp (60 %), sumindo suave nos 22 % de cada
 * borda ([FxParamMath.rulerFade]); sem caixa. Agulha = cápsula de 3 × 24 dp em
 * [needle] (nas linhas de vetor, a cor do eixo ativo) com um ponto de 5 dp
 * embaixo. [limits] (o slider-régua): os riscos param em [min] e [max], com um
 * risco maior em cada limite. 1 risco =
 * [step] (unidade mostrada). O gesto começa depois de ¾ do touch slop; dedo para
 * a esquerda aumenta; arremesso acima de 300 px/s com desaceleração; o valor anda
 * de risco em risco (encaixado) e fica preso em [min]..[max] (estendida até o
 * valor de partida, [dragBounds]). Cruzar um dos [snaps] vibra. Dois dedos =
 * 1/10 (controle fino do AUREA). Um gesto (com o arremesso) = um passo de desfazer.
 */
@Composable
fun FxRuler(
    value: Float,
    step: Float,
    min: Float,
    max: Float,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    modifier: Modifier = Modifier,
    state: FxRulerState = rememberFxRulerState(),
    enabled: Boolean = true,
    needle: Color = AureaColors.FxNeedle,
    snaps: List<Float> = emptyList(),
    limits: Boolean = false,
) {
    val density = LocalDensity.current
    val spacing = with(density) { FxParamMath.TICK_SPACING.dp.toPx() }
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val view = LocalView.current
    val scope = rememberCoroutineScope()
    val decay = remember(density) { splineBasedDecay<Float>(density) }
    val current by rememberUpdatedState(value)
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    val snapList by rememberUpdatedState(snaps)
    val stepNow = if (step > 0f && step.isFinite()) step else 1f
    val stepRef by rememberUpdatedState(stepNow)
    DisposableEffect(state) {
        onDispose {
            state.fling?.cancel()
            state.fling = null
            if (FxRulerFling.owner === state) FxRulerFling.owner = null
            if (state.open) {
                state.open = false
                state.live = null
                finish()
            }
        }
    }
    Canvas(
        modifier.pointerInput(enabled, min, max) {
            if (!enabled) return@pointerInput
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val other = FxRulerFling.owner
                if (other != null && other !== state) return@awaitEachGesture
                var inGesture = false
                // Um toque pode parar a inércia sem tomar o gesto vertical da lista.
                val interrupted = state.live
                if (state.fling?.isActive == true) {
                    state.fling?.cancel()
                    state.fling = null
                    if (FxRulerFling.owner === state) FxRulerFling.owner = null
                    state.live = null
                    if (state.open) {
                        state.open = false
                        finish()
                    }
                }
                val tracker = VelocityTracker()
                tracker.addPosition(down.uptimeMillis, down.position)
                var start = interrupted ?: current
                if (!start.isFinite()) start = 0f
                var walked = 0f
                var lastX = down.position.x
                var pointer = down.id
                var gain = 1f
                fun applyRaw(raw: Float) {
                    val (lo, hi) = dragBounds(min, max, start)
                    val v = FxParamMath.limitRange(FxParamMath.snapToTick(raw, stepRef), lo, hi)
                    val before = state.live ?: start
                    if (v == state.live) return
                    if (FxParamMath.crossedSnap(before, v, snapList)) FxRow.tick(view)
                    state.live = v
                    send(v)
                }
                while (true) {
                    val event = awaitPointerEvent()
                    val pressed = event.changes.filter { it.pressed }
                    if (pressed.isEmpty()) break
                    val main = pressed.firstOrNull { it.id == pointer }
                    if (main == null) {
                        // O dedo que arrastava saiu: segue com outro, sem salto.
                        pointer = pressed.first().id
                        lastX = pressed.first().position.x
                        continue
                    }
                    tracker.addPosition(main.uptimeMillis, main.position)
                    if (!inGesture) {
                        if (main.isConsumed) return@awaitEachGesture
                        val dx = main.position.x - down.position.x
                        val dy = main.position.y - down.position.y
                        if (abs(dy) > touchSlop && abs(dy) > abs(dx)) return@awaitEachGesture
                        if (abs(dx) <= abs(dy) * FxParamMath.HORIZONTAL_DOMINANCE ||
                            abs(dx) <= touchSlop * FxParamMath.RULER_SLOP_FRACTION) continue
                        inGesture = true
                        lastX = main.position.x
                        main.consume()
                        if (!state.open) {
                            state.open = true
                            begin()
                        }
                        start = state.live ?: interrupted ?: current
                        if (!start.isFinite()) start = 0f
                        continue
                    }
                    val d = main.position.x - lastX
                    lastX = main.position.x
                    event.changes.forEach { if (it.positionChange() != Offset.Zero) it.consume() }
                    gain = if (pressed.size >= 2) ParamRowMath.TWO_FINGER_GAIN else 1f
                    if (d != 0f) {
                        walked -= d * gain   // dedo para a esquerda aumenta
                        applyRaw(FxParamMath.rulerValueAfter(start, walked, spacing, stepRef))
                    }
                }
                if (!inGesture) return@awaitEachGesture
                val velocity = tracker.calculateVelocity().x
                if (abs(velocity) > FxParamMath.FLING_MIN_PX_S) {
                    FxRulerFling.owner = state
                    state.fling = scope.launch {
                        var last = 0f
                        val anim = AnimationState(initialValue = 0f, initialVelocity = -velocity * gain)
                        var finished = false
                        try {
                            anim.animateDecay(decay) {
                                val delta = this.value - last
                                last = this.value
                                walked += delta
                                val before = state.live
                                applyRaw(FxParamMath.rulerValueAfter(start, walked, spacing, stepRef))
                                // Chegou ao limite da faixa: para (limitRange).
                                if (state.live == before && delta != 0f) {
                                    val (lo, hi) = dragBounds(min, max, start)
                                    val v = state.live ?: start
                                    if (v <= lo || v >= hi) cancelAnimation()
                                }
                            }
                            finished = isActive
                        } finally {
                            if (FxRulerFling.owner === state) FxRulerFling.owner = null
                        }
                        if (finished) {
                            state.fling = null
                            state.live = null
                            state.open = false
                            finish()
                        }
                    }
                } else {
                    state.live = null
                    state.open = false
                    finish()
                }
            }
        },
    ) {
        drawFxRuler(state.live ?: value, stepNow, spacing, if (enabled) needle else AureaColors.FxDisabledText, snaps,
            if (limits) kotlin.math.min(min, max) else Float.NEGATIVE_INFINITY,
            if (limits) kotlin.math.max(min, max) else Float.POSITIVE_INFINITY)
    }
}

private fun DrawScope.drawFxRuler(
    value: Float, step: Float, spacing: Float, needle: Color, snaps: List<Float>,
    lo: Float = Float.NEGATIVE_INFINITY, hi: Float = Float.POSITIVE_INFINITY,
) {
    if (size.height <= 0f || size.width <= 0f) return
    val c = size.width / 2f
    val cy = size.height / 2f
    val v = if (value.isFinite()) value else 0f
    val pos = v.toDouble() / step
    val reach = c / spacing
    val first = floor(pos - reach).toLong() - 1
    val last = ceil(pos + reach).toLong() + 1
    val w = 1.5.dp.toPx()
    val ink = AureaColors.Text
    // Alturas presas na régua (linhas baixas não vazam).
    val minorH = kotlin.math.min(10.dp.toPx(), size.height)
    val majorH = kotlin.math.min(16.dp.toPx(), size.height)
    val snapH = kotlin.math.min(18.dp.toPx(), size.height)
    if (last - first < 4000) {
        var k = first
        while (k <= last) {
            val x = c + ((k - pos) * spacing).toFloat()
            val fade = FxParamMath.rulerFade(x, size.width)
            // Faixa fechada (slider-régua): sem riscos além dos limites.
            val at = k * step.toDouble()
            val inside = at >= lo - step * 1e-3 && at <= hi + step * 1e-3
            if (fade > 0f && inside) {
                val isMajor = Math.floorMod(k, FxParamMath.MAJOR_EVERY.toLong()) == 0L
                val h = if (isMajor) majorH else minorH
                val a = if (isMajor) 0.6f else 0.35f
                drawLine(ink.copy(alpha = a * fade), Offset(x, cy - h / 2f), Offset(x, cy + h / 2f), w, StrokeCap.Round)
            }
            k++
        }
    }
    // Os limites da faixa fechada: um risco maior em cada ponta.
    for (edge in floatArrayOf(lo, hi)) {
        if (!edge.isFinite()) continue
        val x = c + ((edge - v) / step) * spacing
        val fade = FxParamMath.rulerFade(x, size.width)
        if (fade > 0f) drawLine(ink.copy(alpha = 0.6f * fade), Offset(x, cy - majorH / 2f), Offset(x, cy + majorH / 2f), w, StrokeCap.Round)
    }
    // Riscos de encaixe (padrão, zero): um pouco mais altos e fortes.
    for (s in snaps) {
        if (!s.isFinite()) continue
        val x = c + ((s - v) / step) * spacing
        val fade = FxParamMath.rulerFade(x, size.width)
        if (fade > 0f) drawLine(ink.copy(alpha = 0.85f * fade), Offset(x, cy - snapH / 2f), Offset(x, cy + snapH / 2f), w, StrokeCap.Round)
    }
    // Agulha: cápsula de 3 × 24 dp e um ponto de 5 dp logo abaixo (cabe na altura).
    val dot = 5.dp.toPx()
    val gap = 2.dp.toPx()
    val needleH = kotlin.math.min(24.dp.toPx(), size.height - 2f * (dot + gap))
    if (needleH > 0f) {
        val nw = 3.dp.toPx()
        drawLine(needle, Offset(c, cy - needleH / 2f + nw / 2f), Offset(c, cy + needleH / 2f - nw / 2f), nw, StrokeCap.Round)
        drawCircle(needle, dot / 2f, Offset(c, cy + needleH / 2f + gap + dot / 2f))
    } else {
        drawLine(needle, Offset(c, 0f), Offset(c, size.height), 1.5.dp.toPx())
    }
}

// =============================================================================
// Escolha segmentada e suspensa
// =============================================================================

/**
 * ESCOLHA SEGMENTADA (`effect_setting_selector`, estilo RADIO). 1.0.8 visual
 * minimalista: moldura de raio 8 com fio de 1 dp (texto a 12 %), sem fundo; o
 * escolhido ganha uma pílula de raio 6 em destaque a 22 % e texto em destaque.
 * Uma linha com reticências. [selected] < 0 = nenhuma. testTags `"$tag.<k>"`.
 */
@Composable
fun FxSegmented(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    tag: String = "fx.segmented",
) {
    Row(
        modifier
            .height(FxRow.Segment)
            .clip(RoundedCornerShape(FxRow.Soft))
            .border(1.dp, AureaColors.FxHairline, RoundedCornerShape(FxRow.Soft))
            .padding(2.dp)
            .testTag(tag),
    ) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(FxRow.Soft - 2.dp))
                    .background(if (on) AureaColors.FxSegmentPick else Color.Transparent)
                    .testTag("$tag.$i")
                    .semantics {
                        role = Role.RadioButton
                        this.selected = on
                        if (!enabled) disabled()
                    }
                    .tocavel(enabled = enabled, shrink = 1f, role = null) { onSelect(i) }
                    .padding(horizontal = 4.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    o,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(
                        TextStyle(
                            fontSize = FxRow.LabelText,
                            color = when {
                                !enabled -> AureaColors.FxDisabledText
                                on -> AureaColors.Accent
                                else -> AureaColors.Text
                            },
                        ),
                    ),
                )
            }
        }
    }
}

/**
 * ESCOLHA SUSPENSA (estilo DROPDOWN). 1.0.8 visual minimalista: caixa de raio 8
 * só com o fio de 1 dp (texto a 12 %), inset 4 × 2, a opção vigente e ▾ no destaque;
 * o toque abre o menu ancorado com as opções.
 */
@Composable
fun FxDropdownBox(
    text: String,
    entries: List<MenuEntry>,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    tag: String = "fx.dropdown",
    contentDescription: String = text,
) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        Row(
            Modifier
                .fillMaxWidth()
                // 1.0.8: inset 2 em cima/embaixo (36 + 4 cabe na linha baixa de 40).
                .padding(horizontal = 4.dp, vertical = 2.dp)
                .height(FxRow.Segment)  // 0.3.4 painéis: 36 (antes 30)
                .clip(RoundedCornerShape(FxRow.Soft))
                .border(1.dp, AureaColors.FxHairline, RoundedCornerShape(FxRow.Soft))
                .testTag(tag)
                .semantics { this.contentDescription = contentDescription; if (!enabled) disabled() }
                .tocavel(enabled = enabled, shrink = 1f) { open = true }
                .padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
                style = AureaType.Base.merge(
                    TextStyle(fontSize = 14.sp, color = if (enabled) AureaColors.Accent else AureaColors.FxDisabledText),
                ),
            )
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.ChevronDown, 14.dp, if (enabled) AureaColors.Accent else AureaColors.FxDisabledText)
        }
        AnchoredMenu(expanded = open, onDismiss = { open = false }, entries = entries, matchAnchorWidth = true, tag = "$tag.menu")
    }
}

// =============================================================================
// "Avançado" (outros painéis)
// =============================================================================

/**
 * "Avançado ▾" — a porta dos parâmetros que não são principais. Mostra quantos
 * há atrás dela; aberta vira "Avançado ▴". Linha de 44 dp inteira tocável.
 * (Os painéis de efeito da Omega desenham o Avançado como SEÇÃO da referência;
 * este componente segue nos outros painéis.)
 */
@Composable
fun AdvancedToggle(open: Boolean, count: Int, onToggle: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .height(44.dp)
            .tocavel(shrink = 1f, onClick = onToggle)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            stringResource(R.string.ds_avancado),
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 13.dp, AureaColors.Accent)
        Spacer(Modifier.weight(1f))
        if (!open) {
            Text(
                if (count == 1) stringResource(R.string.ds_1_ajuste) else stringResource(R.string.ds_adjustment_count, count),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
    }
}
