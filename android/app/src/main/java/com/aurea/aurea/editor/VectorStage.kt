package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.VVertex
import com.aurea.aurea.state.EditorStore
import kotlin.math.abs
import kotlin.math.hypot

/**
 * O palco no MODO VETORIAL antigo (Fase 7D), mantido para os atalhos que ainda
 * o ligam (`vectorTool`):
 *
 *  - Pontos (`vectorTool == 1`): o mesmo palco do "Editar pontos" (EditPoints.kt).
 *  - Mão livre (`vectorTool == 2`): o traço do dedo aparece enquanto desenha e
 *    vira um caminho suave no motor ao soltar (ajuste de Schneider).
 *
 * Omega conteúdo (área 06, P1-8): as REGRAS de ponto (inserir, apagar, canto ↔
 * curva, alça espelhada, abrir/fechar) saíram daqui e moram no motor
 * (`Engine::edit_vector_path`, vector/PathEdit.cpp) — o mesmo código do iOS.
 */
internal object VectorStageState {
    /** Traço da mão livre em curso (px da composição, x,y intercalados). */
    var stroke by mutableStateOf(FloatArray(0))

    /** Ferramenta de pontos antiga (a faixa do palco ainda lê o nome dela). */
    var pointTool by mutableIntStateOf(PointTool.SELECT)
}

/** As ferramentas da barra antiga do Vetor (a faixa do palco diz o que o dedo faz). */
internal object PointTool {
    const val SELECT = 0
    const val ADD = 1
    const val REMOVE = 2
    const val CORNER = 3

    /** Caminho com menos de 2 pontos só tem o que acrescentar: a ferramenta vira Adicionar. */
    fun effective(tool: Int, vertices: Int): Int = if (vertices < 2) ADD else tool
}

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
    drawEditPointsOverlay(store, m)
}

/** Gesto no modo vetorial. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.vectorGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    if (!m.valid) return
    if (store.vectorTool == 2) {
        freehand(store, m, down)
        return
    }
    editPointsGesture(store, m, down)
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
    down.consume()
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        c.consume()
        if (!c.pressed) break
        pts += m.cx(c.position.x); pts += m.cy(c.position.y)
        VectorStageState.stroke = pts.toFloatArray()
    }
    val xy = pts.toFloatArray()
    VectorStageState.stroke = FloatArray(0)
    store.commitFreehand(xy)
}

/** Operações do painel sobre o caminho escolhido — todas pelo motor (PathEdit). */
internal object VectorPathOps {
    fun deletePoint(store: EditorStore) {
        val i = store.vectorPoint
        val n = store.vectorPathAt?.path?.v?.size ?: return
        if (i !in 0 until n) return
        val r = store.editVectorPath(PathOps.REMOVE, floatArrayOf(i.toFloat()), false)
        val left = store.vectorPathAt?.path?.v?.size ?: 0
        store.vectorPoint = if (r < 0 || left == 0) -1 else r.coerceAtMost(left - 1)
    }

    fun toggleClosed(store: EditorStore) {
        val at = store.vectorPathAt ?: return
        if (at.path.v.size < 2) return
        store.editVectorPath(PathOps.SET_CLOSED, floatArrayOf(if (at.path.closed) 0f else 1f), false)
    }

    /** Suave ↔ canto: a regra (alças a 1/3 dos trechos, na direção dos vizinhos) é do motor. */
    fun toggleSmooth(store: EditorStore) {
        val at = store.vectorPathAt ?: return
        val i = store.vectorPoint
        val p = at.path.v.getOrNull(i) ?: return
        val curve = hypot(p.inX, p.inY) > 1e-3f || hypot(p.outX, p.outY) > 1e-3f
        store.editVectorPath(PathOps.SET_TYPE, floatArrayOf(i.toFloat(), if (curve) 0f else 1f), false)
    }
}
