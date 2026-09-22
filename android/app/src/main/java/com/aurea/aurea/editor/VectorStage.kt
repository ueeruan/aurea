package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.VBezier
import com.aurea.aurea.engine.VPathAt
import com.aurea.aurea.engine.VVertex
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.abs
import kotlin.math.hypot

/**
 * O palco no MODO VETORIAL (Fase 7D):
 *
 *  - Pontos (`vectorTool == 1`): o caminho escolhido da camada vetorial com os
 *    vértices e as alças do vértice escolhido. Tocar no vazio acrescenta um
 *    vértice no fim (arrastar ao criar puxa as alças, curva suave); tocar num
 *    vértice escolhe; arrastar move o vértice ou a alça (alça de ponto suave
 *    espelha a direção da outra); tocar no primeiro vértice fecha o caminho.
 *    Cada arrasto é UM passo de desfazer (o primeiro envio abre, os seguintes
 *    continuam). Com keyframes de forma, a edição grava no cabeçote (morph).
 *  - Mão livre (`vectorTool == 2`): o traço do dedo aparece enquanto desenha e
 *    vira um caminho suave no motor ao soltar (ajuste de Schneider).
 *
 * O caminho vem do motor já avaliado no cabeçote (`vectorPathAt`), com a afim
 * grupo → composição: os pontos ficam exatamente onde o renderer desenha.
 */
internal object VectorStageState {
    /** Traço da mão livre em curso (px da composição, x,y intercalados). */
    var stroke by mutableStateOf(FloatArray(0))

    /**
     * Ferramenta de pontos (Frente D): o que o toque faz no modo de pontos.
     * Arrastar ponto ou alça funciona em todas; o que muda é o TOQUE.
     */
    var pointTool by mutableIntStateOf(PointTool.SELECT)
}

/** As ferramentas da barra do Vetor (ícone + nome no painel). */
internal object PointTool {
    const val SELECT = 0    // escolher e arrastar pontos/alças; toque no vazio solta
    const val ADD = 1       // toque na linha insere; no vazio (caminho aberto) acrescenta no fim
    const val REMOVE = 2    // toque num ponto o apaga
    const val CORNER = 3    // toque num ponto alterna canto ↔ suave

    /** Caminho com menos de 2 pontos só tem o que acrescentar: a ferramenta vira Adicionar. */
    fun effective(tool: Int, vertices: Int): Int = if (vertices < 2) ADD else tool
}

private val PathLine = Color(0xFF5AA8FF)
private val HandleLine = Color(0xFFB8C4D0)

internal fun DrawScope.drawVectorOverlay(store: EditorStore, m: StageMapper) {
    if (store.vectorTool == 2) {
        val s = VectorStageState.stroke
        if (s.size >= 4) {
            val p = Path()
            p.moveTo(m.sx(s[0]), m.sy(s[1]))
            var i = 2
            while (i + 1 < s.size) { p.lineTo(m.sx(s[i]), m.sy(s[i + 1])); i += 2 }
            drawPath(p, ShellColors.OutlineUnder, style = Stroke(5.dp.toPx(), cap = StrokeCap.Round))
            drawPath(p, Color.White, style = Stroke(3.dp.toPx(), cap = StrokeCap.Round))
        }
        return
    }
    val at = store.vectorPathAt ?: return
    val v = at.path.v
    val q = FloatArray(2)
    val pts = FloatArray(v.size * 6)
    for ((i, p) in v.withIndex()) {
        at.toComp(p.x, p.y, q); pts[i * 6] = m.sx(q[0]); pts[i * 6 + 1] = m.sy(q[1])
        at.toComp(p.x + p.inX, p.y + p.inY, q); pts[i * 6 + 2] = m.sx(q[0]); pts[i * 6 + 3] = m.sy(q[1])
        at.toComp(p.x + p.outX, p.y + p.outY, q); pts[i * 6 + 4] = m.sx(q[0]); pts[i * 6 + 5] = m.sy(q[1])
    }
    if (v.size >= 2) {
        val path = Path()
        path.moveTo(pts[0], pts[1])
        val segs = if (at.path.closed) v.size else v.size - 1
        for (i in 0 until segs) {
            val j = (i + 1) % v.size
            path.cubicTo(pts[i * 6 + 4], pts[i * 6 + 5], pts[j * 6 + 2], pts[j * 6 + 3], pts[j * 6], pts[j * 6 + 1])
        }
        if (at.path.closed) path.close()
        drawPath(path, ShellColors.OutlineUnder, style = Stroke(3.dp.toPx()))
        drawPath(path, PathLine, style = Stroke(1.5.dp.toPx()))
    }
    val sel = store.vectorPoint
    if (sel in v.indices) {
        val c = Offset(pts[sel * 6], pts[sel * 6 + 1])
        for (k in 0..1) {
            val h = Offset(pts[sel * 6 + 2 + k * 2], pts[sel * 6 + 3 + k * 2])
            if (hypot(h.x - c.x, h.y - c.y) < 1f) continue
            drawLine(HandleLine, c, h, 1.4.dp.toPx())
            drawCircle(ShellColors.OutlineUnder, 7.5.dp.toPx(), h)
            drawCircle(Color.White, 6.dp.toPx(), h)
        }
    }
    val r = 6.dp.toPx()
    for (i in v.indices) {
        val c = Offset(pts[i * 6], pts[i * 6 + 1])
        val chosen = i == sel
        drawRect(ShellColors.OutlineUnder, Offset(c.x - r - 1.5f, c.y - r - 1.5f), Size(2 * r + 3f, 2 * r + 3f))
        drawRect(if (chosen) AureaColors.Accent else Color.White, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
        // O primeiro vértice de um caminho aberto: anel (tocar nele fecha).
        if (i == 0 && !at.path.closed && v.size >= 2) drawCircle(AureaColors.Accent, r * 1.9f, c, style = Stroke(1.5.dp.toPx()))
    }
}

/** Gesto no modo vetorial. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.vectorGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    if (!m.valid) return
    if (store.vectorTool == 2) {
        freehand(store, m, down)
        return
    }
    val at = store.vectorPathAt
    val id = store.primary
    if (at == null || id == null || !store.isVectorLayer) {
        waitUp(down)
        return
    }
    if (!at.free) {
        // Paramétrico: vira caminho livre (mesma forma) antes da primeira edição.
        store.makeVectorPathEditable()
        waitUp(down)
        return
    }
    val reach = 28.dp.toPx()
    val q = FloatArray(2)
    val v = at.path.v
    // Caminho vazio (desenho novo): fica em Adicionar até a pessoa trocar.
    if (v.size < 2) VectorStageState.pointTool = PointTool.ADD
    val tool = PointTool.effective(VectorStageState.pointTool, v.size)
    fun screenOf(x: Float, y: Float): Offset {
        at.toComp(x, y, q)
        return Offset(m.sx(q[0]), m.sy(q[1]))
    }
    val dp = down.position
    // Alças do vértice escolhido têm prioridade (ficam por cima).
    val sel = store.vectorPoint
    var handle = -1
    if (sel in v.indices) {
        val p = v[sel]
        val hin = screenOf(p.x + p.inX, p.y + p.inY)
        val hout = screenOf(p.x + p.outX, p.y + p.outY)
        val c = screenOf(p.x, p.y)
        val din = if (hypot(hin.x - c.x, hin.y - c.y) > 1f) hypot(dp.x - hin.x, dp.y - hin.y) else Float.MAX_VALUE
        val dout = if (hypot(hout.x - c.x, hout.y - c.y) > 1f) hypot(dp.x - hout.x, dp.y - hout.y) else Float.MAX_VALUE
        if (din < reach && din <= dout) handle = 0 else if (dout < reach) handle = 1
    }
    var hit = -1
    if (handle < 0) {
        var best = reach
        for (i in v.indices) {
            val c = screenOf(v[i].x, v[i].y)
            val d = hypot(dp.x - c.x, dp.y - c.y)
            if (d < best) { best = d; hit = i }
        }
    }
    val work = at.path.copyDeep()
    val g = FloatArray(2)
    fun groupOf(pos: Offset): Boolean = at.fromComp(m.cx(pos.x), m.cy(pos.y), g)
    val slop = 4.dp.toPx()

    // Remover / Canto-suave: o toque num ponto age nele (sem arrasto).
    if (handle < 0 && hit >= 0 && (tool == PointTool.REMOVE || tool == PointTool.CORNER)) {
        waitUp(down)
        store.vectorPoint = hit
        if (tool == PointTool.REMOVE) VectorPathOps.deletePoint(store) else VectorPathOps.toggleSmooth(store)
        return
    }
    // Adicionar: toque sobre a linha insere um ponto ali (a curva não muda).
    if (handle < 0 && hit < 0 && tool == PointTool.ADD && v.size >= 2) {
        val ins = VectorPathOps.insertAt(at, m, dp, reach)
        if (ins != null) {
            store.setVectorPathShape(ins.first, continuing = false)
            store.vectorPoint = ins.second
            waitUp(down)
            return
        }
    }
    when {
        handle >= 0 -> {
            val p = work.v[sel]
            // Suave no começo do gesto (alças opostas): a outra acompanha a direção.
            val smooth = isSmooth(p)
            var sent = false
            dragLoop(down) { pos ->
                if (!groupOf(pos)) return@dragLoop
                val dx = g[0] - p.x
                val dy = g[1] - p.y
                if (handle == 0) { p.inX = dx; p.inY = dy } else { p.outX = dx; p.outY = dy }
                if (smooth) {
                    val len = if (handle == 0) hypot(p.outX, p.outY) else hypot(p.inX, p.inY)
                    val l = hypot(dx, dy)
                    if (l > 1e-4f) {
                        if (handle == 0) { p.outX = -dx / l * len; p.outY = -dy / l * len } else { p.inX = -dx / l * len; p.inY = -dy / l * len }
                    }
                }
                store.setVectorPathShape(work, continuing = sent)
                sent = true
            }
        }
        hit >= 0 -> {
            // Tocar no primeiro vértice de um caminho aberto fecha.
            var moved = false
            var sent = false
            val p = work.v[hit]
            val start = Offset(p.x, p.y)
            val ok = groupOf(dp)
            val g0x = g[0]
            val g0y = g[1]
            dragLoop(down) { pos ->
                if (!moved && hypot(pos.x - dp.x, pos.y - dp.y) < slop) return@dragLoop
                moved = true
                if (!ok || !groupOf(pos)) return@dragLoop
                p.x = start.x + (g[0] - g0x)
                p.y = start.y + (g[1] - g0y)
                store.setVectorPathShape(work, continuing = sent)
                sent = true
            }
            if (!moved) {
                if (tool == PointTool.ADD && hit == 0 && !work.closed && work.v.size >= 3 && store.vectorPoint == work.v.size - 1) {
                    work.closed = true
                    store.setVectorPathShape(work, continuing = false)
                }
                store.vectorPoint = hit
            } else {
                store.vectorPoint = hit
            }
        }
        work.closed || tool != PointTool.ADD -> {
            // Toque no vazio: solta o ponto escolhido.
            store.vectorPoint = -1
            waitUp(down)
        }
        else -> {
            // Vértice novo no fim; arrastar ao criar puxa as alças (simétricas).
            if (!groupOf(dp)) { waitUp(down); return }
            val nv = VVertex(g[0], g[1])
            work.v += nv
            store.setVectorPathShape(work, continuing = false)
            store.vectorPoint = work.v.size - 1
            var dragging = false
            val t = FloatArray(2)
            dragLoop(down) { pos ->
                if (!dragging && hypot(pos.x - dp.x, pos.y - dp.y) < slop) return@dragLoop
                dragging = true
                if (!at.vecFromComp((pos.x - dp.x) / m.fit, (pos.y - dp.y) / m.fit, t)) return@dragLoop
                nv.outX = t[0]; nv.outY = t[1]
                nv.inX = -t[0]; nv.inY = -t[1]
                store.setVectorPathShape(work, continuing = true)
            }
        }
    }
}

/** Alças opostas e colineares (ou só uma): o ponto é suave. */
internal fun isSmooth(p: VVertex): Boolean {
    val li = hypot(p.inX, p.inY)
    val lo = hypot(p.outX, p.outY)
    if (li < 1e-3f || lo < 1e-3f) return false
    val cross = p.inX * p.outY - p.inY * p.outX
    val dot = p.inX * p.outX + p.inY * p.outY
    return dot < 0f && abs(cross) / (li * lo) < 0.02f
}

private suspend fun AwaitPointerEventScope.freehand(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val pts = ArrayList<Float>(512)
    pts += m.cx(down.position.x); pts += m.cy(down.position.y)
    VectorStageState.stroke = pts.toFloatArray()
    dragLoop(down) { pos ->
        pts += m.cx(pos.x); pts += m.cy(pos.y)
        VectorStageState.stroke = pts.toFloatArray()
    }
    val xy = pts.toFloatArray()
    VectorStageState.stroke = FloatArray(0)
    store.commitFreehand(xy)
}

private suspend fun AwaitPointerEventScope.dragLoop(down: PointerInputChange, onMove: (Offset) -> Unit) {
    down.consume()
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        c.consume()
        if (!c.pressed) break
        onMove(c.position)
    }
}

private suspend fun AwaitPointerEventScope.waitUp(down: PointerInputChange) = dragLoop(down) { }

/** Operações do painel sobre o caminho escolhido (fora do palco). */
internal object VectorPathOps {
    /**
     * Ponto novo sobre a linha perto de [pos] (px do palco): divide o trecho
     * pela conta de De Casteljau, então a curva continua IGUAL. Devolve o
     * caminho novo e o índice do ponto, ou nulo se o toque não caiu na linha.
     */
    fun insertAt(at: VPathAt, m: StageMapper, pos: Offset, reach: Float): Pair<VBezier, Int>? {
        val v = at.path.v
        val n = v.size
        if (n < 2) return null
        val segs = if (at.path.closed) n else n - 1
        val q = FloatArray(2)
        var bestD = reach
        var bestSeg = -1
        var bestT = 0f
        val steps = 48
        for (i in 0 until segs) {
            val a = v[i]
            val b = v[(i + 1) % n]
            for (k in 1 until steps) {
                val t = k / steps.toFloat()
                val x = cubic(a.x, a.x + a.outX, b.x + b.inX, b.x, t)
                val y = cubic(a.y, a.y + a.outY, b.y + b.inY, b.y, t)
                at.toComp(x, y, q)
                val d = hypot(m.sx(q[0]) - pos.x, m.sy(q[1]) - pos.y)
                if (d < bestD) { bestD = d; bestSeg = i; bestT = t }
            }
        }
        if (bestSeg < 0) return null
        val w = at.path.copyDeep()
        val i = bestSeg
        val j = (i + 1) % n
        val a = w.v[i]
        val b = w.v[j]
        val t = bestT
        fun lerp(p: Float, r: Float) = p + (r - p) * t
        val p1x = a.x + a.outX; val p1y = a.y + a.outY
        val p2x = b.x + b.inX; val p2y = b.y + b.inY
        val ax = lerp(a.x, p1x); val ay = lerp(a.y, p1y)
        val bx = lerp(p1x, p2x); val by = lerp(p1y, p2y)
        val cx = lerp(p2x, b.x); val cy = lerp(p2y, b.y)
        val dx = lerp(ax, bx); val dy = lerp(ay, by)
        val ex = lerp(bx, cx); val ey = lerp(by, cy)
        val fx = lerp(dx, ex); val fy = lerp(dy, ey)
        a.outX = ax - a.x; a.outY = ay - a.y
        b.inX = cx - b.x; b.inY = cy - b.y
        val nv = VVertex(fx, fy, dx - fx, dy - fy, ex - fx, ey - fy)
        val idx = if (j == 0) n else i + 1
        w.v.add(idx, nv)
        return w to idx
    }

    private fun cubic(p0: Float, p1: Float, p2: Float, p3: Float, t: Float): Float {
        val u = 1f - t
        return u * u * u * p0 + 3f * u * u * t * p1 + 3f * u * t * t * p2 + t * t * t * p3
    }

    fun deletePoint(store: EditorStore) {
        val at: VPathAt = store.vectorPathAt ?: return
        val i = store.vectorPoint
        if (i !in at.path.v.indices) return
        val w: VBezier = at.path.copyDeep()
        w.v.removeAt(i)
        if (w.v.size < 3) w.closed = false
        store.setVectorPathShape(w, continuing = false)
        store.vectorPoint = if (w.v.isEmpty()) -1 else minOf(i, w.v.size - 1)
    }

    fun toggleClosed(store: EditorStore) {
        val at = store.vectorPathAt ?: return
        val w = at.path.copyDeep()
        if (w.v.size < 2) return
        w.closed = !w.closed
        store.setVectorPathShape(w, continuing = false)
    }

    /** Suave ↔ canto: suave ganha alças pela direção dos vizinhos; canto perde as alças. */
    fun toggleSmooth(store: EditorStore) {
        val at = store.vectorPathAt ?: return
        val i = store.vectorPoint
        val w = at.path.copyDeep()
        if (i !in w.v.indices) return
        val p = w.v[i]
        if (hypot(p.inX, p.inY) > 1e-3f || hypot(p.outX, p.outY) > 1e-3f) {
            p.inX = 0f; p.inY = 0f; p.outX = 0f; p.outY = 0f
        } else {
            val n = w.v.size
            val prev = w.v[if (i > 0) i - 1 else if (w.closed) n - 1 else i]
            val next = w.v[if (i < n - 1) i + 1 else if (w.closed) 0 else i]
            var dx = next.x - prev.x
            var dy = next.y - prev.y
            val l = hypot(dx, dy)
            if (l < 1e-3f) return
            dx /= l; dy /= l
            val lin = hypot(p.x - prev.x, p.y - prev.y) / 3f
            val lout = hypot(next.x - p.x, next.y - p.y) / 3f
            p.inX = -dx * lin; p.inY = -dy * lin
            p.outX = dx * lout; p.outY = dy * lout
        }
        store.setVectorPathShape(w, continuing = false)
    }
}
