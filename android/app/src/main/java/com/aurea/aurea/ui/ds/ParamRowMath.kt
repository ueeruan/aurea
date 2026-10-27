package com.aurea.aurea.ui.ds

import kotlin.math.abs

/**
 * A CONTA da linha de parâmetro do redesenho 2026-09-29 (Efeitos.dc.html), sem
 * Compose — testada na JVM e espelhada em Swift (`FxParamRowMath`).
 *
 * - Rótulo: 13 sp até 8 letras; mais longo, 11 sp (numa linha até 12, depois duas).
 * - Régua: o valor ACUMULA desde o início do gesto (`início + andado × porDp`),
 *   preso na faixa estendida até o valor de partida ([dragBounds]).
 * - Controle fino: dois dedos = 1/10; arrasto lento = 1/4, subindo até 1 com a
 *   velocidade (o dedo rápido atravessa a faixa, o lento acerta o número).
 * - Riscos: mais claros no centro, apagando para as bordas.
 */
object ParamRowMath {
    /** Fonte do rótulo cheio / encolhido (sp). */
    const val LABEL_FULL_SP = 13f
    const val LABEL_SMALL_SP = 11f

    /** Até quantas letras o rótulo cabe em 13 sp na caixa de 70 dp. */
    const val LABEL_FULL_MAX_CHARS = 8

    /** Até quantas letras o rótulo encolhido cabe numa linha só. */
    const val LABEL_ONE_LINE_MAX_CHARS = 12

    /** Ganho com dois dedos na régua. */
    const val TWO_FINGER_GAIN = 0.1f

    /** Ganho do arrasto lento (abaixo de [SLOW_SPEED]). */
    const val SLOW_GAIN = 0.25f

    /** Velocidades (dp/ms) em que o ganho começa a subir e chega a 1. */
    const val SLOW_SPEED = 0.08f
    const val FAST_SPEED = 0.35f

    /** Peso da amostra nova na média da velocidade. */
    const val SPEED_SMOOTHING = 0.35f

    fun labelFontSp(label: String): Float =
        if (label.trim().length <= LABEL_FULL_MAX_CHARS) LABEL_FULL_SP else LABEL_SMALL_SP

    fun labelMaxLines(label: String): Int =
        if (label.trim().length <= LABEL_ONE_LINE_MAX_CHARS) 1 else 2

    /** Ganho do arrasto: dois dedos, lento (fino) ou rápido (1:1). */
    fun scrubGain(pointers: Int, speedDpPerMs: Float): Float {
        if (pointers >= 2) return TWO_FINGER_GAIN
        val s = abs(speedDpPerMs)
        if (!s.isFinite() || s >= FAST_SPEED) return 1f
        if (s <= SLOW_SPEED) return SLOW_GAIN
        val t = (s - SLOW_SPEED) / (FAST_SPEED - SLOW_SPEED)
        return SLOW_GAIN + (1f - SLOW_GAIN) * t
    }

    /** Velocidade suavizada (dp/ms) com a amostra `dx` em `dt`; `dt` inválido mantém a anterior. */
    fun smoothSpeed(previous: Float, dxDp: Float, dtMs: Float): Float {
        if (!(dtMs > 0f) || !dxDp.isFinite()) return previous
        val sample = abs(dxDp) / dtMs
        return previous + (sample - previous) * SPEED_SMOOTHING
    }

    /**
     * O valor do arrasto: `início + andado × porDp`, preso em [min]..[max]
     * estendida até o início (valor digitado além da régua não salta de volta).
     * Início não finito parte do zero; resultado nunca é NaN.
     */
    fun scrubValue(from: Float, walkedDp: Float, unitsPerDp: Float, min: Float, max: Float): Float {
        val start = if (from.isFinite()) from else 0f
        val lo = if (min.isNaN()) Float.NEGATIVE_INFINITY else min
        val hi = if (max.isNaN()) Float.POSITIVE_INFINITY else max
        val (l, h) = dragBounds(lo, hi, start)
        val v = start + walkedDp * (if (unitsPerDp.isFinite()) unitsPerDp else 0f)
        return if (v.isNaN()) start else v.coerceIn(l, h)
    }

    /** Brilho do risco em `x` numa régua de largura `width`: 1 no centro, 0 nas bordas. */
    fun tickBrightness(x: Float, width: Float): Float {
        if (!(width > 0f)) return 0f
        val half = width / 2f
        return (1f - abs(x - half) / half).coerceIn(0f, 1f)
    }
}

// =============================================================================
// Omega efeitos (área 07): a CONTA dos controles da referência — slider com
// zonas de encaixe, régua de riscos (1 risco = 1 passo, arremesso, encaixe),
// alturas das linhas, casas pelo passo, voltas de ângulo, a regra de abrir o
// efeito novo e o trackball da orientação. Sem Compose: testada na JVM
// (`FxParamMathTest`) e espelhada em Swift com os mesmos nomes (`FxParamMath`).
// =============================================================================
object FxParamMath {
    /**
     * Alturas das linhas (dp). 0.3.4 painéis: ~15 % maiores que as da referência
     * ("está muito compacto"; eram 51 / 57 / 38 / 43 / 37). Par do `FxParamMath` do iOS.
     */
    const val ROW_SLIDER = 60f
    const val ROW_RULER = 66f
    const val ROW_SMALL = 46f
    const val ROW_SECTION = 48f
    const val ROW_LIST = 44f
    /** Folga entre itens da lista e padding da sobreposição (dp). */
    const val LIST_GAP = 6.5f
    /** Rótulo e caixa de valor (dp; eram 75 / 56); margem 3 em volta do rótulo. */
    const val LABEL_W = 80f
    const val VALUE_W = 62f
    const val LABEL_MARGIN = 3f
    /** Régua: risco a cada 5,5 dp, maior a cada 5 riscos; agulha 1,5 dp. */
    const val TICK_SPACING = 5.5f
    const val MAJOR_EVERY = 5
    /** Arremesso acima de 300 px/s; o gesto começa depois de ¾ do touch slop. */
    const val FLING_MIN_PX_S = 300f
    const val RULER_SLOP_FRACTION = 0.75f
    /** Slider: o gesto começa depois de ½ touch slop; 10 dp de pista em cada encaixe. */
    const val SLIDER_SLOP_FRACTION = 0.5f
    const val SLIDER_SNAP_DP = 10f
    /** Abrir/fechar os controles e a seta da seção (ms). */
    const val OPEN_MS = 200
    const val SECTION_ARROW_MS = 300

    /** Controles do motor (`ParamControl`). */
    fun rowHeight(control: Int, type: Int): Float = when (control) {
        com.aurea.aurea.engine.ParamControl.SLIDER -> ROW_SLIDER
        com.aurea.aurea.engine.ParamControl.RULER,
        com.aurea.aurea.engine.ParamControl.POINT,
        com.aurea.aurea.engine.ParamControl.XYZ,
        com.aurea.aurea.engine.ParamControl.ORIENTATION -> ROW_RULER
        com.aurea.aurea.engine.ParamControl.HUE_DISC -> 150f
        else -> if (type == com.aurea.aurea.engine.ParamType.FLOAT || type == com.aurea.aurea.engine.ParamType.INT ||
            type == com.aurea.aurea.engine.ParamType.ANGLE) ROW_RULER else ROW_SMALL
    }

    /**
     * O controle que a linha usa: o do motor; linha antiga (0) num número cai na
     * régua, num ponto em Point/Xyz.
     */
    fun controlFor(control: Int, type: Int): Int = when {
        control != com.aurea.aurea.engine.ParamControl.AUTO -> control
        type == com.aurea.aurea.engine.ParamType.POINT2D -> com.aurea.aurea.engine.ParamControl.POINT
        type == com.aurea.aurea.engine.ParamType.POINT3D -> com.aurea.aurea.engine.ParamControl.XYZ
        type == com.aurea.aurea.engine.ParamType.FLOAT || type == com.aurea.aurea.engine.ParamType.INT ||
            type == com.aurea.aurea.engine.ParamType.ANGLE -> com.aurea.aurea.engine.ParamControl.RULER
        else -> com.aurea.aurea.engine.ParamControl.AUTO
    }

    // --- Slider ---------------------------------------------------------------

    /** Encaixes dentro da faixa, ordenados e sem repetição. */
    private fun snapsIn(min: Float, max: Float, snaps: List<Float>): List<Float> =
        snaps.filter { it.isFinite() && it > min && it < max }.distinct().sorted()

    /**
     * Valor na posição `x` (px) de um trilho de `width` px: cada encaixe ganha
     * `snapPx` de pista onde o valor fica parado; fora deles, linear, preso e
     * quantizado em `step` (0 = contínuo). Absoluto: o valor vai para o dedo.
     */
    fun sliderValueAt(x: Float, width: Float, min: Float, max: Float, step: Float, snaps: List<Float>, snapPx: Float): Float {
        if (!(max > min) || !(width > 0f)) return min
        val inner = snapsIn(min, max, snaps)
        val zone = if (snapPx.isFinite() && snapPx > 0f) snapPx else 0f
        val usable = (width - zone * inner.size).coerceAtLeast(1f)
        val range = max - min
        val pos = x.coerceIn(0f, width)
        // Pistas: antes do encaixe k (0-based) já passaram k zonas.
        var passed = 0
        for ((k, s) in inner.withIndex()) {
            val start = (s - min) / range * usable + zone * k
            if (pos < start) break
            if (pos <= start + zone) return s
            passed = k + 1
        }
        val raw = min + ((pos - zone * passed) / usable).coerceIn(0f, 1f) * range
        val q = if (step > 0f && step.isFinite()) min + kotlin.math.round((raw - min) / step) * step else raw
        return q.coerceIn(min, max)
    }

    /** O inverso: onde (px) o polegar fica para `value` (no começo da pista do encaixe). */
    fun sliderX(value: Float, width: Float, min: Float, max: Float, snaps: List<Float>, snapPx: Float): Float {
        if (!(max > min) || !(width > 0f)) return 0f
        val inner = snapsIn(min, max, snaps)
        val zone = if (snapPx.isFinite() && snapPx > 0f) snapPx else 0f
        val usable = (width - zone * inner.size).coerceAtLeast(1f)
        val v = value.coerceIn(min, max)
        val before = inner.count { it < v }
        val onSnap = inner.indexOfFirst { it == v }
        val base = (v - min) / (max - min) * usable + zone * before
        return if (onSnap >= 0) base + zone / 2f else base
    }

    // --- Régua ---------------------------------------------------------------

    /** Valor depois de andar `scrolledPx` (dedo para a esquerda = positivo): 1 risco = 1 `step`. */
    fun rulerValueAfter(start: Float, scrolledPx: Float, spacingPx: Float, step: Float): Float {
        if (!(spacingPx > 0f) || !step.isFinite() || step <= 0f) return start
        return start + scrolledPx / spacingPx * step
    }

    /** Encaixa no risco mais próximo (múltiplos de `step`). */
    fun snapToTick(value: Float, step: Float): Float =
        if (!(step > 0f) || !step.isFinite() || !value.isFinite()) value else kotlin.math.round(value / step) * step

    /** Prende na faixa da régua (faixa invertida ou NaN: devolve o valor). */
    fun limitRange(value: Float, min: Float, max: Float): Float {
        if (min.isNaN() || max.isNaN()) return value
        return value.coerceIn(kotlin.math.min(min, max), kotlin.math.max(min, max))
    }

    /** O passo de `from` para `to` cruzou (ou chegou a) um risco de encaixe? Vibra uma vez. */
    fun crossedSnap(from: Float, to: Float, snaps: List<Float>): Boolean {
        if (from == to) return false
        val lo = kotlin.math.min(from, to)
        val hi = kotlin.math.max(from, to)
        return snaps.any { it.isFinite() && it != from && it in lo..hi }
    }

    // --- Formato --------------------------------------------------------------

    /** Casas pelo passo (regra da referência): < 0,00009 → 5 … < 0,9 → 1; senão 0. */
    fun decimalsForStep(step: Float): Int {
        val s = abs(step)
        return when {
            !s.isFinite() || s == 0f -> 2
            s < 0.00009f -> 5
            s < 0.0009f -> 4
            s < 0.009f -> 3
            s < 0.09f -> 2
            s < 0.9f -> 1
            else -> 0
        }
    }

    /**
     * Ângulo além de ±360°: as voltas inteiras vão para a linha auxiliar ("N× +")
     * e o valor mostra o resto. Dentro de ±360°: sem auxiliar (0 voltas).
     */
    fun angleTurns(degrees: Float): Pair<Int, Float> {
        if (!degrees.isFinite() || abs(degrees) <= 360f) return 0 to degrees
        val turns = (degrees / 360f).toInt()
        return turns to degrees - turns * 360f
    }

    // --- Lista -----------------------------------------------------------------

    /**
     * O efeito recém-adicionado abre os controles sozinho? Só quando a camada
     * NÃO tinha efeito nenhum antes de adicionar (referência: `autoSelectEffect`).
     */
    fun opensAfterAdd(previousCount: Int, addedCount: Int): Boolean = previousCount == 0 && addedCount > 0

    // --- Guia "deslize para ajustar" -------------------------------------------

    /** Tempos do guia (ms): entra, espera, sai. */
    enum class Guide(val fadeIn: Int, val hold: Int, val fadeOut: Int) {
        Short(100, 500, 900), Medium(100, 900, 1400), Long(300, 1800, 2000);

        val total: Int get() = fadeIn + hold + fadeOut

        /** Alfa do guia `t` ms depois de aparecer. */
        fun alphaAt(t: Long): Float = when {
            t < 0 -> 0f
            t < fadeIn -> t.toFloat() / fadeIn
            t < fadeIn + hold -> 1f
            t < total -> 1f - (t - fadeIn - hold).toFloat() / fadeOut
            else -> 0f
        }
    }

    // --- Trackball da orientação (α inclinação em X, β guinada em Y, γ rolagem em Z) ---

    /**
     * Gira a orientação (graus) pelo arrasto de (x0,y0) a (x1,y1), normalizados em
     * [−1, 1] (centro do preview = 0, y para baixo). Trackball clássico: os dois
     * pontos sobem para a esfera unitária e a rotação entre eles compõe com a
     * atual. `invert` troca a ordem do produto (referência). Devolve α, β, γ.
     */
    fun trackball(alpha: Float, beta: Float, gamma: Float, x0: Float, y0: Float, x1: Float, y1: Float,
                  invert: Boolean = false): FloatArray {
        fun sphere(x: Float, y: Float): DoubleArray {
            val px = x.toDouble(); val py = -y.toDouble()
            val d = px * px + py * py
            return if (d <= 1.0) doubleArrayOf(px, py, kotlin.math.sqrt(1.0 - d))
            else { val n = kotlin.math.sqrt(d); doubleArrayOf(px / n, py / n, 0.0) }
        }
        val a = sphere(x0, y0)
        val b = sphere(x1, y1)
        val ax = a[1] * b[2] - a[2] * b[1]
        val ay = a[2] * b[0] - a[0] * b[2]
        val az = a[0] * b[1] - a[1] * b[0]
        val dot = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]).coerceIn(-1.0, 1.0)
        val len = kotlin.math.sqrt(ax * ax + ay * ay + az * az)
        if (len < 1e-9) return floatArrayOf(alpha, beta, gamma)
        val half = kotlin.math.acos(dot) / 2.0
        val s = kotlin.math.sin(half) / len
        val drag = doubleArrayOf(kotlin.math.cos(half), ax * s, ay * s, az * s)   // w, x, y, z
        val cur = eulerToQuat(alpha.toDouble(), beta.toDouble(), gamma.toDouble())
        val q = if (invert) quatMul(cur, drag) else quatMul(drag, cur)
        val e = quatToEuler(q)
        return floatArrayOf(e[0].toFloat(), e[1].toFloat(), e[2].toFloat())
    }

    /** Ordem X (α) → Y (β) → Z (γ), graus → quatérnio (w, x, y, z). */
    fun eulerToQuat(alpha: Double, beta: Double, gamma: Double): DoubleArray {
        val r = Math.PI / 180.0
        val cx = kotlin.math.cos(alpha * r / 2); val sx = kotlin.math.sin(alpha * r / 2)
        val cy = kotlin.math.cos(beta * r / 2); val sy = kotlin.math.sin(beta * r / 2)
        val cz = kotlin.math.cos(gamma * r / 2); val sz = kotlin.math.sin(gamma * r / 2)
        // q = qz · qy · qx (aplica X, depois Y, depois Z)
        return doubleArrayOf(
            cz * cy * cx + sz * sy * sx,
            cz * cy * sx - sz * sy * cx,
            cz * sy * cx + sz * cy * sx,
            sz * cy * cx - cz * sy * sx,
        )
    }

    /** O inverso de [eulerToQuat] (graus). */
    fun quatToEuler(q: DoubleArray): DoubleArray {
        val w = q[0]; val x = q[1]; val y = q[2]; val z = q[3]
        val d = 180.0 / Math.PI
        val sinp = (2 * (w * y - z * x)).coerceIn(-1.0, 1.0)
        val alpha = kotlin.math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * d
        val beta = kotlin.math.asin(sinp) * d
        val gamma = kotlin.math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * d
        return doubleArrayOf(alpha, beta, gamma)
    }

    private fun quatMul(a: DoubleArray, b: DoubleArray): DoubleArray = doubleArrayOf(
        a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
        a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
        a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
        a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
    )
}
