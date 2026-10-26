package com.aurea.aurea.editor

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.exponentialDecay
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.engine.VBezier
import com.aurea.aurea.engine.VPathAt
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.CurveRailIcon
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

// =============================================================================
// Omega conteúdo (área 06, P0-5 / P1-8): o modo EDITAR PONTOS.
//
//   ┌──────────┬──────────────────────────────────┬──────────┐
//   │ ‹        │ ⌜                              ⌝ │  ·       │ faixa de pontos
//   │ Contornos│     TRACKPAD                     │  ●  ←sel │ (60 dp)
//   │ Tipo     │  texto-guia                      │  ·       │
//   │ ± ponto  │ ⌞                              ⌟ │  ·       │
//   └──────────┴──────────────────────────────────┴──────────┘
// Na barra da seção, o segmento ◆ keyframes | • pontos (OmegaContent.kt).
// As REGRAS (inserir, apagar, canto ↔ curva, travas, abrir/fechar, furo) são do
// motor (Engine::edit_vector_path); aqui só a escolha e os gestos.
// Par do EditPointsView.swift (mesmos ids).
// =============================================================================

/** Operações de `Engine::PathOp` (mesma numeração). */
internal object PathOps {
    const val MOVE_POINTS = 0
    const val MOVE_HANDLE = 1
    const val INSERT = 2
    const val REMOVE = 3
    const val SET_TYPE = 4
    const val APPEND = 5
    const val SET_CLOSED = 6
    const val SET_EXCLUDE = 7
}

/** O que está escolhido no contorno (a faixa, o palco e o trackpad leem daqui). */
internal object EditPointsState {
    const val NONE = 0
    const val POINT = 1
    const val IN = 2
    const val OUT = 3
    const val BETWEEN = 4

    var keysMode by mutableStateOf(false)
    var lockAngle by mutableStateOf(true)
    var lockLength by mutableStateOf(false)
    var kind by mutableIntStateOf(NONE)
    /** BETWEEN: o trecho e a posição nele. */
    var segment by mutableIntStateOf(-1)
    var t by mutableFloatStateOf(0.5f)
    /** Laço: os pontos escolhidos juntos (andam juntos). */
    var multi by mutableStateOf<Set<Int>>(emptySet())
    /** Ponto pendente (espaço do grupo) na ponta aberta; o toque no trackpad fixa. */
    var pending by mutableStateOf<Offset?>(null)
    var pendingAtStart = false
    /** Retângulo do laço na tela (x0, y0, x1, y1), só enquanto o dedo arrasta. */
    var lasso by mutableStateOf<FloatArray?>(null)
    /** Incrementa quando a escolha vem de fora da faixa (palco, botões): a faixa centraliza. */
    var revision by mutableIntStateOf(0)

    fun reset() {
        kind = NONE; segment = -1; t = 0.5f; multi = emptySet(); pending = null; lasso = null
    }

    fun choosePoint(store: EditorStore, i: Int, k: Int = POINT, fromStrip: Boolean = false) {
        store.vectorPoint = i
        kind = if (i < 0) NONE else k
        segment = -1
        pending = null
        if (!fromStrip) revision++
    }

    fun chooseBetween(store: EditorStore, seg: Int, at: Float, fromStrip: Boolean = false) {
        store.vectorPoint = -1
        kind = BETWEEN
        segment = seg
        t = at.coerceIn(0.02f, 0.98f)
        pending = null
        if (!fromStrip) revision++
    }
}

/** O ponto escolhido está na ponta de um contorno aberto (ou o contorno está vazio)? */
private fun atOpenEnd(store: EditorStore, path: VBezier?): Boolean {
    val p = path ?: return true
    if (p.v.isEmpty()) return true
    if (p.closed) return false
    val i = store.vectorPoint
    return EditPointsState.kind == EditPointsState.POINT && (i == p.v.size - 1 || (i == 0 && p.v.size > 1))
}

private fun isCurve(path: VBezier?, i: Int): Boolean {
    val v = path?.v?.getOrNull(i) ?: return false
    return hypot(v.inX, v.inY) > 1e-3f || hypot(v.outX, v.outY) > 1e-3f
}

private fun cubicAt(p0: Float, p1: Float, p2: Float, p3: Float, t: Float): Float {
    val u = 1f - t
    return u * u * u * p0 + 3f * u * u * t * p1 + 3f * u * t * t * p2 + t * t * t * p3
}

/** Ponto do trecho [seg] em [t] (espaço do grupo). */
private fun segmentPoint(path: VBezier, seg: Int, t: Float): Offset? {
    val n = path.v.size
    if (n < 2 || seg < 0 || seg >= (if (path.closed) n else n - 1)) return null
    val a = path.v[seg]
    val b = path.v[(seg + 1) % n]
    return Offset(cubicAt(a.x, a.x + a.outX, b.x + b.inX, b.x, t), cubicAt(a.y, a.y + a.outY, b.y + b.inY, b.y, t))
}

private fun segmentLength(path: VBezier, seg: Int): Float {
    var len = 0f
    var prev = segmentPoint(path, seg, 0f) ?: return 0f
    for (k in 1..16) {
        val q = segmentPoint(path, seg, k / 16f) ?: break
        len += hypot(q.x - prev.x, q.y - prev.y)
        prev = q
    }
    return len
}

// =============================================================================
// O painel (três colunas)
// =============================================================================
@Composable
internal fun EditPointsPanel(env: PanelEnv) {
    val store = env.store
    val at = store.vectorPathAt
    // Trocar de camada ou de contorno começa sem escolha.
    LaunchedEffect(store.primary, store.vectorGroup, store.vectorPath) { EditPointsState.reset() }
    // A barra da seção: o segmento keyframes | pontos (P0-6).
    com.aurea.aurea.editor.panels.PublishBarActions(null) { ContentSectionTrailing(store, EditorPanel.EditPoints) }
    Row(Modifier.fillMaxSize().background(AureaColors.PanelFrameBackground).testTag("points.panel")) {
        PointsColumn(env, at)
        Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        Trackpad(store, at, Modifier.weight(1f).fillMaxHeight())
        Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        PointsStrip(store, at?.path, Modifier.width(60.dp).fillMaxHeight())
    }
}

@Composable
private fun PointsColumn(env: PanelEnv, at: VPathAt?) {
    val store = env.store
    val path = at?.path
    val kind = EditPointsState.kind
    val i = store.vectorPoint
    var contours by remember { mutableStateOf(false) }
    var askDeleteKeys by remember { mutableStateOf(false) }
    var curveWarning by remember { mutableStateOf(false) }
    Column(Modifier.width(60.dp).fillMaxHeight()) {
        PointsCell("points.back", stringResource(R.string.panel_voltar_ferramentas), true, onClick = env.onClose) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, it)
        }
        if (EditPointsState.keysMode) {
            val look = when {
                at?.keyHere == true -> KeyframeLook.KeyHere
                at?.animated == true -> KeyframeLook.Animated
                else -> KeyframeLook.None
            }
            PointsCell("points.key", stringResource(if (look == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui),
                at != null, onClick = { togglePathKey(store) },
                onLongClick = if (look == KeyframeLook.KeyHere) ({ askDeleteKeys = true }) else null) {
                KeyframeDiamondIcon(look, enabled = true, modifier = Modifier.padding(1.dp))
            }
            val keyCount = store.vectorDoc?.groups?.getOrNull(store.vectorGroup)?.paths?.getOrNull(store.vectorPath)?.keys?.size ?: 0
            PointsCell("points.curve", stringResource(R.string.panel_editar_curva_propriedade), at != null,
                onClick = { if (keyCount >= 2) env.onOpenPanel(EditorPanel.Curve) else curveWarning = true }) {
                CurveRailIcon(enabled = true, animated = keyCount > 0)
            }
            Box(Modifier.weight(1f))
        } else if (kind == EditPointsState.IN || kind == EditPointsState.OUT) {
            PointsCell("points.lock.angle", stringResource(R.string.om_pts_lock_angle), true, on = EditPointsState.lockAngle,
                onClick = { EditPointsState.lockAngle = !EditPointsState.lockAngle; if (!EditPointsState.lockAngle) EditPointsState.lockLength = false }) {
                ContentIcon(ContentIconKind.AngleLock, 22.dp, it)
            }
            PointsCell("points.lock.length", stringResource(R.string.om_pts_lock_length), EditPointsState.lockAngle, on = EditPointsState.lockLength,
                onClick = { EditPointsState.lockLength = !EditPointsState.lockLength }) {
                ContentIcon(ContentIconKind.LengthLock, 22.dp, it)
            }
            AddRemoveCell(store, path)
        } else {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                PointsCellBody("points.contours", stringResource(R.string.om_pts_contours), store.vectorDoc != null,
                    onClick = { contours = true }) { CupertinoIcon(CupertinoGlyph.SquareOnSquare, 22.dp, it) }
                AnchoredMenu(expanded = contours, onDismiss = { contours = false }, entries = contourEntries(store), tag = "points.contours.menu")
            }
            val curve = isCurve(path, i)
            val canType = kind == EditPointsState.POINT && path != null && i in path.v.indices
            // O ícone mostra o tipo PARA O QUAL o ponto vai mudar.
            PointsCell("points.type", stringResource(if (curve) R.string.om_pts_type_corner else R.string.om_pts_type_curve), canType,
                onClick = { store.editVectorPath(PathOps.SET_TYPE, floatArrayOf(i.toFloat(), if (curve) 0f else 1f), false) }) {
                ContentIcon(if (curve) ContentIconKind.PointCorner else ContentIconKind.PointCurve, 22.dp, it)
            }
            AddRemoveCell(store, path)
        }
    }
    if (askDeleteKeys) {
        AureaAlert(
            title = stringResource(R.string.om_panel_delete_all_keyframes_q),
            confirmLabel = stringResource(R.string.panel_apagar),
            destructive = true,
            onConfirm = { clearPathKeys(store) },
            onDismiss = { askDeleteKeys = false },
        )
    }
    if (curveWarning) {
        AureaAlert(
            title = stringResource(R.string.om_panel_keyframes_needed),
            message = stringResource(R.string.pn_curve_need_two_keys),
            showCancel = false,
            onConfirm = {},
            onDismiss = { curveWarning = false },
        )
    }
}

/** ± ponto: "+" com a escolha ENTRE dois pontos; "−" apaga o ponto escolhido. */
@Composable
private fun ColumnScope.AddRemoveCell(store: EditorStore, path: VBezier?) {
    val kind = EditPointsState.kind
    val i = store.vectorPoint
    val between = kind == EditPointsState.BETWEEN
    val canRemove = path != null && i in path.v.indices && kind != EditPointsState.BETWEEN && kind != EditPointsState.NONE
    PointsCell("points.addRemove", stringResource(if (between) R.string.om_pts_add else R.string.om_pts_delete), between || canRemove,
        onClick = {
            if (between) {
                val r = store.editVectorPath(PathOps.INSERT, floatArrayOf(EditPointsState.segment.toFloat(), EditPointsState.t), false)
                if (r >= 0) EditPointsState.choosePoint(store, r)
            } else if (canRemove) {
                val r = store.editVectorPath(PathOps.REMOVE, floatArrayOf(i.toFloat()), false)
                val left = store.vectorPathAt?.path?.v?.size ?: 0
                if (r >= 0 && left > 0) EditPointsState.choosePoint(store, r.coerceAtMost(left - 1)) else EditPointsState.choosePoint(store, -1)
            }
        }) {
        CupertinoIcon(if (between) CupertinoGlyph.AddCircled else CupertinoGlyph.MinusCircle, 22.dp, it)
    }
}

@Composable
private fun ColumnScope.PointsCell(
    tag: String,
    label: String,
    enabled: Boolean,
    on: Boolean = false,
    onClick: () -> Unit,
    onLongClick: (() -> Unit)? = null,
    icon: @Composable (Color) -> Unit,
) {
    Box(Modifier.weight(1f).fillMaxWidth()) { PointsCellBody(tag, label, enabled, on, onClick, onLongClick, icon) }
}

@Composable
private fun PointsCellBody(
    tag: String,
    label: String,
    enabled: Boolean,
    on: Boolean = false,
    onClick: () -> Unit,
    onLongClick: (() -> Unit)? = null,
    icon: @Composable (Color) -> Unit,
) {
    Box(
        Modifier
            .fillMaxSize()
            .testTag(tag)
            .semantics {
                contentDescription = label
                selected = on
                if (!enabled) disabled()
            }
            .alpha(if (enabled) 1f else 0.3f)
            .tocavel(enabled = enabled, shrink = 1f, onLongClick = onLongClick, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { icon(if (on) AureaColors.Accent else AureaColors.Text) }
}

/** O menu de contornos: um por caminho do grupo, com preencher/furo, apagar, abrir/fechar; e "Novo contorno". */
@Composable
private fun contourEntries(store: EditorStore): List<MenuEntry> {
    val group = store.vectorDoc?.groups?.getOrNull(store.vectorGroup) ?: return emptyList()
    val currentEmpty = (store.vectorPathAt?.path?.v?.size ?: 0) == 0
    val fillLabel = stringResource(R.string.om_pts_fill_contour)
    val holeLabel = stringResource(R.string.om_pts_hole_contour)
    val deleteLabel = stringResource(R.string.om_pts_delete_contour)
    val closeLabel = stringResource(R.string.panel_fechar_caminho)
    val openLabel = stringResource(R.string.panel_abrir_caminho)
    val entries = ArrayList<MenuEntry>()
    group.paths.forEachIndexed { n, p ->
        val name = stringResource(R.string.om_pts_contour_n, n + 1)
        val select = { store.chooseContour(n); EditPointsState.reset() }
        entries += MenuEntry.Item(
            name,
            glyph = CupertinoGlyph.SquareOnSquare,
            sublabel = if (p.exclude) holeLabel else null,
            selected = n == store.vectorPath,
            tag = "points.contour.${n + 1}",
            submenu = listOf(
                MenuEntry.Item(name, selected = n == store.vectorPath) { select() },
                MenuEntry.Item(if (p.exclude) fillLabel else holeLabel, tag = "points.contour.${n + 1}.fill") {
                    select()
                    store.editVectorPath(PathOps.SET_EXCLUDE, floatArrayOf(if (p.exclude) 0f else 1f), false)
                },
                MenuEntry.Item(if (p.path.closed) openLabel else closeLabel, enabled = p.path.v.size >= 2, tag = "points.contour.${n + 1}.close") {
                    select()
                    store.editVectorPath(PathOps.SET_CLOSED, floatArrayOf(if (p.path.closed) 0f else 1f), false)
                },
                MenuEntry.Item(deleteLabel, destructive = true, enabled = group.paths.size > 1, tag = "points.contour.${n + 1}.delete") {
                    store.removeVectorPath(n)
                    EditPointsState.reset()
                },
            ),
        )
    }
    entries += MenuEntry.Divider
    entries += MenuEntry.Item(stringResource(R.string.om_pts_add_contour), glyph = CupertinoGlyph.Plus, enabled = !currentEmpty, tag = "points.contour.add") {
        store.addVectorPath(0)
        EditPointsState.reset()
    }
    return entries
}

/** ◆ do modo keyframes: marca o keyframe da forma aqui; com keyframe aqui, tira. */
private fun togglePathKey(store: EditorStore) {
    val at = store.vectorPathAt ?: return
    if (!at.keyHere) { store.toggleVectorPathKey(); return }
    val row = store.layers.firstOrNull { it.id == store.primary } ?: return
    val local = store.playhead - row.startFrame + row.offsetFrames
    val g = store.vectorGroup
    val pIndex = store.vectorPath
    val shape = at.path.copyDeep()
    store.editVectorDoc { d ->
        val p = d.groups.getOrNull(g)?.paths?.getOrNull(pIndex) ?: return@editVectorDoc
        if (p.keys.size == 1) p.path = shape
        p.keys.removeAll { it.frame == local }
    }
}

/** Segurar ◆: apaga todos os keyframes da forma (a forma do cabeçote fica parada). Um passo de desfazer. */
private fun clearPathKeys(store: EditorStore) {
    val shape = store.vectorPathAt?.path?.copyDeep() ?: return
    val g = store.vectorGroup
    val pIndex = store.vectorPath
    store.editVectorDoc { d ->
        val p = d.groups.getOrNull(g)?.paths?.getOrNull(pIndex) ?: return@editVectorDoc
        p.path = shape
        p.keys.clear()
    }
}

// =============================================================================
// Trackpad (centro)
// =============================================================================
@Composable
private fun Trackpad(store: EditorStore, at: VPathAt?, modifier: Modifier) {
    val path = at?.path
    val guide = stringResource(when {
        path == null || path.v.isEmpty() || atOpenEnd(store, path) -> R.string.om_pts_guide_next
        EditPointsState.kind == EditPointsState.BETWEEN -> R.string.om_pts_guide_insert
        else -> R.string.om_pts_guide_move
    })
    val density = LocalDensity.current.density
    val corner = AureaColors.Text
    Box(
        modifier
            .padding(6.dp)
            .background(AureaColors.ContentTrackpad)
            .testTag("points.trackpad")
            .semantics { contentDescription = guide }
            .pointerInput(Unit) { trackpadGestures(store, density) },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val l = 8.dp.toPx()
            val w = 1.5.dp.toPx()
            val o = 6.dp.toPx()
            val x0 = o; val y0 = o; val x1 = size.width - o; val y1 = size.height - o
            drawLine(corner, Offset(x0, y0), Offset(x0 + l, y0), w); drawLine(corner, Offset(x0, y0), Offset(x0, y0 + l), w)
            drawLine(corner, Offset(x1, y0), Offset(x1 - l, y0), w); drawLine(corner, Offset(x1, y0), Offset(x1, y0 + l), w)
            drawLine(corner, Offset(x0, y1), Offset(x0 + l, y1), w); drawLine(corner, Offset(x0, y1), Offset(x0, y1 - l), w)
            drawLine(corner, Offset(x1, y1), Offset(x1 - l, y1), w); drawLine(corner, Offset(x1, y1), Offset(x1, y1 - l), w)
        }
        Text(guide, Modifier.padding(horizontal = 18.dp), textAlign = TextAlign.Center, style = AureaType.of(12f, color = AureaColors.ContentGuide))
    }
}

/**
 * O dedo no trackpad: a diferença (1 dp = 1 px da composição, convertida para o
 * espaço do grupo pela rotação e escala da camada) move o ponto, a alça ou os
 * pontos do laço. Na ponta aberta (ou ENTRE pontos) o arraste posiciona o
 * ponto pendente e o toque o fixa. Um gesto = um passo de desfazer.
 */
private suspend fun androidx.compose.ui.input.pointer.PointerInputScope.trackpadGestures(store: EditorStore, density: Float) {
    awaitEachGesture {
        val down = awaitFirstDown()
        down.consume()
        val slop = viewConfiguration.touchSlop
        var last = down.position
        var moved = false
        var sent = false
        val g = FloatArray(2)
        while (true) {
            val e = awaitPointerEvent()
            val c = e.changes.firstOrNull { it.id == down.id } ?: break
            c.consume()
            if (!c.pressed) break
            if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < slop) continue
            moved = true
            val at = store.vectorPathAt ?: continue
            val path = at.path
            val dx = (c.position.x - last.x) / density
            val dy = (c.position.y - last.y) / density
            last = c.position
            if (!at.vecFromComp(dx, dy, g)) continue
            val i = store.vectorPoint
            when {
                atOpenEnd(store, path) || EditPointsState.kind == EditPointsState.BETWEEN || EditPointsState.pending != null -> {
                    // O ponto pendente nasce na ponta (ou no trecho) e segue o dedo.
                    val start = EditPointsState.pending ?: pendingStart(store, at, path)
                    EditPointsState.pending = Offset(start.x + g[0], start.y + g[1])
                }
                EditPointsState.kind == EditPointsState.POINT && i in path.v.indices -> {
                    val idx = (EditPointsState.multi.takeIf { i in it } ?: setOf(i)).sorted()
                    val args = FloatArray(2 + idx.size)
                    args[0] = g[0]; args[1] = g[1]
                    idx.forEachIndexed { k, p -> args[2 + k] = p.toFloat() }
                    store.editVectorPath(PathOps.MOVE_POINTS, args, sent)
                    sent = true
                }
                (EditPointsState.kind == EditPointsState.IN || EditPointsState.kind == EditPointsState.OUT) && i in path.v.indices -> {
                    store.editVectorPath(PathOps.MOVE_HANDLE, floatArrayOf(i.toFloat(), if (EditPointsState.kind == EditPointsState.OUT) 1f else 0f, g[0], g[1],
                        if (EditPointsState.lockAngle) 1f else 0f, if (EditPointsState.lockLength) 1f else 0f), sent)
                    sent = true
                }
            }
        }
        if (!moved) confirmPending(store)
    }
}

/** Onde o ponto pendente nasce: na ponta aberta (um passo para fora) ou no trecho escolhido. */
private fun pendingStart(store: EditorStore, at: VPathAt, path: VBezier): Offset {
    if (EditPointsState.kind == EditPointsState.BETWEEN) {
        segmentPoint(path, EditPointsState.segment, EditPointsState.t)?.let { return it }
    }
    val v = path.v
    if (v.isEmpty()) {
        // Contorno vazio: o primeiro ponto nasce no centro da composição.
        val g = FloatArray(2)
        EditPointsState.pendingAtStart = false
        return if (at.fromComp(store.project.width / 2f, store.project.height / 2f, g)) Offset(g[0], g[1]) else Offset.Zero
    }
    val atStart = store.vectorPoint == 0 && v.size > 1
    EditPointsState.pendingAtStart = atStart
    val end = if (atStart) v.first() else v.last()
    return Offset(end.x, end.y)
}

/** O toque no trackpad fixa o ponto pendente (no fim, no começo ou entre os pontos). */
private fun confirmPending(store: EditorStore) {
    val p = EditPointsState.pending ?: return
    if (EditPointsState.kind == EditPointsState.BETWEEN && EditPointsState.segment >= 0) {
        val r = store.editVectorPath(PathOps.INSERT, floatArrayOf(EditPointsState.segment.toFloat(), EditPointsState.t), false)
        if (r >= 0) {
            val at = store.vectorPathAt
            val q = at?.path?.v?.getOrNull(r)
            if (q != null) store.editVectorPath(PathOps.MOVE_POINTS, floatArrayOf(p.x - q.x, p.y - q.y, r.toFloat()), true)
            EditPointsState.choosePoint(store, r)
        }
    } else {
        val r = store.editVectorPath(PathOps.APPEND, floatArrayOf(p.x, p.y, if (EditPointsState.pendingAtStart) 1f else 0f), false)
        if (r >= 0) EditPointsState.choosePoint(store, r)
    }
    EditPointsState.pending = null
}

// =============================================================================
// Faixa de pontos (direita)
// =============================================================================

/** Uma parada da faixa: o ponto (MAIN), a alça de entrada (IN) ou a de saída (OUT). */
private class StripStop(val y: Float, val kind: Int, val index: Int)

/** A faixa "esticada": a altura de cada parada e de cada trecho, em dp. */
private class StripLayout(val stops: List<StripStop>, val segments: List<Triple<Float, Float, Int>>, val total: Float)

private fun stripLayout(path: VBezier?): StripLayout {
    if (path == null || path.v.isEmpty()) return StripLayout(emptyList(), emptyList(), 0f)
    val n = path.v.size
    val segs = if (path.closed) n else n - 1
    val lengths = (0 until segs).map { segmentLength(path, it) }
    val maxLen = lengths.maxOrNull()?.coerceAtLeast(1e-3f) ?: 1f
    val stops = ArrayList<StripStop>()
    val segments = ArrayList<Triple<Float, Float, Int>>()
    var y = 0f
    for (i in 0 until n) {
        val v = path.v[i]
        if (hypot(v.inX, v.inY) > 1e-3f) stops += StripStop(y - 14f, EditPointsState.IN, i)
        stops += StripStop(y, EditPointsState.POINT, i)
        if (hypot(v.outX, v.outY) > 1e-3f) stops += StripStop(y + 14f, EditPointsState.OUT, i)
        if (i < segs) {
            // Espaço de 75 a 95 dp, pelo comprimento do trecho.
            val space = 75f + 20f * (lengths[i] / maxLen)
            segments += Triple(y, y + space, i)
            y += space
        }
    }
    return StripLayout(stops, segments, y)
}

@Composable
private fun PointsStrip(store: EditorStore, path: VBezier?, modifier: Modifier) {
    val layout = remember(path) { stripLayout(path) }
    val scroll = remember { Animatable(0f) }
    val scope = rememberCoroutineScope()
    var dragging by remember { mutableStateOf(false) }
    val accent = AureaColors.Accent
    val base = AureaColors.ContentPointMarker
    val label = stringResource(R.string.om_pts_strip_cd)
    // A escolha veio de fora (palco, botões): centraliza a parada dela.
    LaunchedEffect(EditPointsState.revision, layout) {
        if (dragging) return@LaunchedEffect
        val target = when (EditPointsState.kind) {
            EditPointsState.BETWEEN -> layout.segments.firstOrNull { it.third == EditPointsState.segment }?.let { it.first + (it.second - it.first) * EditPointsState.t }
            EditPointsState.NONE -> null
            else -> layout.stops.firstOrNull { it.index == store.vectorPoint && it.kind == EditPointsState.kind }?.y
        }
        if (target != null) scroll.animateTo(target)
    }
    // O centro da faixa escolhe (só quando é a faixa que anda).
    LaunchedEffect(layout) {
        snapshotFlow { scroll.value }.collect { y ->
            if (!dragging) return@collect
            applyStripSelection(store, layout, y)
        }
    }
    BoxWithConstraints(
        modifier
            .testTag("points.strip")
            .semantics { contentDescription = label }
            .pointerInput(layout) {
                val tracker = VelocityTracker()
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    tracker.resetTracking()
                    tracker.addPosition(down.uptimeMillis, down.position)
                    val slop = viewConfiguration.touchSlop
                    var moved = false
                    var last = down.position.y
                    while (true) {
                        val e = awaitPointerEvent()
                        val c = e.changes.firstOrNull { it.id == down.id } ?: break
                        c.consume()
                        tracker.addPosition(c.uptimeMillis, c.position)
                        if (!c.pressed) break
                        if (!moved && abs(c.position.y - down.position.y) < slop) continue
                        if (!moved) { moved = true; dragging = true }
                        val dy = (c.position.y - last) / density
                        last = c.position.y
                        scope.launch { scroll.snapTo((scroll.value - dy).coerceIn(-20f, layout.total + 20f)) }
                    }
                    if (!moved) {
                        // Toque: a parada mais perto do dedo vem para o centro.
                        val yTap = scroll.value + (down.position.y - size.height / 2f) / density
                        val stop = layout.stops.minByOrNull { abs(it.y - yTap) }
                        if (stop != null && abs(stop.y - yTap) < 20f) {
                            dragging = true
                            scope.launch {
                                scroll.animateTo(stop.y)
                                applyStripSelection(store, layout, stop.y)
                                dragging = false
                            }
                        }
                    } else {
                        val v = -tracker.calculateVelocity().y / density
                        scope.launch {
                            scroll.updateBounds(-20f, layout.total + 20f)
                            scroll.animateDecay(v, exponentialDecay())
                            // Imã de 12 dp para a parada mais perto.
                            val near = layout.stops.minByOrNull { abs(it.y - scroll.value) }
                            if (near != null && abs(near.y - scroll.value) <= 12f) scroll.animateTo(near.y)
                            applyStripSelection(store, layout, scroll.value)
                            scroll.updateBounds(null, null)
                            dragging = false
                        }
                    }
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            val k = density
            fun sy(y: Float) = cy + (y - scroll.value) * k
            val dash = PathEffect.dashPathEffect(floatArrayOf(4.dp.toPx(), 3.dp.toPx()))
            for ((y0, y1, seg) in layout.segments) {
                val curved = path != null && (isCurve(path, seg) || isCurve(path, (seg + 1) % path.v.size))
                drawLine(base, Offset(cx, sy(y0)), Offset(cx, sy(y1)), 1.dp.toPx(), pathEffect = if (curved) dash else null)
            }
            for (s in layout.stops) {
                val chosen = (EditPointsState.kind == s.kind && store.vectorPoint == s.index) || (s.kind == EditPointsState.POINT && s.index in EditPointsState.multi)
                val r = if (chosen) 6.dp.toPx() else 2.5.dp.toPx()
                val y = sy(s.y)
                if (s.kind == EditPointsState.POINT) drawCircle(if (chosen) accent else base, r, Offset(cx, y))
                else drawCircle(if (chosen) accent else base, r, Offset(cx, y), style = Stroke(1.dp.toPx()))
            }
            if (EditPointsState.kind == EditPointsState.BETWEEN) {
                drawCircle(accent, 4.dp.toPx(), Offset(cx, cy), style = Stroke(1.5.dp.toPx()))
            }
            // O centro: a marca da escolha.
            drawLine(accent.copy(alpha = 0.6f), Offset(4.dp.toPx(), cy), Offset(10.dp.toPx(), cy), 1.5.dp.toPx(), cap = StrokeCap.Round)
            drawLine(accent.copy(alpha = 0.6f), Offset(size.width - 10.dp.toPx(), cy), Offset(size.width - 4.dp.toPx(), cy), 1.5.dp.toPx(), cap = StrokeCap.Round)
        }
    }
}

/** A posição [y] da faixa (dp) vira a escolha: parada a até 12 dp, senão "entre" no trecho. */
private fun applyStripSelection(store: EditorStore, layout: StripLayout, y: Float) {
    val near = layout.stops.minByOrNull { abs(it.y - y) }
    if (near != null && abs(near.y - y) <= 12f) {
        if (store.vectorPoint != near.index || EditPointsState.kind != near.kind) EditPointsState.choosePoint(store, near.index, near.kind, fromStrip = true)
        return
    }
    val seg = layout.segments.firstOrNull { y >= it.first && y <= it.second } ?: return
    val t = (y - seg.first) / max(1e-3f, seg.second - seg.first)
    EditPointsState.chooseBetween(store, seg.third, t, fromStrip = true)
}

// =============================================================================
// O palco no Editar pontos
// =============================================================================

/** O palco está no Editar pontos? */
internal fun editPointsActive(store: EditorStore, ui: EditorUi): Boolean {
    if (ui.panel != EditorPanel.EditPoints || store.selection.size != 1 || !store.isVectorLayer) return false
    val d = store.detail ?: return false
    return !d.locked
}

private val PathInk = Color(0xFF5AA8FF)
private val HandleInk = Color(0xFFB8C4D0)

/** O contorno atual (pontos, alças da escolha, "entre", pendente e laço) e os outros contornos do grupo. */
internal fun DrawScope.drawEditPointsOverlay(store: EditorStore, m: StageMapper) {
    val at = store.vectorPathAt ?: return
    val q = FloatArray(2)
    fun sx(x: Float, y: Float): Offset { at.toComp(x, y, q); return Offset(m.sx(q[0]), m.sy(q[1])) }
    // Os outros contornos do grupo (finos).
    store.vectorDoc?.groups?.getOrNull(store.vectorGroup)?.paths?.forEachIndexed { n, p ->
        if (n == store.vectorPath) return@forEachIndexed
        drawBezier(p.path, ::sx, ShellColors.OutlineUnder, HandleInk.copy(alpha = 0.6f), 1.dp.toPx())
    }
    val path = at.path
    drawBezier(path, ::sx, ShellColors.OutlineUnder, PathInk, 1.5.dp.toPx())
    val sel = store.vectorPoint
    val kind = EditPointsState.kind
    if (sel in path.v.indices && kind != EditPointsState.BETWEEN) {
        val p = path.v[sel]
        val c = sx(p.x, p.y)
        for (k in 0..1) {
            val hx = if (k == 0) p.x + p.inX else p.x + p.outX
            val hy = if (k == 0) p.y + p.inY else p.y + p.outY
            val h = sx(hx, hy)
            if (hypot(h.x - c.x, h.y - c.y) < 1f) continue
            val chosen = (k == 0 && kind == EditPointsState.IN) || (k == 1 && kind == EditPointsState.OUT)
            drawLine(HandleInk, c, h, 1.4.dp.toPx())
            drawCircle(ShellColors.OutlineUnder, 7.5.dp.toPx(), h)
            drawCircle(if (chosen) AureaColors.Accent else Color.White, 6.dp.toPx(), h)
        }
    }
    val r = 6.dp.toPx()
    for (i in path.v.indices) {
        val c = sx(path.v[i].x, path.v[i].y)
        val chosen = (i == sel && kind == EditPointsState.POINT) || i in EditPointsState.multi
        drawRect(ShellColors.OutlineUnder, Offset(c.x - r - 1.5f, c.y - r - 1.5f), Size(2 * r + 3f, 2 * r + 3f))
        drawRect(if (chosen) AureaColors.Accent else Color.White, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
        if (i == 0 && !path.closed && path.v.size >= 2) drawCircle(AureaColors.Accent, r * 1.9f, c, style = Stroke(1.5.dp.toPx()))
    }
    if (kind == EditPointsState.BETWEEN) {
        segmentPoint(path, EditPointsState.segment, EditPointsState.t)?.let { g ->
            val c = sx(g.x, g.y)
            drawCircle(ShellColors.OutlineUnder, 7.dp.toPx(), c)
            drawCircle(AureaColors.Accent, 5.5.dp.toPx(), c, style = Stroke(2.dp.toPx()))
        }
    }
    EditPointsState.pending?.let { g ->
        val c = sx(g.x, g.y)
        val anchor = when {
            kind == EditPointsState.BETWEEN -> segmentPoint(path, EditPointsState.segment, EditPointsState.t)
            path.v.isEmpty() -> null
            EditPointsState.pendingAtStart -> Offset(path.v.first().x, path.v.first().y)
            else -> Offset(path.v.last().x, path.v.last().y)
        }
        anchor?.let { a -> drawLine(AureaColors.Accent, sx(a.x, a.y), c, 1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 5f))) }
        drawCircle(ShellColors.OutlineUnder, 8.dp.toPx(), c)
        drawCircle(AureaColors.Accent, 6.5.dp.toPx(), c, style = Stroke(2.dp.toPx()))
    }
    EditPointsState.lasso?.let { b ->
        val x0 = min(b[0], b[2]); val y0 = min(b[1], b[3])
        drawRect(AureaColors.Accent.copy(alpha = 0.12f), Offset(x0, y0), Size(abs(b[2] - b[0]), abs(b[3] - b[1])))
        drawRect(AureaColors.Accent, Offset(x0, y0), Size(abs(b[2] - b[0]), abs(b[3] - b[1])), style = Stroke(1.dp.toPx()))
    }
}

private fun DrawScope.drawBezier(path: VBezier, sx: (Float, Float) -> Offset, under: Color, ink: Color, width: Float) {
    val v = path.v
    if (v.size < 2) return
    val p = Path()
    val o0 = sx(v[0].x, v[0].y)
    p.moveTo(o0.x, o0.y)
    val segs = if (path.closed) v.size else v.size - 1
    for (i in 0 until segs) {
        val a = v[i]
        val b = v[(i + 1) % v.size]
        val c1 = sx(a.x + a.outX, a.y + a.outY)
        val c2 = sx(b.x + b.inX, b.y + b.inY)
        val e = sx(b.x, b.y)
        p.cubicTo(c1.x, c1.y, c2.x, c2.y, e.x, e.y)
    }
    if (path.closed) p.close()
    drawPath(p, under, style = Stroke(width * 2f))
    drawPath(p, ink, style = Stroke(width))
}

/**
 * O gesto do palco no Editar pontos. Alcance de 25 dp: alça da escolha > ponto
 * do contorno atual > ponto de outro contorno (troca o contorno) > linha (põe um
 * ponto) > vazio (laço de vários pontos; tocar acrescenta na ponta aberta ou
 * solta a escolha). Arrastar move o ponto, a alça ou o grupo do laço — um passo
 * de desfazer por arrasto.
 */
internal suspend fun AwaitPointerEventScope.editPointsGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val at = store.vectorPathAt
    if (at == null) { drain(down); return }
    val reach = 25.dp.toPx()
    val slop = 6.dp.toPx()
    val q = FloatArray(2)
    fun screen(x: Float, y: Float): Offset { at.toComp(x, y, q); return Offset(m.sx(q[0]), m.sy(q[1])) }
    val path = at.path
    val v = path.v
    val dp = down.position
    val sel = store.vectorPoint
    // 1) Alças do ponto escolhido.
    var handle = -1
    if (sel in v.indices && EditPointsState.kind != EditPointsState.BETWEEN) {
        val p = v[sel]
        val c = screen(p.x, p.y)
        val hin = screen(p.x + p.inX, p.y + p.inY)
        val hout = screen(p.x + p.outX, p.y + p.outY)
        val din = if (hypot(hin.x - c.x, hin.y - c.y) > 1f) hypot(dp.x - hin.x, dp.y - hin.y) else Float.MAX_VALUE
        val dout = if (hypot(hout.x - c.x, hout.y - c.y) > 1f) hypot(dp.x - hout.x, dp.y - hout.y) else Float.MAX_VALUE
        if (din < reach && din <= dout) handle = EditPointsState.IN else if (dout < reach) handle = EditPointsState.OUT
    }
    // 2) Pontos do contorno atual.
    var hit = -1
    if (handle < 0) {
        var best = reach
        for (i in v.indices) {
            val c = screen(v[i].x, v[i].y)
            val d = hypot(dp.x - c.x, dp.y - c.y)
            if (d < best) { best = d; hit = i }
        }
    }
    // 3) Outro contorno do grupo: troca o contorno.
    if (handle < 0 && hit < 0) {
        val group = store.vectorDoc?.groups?.getOrNull(store.vectorGroup)
        group?.paths?.forEachIndexed { n, p ->
            if (n == store.vectorPath || hit >= 0) return@forEachIndexed
            for (i in p.path.v.indices) {
                val c = screen(p.path.v[i].x, p.path.v[i].y)
                if (hypot(dp.x - c.x, dp.y - c.y) < reach) {
                    drain(down)
                    store.chooseContour(n)
                    EditPointsState.reset()
                    EditPointsState.choosePoint(store, i)
                    return
                }
            }
        }
    }
    val g = FloatArray(2)
    when {
        handle >= 0 -> {
            EditPointsState.choosePoint(store, sel, handle)
            var last = dp
            var sent = false
            drag(down) { pos ->
                if (!at.vecFromComp((pos.x - last.x) / m.fit, (pos.y - last.y) / m.fit, g)) return@drag
                last = pos
                store.editVectorPath(PathOps.MOVE_HANDLE, floatArrayOf(sel.toFloat(), if (handle == EditPointsState.OUT) 1f else 0f, g[0], g[1],
                    if (EditPointsState.lockAngle) 1f else 0f, if (EditPointsState.lockLength) 1f else 0f), sent)
                sent = true
            }
        }
        hit >= 0 -> {
            var moved = false
            var last = dp
            var sent = false
            val group = (EditPointsState.multi.takeIf { hit in it } ?: setOf(hit)).sorted()
            drag(down) { pos ->
                if (!moved && hypot(pos.x - dp.x, pos.y - dp.y) < slop) return@drag
                moved = true
                if (!at.vecFromComp((pos.x - last.x) / m.fit, (pos.y - last.y) / m.fit, g)) return@drag
                last = pos
                val args = FloatArray(2 + group.size)
                args[0] = g[0]; args[1] = g[1]
                group.forEachIndexed { k, p -> args[2 + k] = p.toFloat() }
                store.editVectorPath(PathOps.MOVE_POINTS, args, sent)
                sent = true
            }
            if (!moved) {
                // Tocar no 1º ponto com a ponta escolhida fecha o contorno aberto.
                if (hit == 0 && !path.closed && v.size >= 3 && sel == v.size - 1 && EditPointsState.kind == EditPointsState.POINT) {
                    store.editVectorPath(PathOps.SET_CLOSED, floatArrayOf(1f), false)
                }
                if (hit !in EditPointsState.multi) EditPointsState.multi = emptySet()
            }
            EditPointsState.choosePoint(store, hit)
        }
        else -> {
            // 4) Linha: põe um ponto ali (a curva não muda).
            val line = nearestOnLine(path, ::screen, dp, reach)
            var dragged = false
            val start = dp
            drag(down) { pos ->
                if (!dragged && hypot(pos.x - start.x, pos.y - start.y) < slop) return@drag
                dragged = true
                EditPointsState.lasso = floatArrayOf(start.x, start.y, pos.x, pos.y)
            }
            val box = EditPointsState.lasso
            EditPointsState.lasso = null
            if (dragged && box != null) {
                // Laço: os pontos dentro do retângulo andam juntos.
                val x0 = min(box[0], box[2]); val x1 = max(box[0], box[2])
                val y0 = min(box[1], box[3]); val y1 = max(box[1], box[3])
                val inside = v.indices.filter { val c = screen(v[it].x, v[it].y); c.x in x0..x1 && c.y in y0..y1 }.toSet()
                EditPointsState.multi = inside
                if (inside.isNotEmpty()) EditPointsState.choosePoint(store, inside.min()) else EditPointsState.choosePoint(store, -1)
                return
            }
            if (line != null) {
                val r = store.editVectorPath(PathOps.INSERT, floatArrayOf(line.first.toFloat(), line.second), false)
                if (r >= 0) EditPointsState.choosePoint(store, r)
                return
            }
            // Vazio: com a ponta aberta escolhida (ou o contorno vazio), acrescenta ali.
            if (atOpenEnd(store, path) && at.fromComp(m.cx(dp.x), m.cy(dp.y), g)) {
                val atStart = v.size > 1 && sel == 0
                val r = store.editVectorPath(PathOps.APPEND, floatArrayOf(g[0], g[1], if (atStart) 1f else 0f), false)
                if (r >= 0) EditPointsState.choosePoint(store, r)
            } else {
                EditPointsState.multi = emptySet()
                EditPointsState.choosePoint(store, -1)
            }
        }
    }
}

/** O trecho e o t do ponto da linha mais perto de [pos] (tela), ou nulo fora do alcance. */
private fun nearestOnLine(path: VBezier, screen: (Float, Float) -> Offset, pos: Offset, reach: Float): Pair<Int, Float>? {
    val n = path.v.size
    if (n < 2) return null
    val segs = if (path.closed) n else n - 1
    var best = reach
    var bestSeg = -1
    var bestT = 0f
    for (i in 0 until segs) {
        for (k in 1 until 48) {
            val t = k / 48f
            val g = segmentPoint(path, i, t) ?: continue
            val c = screen(g.x, g.y)
            val d = hypot(c.x - pos.x, c.y - pos.y)
            if (d < best) { best = d; bestSeg = i; bestT = t }
        }
    }
    return if (bestSeg >= 0) bestSeg to bestT else null
}

private suspend fun AwaitPointerEventScope.drag(down: PointerInputChange, onMove: (Offset) -> Unit) {
    down.consume()
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        c.consume()
        if (!c.pressed) break
        onMove(c.position)
    }
}

private suspend fun AwaitPointerEventScope.drain(down: PointerInputChange) = drag(down) { }
