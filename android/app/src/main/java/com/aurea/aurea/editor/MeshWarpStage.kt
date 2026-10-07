package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.hypot

/**
 * MALHA DE DEFORMAÇÃO no palco (aurea.distort.mesh_warp), como o Mesh Warp do
 * AE: com o cartão do efeito aberto, a grade deformada e os vértices aparecem
 * sobre a camada. Arrastar um vértice o move (as alças vão junto); com um
 * vértice escolhido, as 4 alças de bezier dele aparecem e também arrastam.
 * Cada arrasto = UM passo de desfazer; com o auto-key do palco ligado (ou a
 * malha já animada) o motor grava o key da malha no cabeçote.
 *
 * O motor guarda e avalia a malha (Engine::query_mesh_warp e família), em
 * coordenadas normalizadas à caixa da camada; aqui só dedo e desenho, levando
 * (u, v) à composição pelos cantos da camada.
 */
internal object MeshWarpStage {
    const val HEADER = 4
    const val FLOATS = 10
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)
    /** Vértice escolhido (alças à mostra); −1 = nenhum. */
    var selected by mutableIntStateOf(-1)

    fun open(layerId: Long, effectId: Int) {
        if (layer != layerId || effect != effectId) selected = -1
        layer = layerId
        effect = effectId
    }

    fun close(effectId: Int) {
        if (effect != effectId) return
        effect = -1
        selected = -1
    }
}

/** O palco está na malha do efeito aberto na camada escolhida? */
internal fun meshWarpActive(store: EditorStore): Boolean {
    if (MeshWarpStage.effect < 0 || store.selection.size != 1 || store.primary != MeshWarpStage.layer) return false
    val d = store.detail ?: return false
    return d.id == MeshWarpStage.layer && !d.locked
}

/** Cantos da camada na composição (0,0 · w,0 · w,h · 0,h) ou null. */
private fun meshCorners(store: EditorStore): FloatArray? {
    val d = store.detail ?: return null
    val c = FloatArray(8)
    return if (LayerGeometry.corners(d, c)) c else null
}

/** (u, v) normalizado → composição, pela afim dos cantos. */
private fun toComp(c: FloatArray, u: Float, v: Float, out: FloatArray) {
    out[0] = c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])
    out[1] = c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])
}

/** Composição → (u, v); false se a camada não tem área na tela. */
private fun toUv(c: FloatArray, x: Float, y: Float, out: FloatArray): Boolean {
    val ax = c[2] - c[0]; val ay = c[3] - c[1]
    val bx = c[6] - c[0]; val by = c[7] - c[1]
    val det = ax * by - ay * bx
    if (kotlin.math.abs(det) < 1e-6f) return false
    val px = x - c[0]; val py = y - c[1]
    out[0] = (px * by - py * bx) / det
    out[1] = (ax * py - ay * px) / det
    return true
}

private val MeshLine = Color(0xFFFFD166)

internal fun DrawScope.drawMeshWarpOverlay(store: EditorStore, m: StageMapper) {
    store.rigRevision; store.playhead   // editar ou andar o cabeçote redesenha
    val mesh = store.meshWarp(MeshWarpStage.layer, MeshWarpStage.effect)
    val c = meshCorners(store) ?: return
    if (mesh.size < MeshWarpStage.HEADER) return
    val rows = mesh[0].toInt(); val cols = mesh[1].toInt()
    val n = (rows + 1) * (cols + 1)
    if (mesh.size < MeshWarpStage.HEADER + n * MeshWarpStage.FLOATS) return
    val p = FloatArray(2)
    fun at(vertex: Int, slot: Int, rel: Boolean): Offset {
        val b = MeshWarpStage.HEADER + vertex * MeshWarpStage.FLOATS
        var u = mesh[b + slot]; var v = mesh[b + slot + 1]
        if (rel) { u += mesh[b]; v += mesh[b + 1] }
        toComp(c, u, v, p)
        return Offset(m.sx(p[0]), m.sy(p[1]))
    }
    val under = ShellColors.OutlineUnder
    val thin = 1.5.dp.toPx()
    // Arestas como curvas cúbicas (vértice → alça → alça do vizinho → vizinho).
    val path = androidx.compose.ui.graphics.Path()
    for (r in 0..rows) for (col in 0..cols) {
        val i = r * (cols + 1) + col
        val a = at(i, 0, false)
        if (col < cols) {
            val j = i + 1
            val b1 = at(i, 4, true); val b2 = at(j, 2, true); val e = at(j, 0, false)
            path.moveTo(a.x, a.y); path.cubicTo(b1.x, b1.y, b2.x, b2.y, e.x, e.y)
        }
        if (r < rows) {
            val j = i + cols + 1
            val b1 = at(i, 8, true); val b2 = at(j, 6, true); val e = at(j, 0, false)
            path.moveTo(a.x, a.y); path.cubicTo(b1.x, b1.y, b2.x, b2.y, e.x, e.y)
        }
    }
    drawPath(path, under, style = Stroke(thin + 2.dp.toPx()))
    drawPath(path, MeshLine, style = Stroke(thin))
    val keyed = mesh[2] > 0.5f
    // Pontos do tamanho da célula NA TELA (6..14 dp de diâmetro): numa camada
    // pequena a grade 8×8 não vira um borrão de bolinhas por cima dela.
    val dotR = meshDotDiameter(c, m, rows, cols, 6.dp.toPx(), 14.dp.toPx()) * 0.5f
    val ring = 1.dp.toPx()
    for (i in 0 until n) {
        val o = at(i, 0, false)
        val chosen = i == MeshWarpStage.selected
        val rr = if (chosen) dotR + 1.5.dp.toPx() else dotR
        drawCircle(under, rr + ring, o)
        drawCircle(if (chosen) AureaColors.Accent else Color.White, rr, o)
    }
    // Alças só do vértice escolhido.
    val s = MeshWarpStage.selected
    if (s in 0 until n) {
        val o = at(s, 0, false)
        for (h in 1..4) {
            val t = at(s, h * 2, true)
            drawLine(under, o, t, 3.dp.toPx(), cap = StrokeCap.Round)
            drawLine(Color.White, o, t, 1.5.dp.toPx(), cap = StrokeCap.Round)
            drawCircle(under, dotR + ring, t)
            drawCircle(AureaColors.Accent, dotR, t)
        }
        // Key da malha no cabeçote: anel de destaque no vértice escolhido.
        if (keyed) drawCircle(MeshLine, dotR + 5.dp.toPx(), o, style = Stroke(2.dp.toPx()))
    }
}

/** Diâmetro do ponto: ~45 % da menor célula da malha de repouso na tela, entre [min] e [max]. */
private fun meshDotDiameter(c: FloatArray, m: StageMapper, rows: Int, cols: Int, min: Float, max: Float): Float {
    val w = hypot(m.sx(c[2]) - m.sx(c[0]), m.sy(c[3]) - m.sy(c[1])) / cols.coerceAtLeast(1)
    val h = hypot(m.sx(c[6]) - m.sx(c[0]), m.sy(c[7]) - m.sy(c[1])) / rows.coerceAtLeast(1)
    return (kotlin.math.min(w, h) * 0.45f).coerceIn(min, max)
}

/** Alvo sob o dedo: o mais PERTO entre os vértices e as alças do escolhido (vértice ganha empate). */
private fun pickMesh(mesh: FloatArray, c: FloatArray, m: StageMapper, x: Float, y: Float, reach: Float): IntArray? {
    val rows = mesh[0].toInt(); val cols = mesh[1].toInt()
    val n = (rows + 1) * (cols + 1)
    val p = FloatArray(2)
    var best = reach
    var hit: IntArray? = null
    fun test(vertex: Int, grip: Int) {
        val b = MeshWarpStage.HEADER + vertex * MeshWarpStage.FLOATS
        var u = mesh[b + grip * 2]; var v = mesh[b + grip * 2 + 1]
        if (grip > 0) { u += mesh[b]; v += mesh[b + 1] }
        toComp(c, u, v, p)
        val d = hypot(x - m.sx(p[0]), y - m.sy(p[1]))
        if (d < best) { best = d; hit = intArrayOf(vertex, grip) }
    }
    for (i in 0 until n) test(i, 0)
    val s = MeshWarpStage.selected
    if (s in 0 until n) for (g in 1..4) test(s, g)
    return hit
}

/** Gesto na malha. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.meshWarpGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val layer = MeshWarpStage.layer
    val effect = MeshWarpStage.effect
    val mesh = store.meshWarp(layer, effect)
    val c = meshCorners(store)
    val hit = if (m.valid && c != null && mesh.size >= MeshWarpStage.HEADER)
        pickMesh(mesh, c, m, down.position.x, down.position.y, 22.dp.toPx()) else null
    val slop = 6.dp.toPx()
    down.consume()
    var moved = false
    var sent = false
    val uv = FloatArray(2)
    while (true) {
        val e = awaitPointerEvent()
        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
        ch.consume()
        if (!ch.pressed) break
        if (!moved && hypot(ch.position.x - down.position.x, ch.position.y - down.position.y) < slop) continue
        moved = true
        if (hit == null || c == null || !m.valid) continue
        if (!toUv(c, m.cx(ch.position.x), m.cy(ch.position.y), uv)) continue
        store.meshWarpDrag(layer, effect, hit[0], hit[1], uv[0], uv[1], sent)
        sent = true
    }
    if (sent) {
        if (hit != null && hit[1] == 0) MeshWarpStage.selected = hit[0]
        store.rigGestureEnd()
        return
    }
    if (moved) return
    // Toque: vértice = escolhe (de novo = solta); vazio = solta.
    MeshWarpStage.selected = if (hit != null && hit[1] == 0 && MeshWarpStage.selected != hit[0]) hit[0]
        else if (hit != null && hit[1] > 0) MeshWarpStage.selected else -1
}

/**
 * Ferramentas da malha no cartão do efeito: liga o palco enquanto o cartão
 * está aberto e oferece "Redefinir malha" (um passo de desfazer).
 */
@Composable
internal fun MeshWarpCardTools(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    DisposableEffect(layer, effectId) {
        MeshWarpStage.open(layer, effectId)
        onDispose { MeshWarpStage.close(effectId) }
    }
    val reset = stringResource(R.string.mesh_warp_reset)
    Column(Modifier.padding(bottom = 6.dp)) {
        Text(
            stringResource(R.string.mesh_warp_hint),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
        )
        Box(
            Modifier
                .padding(top = 6.dp)
                .heightIn(min = 36.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.Chip)
                .testTag("fx.mesh_warp.reset")
                .semantics { role = Role.Button; contentDescription = reset }
                .tocavel(haptic = true, onClick = { MeshWarpStage.selected = -1; store.meshWarpReset(layer, effectId) })
                .padding(horizontal = 12.dp, vertical = 8.dp),
        ) {
            Text(reset, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
        }
    }
}
