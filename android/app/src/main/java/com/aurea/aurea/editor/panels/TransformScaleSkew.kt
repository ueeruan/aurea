package com.aurea.aurea.editor.panels

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.LayerGeometry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.roundToInt

// =============================================================================
// Omega aparência (área 05, P0-2 / P1-2): ESCALA e INCLINAR.
//
// Escala: Largura 🔗 Altura em PX da caixa (mídia × escala; o teclado em px vira
// escala). Travado = UMA régua (o maior lado); destravado = DUAS (largura com a
// agulha no destaque, altura branca). Trocar o cadeado anima as réguas (escala
// Y 1,1 → 1,0 em 120 ms). Marcas de encaixe nos tamanhos das outras camadas
// (`store.snapTargets(2/3)`), com ímã e háptico. Câmera: a régua edita o ângulo
// de visão. Inclinar: X (destaque) e Y (branco), graus, passo 0,1°, linha do zero.
// =============================================================================

/**
 * Uma régua com marcas por cima: [marks] (valores com encaixe) e, opcional, a
 * linha do ZERO. As marcas usam a mesma conta dos riscos do [TickRuler]
 * (`centro + (marca − valor)/porPx`), então andam junto com a fita.
 */
@Composable
internal fun MarkedRuler(
    value: () -> Float,
    unitsPerDp: Float,
    active: Boolean,
    tag: String,
    description: String,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    modifier: Modifier = Modifier,
    min: Float = Float.NEGATIVE_INFINITY,
    max: Float = Float.POSITIVE_INFINITY,
    marks: FloatArray = FloatArray(0),
    zeroLine: Boolean = false,
    enabled: Boolean = true,
    state: String? = null,
) {
    Box(
        modifier
            .testTag(tag)
            .semantics {
                contentDescription = description
                if (state != null) stateDescription = state
            }
            .valueDrag(
                enabled = enabled,
                start = value,
                unitsPerDp = { unitsPerDp },
                min = min,
                max = max,
                onStart = onStart,
                onValue = onValue,
                onEnd = onEnd,
            ),
    ) {
        TickRuler(value = value, unitsPerDp = unitsPerDp, active = active, modifier = Modifier.matchParentSize())
        if (marks.isNotEmpty() || zeroLine) {
            Canvas(Modifier.matchParentSize()) {
                val v = value().let { if (it.isFinite()) it else 0f }
                val perPx = if (unitsPerDp > 0f) unitsPerDp / density else 1f
                val center = size.width / 2f
                val top = 8.dp.toPx()
                val bottom = size.height - top
                if (bottom <= top) return@Canvas
                if (zeroLine) {
                    val x = center + (0f - v) / perPx
                    if (x in 0f..size.width) drawLine(AureaColors.Text.copy(alpha = 0.55f), Offset(x, top), Offset(x, bottom), 1.dp.toPx())
                }
                val tick = (bottom - top) * 0.28f
                val ink = AureaColors.Accent.copy(alpha = 0.6f)
                val w = 2.dp.toPx()
                for (m in marks) {
                    val x = center + (m - v) / perPx
                    if (x < 0f || x > size.width) continue
                    drawLine(ink, Offset(x, top), Offset(x, top + tick), w)
                    drawLine(ink, Offset(x, bottom - tick), Offset(x, bottom), w)
                }
            }
        }
    }
}

/** Ímã das marcas de uma régua: valor a menos de [tol] de uma marca gruda nela (háptico ao entrar). */
private class RulerSnap(private val haptic: () -> Unit) {
    var marks: FloatArray = FloatArray(0)
    private var active = -1

    fun apply(v: Float, tol: Float): Float {
        var best = -1
        var bestDistance = tol
        for (i in marks.indices) {
            val d = abs(marks[i] - v)
            if (d <= bestDistance) {
                best = i
                bestDistance = d
            }
        }
        if (best >= 0 && best != active) haptic()
        active = best
        return if (best >= 0) marks[best] else v
    }

    fun reset() {
        active = -1
    }
}

@Composable
internal fun ColumnScope.ScaleFace(env: PanelEnv, depth: Boolean) {
    val store = env.store
    val haptic = LocalHapticFeedback.current
    val d = store.detail ?: return
    val owner = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    val baseW = LayerGeometry.width(d)
    val baseH = LayerGeometry.height(d)
    // Sem tamanho de mídia (nulo, áudio): os campos ficam em %, como antes.
    val inPx = baseW > 0f && baseH > 0f && d.kind != LayerType.Audio.kind
    val unit = if (inPx) "px" else "%"
    val kind = d.kind
    val locked = store.scaleAxesLinked
    val enabled = !d.locked
    fun shownW(s: Float) = if (inPx) baseW * s else s * 100f
    fun shownH(s: Float) = if (inPx) baseH * s else s * 100f
    fun scaleOfW(v: Float) = if (inPx) v / baseW else v / 100f
    fun scaleOfH(v: Float) = if (inPx) v / baseH else v / 100f
    fun current(): FloatArray = store.detail?.scale?.let { floatArrayOf(it[0], it[1], it[2]) } ?: floatArrayOf(1f, 1f, 1f)
    fun write(axisH: Boolean, v: Float, from: FloatArray) {
        if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition) return
        val target = if (axisH) scaleOfH(v) else scaleOfW(v)
        if (!target.isFinite()) return
        if (store.scaleAxesLinked) {
            val base = if (axisH) from[1] else from[0]
            if (base == 0f) {
                if (depth) store.setScale3(floatArrayOf(target, target, target))
                else store.setTransform2(TrackProperty.SCALE_X, target, TrackProperty.SCALE_Y, target)
                return
            }
            val k = target / base
            // Volume: zoom uniforme sem esticar (regra do motor: Z de conteúdo acompanha X).
            if (depth) store.setScale3(AureaEngine.gestureScale3D(kind, from[0], from[1], from[2], 3, k))
            else store.setTransform2(TrackProperty.SCALE_X, from[0] * k, TrackProperty.SCALE_Y, from[1] * k)
        } else {
            store.setTransform(if (axisH) TrackProperty.SCALE_Y else TrackProperty.SCALE_X, target)
        }
    }

    val sx = d.scale[0]
    val sy = d.scale[1]
    val sz = d.scale[2]
    val widthLabel = stringResource(R.string.om_ap_tf_width)
    val heightLabel = stringResource(R.string.om_ap_tf_height)
    val linkLabel = stringResource(R.string.om_ap_tf_link)
    val scaleZLabel = stringResource(R.string.edt_scale_z)
    // O maior lado manda na régua única (a razão se mantém no arrasto travado).
    val leadH = abs(shownH(sy)) > abs(shownW(sx))

    // Marcas de encaixe: tamanhos das outras camadas no cabeçote (px).
    var marksW by remember(d.id) { mutableStateOf(FloatArray(0)) }
    var marksH by remember(d.id) { mutableStateOf(FloatArray(0)) }
    fun refreshMarks() {
        if (!inPx) return
        marksW = store.snapTargets(2).map { it.first }.filter { it.isFinite() && it > 0f }.toFloatArray()
        marksH = store.snapTargets(3).map { it.first }.filter { it.isFinite() && it > 0f }.toFloatArray()
    }
    LaunchedEffect(d.id, store.layers, inPx) { refreshMarks() }
    val snapW = remember(d.id) { RulerSnap { haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove) } }
    val snapH = remember(d.id) { RulerSnap { haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove) } }
    val from = remember(d.id) { FloatArray(3) }
    val unitsPerDp = if (inPx) max(baseW, baseH) * 0.005f else 0.5f
    val tolerance = 6f * unitsPerDp

    // Trocar o cadeado: as réguas "assentam" (escala Y 1,1 → 1,0 em 120 ms).
    val settle = remember { Animatable(1f) }
    var lastLocked by remember { mutableStateOf(locked) }
    LaunchedEffect(locked) {
        if (lastLocked != locked) {
            lastLocked = locked
            settle.snapTo(1.1f)
            settle.animateTo(1f, tween(120))
        }
    }

    fun keypadW() = env.openKeypad(KeypadRequest(widthLabel, shownW(sx), unit, Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
        write(false, it, floatArrayOf(sx, sy, sz))
    })
    fun keypadH() = env.openKeypad(KeypadRequest(heightLabel, shownH(sy), unit, Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
        write(true, it, floatArrayOf(sx, sy, sz))
    })

    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState())) {
        // No 3D (com o Z) a fileira pode passar da largura num telefone estreito: rola de lado.
        TfFieldRow(top = 15.dp, scroll = depth) {
            TfField(numeroPtBr(shownW(sx), 1), widthLabel, widthLabel, active = true, tag = "transform.scale.width",
                enabled = enabled, onLongClick = ::keypadW) { keypadW() }
            Spacer(Modifier.width(5.dp))
            Box(
                Modifier
                    .heightIn(min = 44.dp)
                    .testTag("transform.scale.link")
                    .semantics {
                        contentDescription = linkLabel
                        stateDescription = if (locked) "✓" else ""
                    }
                    .tocavel(enabled = enabled, shrink = 1f) { store.scaleAxesLinked = !store.scaleAxesLinked },
                contentAlignment = Alignment.TopCenter,
            ) {
                Box(
                    Modifier.size(25.dp).clip(RoundedCornerShape(TfFieldDims.Radius)).background(AureaColors.ControlButton),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Link, 15.dp, if (locked) AureaColors.Accent else AureaColors.Muted)
                }
            }
            Spacer(Modifier.width(5.dp))
            TfField(numeroPtBr(shownH(sy), 1), heightLabel, heightLabel, active = true, tag = "transform.scale.height",
                enabled = enabled, onLongClick = ::keypadH) { keypadH() }
            if (depth) {
                Spacer(Modifier.width(TfFieldDims.Gap))
                TfField("${numeroPtBr(sz * 100f, 1)}%", "Z", scaleZLabel, active = false, tag = "transform.scale.z", enabled = enabled) {
                    env.openKeypad(KeypadRequest(scaleZLabel, sz * 100f, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        val v = it / 100f
                        if (store.scaleAxesLinked && sz != 0f) store.setScale3(AureaEngine.gestureScale3D(kind, sx, sy, sz, 3, v / sz))
                        else store.setTransform(TrackProperty.SCALE_Z, v)
                    })
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        Column(Modifier.fillMaxWidth().padding(horizontal = 6.dp).graphicsLayer { scaleY = settle.value }) {
            if (locked) {
                val snap = if (leadH) snapH else snapW
                MarkedRuler(
                    value = { current().let { s -> if (leadH) shownH(s[1]) else shownW(s[0]) } },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    tag = "transform.scale.ruler",
                    description = if (leadH) heightLabel else widthLabel,
                    state = "${numeroPtBr(if (leadH) shownH(sy) else shownW(sx), 1)} $unit",
                    enabled = enabled,
                    marks = if (leadH) marksH else marksW,
                    onStart = {
                        current().copyInto(from)
                        refreshMarks()
                        snap.marks = if (leadH) marksH else marksW
                        snap.reset()
                        store.beginGesture("escala")
                    },
                    onValue = { v -> write(leadH, snap.apply(v, tolerance), from) },
                    onEnd = { store.endGesture() },
                    modifier = Modifier.fillMaxWidth().height(72.dp),
                )
            } else {
                MarkedRuler(
                    value = { shownW(current()[0]) },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    tag = "transform.scale.ruler",
                    description = widthLabel,
                    state = "${numeroPtBr(shownW(sx), 1)} $unit",
                    enabled = enabled,
                    marks = marksW,
                    onStart = {
                        current().copyInto(from)
                        refreshMarks()
                        snapW.marks = marksW
                        snapW.reset()
                        store.beginGesture("escala")
                    },
                    onValue = { v -> write(false, snapW.apply(v, tolerance), from) },
                    onEnd = { store.endGesture() },
                    modifier = Modifier.fillMaxWidth().height(52.dp),
                )
                Spacer(Modifier.height(6.dp))
                MarkedRuler(
                    value = { shownH(current()[1]) },
                    unitsPerDp = unitsPerDp,
                    active = false,
                    tag = "transform.scale.ruler.height",
                    description = heightLabel,
                    state = "${numeroPtBr(shownH(sy), 1)} $unit",
                    enabled = enabled,
                    marks = marksH,
                    onStart = {
                        current().copyInto(from)
                        refreshMarks()
                        snapH.marks = marksH
                        snapH.reset()
                        store.beginGesture("escala")
                    },
                    onValue = { v -> write(true, snapH.apply(v, tolerance), from) },
                    onEnd = { store.endGesture() },
                    modifier = Modifier.fillMaxWidth().height(52.dp),
                )
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

/**
 * Escala da CÂMERA: "Câmera: zoom 1,00×, ângulo 50,0°" (texto-3, 10 sp; toque
 * digita o ângulo) e a régua do ângulo de visão (grava a focal equivalente).
 */
@Composable
internal fun ColumnScope.CameraScaleFace(env: PanelEnv) {
    val store = env.store
    val lens = store.cameraLens ?: return
    val fov = lens[1]
    val zoom = CameraLens.zoomFromFocal(lens[0])
    val angleLabel = stringResource(R.string.om_ap_cam_view_angle)
    val summary = stringResource(R.string.om_ap_tf_camera_scale, CameraLens.formatZoom(zoom), CameraLens.formatFov(fov))
    fun writeFov(v: Float) {
        if (!v.isFinite()) return
        store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, CameraLens.focalFromFov(v.coerceIn(CameraLens.MIN_FOV, CameraLens.MAX_FOV)))
    }
    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState())) {
        Spacer(Modifier.height(15.dp))
        Text(
            summary,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 44.dp)
                .testTag("transform.scale.camera")
                .tocavel(shrink = 1f) {
                    env.openKeypad(KeypadRequest(angleLabel, fov, "°", CameraLens.MIN_FOV, CameraLens.MAX_FOV, 1) { writeFov(it) })
                }
                .padding(horizontal = 12.dp, vertical = 12.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Subtle)),
        )
        MarkedRuler(
            value = { store.cameraLens?.get(1) ?: fov },
            unitsPerDp = 0.25f,
            active = true,
            tag = "transform.scale.ruler",
            description = angleLabel,
            state = "${CameraLens.formatFov(fov)}°",
            min = CameraLens.MIN_FOV,
            max = CameraLens.MAX_FOV,
            enabled = store.detail?.locked != true,
            onStart = {
                if (store.playing || store.preview.buffering) store.pause()
                store.beginGesture("ângulo de visão")
            },
            onValue = ::writeFov,
            onEnd = { store.endGesture() },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 6.dp).height(72.dp),
        )
        Spacer(Modifier.height(8.dp))
    }
}

/**
 * INCLINAR: "Inclinação X" (destaque) e "Inclinação Y" (branco), 63 × 25,
 * `%.1f°`; duas réguas com a linha do zero, passo de 0,1°. Graus gravados nas
 * trilhas SkewX (13) / SkewY (14) — o ◆ da moldura grava as duas.
 */
@Composable
internal fun ColumnScope.SkewFace(env: PanelEnv) {
    val store = env.store
    val kx by remember(store) { derivedStateOf { store.detail?.skew?.getOrNull(0) ?: 0f } }
    val ky by remember(store) { derivedStateOf { store.detail?.skew?.getOrNull(1) ?: 0f } }
    val enabled by remember(store) { derivedStateOf { store.detail?.locked != true } }
    val xLabel = stringResource(R.string.om_ap_tf_skew_x)
    val yLabel = stringResource(R.string.om_ap_tf_skew_y)
    val owner = store.primary
    fun write(property: Int, v: Float) {
        if (store.primary != owner || !v.isFinite()) return
        store.setTransform(property, (v * 10f).roundToInt() / 10f)
    }
    fun keypad(label: String, value: Float, property: Int) = env.openKeypad(
        KeypadRequest(label, value, "°", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(property, it) })
    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState())) {
        TfFieldRow(top = 15.dp) {
            TfField("${numeroPtBr(kx, 1)}°", xLabel, xLabel, active = true, tag = "transform.skew.x", enabled = enabled,
                onLongClick = { keypad(xLabel, kx, TrackProperty.SKEW_X) }) { keypad(xLabel, kx, TrackProperty.SKEW_X) }
            Spacer(Modifier.width(TfFieldDims.Gap))
            TfField("${numeroPtBr(ky, 1)}°", yLabel, yLabel, active = false, tag = "transform.skew.y", enabled = enabled,
                onLongClick = { keypad(yLabel, ky, TrackProperty.SKEW_Y) }) { keypad(yLabel, ky, TrackProperty.SKEW_Y) }
        }
        Spacer(Modifier.height(8.dp))
        Column(Modifier.fillMaxWidth().padding(horizontal = 6.dp)) {
            MarkedRuler(
                value = { kx },
                unitsPerDp = 0.2f,
                active = true,
                tag = "transform.skew.ruler.x",
                description = xLabel,
                state = "${numeroPtBr(kx, 1)}°",
                zeroLine = true,
                enabled = enabled,
                onStart = {
                    if (store.playing || store.preview.buffering) store.pause()
                    store.beginGesture("inclinação")
                },
                onValue = { write(TrackProperty.SKEW_X, it) },
                onEnd = { store.endGesture() },
                modifier = Modifier.fillMaxWidth().height(52.dp),
            )
            Spacer(Modifier.height(6.dp))
            MarkedRuler(
                value = { ky },
                unitsPerDp = 0.2f,
                active = false,
                tag = "transform.skew.ruler.y",
                description = yLabel,
                state = "${numeroPtBr(ky, 1)}°",
                zeroLine = true,
                enabled = enabled,
                onStart = {
                    if (store.playing || store.preview.buffering) store.pause()
                    store.beginGesture("inclinação")
                },
                onValue = { write(TrackProperty.SKEW_Y, it) },
                onEnd = { store.endGesture() },
                modifier = Modifier.fillMaxWidth().height(52.dp),
            )
        }
        Spacer(Modifier.height(8.dp))
    }
}
