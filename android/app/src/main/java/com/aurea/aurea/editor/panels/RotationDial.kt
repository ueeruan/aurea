package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

// =============================================================================
// Omega aparência (área 05, P0-2): GIRAR — a roda de rotação (RotateView da
// referência, desenho e código do AUREA).
//
//   • trilha 2,5 dp (preto a 30 %), alça de 35 dp com sombra na posição do
//     ângulo dentro da volta (0° no alto, horário positivo, como a camada);
//   • |ângulo| > 360°: arco do excedente no destaque;
//   • centro: o ângulo dentro da volta `%.1f°` (0,4 × raio, destaque, fundo
//     `campo`) e "N×" voltas acima (0,2 × raio, texto-2);
//   • giro RELATIVO ao ângulo do dedo em volta do centro; ímã a cada 45° com
//     zona de 2° pela ponte (`AureaEngine.dialSnap`, a mesma conta do iOS), com
//     háptico ao grudar; toque curto (< tapTimeout) abre o teclado (°);
//   • um passo de desfazer por gesto (quem chama abre/fecha).
// =============================================================================

private val RotationTracks = intArrayOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z)

/**
 * A face Girar: no 3D, a fileira X° / Y° / Z° (desenho da pílula) escolhe o eixo
 * da roda e o campo ativo abre o teclado; no 2D só Z. [onLetters]: texto 3D
 * (volta às letras).
 */
@Composable
internal fun ColumnScope.RotateFace(
    env: PanelEnv,
    depth: Boolean,
    axis: Int,
    onAxis: (Int) -> Unit,
    onLetters: (() -> Unit)? = null,
) {
    val store = env.store
    val owner = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    fun write(component: Int, value: Float) {
        if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition) return
        if (!value.isFinite()) return
        store.setTransform(RotationTracks[component], value, owner)
    }
    val angles by remember(store) {
        derivedStateOf { store.detail?.rotation?.let { floatArrayOf(it[0], it[1], it[2]) } ?: floatArrayOf(0f, 0f, 0f) }
    }
    val enabled by remember(store) { derivedStateOf { store.detail?.locked != true } }
    // "Rotação X/Y/Z": título do teclado e do leitor de tela.
    val titles = listOf(
        stringResource(R.string.edt_rotation_axis, "X"),
        stringResource(R.string.edt_rotation_axis, "Y"),
        stringResource(R.string.edt_rotation_axis, "Z"),
    )
    val title = titles[axis]
    fun keypad(component: Int) {
        env.openKeypad(KeypadRequest(titles[component], angles[component], "°",
            Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(component, it) })
    }
    Column(Modifier.weight(1f).fillMaxWidth().background(AureaColors.Surface)) {
        if (onLetters != null) {
            androidx.compose.material3.TextButton(onClick = onLetters) { Text(stringResource(R.string.t3d_letters)) }
        }
        if (depth) {
            // 0.3.4 painéis: três campos de 72 com vão de 4 cabem em 360 (226 úteis); rola se não couber.
            TfFieldRow(top = PanelMetrics.FieldRowTop, scroll = true) {
                for (i in 0..2) {
                    if (i > 0) Spacer(Modifier.width(TfFieldDims.Gap / 2))
                    val name = "XYZ"[i].toString()
                    TfField(
                        "${numeroPtBr(angles[i], 1)}°",
                        "$name°",
                        stringResource(R.string.om_ap_tf_axis, name),
                        active = axis == i,
                        tag = "transform.rotation.axis.${name.lowercase()}",
                        enabled = enabled,
                        onLongClick = { onAxis(i); keypad(i) },
                    ) { if (axis == i) keypad(i) else onAxis(i) }
                }
            }
        }
        RotationDial(
            angle = { angles[axis] },
            contentDescription = "${stringResource(R.string.om_ap_tf_dial)} · $title",
            enabled = enabled,
            onGestureStart = {
                if (store.playing || store.preview.buffering) store.pause()
                store.beginGesture("girar")
            },
            onAngle = { write(axis, it) },
            onGestureEnd = { store.endGesture() },
            onTap = { keypad(axis) },
            modifier = Modifier.weight(1f).fillMaxWidth().padding(horizontal = 6.dp, vertical = 8.dp).testTag("transform.rotation.dial"),
        )
    }
}

/**
 * A RODA. [angle] é lido no desenho (mudar o valor só repinta). O gesto chama
 * [onGestureStart] uma vez, [onAngle] com o ângulo TOTAL (com o ímã) e
 * [onGestureEnd]; o toque curto chama [onTap].
 */
@Composable
internal fun RotationDial(
    angle: () -> Float,
    contentDescription: String,
    onGestureStart: () -> Unit,
    onAngle: (Float) -> Unit,
    onGestureEnd: () -> Unit,
    onTap: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val haptic = LocalHapticFeedback.current
    val measurer = rememberTextMeasurer()
    val read by rememberUpdatedState(angle)
    val begin by rememberUpdatedState(onGestureStart)
    val send by rememberUpdatedState(onAngle)
    val finish by rememberUpdatedState(onGestureEnd)
    val tap by rememberUpdatedState(onTap)
    val turnsLabel = stringResource(R.string.om_ap_tf_turns, rotationTurns(angle()).first)
    val now = angle()
    Canvas(
        modifier
            .semantics {
                this.contentDescription = contentDescription
                stateDescription = "${numeroPtBr(now, 1)}° · $turnsLabel"
                onClick { if (enabled) tap(); true }
            }
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                val tapMs = android.view.ViewConfiguration.getTapTimeout().toLong()
                val dead = 12.dp.toPx()
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val pointer = down.id
                    val center = Offset(size.width / 2f, size.height / 2f)
                    var previous = angleOf(down.position - center)
                    var total = Offset.Zero
                    var dragging = false
                    var start = 0f
                    var accum = 0f
                    var stuck = false
                    try {
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == pointer } ?: break
                            if (!change.pressed) {
                                if (!dragging && !change.isConsumed && change.uptimeMillis - down.uptimeMillis < tapMs) tap()
                                break
                            }
                            total += change.position - change.previousPosition
                            if (!dragging) {
                                if (total.getDistance() <= viewConfiguration.touchSlop) continue
                                dragging = true
                                start = read().takeIf { it.isFinite() } ?: 0f
                                accum = 0f
                                begin()
                            }
                            change.consume()
                            val v = change.position - center
                            val current = angleOf(v)
                            // Perto do miolo o ângulo do dedo pula: segue sem somar.
                            if (hypot(v.x, v.y) >= dead) accum += wrap180(current - previous)
                            previous = current
                            val snapped = AureaEngine.dialSnap(start + accum)
                            val value = snapped.getOrNull(0)?.takeIf { it.isFinite() } ?: (start + accum)
                            val glued = (snapped.getOrNull(1) ?: 0f) > 0.5f
                            if (glued && !stuck) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                            stuck = glued
                            send(value)
                        }
                    } finally {
                        if (dragging) finish()
                    }
                }
            },
    ) {
        val a = read().let { if (it.isFinite()) it else 0f }
        val (turns, rest) = rotationTurns(a)
        val handleR = 17.5.dp.toPx()
        val radius = min(size.width, size.height) / 2f - 2.dp.toPx() - handleR
        if (radius <= 4.dp.toPx()) return@Canvas
        val c = Offset(size.width / 2f, size.height / 2f)
        val track = 2.5.dp.toPx()
        // Trilha.
        drawCircle(AureaColors.Scrim.copy(alpha = 0.3f), radius, c, style = Stroke(track))
        // Arco do excedente (passou de uma volta): do alto até o ângulo dentro da volta.
        if (kotlin.math.abs(a) > 360f && rest != 0f) {
            drawArc(
                AureaColors.Accent,
                startAngle = -90f,
                sweepAngle = rest,
                useCenter = false,
                topLeft = Offset(c.x - radius, c.y - radius),
                size = Size(radius * 2f, radius * 2f),
                style = Stroke(track, cap = StrokeCap.Round),
            )
        }
        // Miolo: fundo `campo`, o ângulo dentro da volta e as voltas.
        val inner = radius - handleR - 4.dp.toPx()
        if (inner > 8.dp.toPx()) {
            drawCircle(AureaColors.Chip, inner, c)
            val angleText = "${numeroPtBr(rest, 1)}°"
            var big = TextStyle(fontSize = (radius * 0.4f).toSp(), fontWeight = FontWeight.W600, color = AureaColors.Accent,
                fontFeatureSettings = "tnum", textDirection = TextDirection.Ltr)
            var layout = measurer.measure(angleText, AureaType.Base.merge(big))
            val maxW = inner * 1.7f
            if (layout.size.width > maxW) {
                big = big.copy(fontSize = (radius * 0.4f * maxW / layout.size.width).toSp())
                layout = measurer.measure(angleText, AureaType.Base.merge(big))
            }
            drawText(layout, topLeft = Offset(c.x - layout.size.width / 2f, c.y - layout.size.height / 2f))
            val small = TextStyle(fontSize = (radius * 0.2f).toSp(), color = AureaColors.Muted, textDirection = TextDirection.Ltr)
            val turnsLayout = measurer.measure("${turns}×", AureaType.Base.merge(small))
            if (turnsLayout.size.width < inner * 1.6f) {
                drawText(turnsLayout, topLeft = Offset(c.x - turnsLayout.size.width / 2f, c.y - layout.size.height / 2f - turnsLayout.size.height))
            }
        }
        // Alça: sombra suave + disco claro.
        val t = Math.toRadians(rest.toDouble())
        val h = Offset(c.x + radius * sin(t).toFloat(), c.y - radius * cos(t).toFloat())
        val shadow = 4.dp.toPx()
        drawCircle(
            Brush.radialGradient(
                listOf(AureaColors.Scrim.copy(alpha = 0.45f), Color.Transparent),
                center = h + Offset(0f, 1.5.dp.toPx()),
                radius = handleR + shadow,
            ),
            handleR + shadow,
            h + Offset(0f, 1.5.dp.toPx()),
        )
        drawCircle(AureaColors.Text, handleR, h)
    }
}

/** Ângulo (°) do vetor na tela: 0 no alto, horário positivo. */
private fun angleOf(v: Offset): Float = Math.toDegrees(atan2(v.x.toDouble(), -v.y.toDouble())).toFloat()

/** Diferença de ângulos levada para (−180°, 180°]. */
private fun wrap180(d: Float): Float {
    var x = d % 360f
    if (x > 180f) x -= 360f
    if (x <= -180f) x += 360f
    return x
}
