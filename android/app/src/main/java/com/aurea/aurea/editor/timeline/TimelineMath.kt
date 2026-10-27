package com.aurea.aurea.editor.timeline

import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

// =============================================================================
//  Lógica pura da timeline (sem Android, sem Compose): testada na JVM.
//  Tempo em FRAMES da composição (a unidade do motor); vista em Double para o
//  arrasto e a pinça não andarem de quadro em quadro.
// =============================================================================

/**
 * Tempo da composição ↔ X absoluto da timeline. `view` é o tempo no centro
 * da janela, inclusive durante scroll/zoom fracionário. O cabeçote também
 * passa por xOf: seu frame inteiro pode não coincidir com `view` durante um gesto.
 */
internal object TimeAxis {
    fun safeFps(fps: Float): Float = if (fps.isFinite() && fps in 1f..1000f) fps else 30f

    /** px por frame para um zoom em dp/s. */
    fun pxPerFrame(pps: Float, density: Float, fps: Float): Float = pps * density / safeFps(fps)

    fun xOf(frame: Double, view: Double, pxPerFrame: Float, centerX: Float): Float =
        (centerX + (frame - view) * pxPerFrame).toFloat()

    fun frameAt(x: Float, view: Double, pxPerFrame: Float, centerX: Float): Double =
        if (!pxPerFrame.isFinite() || pxPerFrame <= 0f || !x.isFinite() || !centerX.isFinite()) view
        else view + (x.toDouble() - centerX) / pxPerFrame

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

/**
 * Decoration must fit inside the temporal rectangle, including subpixel clips.
 * Touch padding is independent and must never become painted duration.
 */
internal object ClipGeometry {
    fun strokeWidth(width: Float, height: Float, requested: Float): Float =
        max(0f, min(requested, min(width, height)))

    fun cornerRadius(width: Float, height: Float, requested: Float): Float =
        max(0f, min(requested, min(width, height) / 2f))

    /** A fixed external grip must not dwarf a short clip at low zoom. */
    fun trimPaintWidth(clipWidth: Float, requested: Float): Float =
        max(0f, min(requested, clipWidth / 2f))

    /** Narrow end grips finish at the beat; their padded hit target stays external. */
    fun trimPaintEndInset(clipWidth: Float, requested: Float): Float =
        if (clipWidth < requested * 2f) trimPaintWidth(clipWidth, requested) else 0f
}

/** Zoom em dp por segundo. */
internal object Zoom {
    const val MIN_PPS = 2f
    const val MAX_PPS = 800f
    /** Reference editor initial scale: 100 dp per second. */
    const val DEFAULT_PPS = 100f
    /** A.01 enquadra projetos longos (≥ 20 s) na largura ao abrir. */
    const val AUTO_FIT_MIN_SECONDS = 20f

    fun clamp(pps: Float): Float = if (pps.isFinite()) pps.coerceIn(MIN_PPS, MAX_PPS) else DEFAULT_PPS

    /** Long projects fit the viewport while preserving the initial zoom ceiling. */
    fun autoFit(availableDp: Float, seconds: Float): Float =
        if (!seconds.isFinite() || !availableDp.isFinite() || seconds <= 0f) DEFAULT_PPS else (availableDp / seconds).coerceIn(4f, DEFAULT_PPS)

    /** Pinça: a vista que mantém `focusFrame` sob o ponto focal `focusX`. */
    fun anchoredView(focusFrame: Double, focusX: Float, centerX: Float, pxPerFrame: Float): Double =
        focusFrame - TimeAxis.frameAt(focusX, 0.0, pxPerFrame, centerX)
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
        val base = String.format(Locale.ROOT, if (TimeAxis.safeFps(fps) > 100f) "%02d:%02d:%03d" else "%02d:%02d:%02d", p[1], p[2], p[3])
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
            b != NONE -> { out[0] = (b.toLong() - length).coerceIn(Int.MIN_VALUE.toLong(), Int.MAX_VALUE.toLong()).toInt(); out[1] = b }
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
    /** Dt máximo por quadro (s): um quadro atrasado não dá um salto. */
    const val MAX_DT = 0.05f

    fun direction(pos: Float, from: Float, low: Float, high: Float, intent: Float): Int = when {
        pos < low && pos < from - intent -> -1
        pos > high && pos > from + intent -> 1
        else -> 0
    }

    /** Faixa da borda: [maxZone] (48 dp), mas nunca mais que 1/3 da janela (timeline baixa). */
    fun zone(maxZone: Float, viewport: Float): Float = max(0f, minOf(maxZone, viewport / 3f))

    /**
     * Rolagem em RAMPA (veio do app antigo): fator com sinal em [−1, 1] — 0 fora
     * da faixa, crescendo com o quanto o dedo entrou nela (1 = na borda ou
     * além). A janela vai de [start] a [end]; só conta o lado a que o dedo FOI
     * desde [from] (pegar já perto da borda não sai rolando).
     */
    fun speed(pos: Float, from: Float, start: Float, end: Float, zone: Float, intent: Float): Float {
        if (!(zone > 0f) || end <= start || !pos.isFinite()) return 0f
        val low = start + zone
        val high = end - zone
        return when {
            pos < low && pos < from - intent -> -((low - pos) / zone).coerceIn(0f, 1f)
            pos > high && pos > from + intent -> ((pos - high) / zone).coerceIn(0f, 1f)
            else -> 0f
        }
    }

    /** Quanto andar neste quadro (px): fator × velocidade máxima × dt (dt limitado a [MAX_DT]). */
    fun step(factor: Float, maxSpeed: Float, dt: Float): Float = factor * maxSpeed * dt.coerceIn(0f, MAX_DT)
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

        /** Omega: prazos curtos (25 % do toque longo) pedem só [quietMs] de dedo quieto. */
        fun quietFor(now: Long, quietMs: Long): Boolean = now - lastMoveAt >= quietMs
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
     * Inclusive clip bounds (the animation endpoint at end is editable).
     * Neighbor keys may be crossed; the engine atomically
     * rejects an occupied destination without deleting or partially moving keys.
     */
    fun dragLimits(instants: IntArray, index: Int, start: Int, end: Int, out: IntArray) {
        val t = instants[index]
        val lo = minOf(start, t)
        val hi = maxOf(end, t)
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

/**
 * Teto da auto-rolagem durante um arraste/aparo (docs/diagnostico/export-falhas.md,
 * A.3 item 1 e C2 item 1).
 *
 * Sem ele, segurar um clipe (ou a borda dele) na margem direita virava uma
 * catraca: a vista é limitada pelo `navigationEnd` VIVO, que durante o arraste
 * é o fim do próprio clipe; cada passo da auto-rolagem empurra o clipe, que
 * empurra o teto, e a composição crescia até horas.
 *
 * Regra (a mesma no iOS, `GestureLimit` em TimelineMath.swift): no começo do
 * gesto vale `max(navigationEnd, duração) + 10 s` (em quadros, pelo fps da
 * composição), congelado até soltar. A vista e o deslocamento aplicado não
 * passam disso. Fora de gesto nada muda ([NONE]).
 */
internal object GestureLimit {
    /** Folga além do conteúdo que um gesto pode abrir de uma vez. */
    const val MARGIN_SECONDS = 10.0

    /** Sem gesto em curso: nenhum teto extra. */
    const val NONE = Long.MAX_VALUE

    /** Maior quadro do protocolo de frames (o mesmo teto de [TimeAxis.clampView]). */
    private const val MAX_FRAME = Int.MAX_VALUE - 1L

    fun autoScrollLimit(startNavEnd: Long, startDuration: Long, fps: Double): Long {
        val safe = if (fps.isFinite() && fps >= 1.0 && fps <= 1000.0) fps else 30.0
        val margin = ceil(MARGIN_SECONDS * safe).toLong()
        val base = max(0L, max(startNavEnd, startDuration)).coerceAtMost(MAX_FRAME)
        return (base + margin).coerceAtMost(MAX_FRAME)
    }

    /**
     * Próxima vista da auto-rolagem: não avança além de [limit]. Se a vista já
     * estava além (o teto não a puxa de volta), só deixa recuar.
     */
    fun clampView(next: Double, current: Double, limit: Long): Double {
        if (limit == NONE || !(next > current)) return next
        return min(next, max(current, limit.toDouble()))
    }

    /** Quadro-alvo de um arraste/aparo: no máximo [limit]. */
    fun clampFrame(frame: Int, limit: Long): Int =
        if (limit == NONE || frame <= limit) frame else limit.coerceIn(0L, MAX_FRAME).toInt()

    /**
     * Maior deslocamento à direita de um lote cujo maior fim é [maxEnd]: nem
     * além do protocolo de frames, nem com o fim passando de [limit].
     */
    fun maxShift(maxEnd: Int, limit: Long): Int {
        val protocol = MAX_FRAME - maxEnd
        val bounded = if (limit == NONE) protocol else min(protocol, max(0L, limit - maxEnd))
        return bounded.coerceIn(0L, Int.MAX_VALUE.toLong()).toInt()
    }
}

// =============================================================================
//  Omega 0.3.2 (área 03): regras puras da timeline da referência. Par do bloco
//  "Omega" de TimelineMath.swift; os testes de JVM conferem os mesmos números.
// =============================================================================

/**
 * 0.3.4 (bugs-034, cores): cor do cartão do clipe, identidade própria do AUREA.
 * Cor SÓLIDA do tipo (`LayerType.color`, a mesma da lista de camadas), um pouco
 * dessaturada (1/5 rumo ao cinza de mesma luma) e escurecida 1/8, para o rótulo
 * branco ficar legível (contraste ≥ 4,5 em todos os tipos). Sem degradê e sem
 * variação por id: a mesma camada tem a cor do tipo dela. Conta inteira, igual
 * no iOS (`ClipColors.solid`).
 */
internal object ClipColors {
    /** Interpolação por canal ARGB, `num/den` de [a] para [b], arredondada. */
    fun mix(a: Int, b: Int, num: Int, den: Int): Int {
        if (den <= 0) return a
        val n = num.coerceIn(0, den)
        var out = 0
        for (shift in intArrayOf(24, 16, 8, 0)) {
            val x = (a ushr shift) and 0xFF
            val y = (b ushr shift) and 0xFF
            val v = (x * (den - n) + y * n + den / 2) / den
            out = out or (v.coerceIn(0, 255) shl shift)
        }
        return out
    }

    /** Luma inteira (Rec. 601, arredondada) de uma cor ARGB. */
    fun luma(argb: Int): Int {
        val r = (argb ushr 16) and 0xFF
        val g = (argb ushr 8) and 0xFF
        val b = argb and 0xFF
        return (r * 299 + g * 587 + b * 114 + 500) / 1000
    }

    /** Cor do cartão a partir da cor do tipo [base] (ARGB): 1/5 dessaturada, 1/8 mais escura, opaca. */
    fun solid(base: Int): Int {
        val y = luma(base)
        val gray = (0xFF shl 24) or (y shl 16) or (y shl 8) or y
        val muted = mix(base or (0xFF shl 24), gray, 1, 5)
        return mix(muted, 0xFF000000.toInt(), 1, 8)
    }
}

/**
 * Cor das marcas. O motor guarda RGBA8 com o R no byte BAIXO (0xAABBGGRR,
 * `Marker::color` em Composition.hpp); a timeline desenha ARGB. A marca nova do
 * toque (`toggle_marker`) nasce com a cor padrão do motor e aparece como o
 * primeiro papel (rosa); as quatro aparências da Omega gravam a cor certa.
 */
internal object MarkerColors {
    /** A cor padrão que o motor dá à marca criada no toque. */
    const val ENGINE_DEFAULT: Int = 0xFFF7C34F.toInt()

    /** RGBA do motor (R baixo) → ARGB. A troca R↔B é a própria inversa. */
    fun unpack(packed: Int): Int =
        (0xFF shl 24) or ((packed and 0xFF) shl 16) or (packed and 0xFF00) or ((packed ushr 16) and 0xFF)

    /** ARGB → RGBA do motor (R baixo), opaco. */
    fun pack(argb: Int): Int = unpack(argb)

    // 0.3.3 recursos: o marcador antigo (seis cores) volta, com a correção de cor.
    /**
     * A paleta de seis cores do marcador da 0.3.2, em ARGB (amarelo, azul, verde,
     * laranja, lilás, branco). O amarelo é a cor padrão do motor ([ENGINE_DEFAULT]).
     */
    val LEGACY: IntArray = intArrayOf(
        0xFFF7C34F.toInt(), 0xFF4D6EFF.toInt(), 0xFF70D56B.toInt(),
        0xFFFFB75B.toInt(), 0xFFD67BDB.toInt(), 0xFFFFFFFF.toInt(),
    )

    /**
     * A cor (ARGB) com que a marca aparece. A 0.3.2 gravava os literais ARGB da
     * paleta sem converter (e o motor nasce com [ENGINE_DEFAULT]): esses valores
     * crus são reconhecidos e mostrados na cor que pretendiam. O resto (o que o
     * editor grava agora, com [pack]) é desempacotado normalmente.
     */
    fun display(packed: Int): Int {
        val opaque = packed or (0xFF shl 24)
        for (c in LEGACY) if (c == opaque) return c
        return unpack(packed)
    }

    /** Índice da cor da paleta antiga (0..5) com que a marca aparece; −1 = outra cor. */
    fun legacyIndex(packed: Int): Int = LEGACY.indexOf(display(packed))

    /**
     * Aparência (0..3) de uma marca: a cor exata de um papel, a cor padrão do
     * motor (= 0) ou o papel mais perto (marcas antigas com a paleta de 6 cores).
     */
    fun appearance(packed: Int, roles: IntArray): Int {
        if (roles.isEmpty()) return 0
        if ((packed or (0xFF shl 24)) == ENGINE_DEFAULT) return 0
        val argb = unpack(packed)
        var best = 0
        var bestD = Long.MAX_VALUE
        for (i in roles.indices) {
            val c = roles[i] or (0xFF shl 24)
            if (c == argb) return i
            val dr = ((c ushr 16) and 0xFF) - ((argb ushr 16) and 0xFF)
            val dg = ((c ushr 8) and 0xFF) - ((argb ushr 8) and 0xFF)
            val db = (c and 0xFF) - (argb and 0xFF)
            val d = dr.toLong() * dr + dg.toLong() * dg + db.toLong() * db
            if (d < bestD) {
                bestD = d
                best = i
            }
        }
        return best
    }
}

/** Régua da Omega: tiques pendurados do alto, em três alturas, sem rótulos. */
internal object OmegaRuler {
    /** Menor distância entre dois tiques (dp). */
    const val MIN_TICK_DP = 4f
    const val SECOND = 2
    const val HALF = 1
    const val FRAME = 0

    /**
     * Passo dos tiques de quadro: a próxima potência de 2 de max(1, quadros que
     * cabem em 4 dp). 30 fps a 100 dp/s = 1; a 10 dp/s = 16.
     */
    fun frameStep(pps: Float, fps: Float): Int {
        val ppfDp = pps / TimeAxis.safeFps(fps)
        if (!(ppfDp > 0f) || !ppfDp.isFinite()) return 1 shl 20
        val n = max(1.0, floor(MIN_TICK_DP / ppfDp.toDouble())).coerceAtMost((1 shl 20).toDouble()).toInt()
        var p = 1
        while (p < n) p = p shl 1
        return p
    }

    /** Passo dos tiques de segundo (em segundos, potência de 2): 4 dp no mínimo entre eles. */
    fun secondStep(pps: Float): Int {
        if (!(pps > 0f) || !pps.isFinite()) return 1 shl 20
        var p = 1
        while (p * pps < MIN_TICK_DP && p < (1 shl 20)) p = p shl 1
        return p
    }

    /** O 1º quadro que cai no instante [seconds] (com 29,97 fps o segundo 1 é o quadro 30). */
    fun frameOf(seconds: Double, fps: Float): Long = ceil(seconds * TimeAxis.safeFps(fps) - 1e-6).toLong()

    /** O quadro começa um segundo (2), um meio segundo (1) ou nenhum dos dois (0)? */
    fun tickClass(frame: Long, fps: Float): Int {
        val f = TimeAxis.safeFps(fps).toDouble()
        fun starts(period: Double): Boolean = frame <= 0L ||
            floor(frame / period + 1e-9) > floor((frame - 1) / period + 1e-9)
        return when {
            starts(f) -> SECOND
            starts(f / 2.0) -> HALF
            else -> FRAME
        }
    }

    /** Altura do tique da classe (dp). */
    fun heightDp(tickClass: Int): Float = when (tickClass) {
        SECOND -> 18f
        HALF -> 12f
        else -> 5f
    }

    /**
     * 0.3.6: os tiques de QUADRO do segundo [sec], contados do começo dele (o passo
     * em potência de 2 corria solto pela régua e caía colado no tique do segundo
     * ou do meio segundo: marcas sobrepostas). Fica de fora o quadro a menos de
     * [MIN_TICK_DP] de um tique de segundo ou de meio segundo.
     */
    inline fun frameTicks(sec: Long, fps: Float, pps: Float, out: (Long) -> Unit) {
        val ppfDp = pps / TimeAxis.safeFps(fps)
        if (!(ppfDp > 0f) || !ppfDp.isFinite()) return
        val step = frameStep(pps, fps).toLong()
        val first = frameOf(sec.toDouble(), fps)
        val half = frameOf(sec + 0.5, fps)
        val next = frameOf(sec + 1.0, fps)
        val gap = MIN_TICK_DP / ppfDp.toDouble() - 1e-6
        var k = first + step
        while (k < next) {
            if ((k - first) >= gap && abs(k - half) >= gap && (next - k) >= gap && tickClass(k, fps) == FRAME) out(k)
            k += step
        }
    }
}

/** 0.3.6: revelar a fileira escolhida na parte da timeline que o painel por cima não cobre. */
internal object TimelineReveal {
    /** A altura à vista das fileiras: a timeline menos a régua e a parte coberta embaixo. */
    fun viewport(height: Float, rowsTop: Float, obscured: Float): Float =
        height - rowsTop - obscured.coerceAtLeast(0f)

    /** A rolagem que deixa `[top, bottom]` à vista (fileira mais alta que a vista: o topo). */
    fun target(top: Float, bottom: Float, scroll: Float, viewport: Float): Float = when {
        viewport <= 0f -> scroll
        top < scroll -> top
        bottom > scroll + viewport -> if (bottom - top > viewport) top else bottom - viewport
        else -> scroll
    }
}

/** Auto-rolagem da Omega (referência: 120 dp/s × fator, a cada quadro). */
internal object OmegaScroll {
    const val EDGE_DP = 38f
    const val SPEED_DP = 120f
    /** Teto do fator (a referência não limita além da borda; aqui fica em 8×). */
    const val MAX_FACTOR = 8f

    /**
     * Fator com sinal na faixa das bordas: 1× ao entrar e até 5× na borda
     * (`max(1, profundidade / (faixa/5))`); 0 fora dela. Só conta o lado a que o
     * dedo FOI desde [from] (pegar já na borda não sai rolando).
     */
    fun edgeFactor(pos: Float, from: Float, start: Float, end: Float, zone: Float, intent: Float): Float {
        if (!(zone > 0f) || end <= start || !pos.isFinite()) return 0f
        val low = start + zone
        val high = end - zone
        return when {
            pos < low && pos < from - intent -> -max(1f, (low - pos) / (zone / 5f)).coerceAtMost(MAX_FACTOR)
            pos > high && pos > from + intent -> max(1f, (pos - high) / (zone / 5f)).coerceAtMost(MAX_FACTOR)
            else -> 0f
        }
    }

    /**
     * Reordenar na vertical: em cima, abaixo de `rowsTop + faixa/2`, fator
     * `max(1, (limite − y) / (faixa/3))`; embaixo, a partir de `altura − faixa`,
     * `max(1, (y − altura) / (faixa/5))`. Negativo = rolar para cima.
     */
    fun reorderFactor(y: Float, height: Float, rowsTop: Float, zone: Float): Float {
        if (!(zone > 0f) || !y.isFinite()) return 0f
        val top = rowsTop + zone / 2f
        return when {
            y < top -> -max(1f, (top - y) / (zone / 3f)).coerceAtMost(MAX_FACTOR)
            y > height - zone -> max(1f, (y - height) / (zone / 5f)).coerceAtMost(MAX_FACTOR)
            else -> 0f
        }
    }
}

/**
 * Quanto do toque longo do sistema cada alvo espera na Omega: no foco, alça e
 * corpo em 25 % e losango em 90 %; na visão geral, cabeçalho, clipe e losango
 * em 95 %; o resto (fundo da linha, alça ≡, tempo atual) o toque longo inteiro.
 */
internal object OmegaHold {
    fun fraction(kind: HitKind, compact: Boolean): Float = when {
        compact && (kind == HitKind.TRIM_START || kind == HitKind.TRIM_END || kind == HitKind.BODY) -> 0.25f
        compact && kind == HitKind.KEYFRAME -> 0.9f
        !compact && (kind == HitKind.HEADER || kind == HitKind.BODY || kind == HitKind.KEYFRAME) -> 0.95f
        else -> 1f
    }

    fun millis(longPressMs: Long, kind: HitKind, compact: Boolean): Long =
        max(1L, Math.round(longPressMs * fraction(kind, compact).toDouble()))

    /** O dedo precisa estar quieto este tanto antes do prazo (no máximo [Press.STILL_MS]). */
    fun quietMs(holdMs: Long): Long = minOf(Press.STILL_MS, holdMs / 2)
}

/** Toque curto na régua da Omega: navega entre os marcadores (não busca o quadro tocado). */
internal object MarkerNav {
    /**
     * Marcador a até [near] px do dedo → ele; senão, metade direita da tela = o
     * próximo depois do cabeçote, metade esquerda = o anterior. [Snap.NONE] = nada.
     */
    fun target(frames: IntArray, x: Float, width: Float, playhead: Int, near: Float, xOf: (Int) -> Float): Int {
        var best = Snap.NONE
        var bestD = Float.MAX_VALUE
        for (f in frames) {
            val d = abs(xOf(f) - x)
            if (d <= near && d < bestD) {
                best = f
                bestD = d
            }
        }
        if (best != Snap.NONE) return best
        return if (x >= width / 2f) next(frames, playhead) else previous(frames, playhead)
    }

    fun next(frames: IntArray, playhead: Int): Int {
        for (f in frames) if (f > playhead) return f
        return Snap.NONE
    }

    fun previous(frames: IntArray, playhead: Int): Int {
        var out = Snap.NONE
        for (f in frames) if (f < playhead) out = f
        return out
    }
}

/** Pulso ao levantar o clipe: 1 → 1,04 → 1 em 130 ms, curva sin(πt). */
internal object LiftPulse {
    const val DURATION_MS = 130L
    const val PEAK = 0.04f

    fun scale(elapsedMs: Long): Float {
        if (elapsedMs <= 0L || elapsedMs >= DURATION_MS) return 1f
        val t = elapsedMs.toDouble() / DURATION_MS
        return 1f + PEAK * kotlin.math.sin(Math.PI * t).toFloat()
    }
}

/** Reticências no MEIO ("começo…fim"), a maior que cabe; conta por ponto de código. */
internal object MiddleEllipsis {
    fun cut(text: String, keep: Int): String {
        val cps = text.codePointCount(0, text.length)
        if (keep >= cps) return text
        val k = keep.coerceAtLeast(0)
        val head = (k + 1) / 2
        val tail = k / 2
        val headEnd = text.offsetByCodePoints(0, head)
        val tailStart = text.offsetByCodePoints(text.length, -tail)
        return text.substring(0, headEnd) + "…" + text.substring(tailStart)
    }

    /** Busca binária no total de letras mantidas; "" se nem a reticência cabe. */
    fun fit(text: String, fits: (String) -> Boolean): String {
        if (text.isEmpty() || fits(text)) return text
        val cps = text.codePointCount(0, text.length)
        var lo = 0
        var hi = cps - 1
        while (lo < hi) {
            val mid = (lo + hi + 1) / 2
            if (fits(cut(text, mid))) lo = mid else hi = mid - 1
        }
        val out = cut(text, lo)
        return if (lo == 0 && !fits(out)) "" else out
    }
}

/** Duas camadas ou mais escolhidas, ou o modo de escolher várias: a "seleção múltipla" da referência. */
internal fun omegaMultiSelection(layerSelectMode: Boolean, selected: Int): Boolean =
    layerSelectMode || selected >= 2
