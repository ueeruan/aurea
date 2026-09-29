package com.aurea.aurea.editor.timeline

import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

// =============================================================================
//  Lógica pura da timeline (sem Android, sem Compose): testada na JVM.
//  Tempo em FRAMES da composição (a unidade do motor); vista em Double para o
//  arrasto e a pinça não andarem de quadro em quadro.
// =============================================================================

/**
 * Tempo ↔ px com o cabeçote FIXO no centro da timeline inteira (A e B): quem
 * anda é o conteúdo. `view` é o frame sob o cabeçote.
 */
internal object TimeAxis {
    fun safeFps(fps: Float): Float = if (fps > 0f) fps else 30f

    /** px por frame para um zoom em dp/s. */
    fun pxPerFrame(pps: Float, density: Float, fps: Float): Float = pps * density / safeFps(fps)

    fun xOf(frame: Double, view: Double, pxPerFrame: Float, centerX: Float): Float =
        (centerX + (frame - view) * pxPerFrame).toFloat()

    fun frameAt(x: Float, view: Double, pxPerFrame: Float, centerX: Float): Double =
        view + (x - centerX) / pxPerFrame

    /**
     * A vista não vai para antes do zero, mas PODE passar do fim da composição
     * — o motor também deixa o cursor lá. Prender a vista ao último quadro
     * travava a timeline inteira no fim do projeto (arrastar e ampliar paravam
     * junto com o cursor), que era o "trava em 09 segundo" do relato.
     * A duração cresce ao mover/aparar camadas, por isso não limita a vista.
     * Só o limite numérico do protocolo de frames limita a navegação.
     */
    fun clampView(view: Double, @Suppress("UNUSED_PARAMETER") durationFrames: Int): Double =
        if (view.isFinite()) view.coerceIn(0.0, (Int.MAX_VALUE - 1).toDouble()) else 0.0
}

/** Zoom em dp por segundo. */
internal object Zoom {
    const val MIN_PPS = 2f
    const val MAX_PPS = 800f
    /** A.01: 80 dp/s (10 riscos por segundo a 8 dp). */
    const val DEFAULT_PPS = 80f
    /** A.01 enquadra projetos longos (≥ 20 s) na largura ao abrir. */
    const val AUTO_FIT_MIN_SECONDS = 20f

    fun clamp(pps: Float): Float = pps.coerceIn(MIN_PPS, MAX_PPS)

    /** `clamp((largura − 32) / segundos, 4, 80)` da A.01. */
    fun autoFit(availableDp: Float, seconds: Float): Float =
        if (seconds <= 0f) DEFAULT_PPS else (availableDp / seconds).coerceIn(4f, DEFAULT_PPS)

    /** Pinça: a vista que mantém `focusFrame` sob o ponto focal `focusX`. */
    fun anchoredView(focusFrame: Double, focusX: Float, centerX: Float, pxPerFrame: Float): Double =
        focusFrame - (focusX - centerX) / pxPerFrame
}

/**
 * Passos da régua. No zoom da A.01 (80 dp/s) dá exatamente o print: risco
 * forte por segundo e 10 finos. Fora dele os passos se adaptam para os riscos
 * nunca virarem borrão; só quando o forte vale mais de 1 s aparece rótulo
 * (sem ele não dá para contar tempo na régua).
 */
internal class RulerSteps(
    val majorSeconds: Double,
    /** Subdivisões entre dois fortes (0 = sem riscos finos). Ignorado se [frameMinors]. */
    val subdivisions: Int,
    /** Riscos finos em cada quadro (zoom muito aberto). */
    val frameMinors: Boolean,
    val labels: Boolean,
) {
    companion object {
        private val MAJORS = doubleArrayOf(1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0)
        private val SUBS = intArrayOf(10, 4, 5, 10, 3, 6, 6, 4, 5, 10, 6, 6)
        private val ONE_SECOND_SUBS = intArrayOf(10, 5, 2)
        const val MIN_MAJOR_DP = 40f
        const val MIN_MINOR_DP = 4f
        const val MIN_FRAME_DP = 12f

        fun of(pps: Float, fps: Float): RulerSteps {
            var i = MAJORS.indexOfFirst { it * pps >= MIN_MAJOR_DP }
            if (i < 0) i = MAJORS.lastIndex
            val major = MAJORS[i]
            if (i == 0) {
                if (pps / TimeAxis.safeFps(fps) >= MIN_FRAME_DP) return RulerSteps(major, 0, true, false)
                for (n in ONE_SECOND_SUBS) if (pps / n >= MIN_MINOR_DP) return RulerSteps(major, n, false, false)
                return RulerSteps(major, 0, false, false)
            }
            val n = SUBS[i]
            return RulerSteps(major, if (major * pps / n >= MIN_MINOR_DP) n else 0, false, true)
        }
    }
}

/** Relógio `MM:SS:FF` da A.01; a partir de 1 h ganha a hora (`H:MM:SS:FF`, a A.01 virava "75:12:03"). */
internal object Timecode {
    /** `out` = [horas, minutos, segundos, quadros]. Sem alocação (o relógio repinta a 60 Hz). */
    fun split(frame: Int, fps: Float, out: IntArray) {
        val f = TimeAxis.safeFps(fps).toDouble()
        val fr = max(0, frame)
        val totalSeconds = floor(fr / f + 1e-9).toLong()
        // Quadro dentro do segundo: conta a partir do 1º quadro que cai NAQUELE segundo
        // (com 29,97 fps o segundo 1 começa no quadro 30, não em 29,97).
        val firstOfSecond = ceil(totalSeconds * f - 1e-6).toLong()
        out[0] = (totalSeconds / 3600).toInt()
        out[1] = ((totalSeconds / 60) % 60).toInt()
        out[2] = (totalSeconds % 60).toInt()
        out[3] = max(0L, fr - firstOfSecond).toInt()
    }

    fun format(frame: Int, fps: Float): String {
        val p = IntArray(4)
        split(frame, fps, p)
        val base = String.format(Locale.ROOT, "%02d:%02d:%02d", p[1], p[2], p[3])
        return if (p[0] > 0) "${p[0]}:$base" else base
    }

    /** Rótulo da régua: `m:ss` (ou `h:mm:ss`). */
    fun rulerLabel(seconds: Int): String {
        val h = seconds / 3600
        val m = (seconds / 60) % 60
        val s = seconds % 60
        return if (h > 0) String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s)
        else String.format(Locale.ROOT, "%d:%02d", m, s)
    }
}

/**
 * Ímã. Os alvos são reunidos UMA vez no começo do gesto (ordenados, busca
 * binária); o cabeçote disputa junto a cada passo porque a vista pode andar
 * (auto-rolagem).
 */
internal object Snap {
    const val NONE = Int.MIN_VALUE

    /** Alvo mais perto de `value` a no máximo `tol` frames (`extra` = cabeçote; NONE = sem). */
    fun nearest(targets: IntArray, value: Double, extra: Int, tol: Double): Int {
        var best = NONE
        var bestD = Double.MAX_VALUE
        if (extra != NONE) {
            val d = abs(extra - value)
            if (d <= tol) {
                bestD = d
                best = extra
            }
        }
        if (targets.isNotEmpty()) {
            var i = java.util.Arrays.binarySearch(targets, floor(value).toInt())
            if (i < 0) i = -i - 1
            for (k in i - 1..i + 1) {
                if (k !in targets.indices) continue
                val d = abs(targets[k] - value)
                if (d <= tol && d < bestD) {
                    bestD = d
                    best = targets[k]
                }
            }
        }
        return best
    }

    /**
     * Encaixa um intervalo pelo INÍCIO ou pelo FIM, o que estiver mais perto.
     * Devolve o novo início em `out[0]` e a guia (frame encaixado ou NONE) em `out[1]`.
     */
    fun span(targets: IntArray, start: Int, length: Int, extra: Int, tol: Double, out: IntArray) {
        val a = nearest(targets, start.toDouble(), extra, tol)
        val end = start.toLong() + length
        val b = nearest(targets, end.toDouble(), extra, tol)
        val da = if (a == NONE) Double.MAX_VALUE else abs(a.toLong() - start).toDouble()
        val db = if (b == NONE) Double.MAX_VALUE else abs(b.toLong() - end).toDouble()
        when {
            a != NONE && da <= db -> { out[0] = a; out[1] = a }
            b != NONE -> { out[0] = b - length; out[1] = b }
            else -> { out[0] = start; out[1] = NONE }
        }
    }

    /** Ordena e tira repetidos. */
    fun sortedDistinct(values: IntArray, count: Int): IntArray {
        if (count == 0) return IntArray(0)
        val a = values.copyOf(count)
        a.sort()
        var n = 1
        for (i in 1 until a.size) if (a[i] != a[n - 1]) a[n++] = a[i]
        return a.copyOf(n)
    }
}

/**
 * Auto-rolagem na borda durante arrastos: só rola para o lado a que o dedo
 * FOI desde o começo do gesto (pegar um clipe já perto da borda não sai rolando).
 */
internal object AutoScroll {
    fun direction(pos: Float, from: Float, low: Float, high: Float, intent: Float): Int = when {
        pos < low && pos < from - intent -> -1
        pos > high && pos > from + intent -> 1
        else -> 0
    }
}

/**
 * O que o dedo QUIS, decidido uma vez por gesto (spec §5.1). Rolar e fazer
 * scrub não custam nada e ficam nos 45°; EDITAR o projeto (mover clipe,
 * aparar, arrastar losango, reordenar) exige eixo claro, 2:1 — o empate de
 * 45° classificava uma rolagem um pouco torta como "mover clipe", e a camada
 * era escolhida e ia junto com o dedo.
 */
internal object Press {
    /** |eixo| ≥ EDIT_RATIO·|outro| para um arrasto editar. */
    const val EDIT_RATIO = 2f
    /** Toque longo: o dedo tem de estar quieto há pelo menos isto ao vencer o prazo. */
    const val STILL_MS = 150L

    fun horizontal(dx: Float, dy: Float): Boolean = abs(dx) >= abs(dy)

    /**
     * Rolar a pilha ganha cedo: basta |dy| ≥ SCROLL_RATIO·|dx| (≈ 31° da
     * horizontal). O dedo que sobe um pouco torto quer ver as outras camadas,
     * não arrastar o tempo; o scrub continua com o arrasto de fato deitado.
     * Só para a lista inteira — na fileira compacta o vertical troca de
     * camada, e lá o empate de 45° ([horizontal]) continua valendo.
     */
    const val SCROLL_RATIO = 0.6f
    fun scrollWins(dx: Float, dy: Float): Boolean = dy != 0f && abs(dy) >= SCROLL_RATIO * abs(dx)

    /** Claramente no eixo do tempo: mover, aparar, arrastar losango. */
    fun timeEdit(dx: Float, dy: Float): Boolean = abs(dx) >= EDIT_RATIO * abs(dy)

    /** Claramente na pilha: reordenar. */
    fun stackEdit(dx: Float, dy: Float): Boolean = abs(dy) >= EDIT_RATIO * abs(dx)

    /**
     * Fileira compacta (uma camada só): o arrasto vertical passa pelas camadas
     * como uma roda — cada altura de linha percorrida é uma camada. [travel] é
     * quanto o dedo SUBIU (positivo = a de baixo, como rolar a lista). Só
     * linhas inteiras contam; o resto fica para o próximo passo.
     */
    fun compactSteps(travel: Float, row: Float): Int =
        if (!travel.isFinite() || !row.isFinite() || row <= 0f) 0 else (travel / row).toInt()

    /**
     * Quietude do dedo antes do toque longo. O prazo de 500 ms corria mesmo com
     * o dedo rastejando (menos que o slop) e, vencido, o primeiro movimento
     * virava mover/reordenar — a rolagem que começava devagar levantava a
     * camada. Aqui cada amostra que sai do "ninho" (mais que o tremor) marca a
     * hora; o toque longo só é aceito se a última saída foi há [STILL_MS] ou mais.
     */
    class Stillness(private val jitter: Float) {
        private var restX = 0f
        private var restY = 0f
        private var lastMoveAt = 0L

        fun down(x: Float, y: Float, at: Long) {
            restX = x
            restY = y
            lastMoveAt = at
        }

        fun move(x: Float, y: Float, at: Long) {
            val dx = x - restX
            val dy = y - restY
            if (dx * dx + dy * dy > jitter * jitter) {
                restX = x
                restY = y
                lastMoveAt = at
            }
        }

        /** Aceita o toque longo em [now]? */
        fun still(now: Long): Boolean = now - lastMoveAt >= STILL_MS
    }
}

/**
 * A timeline vira a fileira única da camada? Painel aberto: sempre. Doca
 * aberta: só sem trilhas de propriedade abertas e fora do modo de escolher
 * keyframes — senão fica inteira, para dar para mexer nas trilhas e nos
 * losangos (o dono pediu a fileira única "sem ficar impossível de mexer").
 */
internal fun timelineCompact(panel: Boolean, dock: Boolean, tracksOpen: Boolean, selectingKeys: Boolean): Boolean =
    panel || (dock && !tracksOpen && !selectingKeys)

/**
 * O que um toque no corpo de um clipe faz com a seleção de camadas.
 *
 * Modo "Selecionar várias camadas" (veio do app antigo): o toque SOMA ou
 * TIRA o clipe da seleção, sem trocar — é assim que se junta um lote sem
 * precisar segurar cada clipe. Ele vale antes de tudo (o modo deixa a
 * timeline inteira, nunca compacta). Fora dele: no compacto o toque na
 * escolhida sai do painel (noutro pedaço da linha, troca); com lote de 2+ o toque também soma/tira; senão troca a escolhida.
 */
internal enum class LayerTap { LEAVE_COMPACT, TOGGLE, REPLACE, DESELECT }

/**
 * @param tappedSelected o clipe tocado é a camada escolhida.
 * @param timelineOnly ela foi escolhida só "na mão" da timeline (segurada,
 *   sem opções abertas): aí o toque ABRE as opções em vez de soltar.
 *
 * Tocar de novo na única camada escolhida (com as opções abertas) a solta,
 * como no app antigo — a doca fecha junto. Outra camada troca direto.
 */
internal fun layerTap(
    picking: Boolean,
    compact: Boolean,
    selected: Int,
    tappedSelected: Boolean = false,
    timelineOnly: Boolean = false,
): LayerTap = when {
    picking -> LayerTap.TOGGLE
    // Na fileira compacta os outros pedaços da mesma linha aparecem: tocar num
    // deles troca a escolhida; tocar na própria sai do painel.
    compact -> if (tappedSelected) LayerTap.LEAVE_COMPACT else LayerTap.REPLACE
    selected >= 2 -> LayerTap.TOGGLE
    selected == 1 && tappedSelected && !timelineOnly -> LayerTap.DESELECT
    else -> LayerTap.REPLACE
}

/** Instantes de keyframe de uma camada, em frames da timeline. */
internal object Keyframes {
    /** Tempo local → frame da timeline (`t + start − offset`). */
    fun toTimeline(local: Int, start: Int, offset: Int) = local + start - offset
    fun toLocal(timeline: Int, start: Int, offset: Int) = timeline - start + offset

    /**
     * Limites de arrasto do instante `index` (inclusive): nunca encosta no
     * vizinho (senão duas marcas da mesma trilha se fundem) e não sai da camada
     * — a não ser que já estivesse fora (não pula para dentro sozinho).
     */
    fun dragLimits(instants: IntArray, index: Int, start: Int, end: Int, out: IntArray) {
        val t = instants[index]
        var lo = minOf(start, t)
        var hi = maxOf(end, t)
        if (index > 0) lo = max(lo, instants[index - 1] + 1)
        if (index < instants.lastIndex) hi = minOf(hi, instants[index + 1] - 1)
        out[0] = lo
        out[1] = max(lo, hi)
    }

    /** Índice do instante mais perto de `frame` (a lista está ordenada); −1 se vazia. */
    fun nearestIndex(instants: IntArray, frame: Double): Int {
        if (instants.isEmpty()) return -1
        var i = java.util.Arrays.binarySearch(instants, floor(frame).toInt())
        if (i < 0) i = -i - 1
        var best = -1
        var bestD = Double.MAX_VALUE
        for (k in i - 1..i + 1) {
            if (k !in instants.indices) continue
            val d = abs(instants[k] - frame)
            if (d < bestD) {
                bestD = d
                best = k
            }
        }
        return best
    }

    /** Primeiro índice com instante ≥ `frame`. */
    fun firstAtOrAfter(instants: IntArray, frame: Double): Int {
        val key = ceil(frame).toInt()
        var i = java.util.Arrays.binarySearch(instants, key)
        if (i < 0) i = -i - 1
        return i
    }

    /**
     * Lista de desenho dos losangos de UMA linha: só os instantes na tela
     * (busca binária na borda esquerda, para na direita) e vizinhos a menos de
     * `mergeGap` px viram um grupo (a pílula). Escreve pares [primeiro, último]
     * em `out` e devolve quantos grupos; `out` precisa de 2 × (instantes na
     * tela) — o pintor passa um buffer que cresce e é reusado.
     */
    fun visibleGroups(
        instants: IntArray, view: Double, pxPerFrame: Float, centerX: Float,
        width: Float, margin: Float, mergeGap: Float, out: IntArray,
    ): Int {
        if (instants.isEmpty()) return 0
        var i = firstAtOrAfter(instants, TimeAxis.frameAt(-margin, view, pxPerFrame, centerX))
        var n = 0
        while (i < instants.size) {
            val kx = TimeAxis.xOf(instants[i].toDouble(), view, pxPerFrame, centerX)
            if (kx > width + margin) break
            var j = i
            var lastX = kx
            while (j + 1 < instants.size && lastX <= width + margin) {
                val nx = TimeAxis.xOf(instants[j + 1].toDouble(), view, pxPerFrame, centerX)
                if (nx - lastX >= mergeGap) break
                // Denso (zoom aberto, milhares de marcas): tudo antes de lastX + mergeGap
                // entra no grupo de uma vez (busca binária) em vez de marca a marca —
                // a cada dois passos o grupo anda ≥ mergeGap px: O(largura/mergeGap · log n).
                val k = firstAtOrAfter(instants, TimeAxis.frameAt(lastX + mergeGap, view, pxPerFrame, centerX))
                j = max(j + 1, minOf(k, instants.size) - 1)
                lastX = TimeAxis.xOf(instants[j].toDouble(), view, pxPerFrame, centerX)
            }
            if (2 * n + 1 >= out.size) return n
            out[2 * n] = i
            out[2 * n + 1] = j
            n++
            i = j + 1
        }
        return n
    }

    /** Algum instante do grupo `[a, b]` é `frame`? (busca binária no grupo) */
    fun groupHas(instants: IntArray, a: Int, b: Int, frame: Int): Boolean =
        frame != Snap.NONE && java.util.Arrays.binarySearch(instants, a, b + 1, frame) >= 0
}

/**
 * Miniaturas: o motor as guarda em baldes de 250 ms do tempo da MÍDIA. A
 * timeline pede um frame por balde (o mesmo sempre), então mexer o clipe não
 * troca a chave e a tira não pisca.
 */
internal object Thumbs {
    const val BUCKET_SECONDS = 0.25

    fun bucketOf(localFrame: Double, fps: Float): Int =
        floor(max(0.0, localFrame) / TimeAxis.safeFps(fps) / BUCKET_SECONDS + 1e-9).toInt()

    /** Frame local que o motor põe no balde `bucket` (o 1º quadro dele). */
    fun requestLocalFrame(bucket: Int, fps: Float): Int =
        ceil(bucket * BUCKET_SECONDS * TimeAxis.safeFps(fps) - 1e-6).toInt()
}

/**
 * Waveform em grade FIXA do tempo (fase 8D). O balde `k` cobre os frames
 * `[k·fpb, (k+1)·fpb)` — não depende de onde está a vista, então rolar ou
 * tocar não faz a forma "tremer" (os baldes andavam com a vista) e a janela
 * pedida ao motor serve por várias telas. O tamanho do balde anda em degraus
 * de √2: a pinça só pede de novo quando passa de um degrau.
 */
internal object WaveGrid {
    fun framesPerBucket(targetPx: Float, pxPerFrame: Float): Double {
        if (!(pxPerFrame > 0f) || !(targetPx > 0f)) return 1.0
        val exact = targetPx.toDouble() / pxPerFrame
        val step = kotlin.math.round(kotlin.math.log2(exact) * 2.0) / 2.0
        return 2.0.pow(step)
    }

    fun bucketAt(frame: Double, fpb: Double): Long = floor(frame / fpb).toLong()

    /**
     * Janela a pedir para ver `[first, last]`: a vista com uma tela de folga
     * de cada lado, no máximo `max` baldes. `out` = [início, fim exclusivo].
     */
    fun window(first: Long, last: Long, max: Int, out: LongArray) {
        val visible = last - first + 1
        val pad = ((max - visible) / 2).coerceIn(0L, visible)
        out[0] = first - pad
        out[1] = minOf(last + 1 + pad, out[0] + max)
    }
}

/** Reordenar: linha de destino sob o dedo (0 = topo = camada da frente). */
internal object Reorder {
    /** Presentation only. Expanded property rows travel with their owning layer. */
    data class Preview(val offsets: FloatArray, val sourceStart: Int, val sourceEnd: Int, val gapTop: Float)

    fun preview(tops: FloatArray, ids: LongArray, source: Int, target: Int, dragTop: Float): Preview {
        val offsets = FloatArray(ids.size)
        if (source !in ids.indices || target !in ids.indices || tops.size != ids.size + 1 || !dragTop.isFinite())
            return Preview(offsets, -1, -1, Float.NaN)
        var start = source
        while (start > 0 && ids[start - 1] == ids[source]) start--
        var end = source + 1
        while (end < ids.size && ids[end] == ids[source]) end++
        var targetStart = target
        while (targetStart > 0 && ids[targetStart - 1] == ids[target]) targetStart--
        var targetEnd = target + 1
        while (targetEnd < ids.size && ids[targetEnd] == ids[target]) targetEnd++
        val height = tops[end] - tops[start]
        for (i in ids.indices) offsets[i] = when {
            i in start until end -> dragTop - tops[start]
            targetStart > start && i in end until targetEnd -> -height
            targetStart < start && i in targetStart until start -> height
            else -> 0f
        }
        val gap = if (targetStart > start) tops[targetEnd] - height else tops[targetStart]
        return Preview(offsets, start, end, gap)
    }

    fun targetIndex(y: Float, rowsTop: Float, scroll: Float, rowHeight: Float, count: Int): Int {
        if (count <= 0) return -1
        return floor((y - rowsTop + scroll) / rowHeight).toInt().coerceIn(0, count - 1)
    }

    /** y do traço de destino: acima do destino quando sobe, abaixo quando desce; NaN se não muda. */
    fun dropLineY(source: Int, target: Int, rowsTop: Float, scroll: Float, rowHeight: Float): Float = when {
        target < 0 || target == source -> Float.NaN
        target < source -> rowsTop + target * rowHeight - scroll
        else -> rowsTop + (target + 1) * rowHeight - scroll
    }
}

/** Arredonda ao quadro mais perto (a grade do motor). */
internal fun Double.toFrame(): Int = roundToInt()
