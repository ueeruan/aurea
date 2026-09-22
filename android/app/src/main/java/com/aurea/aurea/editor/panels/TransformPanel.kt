package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AspectRatio
import androidx.compose.material.icons.rounded.FilterCenterFocus
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.LinkOff
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.automirrored.rounded.RotateRight
import androidx.compose.material.icons.rounded.Transform
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * As faces de Transformar [A] (`ModoDeTransformacao`), na ordem do trilho
 * direito. [title] vai para o cabeçalho ("Transformar · Posição"); [props] é o
 * grupo que o losango do trilho crava.
 */
enum class TransformTab(val title: String, val icon: ImageVector, val railLabel: String, val props: IntArray) {
    Mover("Posição", Icons.Rounded.OpenWith, "Mover", intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y)),
    Girar("Rotação", Icons.AutoMirrored.Rounded.RotateRight, "Girar", intArrayOf(TrackProperty.ROTATION_Z)),
    Escalar("Escala", Icons.Rounded.AspectRatio, "Escalar", intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y)),
    Inclinar("Inclinação", Icons.Rounded.Transform, "Inclinar", intArrayOf(TrackProperty.SKEW_X, TrackProperty.SKEW_Y)),
    Pivo("Pivô", Icons.Rounded.FilterCenterFocus, "Pivô", intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y)),
}

/**
 * O PAINEL DE TRANSFORMAÇÃO [A] (`PainelDeTransformacao`): trilho esquerdo
 * (‹ · ◇ · curva · ⋯), miolo com os campos no topo e UMA superfície por face
 * (almofada, dial, fitas) e o trilho direito com as faces. Nenhum deslizante:
 * posição é 2D, ângulo é circular, escala não tem intervalo natural.
 */
@Composable
internal fun TransformPanel(env: PanelEnv, tab: TransformTab, onTab: (TransformTab) -> Unit) {
    val store = env.store
    var menu by remember { mutableStateOf(false) }
    val props = tab.props
    val look by remember(store, tab) { derivedStateOf { transformLook(store.detail, props) } }
    val curveReady by remember(store, tab) { derivedStateOf { store.primaryKeys().transformTrack(props[0]).size >= 2 } }
    val canKey = tab != TransformTab.Inclinar

    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = if (canKey) ({ store.toggleTransformKeyframe(props) }) else null,
            curveAnimated = look != com.aurea.aurea.ui.ds.KeyframeLook.None,
            onCurve = if (curveReady) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        store.primaryKeys().transformTrack(props[0]).segmentStart(t)?.let { key ->
                            store.selectKeyframe(layer, key)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
            more = { RailMoreButton(active = tab == TransformTab.Pivo) { menu = true } },
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            when (tab) {
                TransformTab.Mover -> MoveFace(env, pivot = false)
                TransformTab.Girar -> {
                    Spacer(Modifier.height(44.dp))
                    Box(Modifier.weight(1f).fillMaxWidth()) { RotationDial(env) }
                }
                TransformTab.Escalar -> ScaleFace(env)
                TransformTab.Inclinar -> Unit
                TransformTab.Pivo -> MoveFace(env, pivot = true)
            }
            Spacer(Modifier.height(10.dp))
        }
        RightRail(
            modes = TransformTab.entries.map { RailMode(it.icon, it.railLabel) },
            selected = tab.ordinal,
            onSelect = { i ->
                val t = TransformTab.entries[i]
                // O motor novo ainda não tem inclinação: diz, não finge.
                if (t == TransformTab.Inclinar) store.comingSoon("Inclinação") else onTab(t)
            },
        )
    }

    if (menu) {
        AureaActionSheet(
            title = "Transformar",
            actions = buildList {
                add(SheetAction("Transformação 3D") { store.comingSoon("Transformação 3D") })
                if (tab == TransformTab.Mover) add(SheetAction("Vincular posição") { store.comingSoon("Vincular posição") })
                add(SheetAction("Auto-key") { store.comingSoon("Auto-key") })
                add(SheetAction("Keyframe anterior") { store.pause(); store.stepToKeyframe(-1) })
                add(SheetAction("Próximo keyframe") { store.pause(); store.stepToKeyframe(1) })
                add(SheetAction("Resetar propriedade") { resetTab(env, tab) })
                add(SheetAction("Editar pivô") { onTab(TransformTab.Pivo) })
                add(SheetAction("Opacidade") { env.onOpenPanel(EditorPanel.Appearance) })
            },
            onDismiss = { menu = false },
        )
    }
}

/** "Resetar propriedade": posição no centro da composição, rotação 0, escala 100 %, pivô no centro da mídia. */
private fun resetTab(env: PanelEnv, tab: TransformTab) {
    val store = env.store
    val d = store.detail ?: return
    when (tab) {
        TransformTab.Mover -> store.setTransform2(
            TrackProperty.POSITION_X, store.project.width / 2f, TrackProperty.POSITION_Y, store.project.height / 2f,
        )
        TransformTab.Girar -> store.setTransform(TrackProperty.ROTATION_Z, 0f)
        TransformTab.Escalar -> store.setTransform2(TrackProperty.SCALE_X, 1f, TrackProperty.SCALE_Y, 1f)
        TransformTab.Pivo -> store.setTransform2(
            TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f,
        )
        TransformTab.Inclinar -> store.comingSoon("Inclinação")
    }
}

// =============================================================================
// Mover e Pivô: a almofada com cantos em L
// =============================================================================

/**
 * MOVER (e PIVÔ): almofada relativa — 1 dp de dedo vale `largura/360` px da
 * composição. No Mover, os campos x · y · z moram DENTRO dos cantos (cabeçalho
 * da almofada) e o toque em z troca o arrasto para profundidade; no Pivô eles
 * ficam na fileira de cima com o botão "Centro".
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MoveFace(env: PanelEnv, pivot: Boolean) {
    val store = env.store
    var zMode by rememberSaveable { mutableStateOf(false) }
    var dragging by remember { mutableStateOf(false) }
    val x by remember(store, pivot) {
        derivedStateOf { store.detail?.let { if (pivot) it.anchor[0] - it.sourceWidth / 2f else it.position[0] } ?: 0f }
    }
    val y by remember(store, pivot) {
        derivedStateOf { store.detail?.let { if (pivot) it.anchor[1] - it.sourceHeight / 2f else it.position[1] } ?: 0f }
    }
    val z by remember(store) { derivedStateOf { store.detail?.position?.get(2) ?: 0f } }

    val fields: @Composable () -> Unit = {
        Row(verticalAlignment = Alignment.Top, horizontalArrangement = Arrangement.Center) {
            ValueBox(numeroPtBr(x, 1), width = 61.dp, label = "x", onTap = {
                val d = store.detail ?: return@ValueBox
                env.openKeypad(
                    KeypadRequest(if (pivot) "Pivô em X" else "Posição X", x, "", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_X, it + d.sourceWidth / 2f)
                        else store.setTransform(TrackProperty.POSITION_X, it)
                    },
                )
            })
            Spacer(Modifier.width(6.dp))
            ValueBox(numeroPtBr(y, 1), width = 61.dp, label = "y", onTap = {
                val d = store.detail ?: return@ValueBox
                env.openKeypad(
                    KeypadRequest(if (pivot) "Pivô em Y" else "Posição Y", y, "", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_Y, it + d.sourceHeight / 2f)
                        else store.setTransform(TrackProperty.POSITION_Y, it)
                    },
                )
            })
            Spacer(Modifier.width(14.dp))
            if (pivot) {
                Box(
                    Modifier
                        .height(30.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(AureaColors.ControlButton)
                        .tocavel {
                            val d = store.detail ?: return@tocavel
                            store.setTransform2(TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f)
                        }
                        .padding(horizontal = 14.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text("Centro", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = Color.White)))
                }
            } else {
                // z: o toque ESCOLHE a profundidade para o arrasto; segurar digita.
                ValueBox(
                    numeroPtBr(z, 1),
                    width = 61.dp,
                    label = "z",
                    color = if (zMode) AureaColors.Accent else Color.White,
                    onLongPress = {
                        env.openKeypad(KeypadRequest("Profundidade Z", z, "", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                            store.setTransform(TrackProperty.POSITION_Z, it)
                        })
                    },
                    onTap = { zMode = !zMode },
                )
            }
        }
    }

    if (pivot) {
        Box(Modifier.fillMaxWidth().height(44.dp), contentAlignment = Alignment.Center) { fields() }
    }
    val hint = when {
        pivot -> "Deslize o ponto de giro · o botão Centro devolve o zero"
        zMode -> "Deslize para ajustar Z · toque em Z para voltar a X/Y"
        else -> "Deslize para mover X/Y · toque em Z para profundidade"
    }
    val zNow by rememberUpdatedState(zMode)
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .pointerInput(store, pivot) {
                var startX = 0f
                var startY = 0f
                var startZ = 0f
                var acc = Offset.Zero
                var gain = 1f
                detectDragGestures(
                    onDragStart = {
                        val d = store.detail
                        if (d != null) {
                            startX = if (pivot) d.anchor[0] else d.position[0]
                            startY = if (pivot) d.anchor[1] else d.position[1]
                            startZ = d.position[2]
                        }
                        acc = Offset.Zero
                        // O GANHO da A.01: 360 dp de dedo atravessam a largura da composição.
                        gain = max(1, store.project.width) / 360f
                        dragging = true
                        store.beginGesture(if (pivot) "mover pivô" else "mover")
                    },
                    onDragEnd = { dragging = false; store.endGesture() },
                    onDragCancel = { dragging = false; store.endGesture() },
                ) { change, delta ->
                    change.consume()
                    acc += Offset(delta.x / density, delta.y / density)
                    if (pivot) {
                        store.setTransform2(TrackProperty.ANCHOR_X, startX + acc.x * gain, TrackProperty.ANCHOR_Y, startY + acc.y * gain)
                        return@detectDragGestures
                    }
                    if (zNow) {
                        store.setTransform(TrackProperty.POSITION_Z, startZ - acc.y * gain)
                        return@detectDragGestures
                    }
                    var tx = startX + acc.x * gain
                    var ty = startY + acc.y * gain
                    // Gesto quase reto segue o eixo inicial; perto do centro, encaixa.
                    if (abs(acc.x) > 12f && abs(acc.y) < 6f) ty = startY
                    else if (abs(acc.y) > 12f && abs(acc.x) < 6f) tx = startX
                    val cx = store.project.width / 2f
                    val cy = store.project.height / 2f
                    if (abs(tx - cx) < 5f * gain) tx = cx
                    if (abs(ty - cy) < 5f * gain) ty = cy
                    store.setTransform2(TrackProperty.POSITION_X, tx, TrackProperty.POSITION_Y, ty)
                }
            },
    ) {
        CornerMarks(Modifier.fillMaxSize())
        if (!pivot) {
            Box(Modifier.fillMaxWidth().padding(top = 10.dp, start = 16.dp, end = 16.dp), contentAlignment = Alignment.TopCenter) { fields() }
        }
        Text(
            hint,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .align(Alignment.Center)
                .padding(top = if (pivot) 0.dp else 36.dp, start = 16.dp, end = 16.dp),
            // A instrução some enquanto o dedo arrasta (pela cor: nada é remedido).
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (dragging) Color.Transparent else AureaColors.Muted)),
        )
    }
}

/**
 * OS QUATRO CANTOS EM L (`_PintorDosCantos`): folga 12, braço 26 (encolhe em área
 * pequena), traço 2 muted a 45 %, pontas redondas. Canto, e não caixa fechada.
 */
@Composable
private fun CornerMarks(modifier: Modifier) {
    Canvas(modifier) {
        val gap = 12.dp.toPx()
        val l = gap
        val r = size.width - gap
        val t = gap
        val b = size.height - gap
        if (r <= l || b <= t) return@Canvas
        val arm = min(26.dp.toPx(), min((r - l) / 2f, (b - t) / 2f))
        val p = Path().apply {
            moveTo(l + arm, t); lineTo(l, t); lineTo(l, t + arm)
            moveTo(r - arm, t); lineTo(r, t); lineTo(r, t + arm)
            moveTo(l + arm, b); lineTo(l, b); lineTo(l, b - arm)
            moveTo(r - arm, b); lineTo(r, b); lineTo(r, b - arm)
        }
        drawPath(p, AureaColors.Muted.copy(alpha = 0.45f), style = Stroke(2.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
    }
}

// =============================================================================
// Girar: o dial
// =============================================================================

/** Raio do botão do dial (alvo grande: o dedo cobre o próprio alvo). */
private const val KNOB_DP = 15f

/** O miolo não tem ângulo: perto do centro um tremor vira dezenas de graus. */
private const val DEAD_ZONE_DP = 22f

/**
 * O DIAL [A] (`DialDeAngulo`): anel `#2E3548`, arco `destaque` do 0 até o botão,
 * botão branco de raio 15 e o ângulo no centro. O gesto ACUMULA o quanto o dedo
 * girou (o ângulo não enrola: 361° é mais que uma volta); o toque seco no anel
 * põe o ângulo tocado na volta em que a camada já está.
 */
@Composable
private fun RotationDial(env: PanelEnv) {
    val store = env.store
    val angle by remember(store) { derivedStateOf { store.detail?.rotation?.get(2) ?: 0f } }
    val current by rememberUpdatedState(angle)
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .pointerInput(store) {
                awaitEachGesture {
                    val center = Offset(size.width / 2f, size.height / 2f)
                    val dead = DEAD_ZONE_DP * density
                    fun raw(p: Offset) = (atan2(p.y - center.y, p.x - center.x) * 180.0 / Math.PI).toFloat()
                    val down = awaitFirstDown(requireUnconsumed = false)
                    var last = down.position
                    var walked = 0f
                    var prevRaw: Float? = if ((down.position - center).getDistance() >= dead) raw(down.position) else null
                    var total = if (current.isFinite()) current else 0f
                    var began = false
                    while (true) {
                        val ev = awaitPointerEvent()
                        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                        if (!ch.pressed) break
                        val p = ch.position
                        walked += (p - last).getDistance()
                        last = p
                        ch.consume()
                        if ((p - center).getDistance() < dead) {
                            prevRaw = null   // sair do miolo recomeça a conta
                            continue
                        }
                        val r = raw(p)
                        val before = prevRaw
                        prevRaw = r
                        if (before == null) continue
                        var step = r - before
                        if (step > 180f) step -= 360f
                        if (step < -180f) step += 360f
                        if (step == 0f) continue
                        if (!began) {
                            began = true
                            store.beginGesture("girar")
                        }
                        total += step
                        store.setTransform(TrackProperty.ROTATION_Z, total)
                    }
                    if (began) {
                        store.endGesture()
                    } else if (walked < 2f * density && (last - center).getDistance() >= dead) {
                        // Toque seco: o ângulo apontado, na volta atual.
                        val turns = floor(current / 360f)
                        store.setTransform(TrackProperty.ROTATION_Z, turns * 360f + raw(last))
                    }
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val knob = KNOB_DP.dp.toPx()
            val radius = max(0f, (min(size.width, size.height) - (knob * 2 + 2.dp.toPx())) / 2f)
            if (radius <= 0f) return@Canvas
            val c = Offset(size.width / 2f, size.height / 2f)
            drawCircle(AureaColors.DialTrack, radius, c, style = Stroke(2.dp.toPx()))
            val g = if (angle.isFinite()) angle else 0f
            if (abs(g) > 0.5f) {
                val sweep = if (abs(g) > 360f) 360f else g
                drawArc(
                    color = AureaColors.Accent,
                    startAngle = 0f,
                    sweepAngle = sweep,
                    useCenter = false,
                    topLeft = Offset(c.x - radius, c.y - radius),
                    size = Rect(c, radius).size,
                    style = Stroke(3.5.dp.toPx(), cap = StrokeCap.Round),
                )
            }
            val rad = Math.toRadians(g.toDouble())
            drawCircle(Color.White, knob, Offset(c.x + cos(rad).toFloat() * radius, c.y + sin(rad).toFloat() * radius))
        }
        // O número no centro: casa decimal só quando existe ("45°", "45,5°").
        val tenth = (angle * 10f).roundToInt() / 10f
        val text = "${numeroPtBr(tenth, if (tenth == tenth.roundToInt().toFloat()) 0 else 1)}°"
        Box(
            Modifier
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.DialValueBox)
                .tocavel(shrink = 1f) {
                    env.openKeypad(KeypadRequest("Rotação", angle, "°", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        store.setTransform(TrackProperty.ROTATION_Z, it)
                    })
                }
                .padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            Text(
                text,
                style = AureaType.Base.merge(TextStyle(fontSize = 24.sp, fontWeight = FontWeight.W700, color = AureaColors.Accent, fontFeatureSettings = "tnum")),
            )
        }
    }
}

// =============================================================================
// Escalar: duas fitas e a corrente
// =============================================================================

/**
 * ESCALAR [A] (05 / 08): `Largura 🔗 Altura` no topo, "Preencher / Ajustar" em
 * foto e vídeo, e DUAS fitas (Largura com o centro aceso, Altura com o centro
 * branco). Com a corrente travada o arrasto escala PROPORCIONAL (a razão X/Y se
 * mantém — o bug B-31 era escrever o mesmo número nos dois eixos e o campo
 * mentir); solta, cada fita cuida do seu eixo. 0,5 %/dp.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleFace(env: PanelEnv) {
    val store = env.store
    var locked by rememberSaveable { mutableStateOf(true) }
    val sx by remember(store) { derivedStateOf { (store.detail?.scale?.get(0) ?: 1f) * 100f } }
    val sy by remember(store) { derivedStateOf { (store.detail?.scale?.get(1) ?: 1f) * 100f } }
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val lockNow by rememberUpdatedState(locked)

    fun write(axisY: Boolean, v: Float, fromX: Float, fromY: Float) {
        if (lockNow) {
            val from = if (axisY) fromY else fromX
            val k = if (from != 0f) v / from else 1f
            val nx = if (axisY) (if (from != 0f) fromX * k else v) else v
            val ny = if (axisY) v else (if (from != 0f) fromY * k else v)
            store.setTransform2(TrackProperty.SCALE_X, nx / 100f, TrackProperty.SCALE_Y, ny / 100f)
        } else {
            store.setTransform(if (axisY) TrackProperty.SCALE_Y else TrackProperty.SCALE_X, v / 100f)
        }
    }

    Row(Modifier.fillMaxWidth().height(44.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
        ValueBox("${numeroPtBr(sx, 1)}%", width = 61.dp, label = "Largura", onTap = {
            env.openKeypad(KeypadRequest("Largura", sx, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(false, it, sx, sy) })
        })
        Box(
            Modifier
                .padding(horizontal = 5.dp)
                .size(width = 34.dp, height = 24.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.ControlButton)
                .tocavel(shrink = 1f) { locked = !locked },
            contentAlignment = Alignment.Center,
        ) {
            Icon(if (locked) Icons.Rounded.Link else Icons.Rounded.LinkOff, contentDescription = if (locked) "Soltar largura e altura" else "Travar largura e altura", tint = Color.White, modifier = Modifier.size(16.dp))
        }
        ValueBox("${numeroPtBr(sy, 1)}%", width = 61.dp, label = "Altura", color = Color.White, onTap = {
            env.openKeypad(KeypadRequest("Altura", sy, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(true, it, sx, sy) })
        })
    }
    if (kind == LayerType.Video.kind || kind == LayerType.Image.kind) {
        MediaFitChips(env)
        Spacer(Modifier.height(6.dp))
    }
    val both = { Pair(sx, sy) }
    ScaleTape(active = true, value = { sx }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy -> write(false, v, fx, fy) }
    Spacer(Modifier.height(8.dp))
    ScaleTape(active = false, value = { sy }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy -> write(true, v, fx, fy) }
}

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleTape(
    active: Boolean,
    value: () -> Float,
    both: () -> Pair<Float, Float>,
    onStart: () -> Unit,
    onEnd: () -> Unit,
    onValue: (v: Float, fromX: Float, fromY: Float) -> Unit,
) {
    // A foto dos DOIS eixos no início do arrasto: a escala proporcional parte dela
    // (partir do valor que volta do motor a cada passo acumularia o arredondamento).
    var from by remember { mutableStateOf(Pair(0f, 0f)) }
    val read by rememberUpdatedState(value)
    val snap by rememberUpdatedState(both)
    val send by rememberUpdatedState(onValue)
    val begin by rememberUpdatedState(onStart)
    val end by rememberUpdatedState(onEnd)
    TickRuler(
        value = value,
        unitsPerDp = 0.5f,
        active = active,
        modifier = Modifier
            .weight(1f)
            .fillMaxWidth()
            .valueDrag(
                enabled = true,
                start = { read() },
                unitsPerDp = { 0.5f },
                min = Float.NEGATIVE_INFINITY,
                max = Float.POSITIVE_INFINITY,
                onStart = {
                    from = snap()
                    begin()
                },
                onValue = { v -> send(v, from.first, from.second) },
                onEnd = { end() },
            ),
    )
}

/**
 * PREENCHER / AJUSTAR (só foto e vídeo): um toque põe a mídia cobrindo a
 * composição inteira ou cabendo inteira nela, no centro — UM passo de desfazer.
 * Aceso quando a escala atual já é aquela.
 */
@Composable
private fun MediaFitChips(env: PanelEnv) {
    val store = env.store
    val fit by remember(store) {
        derivedStateOf {
            val d = store.detail
            val cw = store.project.width.toFloat()
            val ch = store.project.height.toFloat()
            if (d == null || d.sourceWidth <= 0 || d.sourceHeight <= 0 || cw <= 0f || ch <= 0f) {
                null
            } else {
                val cover = max(cw / d.sourceWidth, ch / d.sourceHeight)
                val contain = min(cw / d.sourceWidth, ch / d.sourceHeight)
                val s = d.scale
                Triple(cover, contain, if (abs(s[0] - cover) < 1e-3f && abs(s[1] - cover) < 1e-3f) 0 else if (abs(s[0] - contain) < 1e-3f && abs(s[1] - contain) < 1e-3f) 1 else -1)
            }
        }
    }
    val f = fit ?: return
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
        listOf("Preencher" to f.first, "Ajustar" to f.second).forEachIndexed { i, (label, scale) ->
            val on = f.third == i
            Box(
                Modifier
                    .padding(horizontal = 4.dp)
                    .height(30.dp)
                    .clip(RoundedCornerShape(15.dp))
                    .background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.RailModeFill)
                    .tocavel {
                        store.beginGesture(label.lowercase())
                        store.setTransform2(TrackProperty.SCALE_X, scale, TrackProperty.SCALE_Y, scale)
                        store.setTransform2(TrackProperty.POSITION_X, store.project.width / 2f, TrackProperty.POSITION_Y, store.project.height / 2f)
                        store.endGesture()
                    }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}
