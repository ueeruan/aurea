package com.aurea.aurea.editor.panels

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.PivotDragSession
import com.aurea.aurea.editor.PivotStage
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

// =============================================================================
// Omega aparência (área 05, P0-2 / P1-2): MOVER e PIVÔ.
//
// Mover: pílula X | Y + Z no topo (o campo INATIVO escolhe o modo XY ou Z; o
// ATIVO abre o teclado; segurar digita), trackpad com guia no centro, cantos e
// "Preciso". O trackpad tem ímã grudento (alvos do motor, patamar de 25 px de
// dedo), empurrão de 1 px (toque rápido numa faixa da borda) e aceleração
// opcional (⋯). Com Mover aberto o palco mostra a trajetória.
// Pivô: os campos, "Centro" e a grade de âncora numa fileira; trackpad próprio.
// =============================================================================

/**
 * Preferências do trackpad, do aparelho (não do projeto). Chaves lógicas iguais
 * às do iOS (`@AppStorage`): `omega.transform.snap` (ímã, padrão ligado) e
 * `omega.transform.accelerate` (aceleração, padrão desligada).
 */
internal object TransformPrefs {
    private const val FILE = "aurea.omega.transform"
    const val KEY_SNAP = "omega.transform.snap"
    const val KEY_ACCELERATE = "omega.transform.accelerate"
    private var prefs: SharedPreferences? = null

    var snap by mutableStateOf(true)
        private set
    var accelerate by mutableStateOf(false)
        private set

    fun load(context: Context) {
        if (prefs != null) return
        val p = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        prefs = p
        snap = p.getBoolean(KEY_SNAP, true)
        accelerate = p.getBoolean(KEY_ACCELERATE, false)
    }

    fun toggleSnap() {
        snap = !snap
        prefs?.edit()?.putBoolean(KEY_SNAP, snap)?.apply()
    }

    fun toggleAccelerate() {
        accelerate = !accelerate
        prefs?.edit()?.putBoolean(KEY_ACCELERATE, accelerate)?.apply()
    }
}

/** O que o arrasto do trackpad mexe. */
internal enum class PadMode { Move, Depth, Pivot }

/** Constantes do trackpad (as da referência). */
internal object PadDefaults {
    /** Patamar do ímã em px de DEDO (convertido para a unidade da posição pelo ganho). */
    const val SNAP_PLATEAU_PX = 25f
    /** Toque-empurrão: menos que isto de deslocamento (px). */
    const val NUDGE_SLOP_PX = 20f
    /** Velocidade amortecida da aceleração: 0,65 da anterior + 0,35 da nova. */
    const val SPEED_KEEP = 0.65f
    /** 360 dp de dedo atravessam a largura da composição (o ganho da A.01). */
    const val DP_PER_WIDTH = 360f
    /** "Preciso": o dedo anda 4× mais para o mesmo deslocamento. */
    const val FINE_GAIN = 0.25f
}

@Composable
internal fun ColumnScope.MoveFace(env: PanelEnv, zMode: Boolean, onZMode: (Boolean) -> Unit) {
    val store = env.store
    // Trajetória no palco enquanto Mover está aberto; as guias do ímã somem ao sair.
    DisposableEffect(store) {
        store.showMotionPath = true
        onDispose {
            store.showMotionPath = false
            store.snapGuideX = null
            store.snapGuideY = null
        }
    }
    val x by remember(store) { derivedStateOf { store.detail?.position?.get(0) ?: 0f } }
    val y by remember(store) { derivedStateOf { store.detail?.position?.get(1) ?: 0f } }
    val z by remember(store) { derivedStateOf { store.detail?.position?.get(2) ?: 0f } }
    val enabled by remember(store) { derivedStateOf { store.detail?.locked != true } }
    val xLabel = stringResource(R.string.panel_posicao_x)
    val yLabel = stringResource(R.string.panel_posicao_y)
    val zLabel = stringResource(R.string.panel_profundidade_z)
    fun keypad(label: String, value: Float, property: Int) = env.openKeypad(
        KeypadRequest(label, value, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 2) { store.setTransform(property, it) })
    TransformPad(env, if (zMode) PadMode.Depth else PadMode.Move, Modifier.weight(1f).fillMaxWidth()) {
        // Os campos moram DENTRO do trackpad: arrastar começando neles move a camada.
        TfFieldRow(top = PanelMetrics.FieldRowTop) {
            TfField(numeroPtBr(x, 2), "X", xLabel, active = !zMode, tag = "transform.position.x", shape = TfPillStart,
                enabled = enabled, onLongClick = { keypad(xLabel, x, TrackProperty.POSITION_X) }) {
                if (zMode) onZMode(false) else keypad(xLabel, x, TrackProperty.POSITION_X)
            }
            TfPillGap()
            TfField(numeroPtBr(y, 2), "Y", yLabel, active = !zMode, tag = "transform.position.y", shape = TfPillEnd,
                enabled = enabled, onLongClick = { keypad(yLabel, y, TrackProperty.POSITION_Y) }) {
                if (zMode) onZMode(false) else keypad(yLabel, y, TrackProperty.POSITION_Y)
            }
            Spacer(Modifier.width(TfFieldDims.Gap))
            TfField(numeroPtBr(z, 2), "Z", zLabel, active = zMode, tag = "transform.position.z",
                enabled = enabled, onLongClick = { keypad(zLabel, z, TrackProperty.POSITION_Z) }) {
                if (!zMode) onZMode(true) else keypad(zLabel, z, TrackProperty.POSITION_Z)
            }
        }
    }
}

@Composable
internal fun ColumnScope.PivotFace(env: PanelEnv, depth: Boolean) {
    val store = env.store
    // Face Pivô aberta: o palco passa a mover o pivô (Stage.kt `pivotMode`).
    DisposableEffect(Unit) {
        PivotStage.active = true
        onDispose { PivotStage.active = false }
    }
    // Pivô em px relativos ao centro da mídia (o "Centro" devolve o zero).
    val x by remember(store) { derivedStateOf { store.detail?.let { it.anchor[0] - it.sourceWidth / 2f } ?: 0f } }
    val y by remember(store) { derivedStateOf { store.detail?.let { it.anchor[1] - it.sourceHeight / 2f } ?: 0f } }
    val z by remember(store) { derivedStateOf { store.detail?.anchor?.get(2) ?: 0f } }
    val enabled by remember(store) { derivedStateOf { store.detail?.locked != true } }
    val xLabel = stringResource(R.string.panel_pivo_x)
    val yLabel = stringResource(R.string.panel_pivo_y)
    val zLabel = stringResource(R.string.edt_pivot_z)
    fun keypad(label: String, value: Float, component: Int) = env.openKeypad(
        KeypadRequest(label, value, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 2) { v ->
            store.detail?.let { d ->
                when (component) {
                    0 -> store.setTransform(TrackProperty.ANCHOR_X, v + d.sourceWidth / 2f)
                    1 -> store.setTransform(TrackProperty.ANCHOR_Y, v + d.sourceHeight / 2f)
                    else -> store.setTransform(TrackProperty.ANCHOR_Z, v)
                }
            }
        })
    Column(Modifier.weight(1f).fillMaxWidth().background(AureaColors.Surface)) {
        TfFieldRow(top = PanelMetrics.FieldRowTop, scroll = true) {
            TfField(numeroPtBr(x, 2), "X", xLabel, active = true, tag = "transform.pivot.x", shape = TfPillStart,
                enabled = enabled, onLongClick = { keypad(xLabel, x, 0) }) { keypad(xLabel, x, 0) }
            TfPillGap()
            TfField(numeroPtBr(y, 2), "Y", yLabel, active = true, tag = "transform.pivot.y", shape = TfPillEnd,
                enabled = enabled, onLongClick = { keypad(yLabel, y, 1) }) { keypad(yLabel, y, 1) }
            if (depth) {
                Spacer(Modifier.width(TfFieldDims.Gap))
                TfField(numeroPtBr(z, 2), "Z", zLabel, active = false, tag = "transform.pivot.z",
                    enabled = enabled, onLongClick = { keypad(zLabel, z, 2) }) { keypad(zLabel, z, 2) }
            }
            Spacer(Modifier.width(TfFieldDims.Gap))
            val centerLabel = stringResource(R.string.panel_centro)
            Box(
                Modifier
                    .heightIn(min = PanelMetrics.Touch)
                    .testTag("transform.pivot.center")
                    .semantics { contentDescription = centerLabel }
                    .tocavel(enabled = enabled, shrink = 1f) {
                        val d = store.detail ?: return@tocavel
                        store.setTransform2(TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f)
                    },
                contentAlignment = Alignment.TopCenter,
            ) {
                Box(
                    Modifier.heightIn(min = TfFieldDims.Height).clip(RoundedCornerShape(TfFieldDims.Radius))
                        .background(AureaColors.ControlButton).padding(horizontal = PanelMetrics.ChipPad),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(centerLabel, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.ChipText, fontWeight = FontWeight.W600, color = AureaColors.Text)))
                }
            }
            Spacer(Modifier.width(TfFieldDims.Gap))
            // Âncora predefinida (app antigo): 9 pontos da mídia; a camada fica no lugar.
            AnchorPresetGrid { fx, fy -> store.primary?.let { com.aurea.aurea.editor.LayerOps.presetAnchor(store, it, fx, fy) } }
        }
        TransformPad(env, PadMode.Pivot, Modifier.weight(1f).fillMaxWidth())
    }
}

/**
 * O TRACKPAD (Mover, Z e Pivô): fundo `superfície`, cantos em L, guia no centro
 * (some durante o arrasto) e, fora do Pivô, o "Preciso" no canto. [header]
 * desenha por cima (os campos do Mover).
 */
@Composable
internal fun TransformPad(
    env: PanelEnv,
    mode: PadMode,
    modifier: Modifier = Modifier,
    header: @Composable BoxScope.() -> Unit = {},
) {
    val store = env.store
    val haptic = LocalHapticFeedback.current
    var dragging by remember { mutableStateOf(false) }
    var fine by rememberSaveable { mutableStateOf(false) }
    val fineNow by rememberUpdatedState(fine)
    val snapNow by rememberUpdatedState(TransformPrefs.snap)
    val accelerateNow by rememberUpdatedState(TransformPrefs.accelerate)
    val controlled = when (mode) {
        PadMode.Pivot -> listOf(TrackKey(TrackProperty.ANCHOR_X), TrackKey(TrackProperty.ANCHOR_Y), TrackKey(TrackProperty.POSITION_X), TrackKey(TrackProperty.POSITION_Y))
        PadMode.Depth -> listOf(TrackKey(TrackProperty.POSITION_Z))
        PadMode.Move -> listOf(TrackKey(TrackProperty.POSITION_X), TrackKey(TrackProperty.POSITION_Y))
    }
    val driven = store.expressionLook(controlled).let { it == ExpressionLook.On || it == ExpressionLook.Error }
    val notice = stringResource(R.string.transform_expression_controlled)
    val noticeNow by rememberUpdatedState(if (driven) notice else null)
    val hint = when {
        driven -> notice
        mode == PadMode.Pivot -> stringResource(R.string.om_ap_tf_pivot_hint)
        mode == PadMode.Depth -> stringResource(R.string.om_ap_tf_depth_hint)
        else -> stringResource(R.string.om_ap_tf_move_hint)
    }
    val nudgeHint = stringResource(R.string.om_ap_tf_nudge)
    Box(
        modifier
            .background(AureaColors.Surface)
            .testTag(if (mode == PadMode.Pivot) "transform.pivot.pad" else "transform.move.pad")
            .semantics { contentDescription = "$hint. $nudgeHint" }
            .pointerInput(store, mode) {
                padGestures(
                    store = store,
                    mode = mode,
                    fine = { fineNow },
                    snap = { snapNow },
                    accelerate = { accelerateNow },
                    notice = { noticeNow },
                    onDragging = { dragging = it },
                    haptic = { haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove) },
                )
            },
    ) {
        CornerMarks(Modifier.matchParentSize())
        header()
        // A instrução some enquanto o dedo arrasta (pela cor: nada é remedido).
        val ink = if (driven) AureaColors.Text else if (dragging) Color.Transparent else AureaColors.Muted
        Column(
            // 0.3.4 painéis: abaixo da fileira de campos (12 + 36 + rótulo), dica de 14 sp.
            Modifier.align(Alignment.Center).padding(top = if (mode == PadMode.Pivot) 0.dp else 56.dp, start = 16.dp, end = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (mode == PadMode.Depth && !driven) CupertinoIcon(CupertinoGlyph.ArrowUp, 18.dp, ink)
            Text(
                hint,
                textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Hint, lineHeight = 19.sp, color = ink)),
            )
            if (mode == PadMode.Depth && !driven) CupertinoIcon(CupertinoGlyph.ArrowDown, 18.dp, ink)
        }
        if (mode != PadMode.Pivot) {
            // Preciso (extra do AUREA): 44 dp de toque, 28 de desenho.
            val fineLabel = stringResource(R.string.panel_mover_preciso)
            Box(
                Modifier
                    .align(Alignment.BottomEnd)
                    .padding(end = 10.dp, bottom = 4.dp)
                    .heightIn(min = PanelMetrics.Touch)
                    .testTag("transform.move.fine")
                    .semantics { contentDescription = fineLabel; stateDescription = if (fine) "✓" else "" }
                    .tocavel(shrink = 1f) { fine = !fine },
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    Modifier
                        .heightIn(min = PanelMetrics.ChipHeight)
                        .clip(RoundedCornerShape(PanelMetrics.ChipHeight / 2))
                        .background(if (fine) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.RailModeFill)
                        .padding(horizontal = PanelMetrics.ChipPad),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        fineLabel,
                        maxLines = 1,
                        style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.ChipText, fontWeight = FontWeight.W600, color = if (fine) AureaColors.Accent else AureaColors.Text)),
                    )
                }
            }
        }
    }
}

/**
 * O gesto do trackpad: arrasto (depois do *touch slop*) = um passo de desfazer;
 * toque rápido (< tapTimeout e < 20 px) numa faixa de ¼ da borda = empurrão de
 * 1 px. Um toque que um campo por cima já tratou (consumido) não empurra.
 */
private suspend fun PointerInputScope.padGestures(
    store: EditorStore,
    mode: PadMode,
    fine: () -> Boolean,
    snap: () -> Boolean,
    accelerate: () -> Boolean,
    notice: () -> String?,
    onDragging: (Boolean) -> Unit,
    haptic: () -> Unit,
) {
    val tapMs = android.view.ViewConfiguration.getTapTimeout().toLong()
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false)
        val pointer = down.id
        var total = Offset.Zero
        var drag: PadDrag? = null
        try {
            while (true) {
                val event = awaitPointerEvent()
                val change = event.changes.firstOrNull { it.id == pointer } ?: break
                if (!change.pressed) {
                    if (drag == null && !change.isConsumed && change.uptimeMillis - down.uptimeMillis < tapMs &&
                        total.getDistance() < PadDefaults.NUDGE_SLOP_PX) {
                        padNudge(store, mode, down.position, size.width.toFloat(), size.height.toFloat())
                    }
                    break
                }
                val delta = change.position - change.previousPosition
                total += delta
                if (drag == null && total.getDistance() > viewConfiguration.touchSlop) {
                    val started = PadDrag.start(store, mode, density, fine(), snap(), accelerate(), haptic) ?: break
                    drag = started
                    notice()?.let(store::showToast)
                    onDragging(true)
                }
                val active = drag
                if (active != null) {
                    change.consume()
                    active.move(delta, (change.uptimeMillis - change.previousUptimeMillis).coerceAtLeast(1L))
                }
            }
        } finally {
            val active = drag
            if (active != null) {
                active.end()
                onDragging(false)
            }
        }
    }
}

/** Empurrão de 1 px para o lado tocado (`AureaEngine.nudgeSide`: 1 esq, 2 cima, 3 dir, 4 baixo). */
private fun padNudge(store: EditorStore, mode: PadMode, at: Offset, width: Float, height: Float) {
    val d = store.detail ?: return
    if (d.locked) return
    val side = AureaEngine.nudgeSide(at.x, at.y, width, height)
    val dx = when (side) { 1 -> -1f; 3 -> 1f; else -> 0f }
    val dy = when (side) { 2 -> -1f; 4 -> 1f; else -> 0f }
    if (dx == 0f && dy == 0f) return
    if (mode == PadMode.Depth && dy == 0f) return
    if (store.playing || store.preview.buffering) store.pause()
    store.beginGesture("empurrão de 1 px")
    when (mode) {
        PadMode.Move -> store.setTransform2(TrackProperty.POSITION_X, d.position[0] + dx, TrackProperty.POSITION_Y, d.position[1] + dy)
        // Z: cima aproxima (como o arrasto para cima).
        PadMode.Depth -> store.setTransform(TrackProperty.POSITION_Z, d.position[2] - dy)
        PadMode.Pivot -> {
            val p = FloatArray(2)
            d.parentToComp(d.position[0], d.position[1], p)
            PivotDragSession(store, d, p[0], p[1]).moveTo(p[0] + dx, p[1] + dy)
        }
    }
    store.endGesture()
}

/**
 * Um eixo do ímã grudento: os alvos (crescentes, sem repetição) e a conta do
 * motor (`snapValueFromFinger`/`snapFingerFromValue`). O "dedo" anda livre; o
 * valor fica parado enquanto o dedo atravessa o patamar de um alvo.
 */
private class SnapAxis(
    pairs: List<Pair<Float, Float>>,
    private val plateau: Float,
    private val start: Float,
    private val haptic: () -> Unit,
) {
    private val values: FloatArray
    private val guides: FloatArray
    private val finger0: Float
    private var active = -1

    init {
        val sorted = pairs.filter { it.first.isFinite() && it.second.isFinite() }.sortedBy { it.first }
        val vs = ArrayList<Float>(sorted.size)
        val gs = ArrayList<Float>(sorted.size)
        for ((v, g) in sorted) {
            if (vs.isEmpty() || abs(v - vs[vs.size - 1]) > 1e-4f) {
                vs.add(v)
                gs.add(g)
            }
        }
        values = vs.toFloatArray()
        guides = gs.toFloatArray()
        finger0 = if (values.isEmpty() || !(plateau > 0f)) start else AureaEngine.snapFingerFromValue(values, plateau, start)
    }

    /** Valor para o dedo andado [offset] (unidades da posição); háptico ao entrar num patamar. */
    fun value(offset: Float): Float {
        if (values.isEmpty() || !(plateau > 0f)) {
            active = -1
            return start + offset
        }
        val r = AureaEngine.snapValueFromFinger(values, plateau, finger0 + offset)
        val index = if (r.size > 1) r[1].toInt() else -1
        if (index >= 0 && index != active) haptic()
        active = index
        return if (r.isNotEmpty() && r[0].isFinite()) r[0] else start + offset
    }

    fun release() {
        active = -1
    }

    /** Guia do alvo em que o dedo está (px da composição) ou nulo. */
    fun guide(): Float? = if (active in guides.indices) guides[active] else null
}

/**
 * Um arrasto do trackpad. Valores ABSOLUTOS a partir do começo (nada acumula o
 * arredondamento do motor); o dedo (px) passa pela aceleração opcional e vira
 * unidade da posição pelo ganho (largura da composição / 360 dp, × ¼ no Preciso).
 */
private class PadDrag private constructor(
    private val store: EditorStore,
    private val mode: PadMode,
    d: LayerDetail,
    private val density: Float,
    private val gainPerPx: Float,
    private val accelerate: Boolean,
    snap: Boolean,
    haptic: () -> Unit,
) {
    private val startX = d.position[0]
    private val startY = d.position[1]
    private val startZ = d.position[2]
    private val pivot: PivotDragSession? = if (mode == PadMode.Pivot) {
        val p = FloatArray(2)
        d.parentToComp(d.position[0], d.position[1], p)
        PivotDragSession(store, d, p[0], p[1])
    } else null
    private val plateau = PadDefaults.SNAP_PLATEAU_PX * gainPerPx
    private val snapX: SnapAxis? = if (snap && mode == PadMode.Move) SnapAxis(store.snapTargets(0), plateau, startX, haptic) else null
    private val snapY: SnapAxis? = if (snap && mode == PadMode.Move) SnapAxis(store.snapTargets(1), plateau, startY, haptic) else null
    private var accX = 0f
    private var accY = 0f
    private var speed = 0f

    fun move(delta: Offset, dtMs: Long) {
        val factor = if (accelerate) {
            val instant = delta.getDistance() / dtMs.toFloat() * 1000f
            speed = PadDefaults.SPEED_KEEP * speed + (1f - PadDefaults.SPEED_KEEP) * instant
            AureaEngine.padAcceleration(speed).takeIf { it.isFinite() && it > 0f } ?: 1f
        } else 1f
        accX += delta.x * factor
        accY += delta.y * factor
        when (mode) {
            PadMode.Pivot -> pivot?.let { it.moveTo(it.startX + accX * gainPerPx, it.startY + accY * gainPerPx) }
            PadMode.Depth -> store.setTransform(TrackProperty.POSITION_Z, startZ - accY * gainPerPx)
            PadMode.Move -> {
                // Gesto quase reto segue o eixo inicial (extra do AUREA).
                val dxDp = accX / density
                val dyDp = accY / density
                val holdY = abs(dxDp) > 12f && abs(dyDp) < 6f
                val holdX = abs(dyDp) > 12f && abs(dxDp) < 6f
                val tx = if (holdX) { snapX?.release(); startX } else snapX?.value(accX * gainPerPx) ?: (startX + accX * gainPerPx)
                val ty = if (holdY) { snapY?.release(); startY } else snapY?.value(accY * gainPerPx) ?: (startY + accY * gainPerPx)
                store.setTransform2(TrackProperty.POSITION_X, tx, TrackProperty.POSITION_Y, ty)
                store.snapGuideX = snapX?.guide()
                store.snapGuideY = snapY?.guide()
            }
        }
    }

    fun end() {
        store.snapGuideX = null
        store.snapGuideY = null
        store.endGesture()
    }

    companion object {
        fun start(
            store: EditorStore,
            mode: PadMode,
            density: Float,
            fine: Boolean,
            snap: Boolean,
            accelerate: Boolean,
            haptic: () -> Unit,
        ): PadDrag? {
            val d = store.detail ?: return null
            if (d.locked) return null
            if (store.playing || store.preview.buffering) store.pause()
            val perDp = max(1, store.project.width) / PadDefaults.DP_PER_WIDTH * (if (fine) PadDefaults.FINE_GAIN else 1f)
            store.beginGesture(when (mode) {
                PadMode.Pivot -> "mover pivô"
                PadMode.Depth -> "profundidade"
                PadMode.Move -> "mover"
            })
            return PadDrag(store, mode, d, density, perDp / density, accelerate, snap, haptic)
        }
    }
}

/** 3 × 3 pontos da caixa da mídia (âncora predefinida); o toque escolhe a célula. */
@Composable
private fun AnchorPresetGrid(onPick: (Float, Float) -> Unit) {
    val label = stringResource(R.string.panel_ancora_predefinida)
    Box(Modifier.heightIn(min = PanelMetrics.Touch), contentAlignment = Alignment.TopCenter) {
        Box(
            Modifier
                .size(PanelMetrics.FieldHeight + 4.dp)
                .clip(RoundedCornerShape(TfFieldDims.Radius))
                .background(AureaColors.ControlButton)
                .testTag("transform.pivot.anchor")
                .semantics { contentDescription = label }
                .pointerInput(Unit) {
                    detectTapGestures { p ->
                        val cx = (p.x / size.width * 3f).toInt().coerceIn(0, 2)
                        val cy = (p.y / size.height * 3f).toInt().coerceIn(0, 2)
                        onPick(cx * 0.5f, cy * 0.5f)
                    }
                },
        ) {
            Canvas(Modifier.fillMaxSize()) {
                val r = 1.6.dp.toPx()
                for (i in 0..2) for (j in 0..2) {
                    drawCircle(AureaColors.Text, if (i == 1 && j == 1) r * 1.4f else r, Offset(size.width * (i + 0.5f) / 3f, size.height * (j + 0.5f) / 3f))
                }
            }
        }
    }
}

/**
 * OS QUATRO CANTOS EM L: 8 dp das bordas, braço 22 (encolhe em área pequena),
 * traço 2 muted a 45 %, pontas redondas. Canto, e não caixa fechada.
 */
@Composable
internal fun CornerMarks(modifier: Modifier) {
    Canvas(modifier) {
        val gap = 8.dp.toPx()
        val l = gap
        val r = size.width - gap
        val t = gap
        val b = size.height - gap
        if (r <= l || b <= t) return@Canvas
        val arm = min(22.dp.toPx(), min((r - l) / 2f, (b - t) / 2f))
        val p = Path().apply {
            moveTo(l + arm, t); lineTo(l, t); lineTo(l, t + arm)
            moveTo(r - arm, t); lineTo(r, t); lineTo(r, t + arm)
            moveTo(l + arm, b); lineTo(l, b); lineTo(l, b - arm)
            moveTo(r - arm, b); lineTo(r, b); lineTo(r, b - arm)
        }
        drawPath(p, AureaColors.Muted.copy(alpha = 0.45f), style = Stroke(2.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
    }
}
