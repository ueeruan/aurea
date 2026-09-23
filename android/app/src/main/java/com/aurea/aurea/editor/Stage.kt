package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GIZMO_LENGTH
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sign
import kotlin.math.sin

/**
 * A prévia (palco): a superfície do motor (com o respiro de 8 dp da A.01), o
 * overlay de seleção/alças/encaixe e os gestos, o chip de resolução e o HUD.
 *
 * Nenhum estado de UI é lido no CORPO deste composable: a seleção, o
 * transform e o tempo são lidos no desenho do overlay (invalida só o
 * desenho) e nos eventos de toque. A SurfaceView não recompõe nunca.
 */
@Composable
internal fun PreviewStage(store: EditorStore, ui: EditorUi, modifier: Modifier) {
    val mapper = remember { StageMapper() }
    val haptic = LocalHapticFeedback.current
    val insetPx = with(LocalDensity.current) { ShellDims.StageInset.toPx() }
    // Pontos do rastreio de câmera no vídeo (painel de Rastreio aberto).
    val showTrack = ui.panel == com.aurea.aurea.editor.panels.EditorPanel.Tracking
    androidx.compose.runtime.LaunchedEffect(store.playhead, showTrack, store.cameraTrack) { store.refreshCameraFeatures(showTrack) }
    Box(modifier.background(AureaColors.EditorTopBar).clipToBounds()) {
        PreviewSurface(store, Modifier.fillMaxSize().padding(ShellDims.StageInset))
        Spacer(
            Modifier
                .fillMaxSize()
                .pointerInput(store) { stageGestures(store, ui, mapper, haptic) }
                .drawBehind { drawStageOverlay(store, ui, mapper, insetPx) },
        )
        LockBanner(store, Modifier.align(Alignment.TopCenter).padding(top = 8.dp, start = 8.dp, end = 8.dp))
        VectorToolBanner(store, Modifier.align(Alignment.BottomCenter).padding(bottom = 10.dp, start = 8.dp, end = 8.dp))
        ResolutionChip(store, ui, Modifier.align(Alignment.TopEnd).padding(top = 4.dp, end = 4.dp))
        PerfHud(store, Modifier.align(Alignment.TopStart).padding(start = 8.dp, top = 6.dp))
    }
}

/** Faixa do modo vetorial: o que o dedo faz agora e "Concluir" (volta ao palco normal). */
@Composable
private fun VectorToolBanner(store: EditorStore, modifier: Modifier) {
    val tool = store.vectorTool
    if (tool == 0) return
    Row(
        modifier
            .clip(RoundedCornerShape(10.dp))
            .background(AureaColors.EditorPanelHigh)
            .border(1.dp, ShellColors.AccentHalf, RoundedCornerShape(10.dp))
            .padding(start = 12.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            if (tool == 2) stringResource(R.string.editor_mao_livre_desenhe_dedo)
            else when (PointTool.effective(VectorStageState.pointTool, store.vectorPathAt?.path?.v?.size ?: 0)) {
                PointTool.ADD -> stringResource(R.string.editor_adicionar_toque_linha_ou_vazio_arraste)
                PointTool.REMOVE -> stringResource(R.string.editor_remover_toque_ponto_apagar)
                PointTool.CORNER -> stringResource(R.string.editor_canto_suave_toque_ponto_alternar)
                else -> stringResource(R.string.editor_selecionar_arraste_pontos_alcas)
            },
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)),
            modifier = Modifier.weight(1f, fill = false),
        )
        Spacer(Modifier.width(6.dp))
        Box(
            Modifier
                .heightIn(min = 36.dp)
                .semantics { contentDescription = "Concluir desenho" }
                .tocavel(haptic = true) { store.chooseVectorTool(0) }
                .padding(horizontal = 4.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                stringResource(R.string.editor_concluir),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W700, color = AureaColors.OnAccent)),
                modifier = Modifier
                    .clip(RoundedCornerShape(50))
                    .background(AureaColors.Accent)
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            )
        }
    }
}

/**
 * A faixa do cadeado (`FaixaDeBloqueio` compacta), no lugar das alças que
 * somem: sem ela a camada travada parece normal e o dedo só descobre
 * tentando arrastar. O "Desbloquear" agora tem texto escuro sobre o
 * destaque (branco dava 2,2:1 — bug 11) e alvo de 36 dp de altura.
 */
@Composable
private fun LockBanner(store: EditorStore, modifier: Modifier) {
    val lockedId by remember {
        derivedStateOf {
            val id = store.primary
            if (id != null && store.selection.size == 1 && store.layers.firstOrNull { it.id == id }?.locked == true) id else null
        }
    }
    val id = lockedId ?: return
    Row(
        modifier
            .clip(RoundedCornerShape(10.dp))
            .background(AureaColors.EditorPanelHigh)
            .border(1.dp, ShellColors.AccentHalf, RoundedCornerShape(10.dp))
            .padding(start = 10.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.LockFill, 13.dp, AureaColors.Accent)
        Spacer(Modifier.width(7.dp))
        Text(
            stringResource(R.string.editor_camada_bloqueada),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)),
            modifier = Modifier.weight(1f, fill = false),
        )
        Spacer(Modifier.width(4.dp))
        Box(
            Modifier
                .heightIn(min = 36.dp)
                .semantics { contentDescription = "Desbloquear" }
                .tocavel(haptic = true) { store.setLayerLocked(id, false) }
                .padding(horizontal = 4.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                stringResource(R.string.editor_desbloquear),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W700, color = AureaColors.OnAccent)),
                modifier = Modifier
                    .clip(RoundedCornerShape(50))
                    .background(AureaColors.Accent)
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            )
        }
    }
}

// =============================================================================
// Composição ↔ tela
// =============================================================================

/**
 * Mapeia px da composição ↔ px do palco com a MESMA conta da saída do motor
 * (Renderer.cpp, "saida"): encaixe `min(w/cw, h/ch)` centrado na superfície,
 * que fica 8 dp para dentro do palco. Atualizado a cada desenho; os gestos
 * usam o último valor. Arrays reaproveitados: nada aloca por quadro.
 */
internal class StageMapper {
    var fit = 0f
    var ox = 0f
    var oy = 0f
    var compW = 0f
    var compH = 0f
    var boxW = 0f
    var boxH = 0f
    var valid = false

    /** Cantos da camada principal na tela (TL, TR, BR, BL). */
    val corners = FloatArray(8)

    /** Alças na tela: 0 = giro (sup-dir), 1 = escala inf-dir, 2 = sup-esq, 3 = inf-esq. */
    val handles = FloatArray(8)
    var handlesValid = false

    val scratch = FloatArray(8)
    val path = Path()
    val maskPath = Path()
    val arrow = Path()

    // Traços do overlay, recriados só quando a densidade muda (o desenho roda
    // a cada quadro durante a reprodução e não deve alocar).
    private var strokeDensity = 0f
    var outlineUnder = Stroke(1f)
    var outlineOver = Stroke(1f)
    var batchUnder = Stroke(1f)
    var batchOver = Stroke(1f)
    var ring15 = Stroke(1f)
    var ring1 = Stroke(1f)
    var arc = Stroke(1f)

    fun strokesFor(density: Float) {
        if (density == strokeDensity) return
        strokeDensity = density
        outlineUnder = Stroke(3.5f * density, join = StrokeJoin.Round)
        outlineOver = Stroke(2f * density, join = StrokeJoin.Round)
        batchUnder = Stroke(2.5f * density, join = StrokeJoin.Round)
        batchOver = Stroke(1.5f * density, join = StrokeJoin.Round)
        ring15 = Stroke(1.5f * density)
        ring1 = Stroke(1f * density)
        arc = Stroke(1.6f * density, cap = StrokeCap.Round)
    }

    fun update(w: Float, h: Float, inset: Float, cw: Int, ch: Int) {
        boxW = w
        boxH = h
        val sw = w - 2 * inset
        val sh = h - 2 * inset
        if (cw <= 0 || ch <= 0 || sw <= 0f || sh <= 0f) {
            valid = false
            return
        }
        compW = cw.toFloat()
        compH = ch.toFloat()
        fit = min(sw / compW, sh / compH)
        ox = inset + (sw - compW * fit) / 2
        oy = inset + (sh - compH * fit) / 2
        valid = true
    }

    fun sx(cx: Float) = ox + cx * fit
    fun sy(cy: Float) = oy + cy * fit
    fun cx(sx: Float) = (sx - ox) / fit
    fun cy(sy: Float) = (sy - oy) / fit
}

// =============================================================================
// Desenho: moldura, alças e linhas de encaixe (px de tela)
// =============================================================================

private fun DrawScope.drawStageOverlay(store: EditorStore, ui: EditorUi, m: StageMapper, inset: Float) {
    val project = store.project
    m.update(size.width, size.height, inset, project.width, project.height)
    m.strokesFor(density)
    m.handlesValid = false
    if (!m.valid) return
    // Modo vetorial (pontos / mão livre): o palco é do caminho, sem alças da camada.
    if (store.vectorTool != 0) {
        drawVectorOverlay(store, m)
        return
    }

    // Linha de encaixe: só durante o arrasto e só no eixo que encaixou.
    val snapStroke = 1.5.dp.toPx()
    val snapX = ui.snapX
    if (!snapX.isNaN()) drawLine(ShellColors.SnapLine, Offset(m.sx(snapX), m.oy), Offset(m.sx(snapX), m.sy(m.compH)), snapStroke)
    val snapY = ui.snapY
    if (!snapY.isNaN()) drawLine(ShellColors.SnapLine, Offset(m.ox, m.sy(snapY)), Offset(m.sx(m.compW), m.sy(snapY)), snapStroke)

    // Rastreio de câmera: os pontos seguidos neste quadro (amarelo = entrou no
    // solve, vermelho = rejeitado), como no AE.
    store.cameraFeatures?.let { f ->
        val arm = 3.dp.toPx()
        val w = 1.2.dp.toPx()
        var k = 0
        while (k + 2 < f.size) {
            val c = Offset(m.sx(f[k]), m.sy(f[k + 1]))
            val col = if (f[k + 2] > 0.5f) TrackSolved else TrackRejected
            drawLine(Color.Black.copy(alpha = 0.5f), Offset(c.x - arm - 1, c.y), Offset(c.x + arm + 1, c.y), w + 1.5f)
            drawLine(Color.Black.copy(alpha = 0.5f), Offset(c.x, c.y - arm - 1), Offset(c.x, c.y + arm + 1), w + 1.5f)
            drawLine(col, Offset(c.x - arm, c.y), Offset(c.x + arm, c.y), w)
            drawLine(col, Offset(c.x, c.y - arm), Offset(c.x, c.y + arm), w)
            k += 3
        }
    }
    // Rastreio de ponto: a mira onde o dedo está (bloco seguido + janela de busca).
    store.pickCursor?.let { p ->
        val c = Offset(m.sx(p.x), m.sy(p.y))
        val boxes = store.pickBoxes
        val inner = boxes[0] * 0.5f * m.fit
        val outer = boxes[1] * 0.5f * m.fit
        val stroke = 1.5.dp.toPx()
        drawRect(Color.Black.copy(alpha = 0.55f), Offset(c.x - outer, c.y - outer), androidx.compose.ui.geometry.Size(outer * 2, outer * 2), style = Stroke(stroke + 2f))
        drawRect(TrackSolved, Offset(c.x - outer, c.y - outer), androidx.compose.ui.geometry.Size(outer * 2, outer * 2), style = Stroke(stroke))
        drawRect(Color.Black.copy(alpha = 0.55f), Offset(c.x - inner, c.y - inner), androidx.compose.ui.geometry.Size(inner * 2, inner * 2), style = Stroke(stroke + 2f))
        drawRect(Color.White, Offset(c.x - inner, c.y - inner), androidx.compose.ui.geometry.Size(inner * 2, inner * 2), style = Stroke(stroke))
        val arm = 10.dp.toPx()
        drawLine(Color.Black.copy(alpha = 0.55f), Offset(c.x - arm, c.y), Offset(c.x + arm, c.y), stroke + 2f)
        drawLine(Color.Black.copy(alpha = 0.55f), Offset(c.x, c.y - arm), Offset(c.x, c.y + arm), stroke + 2f)
        drawLine(Color.White, Offset(c.x - arm, c.y), Offset(c.x + arm, c.y), stroke)
        drawLine(Color.White, Offset(c.x, c.y - arm), Offset(c.x, c.y + arm), stroke)
    }

    // Painel de Máscara aberto: os caminhos da camada (a editada com pontos e alças).
    val masking = maskMode(store, ui)
    if (ui.panel == com.aurea.aurea.editor.panels.EditorPanel.Mask) {
        store.masks?.let { drawMasks(m, it, store.maskEdit, store.maskPoint, store.maskDrawing) }
    }

    val selection = store.selection
    val d = store.detail ?: return
    if (selection.isEmpty()) return
    val playhead = store.playhead

    // Outras do lote: traço de fundo 2,5 + traço 1,5 em destaque. Ler
    // `layers` aqui faz o desenho acompanhar quando SÓ elas mudam (alinhar).
    if (selection.size > 1 && store.layers.isNotEmpty()) {
        for (id in selection) {
            if (id == d.id) continue
            val od = store.queryDetail(id) ?: continue
            if (!activeAt(od, playhead) || !toScreenCorners(od, m, m.scratch)) continue
            outline(m, m.scratch, m.batchUnder, m.batchOver, AureaColors.Accent)
        }
    }
    if (!activeAt(d, playhead) || !toScreenCorners(d, m, m.corners)) return
    // Principal: escuro 55 % em 3,5 e destaque em 2 (travada: secundário).
    outline(m, m.corners, m.outlineUnder, m.outlineOver, if (d.locked) AureaColors.Muted else AureaColors.Accent)
    if (selection.size != 1 || d.locked || masking) return   // no modo de máscara o toque é do caminho
    // Editar forma: alças de tamanho e raio da silhueta no lugar de escala/giro.
    if (shapeEditMode(store, ui)) {
        drawShapeEditHandles(store, m)
        return
    }
    placeHandles(m)
    m.handlesValid = true
    val grabbed = ui.grabbedHandle
    for (i in 1..3) {
        val r = (if (grabbed == i) 6.dp else 5.dp).toPx()
        val c = Offset(m.handles[i * 2], m.handles[i * 2 + 1])
        drawCircle(ShellColors.OutlineUnder, r + 1.5.dp.toPx(), c)
        drawCircle(Color.White, r, c)
        drawCircle(AureaColors.Accent, r, c, style = m.ring15)
    }
    drawRotateHandle(m, grabbed == 0)
    // Camada no espaço 3D: as setas do mundo por cima (têm prioridade no toque).
    store.gizmo?.let { drawGizmo(m, it) }
}

private val TrackSolved = Color(0xFFFFD34D)
private val TrackRejected = Color(0xFFFF5A5A)

private val GizmoX = Color(0xFFFF5A5A)
private val GizmoY = Color(0xFF5AD27A)
private val GizmoZ = Color(0xFF5AA8FF)

/** Ponta de cada eixo na tela (Z colapsado vira uma alça ao lado da origem). */
private fun gizmoTips(m: StageMapper, g: FloatArray, zOffset: Float): FloatArray {
    val t = FloatArray(8)
    t[0] = m.sx(g[0]); t[1] = m.sy(g[1])
    for (i in 1..3) { t[i * 2] = m.sx(g[i * 2]); t[i * 2 + 1] = m.sy(g[i * 2 + 1]) }
    if (kotlin.math.hypot(t[6] - t[0], t[7] - t[1]) < zOffset * 0.6f) {
        t[6] = t[0] + zOffset * 0.7f
        t[7] = t[1] - zOffset * 0.7f
    }
    return t
}

/** Gizmo 3D: setas X (vermelha), Y (verde) e Z (azul) do mundo, na origem da camada. */
private fun DrawScope.drawGizmo(m: StageMapper, g: FloatArray) {
    val t = gizmoTips(m, g, 44.dp.toPx())
    val o = Offset(t[0], t[1])
    val colors = arrayOf(GizmoX, GizmoY, GizmoZ)
    for (i in 1..3) {
        val tip = Offset(t[i * 2], t[i * 2 + 1])
        drawLine(ShellColors.OutlineUnder, o, tip, 5.dp.toPx())
        drawLine(colors[i - 1], o, tip, 2.5.dp.toPx())
        drawCircle(ShellColors.OutlineUnder, 9.dp.toPx(), tip)
        drawCircle(colors[i - 1], 7.5.dp.toPx(), tip)
    }
    drawCircle(Color.White, 4.dp.toPx(), o)
}

private fun activeAt(d: LayerDetail, frame: Int) = frame >= d.startFrame && frame < d.endFrame

private fun toScreenCorners(d: LayerDetail, m: StageMapper, out: FloatArray): Boolean {
    if (!LayerGeometry.corners(d, out)) return false
    for (i in 0 until 4) {
        out[i * 2] = m.sx(out[i * 2])
        out[i * 2 + 1] = m.sy(out[i * 2 + 1])
    }
    return true
}

private fun DrawScope.outline(m: StageMapper, pts: FloatArray, under: Stroke, over: Stroke, color: Color) {
    val p = m.path
    p.reset()
    p.moveTo(pts[0], pts[1])
    p.lineTo(pts[2], pts[3])
    p.lineTo(pts[4], pts[5])
    p.lineTo(pts[6], pts[7])
    p.close()
    drawPath(p, ShellColors.OutlineUnder, style = under)
    drawPath(p, color, style = over)
}

/** Alça → canto: giro = TR(1), escala = BR(2), TL(0), BL(3). */
private val HANDLE_CORNER = intArrayOf(1, 2, 0, 3)

/**
 * Posição das alças (`alcas_do_palco.dart`): cada uma a ≥ 30 dp do centro;
 * se giro (sup-dir) e escala inf-dir ficam a < 60 dp, separam ±30 dp em Y a
 * partir do meio; todas presas 22 dp para dentro do palco.
 */
private fun DrawScope.placeHandles(m: StageMapper) {
    val c = m.corners
    val cx = (c[0] + c[2] + c[4] + c[6]) / 4
    val cy = (c[1] + c[3] + c[5] + c[7]) / 4
    val minR = 30.dp.toPx()
    for (i in 0 until 4) {
        val k = HANDLE_CORNER[i]
        var hx = c[k * 2]
        var hy = c[k * 2 + 1]
        var dx = hx - cx
        var dy = hy - cy
        var dist = hypot(dx, dy)
        if (dist < minR) {
            if (dist < 1e-3f) {
                dx = if (k == 1 || k == 2) 1f else -1f
                dy = if (k >= 2) 1f else -1f
                dist = hypot(dx, dy)
            }
            hx = cx + dx / dist * minR
            hy = cy + dy / dist * minR
        }
        m.handles[i * 2] = hx
        m.handles[i * 2 + 1] = hy
    }
    val sep = 60.dp.toPx()
    if (hypot(m.handles[0] - m.handles[2], m.handles[1] - m.handles[3]) < sep) {
        val mid = (m.handles[1] + m.handles[3]) / 2
        m.handles[1] = mid - sep / 2
        m.handles[3] = mid + sep / 2
    }
    val edge = 22.dp.toPx()
    for (i in 0 until 4) {
        m.handles[i * 2] = m.handles[i * 2].coerceIn(edge, max(edge, m.boxW - edge))
        m.handles[i * 2 + 1] = m.handles[i * 2 + 1].coerceIn(edge, max(edge, m.boxH - edge))
    }
}

/** Pegador de giro: disco, arco de 270° com ponta de seta, anel em destaque. */
private fun DrawScope.drawRotateHandle(m: StageMapper, grabbed: Boolean) {
    val c = Offset(m.handles[0], m.handles[1])
    val r = (35f / 2f * (if (grabbed) 0.62f else 0.55f)).dp.toPx()
    drawCircle(ShellColors.OutlineUnder, r + 1.5.dp.toPx(), c)
    drawCircle(if (grabbed) AureaColors.Accent else AureaColors.EditorPanelHigh, r, c)
    if (!grabbed) drawCircle(AureaColors.Accent, r, c, style = m.ring1)
    val ar = r * 0.52f
    val start = -162f
    val sweep = 270f
    drawArc(
        color = Color.White,
        startAngle = start,
        sweepAngle = sweep,
        useCenter = false,
        topLeft = Offset(c.x - ar, c.y - ar),
        size = androidx.compose.ui.geometry.Size(ar * 2, ar * 2),
        style = m.arc,
    )
    // Ponta de seta no fim do arco, apontando no sentido do giro (horário).
    val end = Math.toRadians((start + sweep).toDouble())
    val ex = c.x + ar * cos(end).toFloat()
    val ey = c.y + ar * sin(end).toFloat()
    val tx = -sin(end).toFloat()   // tangente horária
    val ty = cos(end).toFloat()
    val nx = cos(end).toFloat()    // normal (para fora)
    val ny = sin(end).toFloat()
    val s = r * 0.34f
    val a = m.arrow
    a.reset()
    a.moveTo(ex + tx * s, ey + ty * s)
    a.lineTo(ex - tx * s * 0.2f + nx * s * 0.8f, ey - ty * s * 0.2f + ny * s * 0.8f)
    a.lineTo(ex - tx * s * 0.2f - nx * s * 0.8f, ey - ty * s * 0.2f - ny * s * 0.8f)
    a.close()
    drawPath(a, Color.White)
}

// =============================================================================
// Gestos do palco (§3.4): um dono por gesto, nunca troca no meio
// =============================================================================

private const val TARGET_EMPTY = 0
private const val TARGET_HANDLE = 1
private const val TARGET_LAYER = 2

private const val MODE_PENDING = 0
private const val MODE_MOVE = 1
private const val MODE_SCALE = 2
private const val MODE_ROTATE = 3
private const val MODE_PINCH = 4
private const val MODE_IDLE = 5      // gesto recusado ou pinça encerrada: espera todos subirem

/**
 * O árbitro do palco. No toque: alça (giro r 22 / escala r 26, a mais
 * próxima) > dentro da camada JÁ escolhida > camada de cima visível > vazio.
 * Solta sem andar = toque (escolhe a de cima; vazio desseleciona). Anda
 * 18 dp (4 numa alça) = arrasto do alvo. 2º dedo = pinça da camada
 * escolhida (escala + giro, zona morta de 4°, nunca posição). Cada gesto
 * contínuo é UM passo de desfazer.
 */
private suspend fun PointerInputScope.stageGestures(
    store: EditorStore,
    ui: EditorUi,
    m: StageMapper,
    haptic: HapticFeedback,
) {
    val slop = ShellDims.TouchSlop.toPx()
    val handleSlop = ShellDims.HandleSlop.toPx()
    val hitSlack = ShellDims.HitSlack.toPx()
    val rotateTarget = ShellDims.RotateHandleTarget.toPx()
    val scaleTarget = ShellDims.ScaleHandleTarget.toPx()
    val edit = StageEdit(
        store, ui, m, haptic,
        lockMajor = 24.dp.toPx(),
        lockMinor = 12.dp.toPx(),
        minPivot = 8.dp.toPx(),
        snapTol = ShellDims.SnapTolerance.toPx(),
    )

    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false)
        val downX = down.position.x
        val downY = down.position.y

        // Modo vetorial: pontos do caminho ou traço da mão livre.
        if (store.vectorTool != 0) {
            vectorGesture(store, m, down)
            return@awaitEachGesture
        }

        // Editar forma: as alças da silhueta (tamanho/raio) têm a vez.
        if (m.valid && shapeEditMode(store, ui)) {
            val sh = pickShapeHandle(downX, downY, 26.dp.toPx())
            if (sh >= 0) {
                shapeEditGesture(store, m, sh, down)
                return@awaitEachGesture
            }
        }

        // Gizmo 3D: tocar numa ponta de seta arrasta no eixo do mundo.
        val gz = store.gizmo
        if (gz != null && store.selection.size == 1 && m.valid) {
            val zOff = 44.dp.toPx()
            val tips = gizmoTips(m, gz, zOff)
            val reach = 24.dp.toPx()
            var axis = -1
            var best = reach
            for (i in 1..3) {
                val dd = kotlin.math.hypot(downX - tips[i * 2], downY - tips[i * 2 + 1])
                if (dd < best) { best = dd; axis = i - 1 }
            }
            if (axis >= 0) {
                val ax = tips[(axis + 1) * 2] - tips[0]
                val ay = tips[(axis + 1) * 2 + 1] - tips[1]
                val len2 = ax * ax + ay * ay
                val zCollapsed = axis == 2 && kotlin.math.hypot(m.sx(gz[6]) - tips[0], m.sy(gz[7]) - tips[1]) < zOff * 0.6f
                var last = down.position
                store.beginGesture("mover no eixo ${"XYZ"[axis]}")
                do {
                    val e = awaitPointerEvent()
                    val c = e.changes.firstOrNull { it.id == down.id } ?: break
                    c.consume()
                    val dx = c.position.x - last.x
                    val dy = c.position.y - last.y
                    last = c.position
                    val amount = if (zCollapsed) {
                        // Olhando reto para Z: arrastar para cima afasta (Z+ é para dentro).
                        -dy / m.fit * 2f
                    } else if (len2 > 1f) {
                        (dx * ax + dy * ay) / len2 * GIZMO_LENGTH
                    } else {
                        0f
                    }
                    if (amount != 0f) store.gizmoDrag(axis, amount)
                } while (c.pressed)
                store.endGesture()
                return@awaitEachGesture
            }
        }

        // Escolhendo o ponto do rastreio: a mira segue o dedo (dá para ajustar
        // antes de soltar); soltar vira a coordenada da camada.
        if (store.pointPick != null) {
            val d = store.detail
            val q = FloatArray(8)
            var upX = downX
            var upY = downY
            if (m.valid) store.pickCursor = Offset(m.cx(downX), m.cy(downY))
            do {
                val ev = awaitPointerEvent()
                val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                ch.consume()
                upX = ch.position.x
                upY = ch.position.y
                if (m.valid) store.pickCursor = Offset(m.cx(upX), m.cy(upY))
            } while (ch.pressed)
            if (d != null && m.valid && LayerGeometry.corners(d, q)) {
                val cx = m.cx(upX)
                val cy = m.cy(upY)
                // Bloco (17 px) e janela de busca (17 + 2·24 px) do rastreio, que roda
                // numa miniatura de até 360 px de altura → px da camada → composição.
                val layerH = LayerGeometry.height(d).coerceAtLeast(1f)
                val thumb = layerH / kotlin.math.min(360f, layerH)
                val compPerLayer = kotlin.math.hypot(q[2] - q[0], q[3] - q[1]) / LayerGeometry.width(d).coerceAtLeast(1f)
                store.pickBoxes = floatArrayOf(17f * thumb * compPerLayer, 65f * thumb * compPerLayer)
                // Afim pelos cantos TL, TR, BL: p = TL + u·(TR − TL) + v·(BL − TL).
                val ax = q[2] - q[0]; val ay = q[3] - q[1]
                val bx = q[6] - q[0]; val by = q[7] - q[1]
                val det = ax * by - ay * bx
                if (kotlin.math.abs(det) > 1e-6f) {
                    val px = cx - q[0]; val py = cy - q[1]
                    val u = (px * by - py * bx) / det
                    val v = (ax * py - ay * px) / det
                    if (u in 0f..1f && v in 0f..1f) {
                        store.finishPointPick(u * LayerGeometry.width(d), v * LayerGeometry.height(d))
                    } else {
                        store.pickCursor = null
                        store.showToast("Toque dentro do vídeo")
                    }
                }
            }
            return@awaitEachGesture
        }

        // Modo de máscara: o gesto edita o caminho da máscara escolhida.
        val ms = store.masks
        val editMask = store.maskEdit
        if (maskMode(store, ui) && ms != null && editMask != null && m.valid && ms.layer == store.primary) {
            maskGesture(store, m, ms, editMask, down, slop, 28.dp.toPx(), haptic)
            return@awaitEachGesture
        }

        // --- Alvo anotado no toque (nada muda ainda).
        var target = TARGET_EMPTY
        var handle = -1
        var targetLayer = 0L
        if (m.valid) {
            handle = pickHandle(m, downX, downY, rotateTarget, scaleTarget)
            if (handle >= 0) {
                target = TARGET_HANDLE
            } else {
                val d = store.detail
                val cx = m.cx(downX)
                val cy = m.cy(downY)
                if (d != null && store.selection.size == 1 && activeAt(d, store.playhead) && LayerGeometry.contains(d, cx, cy, 0f)) {
                    target = TARGET_LAYER
                    targetLayer = d.id
                } else {
                    hitLayer(store, cx, cy, 0f, includeLocked = true)?.let {
                        target = TARGET_LAYER
                        targetLayer = it
                    }
                }
            }
        }

        var mode = MODE_PENDING
        var multi = false
        var p1: PointerId? = null
        var p2: PointerId? = null
        edit.reset()
        try {
            while (true) {
                val event = awaitPointerEvent()
                var pressed = 0
                for (c in event.changes) if (c.pressed) pressed++
                if (pressed == 0) break

                // --- Segundo dedo: vira pinça (fecha o arrasto antes).
                if (pressed >= 2 && mode != MODE_PINCH && mode != MODE_IDLE) {
                    multi = true
                    edit.end()
                    val a = event.changes.first { it.pressed }
                    val b = event.changes.last { it.pressed }
                    val d = store.detail
                    val slack = if (m.fit > 0f) hitSlack / m.fit else 0f
                    val midX = (a.position.x + b.position.x) / 2
                    val midY = (a.position.y + b.position.y) / 2
                    if (d != null && m.valid && store.selection.size == 1 && !d.locked &&
                        activeAt(d, store.playhead) &&
                        (LayerGeometry.contains(d, m.cx(a.position.x), m.cy(a.position.y), slack) ||
                            LayerGeometry.contains(d, m.cx(b.position.x), m.cy(b.position.y), slack) ||
                            LayerGeometry.contains(d, m.cx(midX), m.cy(midY), slack))
                    ) {
                        p1 = a.id
                        p2 = b.id
                        edit.startPinch(d, a.position.x, a.position.y, b.position.x, b.position.y)
                        mode = MODE_PINCH
                    } else {
                        // Pinça da vista: o store ainda não tem zoom de palco.
                        mode = MODE_IDLE
                    }
                }

                when (mode) {
                    MODE_PENDING -> {
                        val c = event.changes.firstOrNull { it.id == down.id }
                        if (c != null && c.pressed) {
                            val moved = hypot(c.position.x - downX, c.position.y - downY)
                            val threshold = if (target == TARGET_HANDLE) handleSlop else slop
                            if (moved > threshold) {
                                mode = startDrag(store, edit, target, handle, targetLayer, downX, downY)
                                // O 1º passo aplica a folga inteira: o objeto fica sob o dedo.
                                edit.step(mode, c.position.x, c.position.y)
                            }
                        }
                    }
                    MODE_MOVE, MODE_SCALE, MODE_ROTATE -> {
                        val c = event.changes.firstOrNull { it.id == down.id }
                        if (c == null || !c.pressed) mode = MODE_IDLE
                        else edit.step(mode, c.position.x, c.position.y)
                    }
                    MODE_PINCH -> {
                        val a = event.changes.firstOrNull { it.id == p1 }
                        val b = event.changes.firstOrNull { it.id == p2 }
                        // Saiu um dedo da pinça da camada: o que sobra não faz nada.
                        if (a == null || b == null || !a.pressed || !b.pressed) mode = MODE_IDLE
                        else edit.pinch(a.position.x, a.position.y, b.position.x, b.position.y)
                    }
                }
                for (c in event.changes) c.consume()
            }
        } finally {
            edit.end()
        }

        // --- Solta sem andar: toque. Nunca mexe no relógio.
        if (mode == MODE_PENDING && !multi && target != TARGET_HANDLE && m.valid) {
            val cx = m.cx(downX)
            val cy = m.cy(downY)
            val hit = hitLayer(store, cx, cy, 0f, includeLocked = false)
                ?: hitLayer(store, cx, cy, if (m.fit > 0f) hitSlack / m.fit else 0f, includeLocked = false)
            if (hit != null) {
                if (store.primary != hit || store.selection.size != 1) store.select(hit)
            } else {
                store.clearSelection()
            }
        }
    }
}

/** Começa o arrasto do alvo anotado no toque; devolve o modo. */
private fun startDrag(store: EditorStore, edit: StageEdit, target: Int, handle: Int, layer: Long, downX: Float, downY: Float): Int {
    return when (target) {
        TARGET_HANDLE -> {
            val d = store.detail ?: return MODE_IDLE
            edit.startHandle(d, handle, downX, downY)
            if (handle == 0) MODE_ROTATE else MODE_SCALE
        }
        TARGET_LAYER -> {
            // Arrastar escolhe a camada no COMEÇO do gesto.
            if (store.primary != layer || store.selection.size != 1) store.select(layer)
            val row = store.layers.firstOrNull { it.id == layer }
            val d = store.detail
            when {
                row == null || d == null || d.id != layer -> MODE_IDLE
                row.locked -> {
                    // Recusa, e diz por quê (uma vez por gesto).
                    store.showToast("Camada bloqueada: desbloqueie para mover")
                    MODE_IDLE
                }
                else -> {
                    edit.startMove(d, downX, downY)
                    MODE_MOVE
                }
            }
        }
        else -> MODE_IDLE   // vazio: passear a vista pede zoom ≠ 1
    }
}

/**
 * O estado de UM gesto de edição no palco (valores iniciais, trava de eixo,
 * alvos de encaixe) e os passos que viram comandos do store. Reaproveitado
 * entre gestos: `reset` no começo, nada aloca por evento.
 */
private class StageEdit(
    private val store: EditorStore,
    private val ui: EditorUi,
    private val m: StageMapper,
    private val haptic: HapticFeedback,
    private val lockMajor: Float,
    private val lockMinor: Float,
    private val minPivot: Float,
    private val snapTol: Float,
) {
    private var began = false
    private val scratch = FloatArray(8)
    private val box = FloatArray(4)

    // Mover
    private var pos0x = 0f
    private var pos0y = 0f
    private var downX = 0f
    private var downY = 0f
    private var downCx = 0f
    private var downCy = 0f
    private var lastCx = 0f
    private var lastCy = 0f
    private var axisLock = 0              // 1 = só horizontal, 2 = só vertical
    private val offX = FloatArray(3)      // esquerda, centro, direita da caixa − posição
    private val offY = FloatArray(3)
    private var targetsX = FloatArray(0)
    private var targetsY = FloatArray(0)

    // Escala / giro (alça e pinça)
    private var sx0 = 1f
    private var sy0 = 1f
    private var rot0 = 0f
    private var pivotX = 0f
    private var pivotY = 0f
    private var dist0 = 1f
    private var lastAngle = 0f
    private var accAngle = 0f
    private var span0 = 1f
    private var rotActive = false
    private var rotOffset = 0f

    fun reset() {
        began = false
        axisLock = 0
        rotActive = false
        accAngle = 0f
    }

    /** Fecha o passo de desfazer (se houve mudança) e apaga o que o gesto desenhava. */
    fun end() {
        if (began) store.endGesture()
        began = false
        ui.manipulating = false
        ui.snapX = Float.NaN
        ui.snapY = Float.NaN
        ui.grabbedHandle = -1
    }

    private fun begin(label: String) {
        if (!began) {
            store.beginGesture(label)
            began = true
        }
    }

    /** Arrastar pausa a reprodução e troca o transporte pela barra de informações. */
    private fun engage() {
        if (store.playing) store.pause()
        ui.manipulating = true
    }

    private val pt = FloatArray(2)
    private var moveDetail: LayerDetail? = null

    private fun keepTransform(d: LayerDetail) {
        sx0 = d.scale[0]
        sy0 = d.scale[1]
        rot0 = d.rotation[2]
    }

    /** Fator de escala preso para que |escala| fique em [0,001; 100] nos dois eixos. */
    private fun clampFactor(f: Float): Float {
        val ax = max(abs(sx0), 1e-4f)
        val ay = max(abs(sy0), 1e-4f)
        val lo = max(0.001f / ax, 0.001f / ay)
        val hi = min(100f / ax, 100f / ay)
        return if (lo <= hi) f.coerceIn(lo, hi) else f
    }

    fun startMove(d: LayerDetail, x: Float, y: Float) {
        // Tudo em px da composição (mundo); só no fim volta ao espaço do pai.
        moveDetail = d
        d.parentToComp(d.position[0], d.position[1], pt)
        pos0x = pt[0]
        pos0y = pt[1]
        downX = x
        downY = y
        downCx = m.cx(x)
        downCy = m.cy(y)
        lastCx = downCx
        lastCy = downCy
        axisLock = 0
        if (LayerGeometry.bounds(d, scratch, box)) {
            offX[0] = box[0] - pos0x; offX[1] = (box[0] + box[2]) / 2 - pos0x; offX[2] = box[2] - pos0x
            offY[0] = box[1] - pos0y; offY[1] = (box[1] + box[3]) / 2 - pos0y; offY[2] = box[3] - pos0y
        } else {
            offX.fill(0f)
            offY.fill(0f)
        }
        collectSnapTargets(d.id)
        engage()
    }

    fun startHandle(d: LayerDetail, handle: Int, x: Float, y: Float) {
        keepTransform(d)
        d.parentToComp(d.position[0], d.position[1], pt)
        pivotX = m.sx(pt[0])
        pivotY = m.sy(pt[1])
        dist0 = max(minPivot, hypot(x - pivotX, y - pivotY))
        lastAngle = atan2(y - pivotY, x - pivotX)
        accAngle = 0f
        ui.grabbedHandle = handle
        engage()
    }

    fun startPinch(d: LayerDetail, ax: Float, ay: Float, bx: Float, by: Float) {
        keepTransform(d)
        span0 = max(1f, hypot(ax - bx, ay - by))
        lastAngle = atan2(by - ay, bx - ax)
        accAngle = 0f
        rotActive = false
        engage()
    }

    fun step(mode: Int, x: Float, y: Float) {
        when (mode) {
            MODE_MOVE -> move(x, y)
            MODE_SCALE -> {
                // Fator = distância(dedo, pivô) / distância inicial; preserva a
                // proporção X/Y e o espelhamento.
                val f = clampFactor(hypot(x - pivotX, y - pivotY) / dist0)
                begin("escala")
                store.setTransform2(TrackProperty.SCALE_X, sx0 * f, TrackProperty.SCALE_Y, sy0 * f)
            }
            MODE_ROTATE -> {
                // Giro relativo (ângulo varrido em volta do pivô), sem salto.
                val angle = atan2(y - pivotY, x - pivotX)
                accAngle += wrapRad(angle - lastAngle)
                lastAngle = angle
                begin("girar")
                store.setTransform(TrackProperty.ROTATION_Z, rot0 + Math.toDegrees(accAngle.toDouble()).toFloat())
            }
        }
    }

    private fun move(x: Float, y: Float) {
        val cx = m.cx(x)
        val cy = m.cy(y)
        var nx = pos0x + (cx - downCx)
        var ny = pos0y + (cy - downCy)
        // Trava de eixo: > 24 num eixo com < 12 no outro → o outro fica fixo.
        val sdx = abs(x - downX)
        val sdy = abs(y - downY)
        if (axisLock == 0) {
            if (sdx > lockMajor && sdy < lockMinor) axisLock = 1
            else if (sdy > lockMajor && sdx < lockMinor) axisLock = 2
        }
        if (axisLock == 1) ny = pos0y
        if (axisLock == 2) nx = pos0x
        // Encaixe de 10 dp de tela; passo maior que a tolerância não encaixa
        // ("quem passa correndo não está mirando").
        val tol = if (m.fit > 0f) snapTol / m.fit else 0f
        var snapX = Float.NaN
        var snapY = Float.NaN
        if (axisLock != 2 && abs(cx - lastCx) <= tol) {
            val t = nearestTarget(nx, offX, targetsX, tol)
            if (!t.isNaN()) {
                nx += deltaTo(nx, offX, t, tol)
                snapX = t
            }
        }
        if (axisLock != 1 && abs(cy - lastCy) <= tol) {
            val t = nearestTarget(ny, offY, targetsY, tol)
            if (!t.isNaN()) {
                ny += deltaTo(ny, offY, t, tol)
                snapY = t
            }
        }
        lastCx = cx
        lastCy = cy
        if ((!snapX.isNaN() && snapX != ui.snapX) || (!snapY.isNaN() && snapY != ui.snapY)) {
            haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
        }
        ui.snapX = snapX
        ui.snapY = snapY
        begin("mover")
        val d = moveDetail
        if (d != null) {
            d.compToParent(nx, ny, pt)
            nx = pt[0]
            ny = pt[1]
        }
        store.setTransform2(TrackProperty.POSITION_X, nx, TrackProperty.POSITION_Y, ny)
    }

    fun pinch(ax: Float, ay: Float, bx: Float, by: Float) {
        val f = clampFactor(hypot(ax - bx, ay - by) / span0)
        val angle = atan2(by - ay, bx - ax)
        accAngle += wrapRad(angle - lastAngle)
        lastAngle = angle
        val deg = Math.toDegrees(accAngle.toDouble()).toFloat()
        // Zona morta de 4°: sem ela toda pinça de escala entortava a camada.
        if (!rotActive && abs(deg) > 4f) {
            rotActive = true
            rotOffset = 4f * sign(deg)
        }
        begin("pinça")
        store.setTransform2(TrackProperty.SCALE_X, sx0 * f, TrackProperty.SCALE_Y, sy0 * f)
        if (rotActive) store.setTransform(TrackProperty.ROTATION_Z, rot0 + deg - rotOffset)
    }

    /**
     * Alvos do encaixe: centro e bordas da composição, e centro e bordas das
     * outras camadas ativas e visíveis. Uma vez, no começo do arrasto.
     */
    private fun collectSnapTargets(self: Long) {
        val t = store.playhead
        var n = 3
        for (row in store.layers) if (row.id != self && row.visible && LayerGeometry.activeAt(row, t)) n += 3
        if (targetsX.size < n) {
            targetsX = FloatArray(n)
            targetsY = FloatArray(n)
        }
        targetsX.fill(Float.NaN)
        targetsY.fill(Float.NaN)
        targetsX[0] = 0f; targetsX[1] = m.compW / 2; targetsX[2] = m.compW
        targetsY[0] = 0f; targetsY[1] = m.compH / 2; targetsY[2] = m.compH
        var i = 3
        for (row in store.layers) {
            if (row.id == self || !row.visible || !LayerGeometry.activeAt(row, t)) continue
            val d = store.queryDetail(row.id) ?: continue
            if (!LayerGeometry.bounds(d, scratch, box)) continue
            if (i + 3 > targetsX.size) break
            targetsX[i] = box[0]; targetsX[i + 1] = (box[0] + box[2]) / 2; targetsX[i + 2] = box[2]
            targetsY[i] = box[1]; targetsY[i + 1] = (box[1] + box[3]) / 2; targetsY[i + 2] = box[3]
            i += 3
        }
    }
}

private fun wrapRad(a: Float): Float {
    var v = a
    val pi = Math.PI.toFloat()
    while (v > pi) v -= 2 * pi
    while (v < -pi) v += 2 * pi
    return v
}

/** O alvo (px da composição) mais perto de uma das bordas/centro, ou NaN. */
private fun nearestTarget(pos: Float, offsets: FloatArray, targets: FloatArray, tol: Float): Float {
    var best = Float.NaN
    var bestAbs = tol
    for (o in offsets) {
        val v = pos + o
        for (t in targets) {
            if (t.isNaN()) continue
            val d = abs(t - v)
            if (d <= bestAbs) {
                bestAbs = d
                best = t
            }
        }
    }
    return best
}

/** Quanto andar para a borda/centro mais próxima encostar em [target]. */
private fun deltaTo(pos: Float, offsets: FloatArray, target: Float, tol: Float): Float {
    var bestD = 0f
    var bestAbs = Float.MAX_VALUE
    for (o in offsets) {
        val d = target - (pos + o)
        if (abs(d) <= tol && abs(d) < bestAbs) {
            bestAbs = abs(d)
            bestD = d
        }
    }
    return bestD
}

/** Alça sob o dedo (a mais próxima dentro do raio de toque), ou −1. */
private fun pickHandle(m: StageMapper, x: Float, y: Float, rotateR: Float, scaleR: Float): Int {
    if (!m.handlesValid) return -1
    var best = -1
    var bestD = Float.MAX_VALUE
    for (i in 0 until 4) {
        val d = hypot(x - m.handles[i * 2], y - m.handles[i * 2 + 1])
        val r = if (i == 0) rotateR else scaleR
        if (d <= r && d < bestD) {
            bestD = d
            best = i
        }
    }
    return best
}

/**
 * A camada de cima VISÍVEL no ponto (px da composição): fora áudio, oculta,
 * fora do tempo, sem tamanho e opacidade ≤ 1 %. Travada só entra como alvo de
 * arrasto (para avisar); o toque passa por ela.
 */
private fun hitLayer(store: EditorStore, cx: Float, cy: Float, slack: Float, includeLocked: Boolean): Long? {
    val t = store.playhead
    for (row in store.layers) {
        if (!row.visible || row.kind == LayerType.Audio.kind || !LayerGeometry.activeAt(row, t)) continue
        if (row.locked && !includeLocked) continue
        val d = store.queryDetail(row.id) ?: continue
        if (d.opacity <= 0.01f) continue
        if (LayerGeometry.contains(d, cx, cy, slack)) return row.id
    }
    return null
}


// =============================================================================
// Chip de resolução e HUD
// =============================================================================

/**
 * "Full" no canto sup-dir (0xCC171D25, raio 6, 12 sp). Toque: resolução da
 * prévia (só sessão, nunca o export). Toque longo: HUD de desempenho (DEV).
 * Some na tela cheia — ficava embaixo do "Voltar ao editor".
 */
@Composable
private fun ResolutionChip(store: EditorStore, ui: EditorUi, modifier: Modifier) {
    if (ui.fullscreen) return
    var open by remember { mutableStateOf(false) }
    val label = when (val l = store.preview.scaleLabel) {
        "FULL" -> "Full"
        else -> l
    }
    Box(modifier) {
        Box(
            Modifier
                .clip(RoundedCornerShape(6.dp))
                .background(ShellColors.ResolutionChip)
                .semantics { contentDescription = "Resolução da prévia · segure para o diagnóstico" }
                .tocavel(haptic = true, onLongClick = { store.toggleHud() }) { open = true }
                .padding(horizontal = 10.dp, vertical = 8.dp),
        ) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
        }
        if (open) {
            val current = store.preview.scaleLabel
            ShellPopupMenu(
                items = listOf(
                    PopupItem("AUTO", current == "AUTO") { store.setPreviewScale(true) },
                    PopupItem("Full", current == "FULL") { store.setPreviewScale(false, 1, 1) },
                    PopupItem("1/2", current == "1/2") { store.setPreviewScale(false, 1, 2) },
                    PopupItem("1/4", current == "1/4") { store.setPreviewScale(false, 1, 4) },
                    PopupItem("1/8", current == "1/8") { store.setPreviewScale(false, 1, 8) },
                ),
                onDismiss = { open = false },
                width = 160.dp,
            )
        }
    }
}

/** HUD de desempenho (Fase 2, DEV): monoespaçado, canto sup-esq, semitransparente. */
@Composable
private fun PerfHud(store: EditorStore, modifier: Modifier) {
    if (!store.hudVisible) return
    val text = hudText(store.perf, store.uiFps, store.appMemory)
    Box(
        modifier
            .clip(RoundedCornerShape(8.dp))
            .background(ShellColors.FloatingDark)
            .border(0.5.dp, AureaColors.Border, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(
            text,
            style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 14.sp, color = AureaColors.Accent),
        )
    }
}

/** Nome do nível térmico do motor (`ThermalState::Level`). */
private fun thermalName(level: Int) = when (level) {
    0 -> "normal"
    1 -> "morno"
    2 -> "sério"
    3 -> "crítico"
    4 -> "emergência"
    else -> "desconhecido"
}

/**
 * Texto da HUD (Fase 8A, §3). Só aparece o que foi MEDIDO: GPU sem timestamp
 * sai "—", ritmo só depois de tocar, áudio só com a saída aberta, 3D e
 * partículas só com cena/emissor no quadro. Nada estimado para parecer bonito.
 */
private fun hudText(p: PerfStats, uiFps: Float, m: EditorStore.AppMemory): String {
    fun f1(v: Float) = String.format(Locale.ROOT, "%.1f", v)
    fun mb(b: Long) = (b / (1024 * 1024)).toString()
    fun gpu(v: Float) = if (p.gpuTimers) f1(v) else "—"
    fun rate(hits: Int, misses: Int): String {
        val total = hits + misses
        return if (total == 0) "—" else "${hits * 100 / total}% ($hits/$total)"
    }
    val scale = if (p.renderScaleDen <= 1 && p.renderScaleNum <= 1) "1/1" else "${p.renderScaleNum}/${p.renderScaleDen}"
    return buildString {
        append("prévia ").append(f1(p.previewFps)).append(" fps · UI ").append(f1(uiFps)).append(" fps")
        if (m.uiSlowFrames > 0) append(" (").append(m.uiSlowFrames).append(" lentos, pior ").append(f1(m.uiWorstFrameMs)).append(" ms)")
        append('\n')
        if (p.pacingSamples > 0) {
            append("ritmo p50 ").append(f1(p.pacingP50Ms)).append(" · p95 ").append(f1(p.pacingP95Ms))
                .append(" · p99 ").append(f1(p.pacingP99Ms)).append(" · σ ").append(f1(p.pacingStdMs))
                .append(" ms (").append(p.pacingSamples).append(")\n")
        }
        append("cpu ").append(f1(p.cpuFrameMs)).append(" (prep ").append(f1(p.cpuPrepareMs))
            .append(" · grav ").append(f1(p.cpuRecordMs)).append(") · gpu ").append(gpu(p.gpuFrameMs))
            .append(" · orçamento ").append(f1(p.frameBudgetMs)).append(" ms\n")
        append("decode ").append(f1(p.decodeMs)).append(" · cor ").append(gpu(p.colorConvMs))
            .append(" · efeitos ").append(gpu(p.effectsMs)).append('\n')
        append("blur ").append(gpu(p.blurMs)).append(" · glow ").append(gpu(p.glowMs))
            .append(" · comp ").append(gpu(p.compositeMs)).append('\n')
        append("saída ").append(gpu(p.outputMs)).append(" · present ").append(f1(p.presentMs))
            .append(" · acquire ").append(f1(p.acquireMs)).append('\n')
        append("descartes ").append(p.droppedFrames).append(" (recentes ").append(p.droppedRecent)
            .append(") · seek ").append(f1(p.lastSeekMs)).append(" ms\n")
        append("escala ").append(scale).append(if (p.renderAuto) " auto" else "")
            .append(" · ").append(p.previewWidth).append('×').append(p.previewHeight)
            .append(" · térmico ").append(thermalName(p.thermal))
        if (p.heavyScale < 1f) append(" (caros ×").append(String.format(Locale.ROOT, "%.2f", p.heavyScale)).append(')')
        append('\n')
        append("cache decode ").append(p.decodedCacheFrames).append(" quadros · ").append(mb(p.decodedCacheBytes)).append(" MB\n")
        if (p.flowCacheHits + p.flowCacheMisses + p.maskCacheHits + p.maskCacheMisses > 0) {
            append("acerto flow ").append(rate(p.flowCacheHits, p.flowCacheMisses))
                .append(" · máscara ").append(rate(p.maskCacheHits, p.maskCacheMisses)).append('\n')
        }
        append("RAM motor ").append(mb(p.ramBytes)).append('/').append(p.memoryBudgetMB).append(" MB · nativo ")
            .append(mb(m.nativeHeapBytes)).append(" · Java ").append(mb(m.javaHeapBytes)).append(" MB\n")
        if (m.systemTotalBytes > 0) {
            append("sistema livre ").append(mb(m.systemAvailBytes)).append('/').append(mb(m.systemTotalBytes)).append(" MB")
                .append(if (m.lowMemory) " · MEMÓRIA BAIXA" else "").append('\n')
        }
        append("GPU ").append(mb(p.gpuMemoryBytes)).append(" MB usada · ").append(mb(p.gpuReservedBytes))
            .append(" reservada · ").append(p.gpuAllocations).append(" alocações · transit. ")
            .append(mb(p.transientBytes)).append(" MB\n")
        append("passes ").append(p.passesExecuted).append(" (+").append(p.passesCulled).append(" cortados)")
            .append(" · draws ").append(p.drawCalls).append(" · camadas ").append(p.layersRendered)
            .append(" · efeitos ").append(p.activeEffects).append('\n')
        if (p.draws3D > 0 || p.triangles3D > 0) {
            append("3D draws ").append(p.draws3D).append(" · triângulos ").append(p.triangles3D)
                .append(" · fora do frustum ").append(p.culled3D).append(" · residente ").append(mb(p.scene3dBytes)).append(" MB\n")
        }
        if (p.particles > 0) append("partículas ").append(p.particles).append('\n')
        if (p.audioOutputOpen) {
            append("áudio fila ").append(p.audioQueuedMs).append(" ms · saída ").append(p.audioOutputMs)
                .append(" ms · underruns ").append(p.audioUnderruns).append(" · sem bloco ").append(p.audioMissingBlocks).append('\n')
        }
        append("texturas ").append(p.physicalTextures).append(" fís · ").append(p.aliasedTextures)
            .append(" alias · pipelines ").append(p.pipelinesTotal).append(" (+").append(p.pipelineCompilesLive).append(")\n")
        append("seeks ").append(p.seeks).append(" · coalescidos ").append(p.coalesced)
            .append(" · atrasados ").append(p.staleFrames).append('\n')
        append("decoder ").append(p.decoder.ifEmpty { "—" }).append(if (p.hardwareDecoder) " (HW)" else "")
            .append(if (p.zeroCopy) " · zero-copy" else "").append('\n')
        append("GPU ").append(p.gpuName.ifEmpty { "—" })
            .append(if (p.gpuTimers) " · timers" else " · sem timestamp")
    }
}

// =============================================================================
// Modo de máscara (roto): desenho do caminho e gestos
// =============================================================================

private val MaskEditColor = Color(0xFFFFD34D)
private val MaskOtherColor = Color(0xB3FFFFFF)

/** Painel de Máscara aberto com uma máscara escolhida: o palco edita o caminho. */
internal fun maskMode(store: EditorStore, ui: EditorUi): Boolean =
    ui.panel == com.aurea.aurea.editor.panels.EditorPanel.Mask && store.maskEdit != null && store.masks != null

/** Caminhos das máscaras da camada na tela; a editada com pontos e alças. */
private fun DrawScope.drawMasks(m: StageMapper, ms: EditorStore.MaskState, edit: Int?, sel: Int, drawing: Boolean) {
    fun sx(x: Float, y: Float) = m.sx(ms.toCompX(x, y))
    fun sy(x: Float, y: Float) = m.sy(ms.toCompY(x, y))
    val path = m.maskPath
    for (mask in ms.masks) {
        val n = mask.count
        if (n == 0) continue
        val p = mask.points
        val on = mask.id == edit
        path.reset()
        path.moveTo(sx(p[0], p[1]), sy(p[0], p[1]))
        val segs = if (mask.closed) n else n - 1
        for (i in 0 until segs) {
            val a = i * 6
            val b = ((i + 1) % n) * 6
            val c1x = p[a] + p[a + 4]
            val c1y = p[a + 1] + p[a + 5]
            val c2x = p[b] + p[b + 2]
            val c2y = p[b + 1] + p[b + 3]
            path.cubicTo(sx(c1x, c1y), sy(c1x, c1y), sx(c2x, c2y), sy(c2x, c2y), sx(p[b], p[b + 1]), sy(p[b], p[b + 1]))
        }
        if (mask.closed) path.close()
        drawPath(path, ShellColors.OutlineUnder, style = m.outlineUnder)
        drawPath(path, if (on) MaskEditColor else MaskOtherColor, style = if (on) m.outlineOver else m.batchOver)
        if (!on) continue
        // Pontos: quadrados (o 1º maior enquanto desenha: tocar nele fecha).
        val half = 5.5.dp.toPx()
        for (i in 0 until n) {
            val c = Offset(sx(p[i * 6], p[i * 6 + 1]), sy(p[i * 6], p[i * 6 + 1]))
            val h = if (drawing && i == 0 && n >= 3) half * 1.6f else half
            drawRect(ShellColors.OutlineUnder, Offset(c.x - h - 1.5f, c.y - h - 1.5f), androidx.compose.ui.geometry.Size(2 * h + 3f, 2 * h + 3f))
            drawRect(if (i == sel) MaskEditColor else Color.White, Offset(c.x - h, c.y - h), androidx.compose.ui.geometry.Size(2 * h, 2 * h))
        }
        // Alças de bezier do ponto escolhido.
        if (sel in 0 until n) {
            val o = sel * 6
            val c = Offset(sx(p[o], p[o + 1]), sy(p[o], p[o + 1]))
            for (k in intArrayOf(2, 4)) {
                if (p[o + k] == 0f && p[o + k + 1] == 0f) continue
                val tx = p[o] + p[o + k]
                val ty = p[o + 1] + p[o + k + 1]
                val t = Offset(sx(tx, ty), sy(tx, ty))
                drawLine(ShellColors.OutlineUnder, c, t, 3f * density)
                drawLine(MaskEditColor, c, t, 1.5f * density)
                drawCircle(ShellColors.OutlineUnder, 7.5.dp.toPx(), t)
                drawCircle(MaskEditColor, 6.dp.toPx(), t)
            }
        }
    }
}

/**
 * Um gesto no modo de máscara. Alça do ponto escolhido > ponto > vazio.
 * Desenhando: tocar no vazio põe um ponto (arrastar puxa as alças dele,
 * simétricas); tocar no 1º ponto (com 3 ou mais) fecha. Arrastar um ponto o
 * move; arrastar uma alça curva (a oposta espelha). Um gesto = um passo de
 * desfazer (o 1º envio abre, os outros entram nele).
 */
private suspend fun androidx.compose.ui.input.pointer.AwaitPointerEventScope.maskGesture(
    store: EditorStore,
    m: StageMapper,
    ms: EditorStore.MaskState,
    editId: Int,
    down: androidx.compose.ui.input.pointer.PointerInputChange,
    slop: Float,
    reach: Float,
    haptic: HapticFeedback,
) {
    val mask = ms.find(editId) ?: return
    val n = mask.count
    var cur = mask.points.copyOf()
    val closed = mask.closed
    fun scrX(x: Float, y: Float) = m.sx(ms.toCompX(x, y))
    fun scrY(x: Float, y: Float) = m.sy(ms.toCompY(x, y))
    val dx = down.position.x
    val dy = down.position.y
    // 1 ponto, 2 alça de entrada, 3 alça de saída.
    var hitKind = 0
    var hitIdx = -1
    var best = reach
    val sel = store.maskPoint
    if (sel in 0 until n) {
        val o = sel * 6
        for ((kind, k) in listOf(2 to 2, 3 to 4)) {
            if (cur[o + k] == 0f && cur[o + k + 1] == 0f) continue
            val tx = cur[o] + cur[o + k]
            val ty = cur[o + 1] + cur[o + k + 1]
            val d = hypot(scrX(tx, ty) - dx, scrY(tx, ty) - dy)
            if (d < best) { best = d; hitKind = kind; hitIdx = sel }
        }
    }
    if (hitKind == 0) {
        for (i in 0 until n) {
            val d = hypot(scrX(cur[i * 6], cur[i * 6 + 1]) - dx, scrY(cur[i * 6], cur[i * 6 + 1]) - dy)
            if (d < best) { best = d; hitKind = 1; hitIdx = i }
        }
    }
    var undo = true
    var created = -1
    if (hitKind == 0 && store.maskDrawing) {
        val lp = ms.toLayer(m.cx(dx), m.cy(dy)) ?: return
        cur = cur.copyOf(cur.size + 6)
        cur[n * 6] = lp[0]
        cur[n * 6 + 1] = lp[1]
        created = n
        store.setMaskPoints(editId, cur, false, true)
        undo = false
        store.maskPoint = created
        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
    }
    var moved = false
    do {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        c.consume()
        if (!moved && hypot(c.position.x - dx, c.position.y - dy) > slop) moved = true
        if (!moved || (hitKind == 0 && created < 0)) continue
        val lp = ms.toLayer(m.cx(c.position.x), m.cy(c.position.y)) ?: continue
        val i = if (created >= 0) created else hitIdx
        val o = i * 6
        when {
            created >= 0 || hitKind == 3 -> {
                val ox = lp[0] - cur[o]
                val oy = lp[1] - cur[o + 1]
                cur[o + 4] = ox; cur[o + 5] = oy
                cur[o + 2] = -ox; cur[o + 3] = -oy
            }
            hitKind == 2 -> {
                val ix = lp[0] - cur[o]
                val iy = lp[1] - cur[o + 1]
                cur[o + 2] = ix; cur[o + 3] = iy
                cur[o + 4] = -ix; cur[o + 5] = -iy
            }
            hitKind == 1 -> {
                cur[o] = lp[0]
                cur[o + 1] = lp[1]
            }
        }
        store.setMaskPoints(editId, cur, if (created >= 0) false else closed, undo)
        undo = false
    } while (c.pressed)
    if (!moved) {
        when {
            hitKind == 1 && store.maskDrawing && hitIdx == 0 && n >= 3 -> store.closeMaskPath()
            hitKind == 1 -> store.maskPoint = hitIdx
            hitKind == 0 && !store.maskDrawing -> store.maskPoint = -1
        }
    }
    store.maskGestureEnd()
}
