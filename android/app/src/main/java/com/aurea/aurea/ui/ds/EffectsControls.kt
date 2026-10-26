package com.aurea.aurea.ui.ds

import android.view.HapticFeedbackConstants
import androidx.compose.animation.core.AnimationState
import androidx.compose.animation.core.animateDecay
import androidx.compose.animation.splineBasedDecay
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
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
    val Slider: Dp = FxParamMath.ROW_SLIDER.dp
    val Ruler: Dp = FxParamMath.ROW_RULER.dp
    val Small: Dp = FxParamMath.ROW_SMALL.dp
    val Section: Dp = FxParamMath.ROW_SECTION.dp
    val List: Dp = FxParamMath.ROW_LIST.dp
    val Gap: Dp = FxParamMath.LIST_GAP.dp
    /** Coluna X/Y do ponto e caixa de XYZ. */
    val AxisColumn: Dp = 55.dp
    val AxisBox: Dp = 36.dp
    /** Botão da escolha segmentada. */
    val Segment: Dp = 27.5.dp
    /** Toque de vibração ao cruzar um risco de encaixe. */
    fun tick(view: android.view.View) {
        // A referência vibra 5 ms com amplitude 50 (Vibrator); sem a permissão
        // VIBRATE no app, o toque de relógio do sistema é o equivalente leve.
        view.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
    }
}

/**
 * O RÓTULO DA LINHA (`propertyLabel`): [width] × [height], raio 4, fundo «rótulo»
 * normal e «afundado» pressionado ou ATIVO; texto 8–12 sp autoajustável, até 2
 * linhas, centralizado (texto / destaque ativo / desabilitado). O ◆ de 9×8 no
 * canto superior esquerdo aparece SÓ com keyframe; o "=" de expressão (extra do
 * AUREA) no canto superior direito.
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
    val on = active || pressed
    val textColor = when {
        !enabled -> AureaColors.FxDisabledText
        active -> AureaColors.Accent
        else -> AureaColors.Text
    }
    Box(
        modifier
            .size(width, height)
            .clip(RoundedCornerShape(FxRow.Radius))
            .background(if (on && enabled) AureaColors.FxSunken else AureaColors.FxLabel)
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
            .padding(3.dp),
        contentAlignment = Alignment.Center,
    ) {
        BasicText(
            text = label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 12.sp, stepSize = 0.5.sp),
            style = AureaType.Base.merge(
                TextStyle(fontSize = 12.sp, lineHeight = 1.1.em, color = textColor, textAlign = TextAlign.Center),
            ),
        )
        if (keyframe != KeyframeLook.None) {
            val tint = if (active && enabled) AureaColors.Accent else AureaColors.Text.copy(alpha = 0.5f)
            Canvas(Modifier.align(Alignment.TopStart).offset(x = (-1).dp, y = (-1).dp).size(9.dp, 8.dp)) {
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
 * A CAIXA DE VALOR (`propertyValue`): [width] × [height], raio 4, fundo «véu
 * 20 %»; número 8–14 sp autoajustável alinhado ao FIM, sempre LTR. [aux] é a
 * linha auxiliar acima do número (as voltas "N× +" do ângulo), em «texto
 * desabilitado». Desabilitada: texto apagado e sem toque.
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
    background: Color = AureaColors.FxValueVeil,
    pressedBackground: Color = AureaColors.FxSunken,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Box(
        modifier
            .size(width, height)
            .clip(RoundedCornerShape(FxRow.Radius))
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
                    autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 10.sp, stepSize = 0.5.sp),
                    style = AureaType.Base.merge(
                        TextStyle(fontSize = 10.sp, color = AureaColors.FxDisabledText, textAlign = TextAlign.End,
                            textDirection = TextDirection.Ltr),
                    ),
                )
            }
            BasicText(
                text = text,
                maxLines = 1,
                autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 14.sp, stepSize = 0.5.sp),
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = 14.sp,
                        color = if (enabled) color else AureaColors.FxDisabledText,
                        textAlign = TextAlign.End,
                        fontFeatureSettings = "tnum",
                        // Número com sinal/unidade sempre LTR: em RTL "-12,5" viraria "12,5-".
                        textDirection = TextDirection.Ltr,
                    ),
                ),
            )
        }
    }
}

// =============================================================================
// Slider (como o da referência)
// =============================================================================

/**
 * O SLIDER: trilho de 2,5 dp com ponta redonda em «trilho»; preenchimento em
 * destaque da [origin] até o polegar (bipolar quando a origem é 0 numa faixa que
 * cruza o zero); polegar = círculo de 18 dp no texto. [snap] (o padrão) ganha
 * 10 dp de pista onde o valor fica parado. Gesto: o toque parado não muda nada;
 * passou de ½ touch slop, o valor vai para a POSIÇÃO DO DEDO, preso e quantizado
 * em [step]. [onStart]/[onEnd] abrem e fecham o passo de desfazer.
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
    val density = LocalDensity.current
    val thumbR = with(density) { 9.dp.toPx() }
    val snapPx = with(density) { FxParamMath.SLIDER_SNAP_DP.dp.toPx() }
    val slop = LocalViewConfiguration.current.touchSlop
    var live by remember { mutableStateOf<Float?>(null) }
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    val lo = kotlin.math.min(min, max)
    val hi = kotlin.math.max(min, max)
    val snaps = listOfNotNull(snap?.takeIf { it.isFinite() && it > lo && it < hi })
    val shown = live ?: value
    val accent = if (enabled) AureaColors.Accent else AureaColors.FxTrackDisabled
    val track = AureaColors.FxTrack
    val thumb = if (enabled) AureaColors.Text else AureaColors.FxDisabledText
    Canvas(
        modifier
            .semantics {
                contentDescription = a11yLabel
                stateDescription = a11yValue
                if (lo < hi) progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(lo, hi), lo..hi)
                if (!enabled) disabled() else setProgress { next ->
                    if (!next.isFinite()) return@setProgress false
                    begin(); send(next.coerceIn(lo, hi)); finish(); true
                }
            }
            .pointerInput(enabled, lo, hi, step, snap) {
                if (!enabled || !(hi > lo)) return@pointerInput
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    var dragging = false
                    while (true) {
                        val e = awaitPointerEvent()
                        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
                        if (!ch.pressed) break
                        if (!dragging) {
                            if (ch.isConsumed) return@awaitEachGesture
                            val dx = ch.position.x - down.position.x
                            val dy = ch.position.y - down.position.y
                            // Vertical: é da rolagem da lista.
                            if (abs(dy) > slop && abs(dy) > abs(dx)) return@awaitEachGesture
                            if (abs(dx) <= slop * FxParamMath.SLIDER_SLOP_FRACTION) continue
                            dragging = true
                            begin()
                        }
                        ch.consume()
                        val w = size.width - 2f * thumbR
                        val v = FxParamMath.sliderValueAt(ch.position.x - thumbR, w, lo, hi, step, snaps, snapPx)
                        if (v != live) {
                            live = v
                            send(v)
                        }
                    }
                    if (dragging) {
                        live = null
                        finish()
                    }
                }
            },
    ) {
        if (!(hi > lo)) return@Canvas
        val w = size.width - 2f * thumbR
        val cy = size.height / 2f
        val stroke = 2.5.dp.toPx()
        drawLine(track, Offset(thumbR, cy), Offset(thumbR + w, cy), stroke, StrokeCap.Round)
        val xo = thumbR + FxParamMath.sliderX(origin.coerceIn(lo, hi), w, lo, hi, snaps, snapPx)
        val xv = thumbR + FxParamMath.sliderX(shown, w, lo, hi, snaps, snapPx)
        if (xo != xv) drawLine(accent, Offset(xo, cy), Offset(xv, cy), stroke, StrokeCap.Round)
        drawCircle(thumb, thumbR, Offset(xv, cy))
    }
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
 * A RÉGUA DE RISCOS da referência: risco a cada 5,5 dp e 0,5 dp de largura em
 * «risco», o maior a cada 5 riscos no texto, esmaecendo até as bordas; agulha de
 * 1,5 dp em [needle] (nas linhas de vetor, a cor do eixo ativo). 1 risco =
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
                if (other != null && other !== state) {
                    // Outra régua arremessando: este gesto inteiro é ignorado.
                    while (true) {
                        val e = awaitPointerEvent()
                        e.changes.forEach { it.consume() }
                        if (e.changes.none { it.pressed }) break
                    }
                    return@awaitEachGesture
                }
                var inGesture = false
                if (state.fling?.isActive == true) {
                    // Tocar durante o arremesso já é gesto (e para o arremesso).
                    state.fling?.cancel()
                    state.fling = null
                    if (FxRulerFling.owner === state) FxRulerFling.owner = null
                    inGesture = true
                    down.consume()
                }
                val tracker = VelocityTracker()
                tracker.addPosition(down.uptimeMillis, down.position)
                var start = state.live ?: current
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
                if (inGesture && !state.open) {
                    state.open = true
                    begin()
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
                        if (abs(dx) <= touchSlop * FxParamMath.RULER_SLOP_FRACTION) continue
                        inGesture = true
                        lastX = main.position.x
                        main.consume()
                        if (!state.open) {
                            state.open = true
                            begin()
                        }
                        start = state.live ?: current
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
        drawFxRuler(state.live ?: value, stepNow, spacing, needle, snaps, 8.dp.toPx())
    }
}

private fun DrawScope.drawFxRuler(value: Float, step: Float, spacing: Float, needle: Color, snaps: List<Float>, pad: Float) {
    val top = pad
    val bottom = size.height - pad
    if (bottom <= top || size.width <= 0f) return
    val c = size.width / 2f
    val v = if (value.isFinite()) value else 0f
    val pos = v.toDouble() / step
    val reach = c / spacing
    val first = floor(pos - reach).toLong() - 1
    val last = ceil(pos + reach).toLong() + 1
    val w = 0.5.dp.toPx()
    val minor = AureaColors.FxTick
    val major = AureaColors.Text
    if (last - first < 4000) {
        var k = first
        while (k <= last) {
            val x = c + ((k - pos) * spacing).toFloat()
            val b = ParamRowMath.tickBrightness(x, size.width)
            if (b > 0f) {
                val isMajor = Math.floorMod(k, FxParamMath.MAJOR_EVERY.toLong()) == 0L
                drawLine((if (isMajor) major else minor).copy(alpha = b), Offset(x, top), Offset(x, bottom), w)
            }
            k++
        }
    }
    // Riscos de encaixe (padrão, zero): 1,25 px, opacidade reforçada.
    for (s in snaps) {
        if (!s.isFinite()) continue
        val x = c + ((s - v) / step) * spacing
        val b = ParamRowMath.tickBrightness(x, size.width)
        if (b > 0f) drawLine(major.copy(alpha = (b + 1f) / 2f), Offset(x, top), Offset(x, bottom), 1.25f)
    }
    drawLine(needle, Offset(c, 0f), Offset(c, size.height), 1.5.dp.toPx())
}

// =============================================================================
// Escolha segmentada e suspensa
// =============================================================================

/**
 * ESCOLHA SEGMENTADA (`effect_setting_selector`, estilo RADIO): cartão de raio 5
 * com botões de altura igual (27,5 dp) em «segmento» / «segmento ativo», texto
 * no texto / destaque, uma linha com reticências. [selected] < 0 = nenhuma.
 * testTags `"$tag.<k>"`.
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
            .clip(RoundedCornerShape(5.dp))
            .testTag(tag),
        horizontalArrangement = Arrangement.spacedBy(1.dp),
    ) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .background(if (on) AureaColors.FxSegmentOn else AureaColors.FxSegment)
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
                            fontSize = 12.sp,
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
 * ESCOLHA SUSPENSA (estilo DROPDOWN): caixa de raio 5 em «afundado» com inset 4,
 * a opção vigente e ▾ no destaque; o toque abre o menu ancorado com as opções.
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
                .padding(4.dp)
                .height(30.dp)
                .clip(RoundedCornerShape(5.dp))
                .background(AureaColors.FxSunken)
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
                    TextStyle(fontSize = 13.sp, color = if (enabled) AureaColors.Accent else AureaColors.FxDisabledText),
                ),
            )
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.ChevronDown, 12.dp, if (enabled) AureaColors.Accent else AureaColors.FxDisabledText)
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
