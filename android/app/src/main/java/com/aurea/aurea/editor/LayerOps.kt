package com.aurea.aurea.editor

import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.LayerType
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * A geometria de uma camada na composição, com a MESMA conta do motor
 * (`layer_matrix` em Renderer.cpp): `T(posição) · R(z) · S(escala) · T(−âncora)`
 * sobre o retângulo `[0, largura] × [0, altura]` da mídia. Composição e tela
 * têm y para baixo, então o giro positivo é horário nas duas.
 *
 * Tudo escreve em arrays que o chamador reaproveita: roda no desenho de cada
 * quadro e em cada evento de toque, e não pode alocar.
 */
internal object LayerGeometry {

    /**
     * Largura/altura da mídia. O motor ainda não preenche `sourceWidth/Height`
     * para IMAGEM (o `import_image` não grava o tamanho no asset); até lá, a
     * imagem usa o dobro da âncora, que nasce no centro dela. Errado só se a
     * âncora tiver sido movida.
     */
    fun width(d: LayerDetail): Float = when {
        d.sourceWidth > 0 -> d.sourceWidth.toFloat()
        d.kind == LayerType.Image.kind && d.anchor[0] > 0f -> d.anchor[0] * 2f
        else -> 0f
    }

    fun height(d: LayerDetail): Float = when {
        d.sourceHeight > 0 -> d.sourceHeight.toFloat()
        d.kind == LayerType.Image.kind && d.anchor[1] > 0f -> d.anchor[1] * 2f
        else -> 0f
    }

    fun hasSize(d: LayerDetail) = width(d) > 0f && height(d) > 0f

    /** Cantos TL, TR, BR, BL (x,y intercalados) em px da composição. */
    fun corners(d: LayerDetail, out: FloatArray): Boolean {
        if (d.hasWorldCorners) {
            // O motor já resolveu pais e câmera (mesmo cálculo do renderer).
            d.worldCorners.copyInto(out, 0, 0, 8)
            return true
        }
        if (!hasSize(d)) return false
        val w = width(d)
        val h = height(d)
        val rad = Math.toRadians(d.rotation[2].toDouble())
        val c = cos(rad).toFloat()
        val s = sin(rad).toFloat()
        val sx = d.scale[0]
        val sy = d.scale[1]
        // Layer 3D: a âncora é o CENTRO da silhueta (o pivô do modelo).
        val centered = d.kind == LayerType.Model3D.kind
        val ax = d.anchor[0] + if (centered) w * 0.5f else 0f
        val ay = d.anchor[1] + if (centered) h * 0.5f else 0f
        val px = d.position[0]
        val py = d.position[1]
        for (i in 0 until 4) {
            val lx = if (i == 1 || i == 2) w else 0f
            val ly = if (i >= 2) h else 0f
            val dx = (lx - ax) * sx
            val dy = (ly - ay) * sy
            out[i * 2] = px + dx * c - dy * s
            out[i * 2 + 1] = py + dx * s + dy * c
        }
        return true
    }

    /** Caixa alinhada aos eixos (left, top, right, bottom) na composição. */
    fun bounds(d: LayerDetail, scratch: FloatArray, out: FloatArray): Boolean {
        if (!corners(d, scratch)) return false
        var l = Float.MAX_VALUE
        var t = Float.MAX_VALUE
        var r = -Float.MAX_VALUE
        var b = -Float.MAX_VALUE
        for (i in 0 until 4) {
            l = min(l, scratch[i * 2]); r = max(r, scratch[i * 2])
            t = min(t, scratch[i * 2 + 1]); b = max(b, scratch[i * 2 + 1])
        }
        out[0] = l; out[1] = t; out[2] = r; out[3] = b
        return true
    }

    /**
     * O ponto (composição) cai dentro da camada, com [slack] px de composição
     * de folga? Desfaz o transform (escala negativa — espelho — também vale).
     */
    fun contains(d: LayerDetail, x: Float, y: Float, slack: Float): Boolean {
        if (d.hasWorldCorners) return quadContains(d.worldCorners, x, y, slack)
        if (!hasSize(d)) return false
        val sx = d.scale[0]
        val sy = d.scale[1]
        if (abs(sx) < 1e-6f || abs(sy) < 1e-6f) return false
        val rad = Math.toRadians(d.rotation[2].toDouble())
        val c = cos(rad).toFloat()
        val s = sin(rad).toFloat()
        val dx = x - d.position[0]
        val dy = y - d.position[1]
        val ux = dx * c + dy * s
        val uy = -dx * s + dy * c
        val lx = ux / sx + d.anchor[0]
        val ly = uy / sy + d.anchor[1]
        val tx = slack / abs(sx)
        val ty = slack / abs(sy)
        return lx >= -tx && lx <= width(d) + tx && ly >= -ty && ly <= height(d) + ty
    }

    /** Ponto dentro do quadrilátero convexo (ou a menos de `slack` da borda). */
    private fun quadContains(q: FloatArray, x: Float, y: Float, slack: Float): Boolean {
        var pos = 0
        var neg = 0
        var near = false
        for (i in 0 until 4) {
            val ax = q[i * 2]; val ay = q[i * 2 + 1]
            val bx = q[((i + 1) % 4) * 2]; val by = q[((i + 1) % 4) * 2 + 1]
            val ex = bx - ax; val ey = by - ay
            val cr = ex * (y - ay) - ey * (x - ax)
            if (cr > 0f) pos++ else if (cr < 0f) neg++
            val len2 = ex * ex + ey * ey
            val t = if (len2 > 0f) (((x - ax) * ex + (y - ay) * ey) / len2).coerceIn(0f, 1f) else 0f
            val px = ax + ex * t - x; val py = ay + ey * t - y
            if (px * px + py * py <= slack * slack) near = true
        }
        return near || pos == 0 || neg == 0
    }

    /** A camada está no tempo do cabeçote? */
    fun activeAt(row: LayerRow, frame: Int) = frame >= row.startFrame && frame < row.endFrame
}

/** Operações de camada que a casca compõe a partir dos comandos do store. */
internal object LayerOps {

    /**
     * Excluir (`excluirCamadas` da A.01): a bloqueada NÃO vai — as outras
     * somem e a tela diz quantas ficaram.
     */
    fun delete(store: EditorStore, ids: Collection<Long>) {
        if (ids.isEmpty()) return
        val locked = store.layers.filter { it.id in ids && it.locked }.map { it.id }.toSet()
        val free = ids.filter { it !in locked }
        if (free.isNotEmpty()) store.deleteLayers(free)
        if (locked.isNotEmpty()) {
            store.showToast(
                if (locked.size == 1) "Camada bloqueada: desbloqueie para apagar"
                else "${locked.size} camadas bloqueadas: desbloqueie para apagar",
            )
        }
    }

    enum class Edge { Left, CenterH, Right, Top, CenterV, Bottom }

    /**
     * Alinhar à COMPOSIÇÃO (padrão do `alignSelection` da A.01). Cada camada
     * anda o que falta para a borda da caixa dela encostar na borda pedida;
     * tudo num passo só de desfazer.
     */
    fun align(store: EditorStore, ids: Collection<Long>, edge: Edge) {
        val cw = store.project.width.toFloat()
        val ch = store.project.height.toFloat()
        if (cw <= 0f || ch <= 0f) return
        val scratch = FloatArray(8)
        val box = FloatArray(4)
        val moves = ids.mapNotNull { id ->
            val d = store.queryDetail(id) ?: return@mapNotNull null
            if (!LayerGeometry.bounds(d, scratch, box)) return@mapNotNull null
            val dx = when (edge) {
                Edge.Left -> -box[0]
                Edge.CenterH -> cw / 2 - (box[0] + box[2]) / 2
                Edge.Right -> cw - box[2]
                else -> 0f
            }
            val dy = when (edge) {
                Edge.Top -> -box[1]
                Edge.CenterV -> ch / 2 - (box[1] + box[3]) / 2
                Edge.Bottom -> ch - box[3]
                else -> 0f
            }
            Triple(id, d.position[0] + dx, d.position[1] + dy)
        }
        applyPositions(store, moves, "alinhar")
    }

    /**
     * Distribuir com vãos iguais (≥ 3 camadas): as das pontas ficam, as do
     * meio se espalham para que o espaço entre caixas vizinhas seja o mesmo.
     */
    fun distribute(store: EditorStore, ids: Collection<Long>, horizontal: Boolean) {
        val scratch = FloatArray(8)
        data class Item(val id: Long, val x: Float, val y: Float, val lo: Float, val hi: Float)
        val items = ids.mapNotNull { id ->
            val d = store.queryDetail(id) ?: return@mapNotNull null
            val box = FloatArray(4)
            if (!LayerGeometry.bounds(d, scratch, box)) return@mapNotNull null
            if (horizontal) Item(id, d.position[0], d.position[1], box[0], box[2])
            else Item(id, d.position[0], d.position[1], box[1], box[3])
        }.sortedBy { it.lo }
        if (items.size < 3) return
        val span = items.last().hi - items.first().lo
        val sizes = items.sumOf { (it.hi - it.lo).toDouble() }.toFloat()
        val gap = (span - sizes) / (items.size - 1)
        var cursor = items.first().lo
        val moves = items.map { it ->
            val delta = cursor - it.lo
            cursor += (it.hi - it.lo) + gap
            if (horizontal) Triple(it.id, it.x + delta, it.y) else Triple(it.id, it.x, it.y + delta)
        }
        applyPositions(store, moves, "distribuir")
    }

    private fun applyPositions(store: EditorStore, moves: List<Triple<Long, Float, Float>>, label: String) {
        if (moves.isEmpty()) return
        store.beginGesture(label)
        moves.forEach { (id, x, y) ->
            store.setTransform2(TrackProperty.POSITION_X, x, TrackProperty.POSITION_Y, y, id)
        }
        store.endGesture()
    }
}
