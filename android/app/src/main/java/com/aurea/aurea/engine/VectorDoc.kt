package com.aurea.aurea.engine

/**
 * O documento da camada vetorial do lado da UI — espelho EXATO do codec de
 * engine/src/vector/VectorDocument.cpp (versão 1). A verdade continua no
 * motor: o painel lê, muda uma cópia e devolve inteira (`setVectorDocument`),
 * sem mudar a quantidade de grupos (grupos entram/saem por `addVectorGroup`/
 * `removeVectorGroup`, que remapeiam as trilhas animadas).
 *
 * Caminhos: vértices com tangentes RELATIVAS (como no motor), no espaço do grupo.
 */
data class VVertex(var x: Float, var y: Float, var inX: Float = 0f, var inY: Float = 0f, var outX: Float = 0f, var outY: Float = 0f)

data class VBezier(val v: MutableList<VVertex> = mutableListOf(), var closed: Boolean = false) {
    fun copyDeep() = VBezier(v.map { it.copy() }.toMutableList(), closed)

    fun encode(out: MutableList<Float>) {
        out += if (closed) 1f else 0f
        out += v.size.toFloat()
        for (p in v) { out += p.x; out += p.y; out += p.inX; out += p.inY; out += p.outX; out += p.outY }
    }

    fun toArray(): FloatArray = ArrayList<Float>().also { encode(it) }.toFloatArray()

    companion object {
        fun decode(r: VReader): VBezier {
            val closed = r.b()
            val n = r.i()
            val v = MutableList(n) { VVertex(r.f(), r.f(), r.f(), r.f(), r.f(), r.f()) }
            return VBezier(v, closed)
        }
    }
}

data class VStop(var pos: Float, var r: Float, var g: Float, var b: Float, var a: Float)

/** Tinta: 0 sólida, 1 degradê linear, 2 radial (pontos no espaço do grupo). */
data class VPaint(
    var type: Int = 0,
    var r: Float = 1f, var g: Float = 1f, var b: Float = 1f, var a: Float = 1f,
    var sx: Float = -100f, var sy: Float = 0f, var ex: Float = 100f, var ey: Float = 0f,
    var opacity: Float = 100f,
    val stops: MutableList<VStop> = mutableListOf(),
) {
    fun copyDeep() = copy(stops = stops.map { it.copy() }.toMutableList())
}

data class VKey(var frame: Int, var ease: Int, var path: VBezier)

/** kind: 0 livre, 1 retângulo, 2 elipse, 3 polígono, 4 estrela. */
data class VPath(
    var kind: Int = 0, var reversed: Boolean = false,
    var cx: Float = 0f, var cy: Float = 0f, var w: Float = 200f, var h: Float = 200f,
    var roundness: Float = 0f, var points: Float = 5f,
    var outerRadius: Float = 100f, var innerRadius: Float = 50f,
    var outerRoundness: Float = 0f, var innerRoundness: Float = 0f, var rotation: Float = 0f,
    var path: VBezier = VBezier(),
    val keys: MutableList<VKey> = mutableListOf(),
) {
    fun copyDeep() = copy(path = path.copyDeep(), keys = keys.map { it.copy(path = it.path.copyDeep()) }.toMutableList())
}

data class VGroup(
    var name: String = "Grupo",
    var visible: Boolean = true,
    var merge: Int = 0,
    var px: Float = 0f, var py: Float = 0f, var ax: Float = 0f, var ay: Float = 0f,
    var sx: Float = 100f, var sy: Float = 100f, var rotation: Float = 0f, var opacity: Float = 100f,
    var fillOn: Boolean = true, var fillRule: Int = 0, var fill: VPaint = VPaint(),
    var strokeOn: Boolean = false, var strokeWidth: Float = 6f, var cap: Int = 0, var join: Int = 0,
    var miter: Float = 4f, var dashOffset: Float = 0f, val dashes: MutableList<Float> = mutableListOf(),
    var stroke: VPaint = VPaint(),
    var trimOn: Boolean = false, var trimStart: Float = 0f, var trimEnd: Float = 100f, var trimOffset: Float = 0f, var trimMode: Int = 0,
    var repOn: Boolean = false, var repCopies: Float = 3f, var repOffset: Float = 0f,
    var repAx: Float = 0f, var repAy: Float = 0f, var repPx: Float = 120f, var repPy: Float = 0f,
    var repScale: Float = 100f, var repRotation: Float = 0f, var repStartOpacity: Float = 100f, var repEndOpacity: Float = 100f,
    var repAbove: Int = 0,
    val paths: MutableList<VPath> = mutableListOf(),
) {
    fun copyDeep() = copy(
        fill = fill.copyDeep(), stroke = stroke.copyDeep(), dashes = dashes.toMutableList(),
        paths = paths.map { it.copyDeep() }.toMutableList(),
    )
}

class VReader(private val d: FloatArray) {
    var pos = 0
        private set
    fun f(): Float = if (pos < d.size) d[pos++] else throw IllegalStateException("documento vetorial curto")
    fun i(): Int = f().toInt()
    fun b(): Boolean = f() > 0.5f
}

data class VectorDoc(val groups: MutableList<VGroup> = mutableListOf()) {
    fun copyDeep() = VectorDoc(groups.map { it.copyDeep() }.toMutableList())

    fun encode(): FloatArray {
        val o = ArrayList<Float>(256)
        o += 1f
        o += groups.size.toFloat()
        for (g in groups) {
            o += if (g.visible) 1f else 0f; o += g.merge.toFloat()
            o += g.px; o += g.py; o += g.ax; o += g.ay; o += g.sx; o += g.sy; o += g.rotation; o += g.opacity
            o += if (g.fillOn) 1f else 0f; o += g.fillRule.toFloat()
            putPaint(g.fill, o)
            o += if (g.strokeOn) 1f else 0f; o += g.strokeWidth; o += g.cap.toFloat(); o += g.join.toFloat(); o += g.miter; o += g.dashOffset
            o += g.dashes.size.toFloat(); o.addAll(g.dashes)
            putPaint(g.stroke, o)
            o += if (g.trimOn) 1f else 0f; o += g.trimStart; o += g.trimEnd; o += g.trimOffset; o += g.trimMode.toFloat()
            o += if (g.repOn) 1f else 0f; o += g.repCopies; o += g.repOffset; o += g.repAx; o += g.repAy; o += g.repPx; o += g.repPy
            o += g.repScale; o += g.repRotation; o += g.repStartOpacity; o += g.repEndOpacity; o += g.repAbove.toFloat()
            o += g.paths.size.toFloat()
            for (p in g.paths) {
                o += p.kind.toFloat(); o += if (p.reversed) 1f else 0f; o += p.cx; o += p.cy; o += p.w; o += p.h; o += p.roundness
                o += p.points; o += p.outerRadius; o += p.innerRadius; o += p.outerRoundness; o += p.innerRoundness; o += p.rotation
                p.path.encode(o)
                o += p.keys.size.toFloat()
                for (k in p.keys) { o += k.frame.toFloat(); o += k.ease.toFloat(); k.path.encode(o) }
            }
        }
        return o.toFloatArray()
    }

    fun names(): String = groups.joinToString("\n") { it.name.replace('\n', ' ') }

    companion object {
        private fun putPaint(p: VPaint, o: MutableList<Float>) {
            o += p.type.toFloat(); o += p.r; o += p.g; o += p.b; o += p.a; o += p.sx; o += p.sy; o += p.ex; o += p.ey; o += p.opacity
            o += p.stops.size.toFloat()
            for (s in p.stops) { o += s.pos; o += s.r; o += s.g; o += s.b; o += s.a }
        }

        private fun getPaint(r: VReader): VPaint {
            val p = VPaint(r.i(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f())
            repeat(r.i()) { p.stops += VStop(r.f(), r.f(), r.f(), r.f(), r.f()) }
            return p
        }

        /** Nulo se o fluxo não for um documento válido. */
        fun decode(d: FloatArray?, names: String?): VectorDoc? {
            if (d == null || d.size < 2) return null
            return try {
                val r = VReader(d)
                if (r.f() != 1f) return null
                val nm = names?.split('\n') ?: emptyList()
                val n = r.i()
                val doc = VectorDoc()
                repeat(n) { gi ->
                    val g = VGroup(name = nm.getOrNull(gi)?.takeIf { it.isNotEmpty() } ?: "Grupo ${gi + 1}")
                    g.visible = r.b(); g.merge = r.i()
                    g.px = r.f(); g.py = r.f(); g.ax = r.f(); g.ay = r.f(); g.sx = r.f(); g.sy = r.f(); g.rotation = r.f(); g.opacity = r.f()
                    g.fillOn = r.b(); g.fillRule = r.i(); g.fill = getPaint(r)
                    g.strokeOn = r.b(); g.strokeWidth = r.f(); g.cap = r.i(); g.join = r.i(); g.miter = r.f(); g.dashOffset = r.f()
                    repeat(r.i()) { g.dashes += r.f() }
                    g.stroke = getPaint(r)
                    g.trimOn = r.b(); g.trimStart = r.f(); g.trimEnd = r.f(); g.trimOffset = r.f(); g.trimMode = r.i()
                    g.repOn = r.b(); g.repCopies = r.f(); g.repOffset = r.f(); g.repAx = r.f(); g.repAy = r.f(); g.repPx = r.f(); g.repPy = r.f()
                    g.repScale = r.f(); g.repRotation = r.f(); g.repStartOpacity = r.f(); g.repEndOpacity = r.f(); g.repAbove = r.i()
                    repeat(r.i()) {
                        val p = VPath(
                            r.i(), r.b(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(),
                        )
                        p.path = VBezier.decode(r)
                        repeat(r.i()) { p.keys += VKey(r.f().toInt(), r.i(), VBezier.decode(r)) }
                        g.paths += p
                    }
                    doc.groups += g
                }
                doc
            } catch (_: IllegalStateException) {
                null
            }
        }
    }
}

/** Parâmetros animáveis do grupo (VectorParam do motor, mesma numeração). */
object VParam {
    const val TRIM_START = 0
    const val TRIM_END = 1
    const val TRIM_OFFSET = 2
    const val STROKE_WIDTH = 3
    const val DASH_OFFSET = 4
    const val FILL_OPACITY = 5
    const val STROKE_OPACITY = 6
    const val REP_COPIES = 7
    const val REP_OFFSET = 8
    const val REP_POS_X = 9
    const val REP_POS_Y = 10
    const val REP_ROTATION = 11
    const val REP_SCALE = 12
    const val REP_START_OPACITY = 13
    const val REP_END_OPACITY = 14
    const val POS_X = 15
    const val POS_Y = 16
    const val ROTATION = 17
    const val SCALE = 18
    const val OPACITY = 19
    const val COUNT = 20
}

/** Caminho no cabeçote: afim grupo → composição, flags e o bezier. */
class VPathAt(val a: FloatArray, val flags: Int, val path: VBezier) {
    val free: Boolean get() = flags and 1 != 0
    val animated: Boolean get() = flags and 2 != 0
    val keyHere: Boolean get() = flags and 4 != 0

    fun toComp(x: Float, y: Float, out: FloatArray) {
        out[0] = a[0] * x + a[2] * y + a[4]
        out[1] = a[1] * x + a[3] * y + a[5]
    }

    /** Composição → grupo (nulo se a afim não inverte). */
    fun fromComp(x: Float, y: Float, out: FloatArray): Boolean {
        val det = a[0] * a[3] - a[1] * a[2]
        if (kotlin.math.abs(det) < 1e-9f) return false
        val dx = x - a[4]
        val dy = y - a[5]
        out[0] = (a[3] * dx - a[2] * dy) / det
        out[1] = (-a[1] * dx + a[0] * dy) / det
        return true
    }

    /** Vetor (tangente) composição → grupo. */
    fun vecFromComp(x: Float, y: Float, out: FloatArray): Boolean {
        val det = a[0] * a[3] - a[1] * a[2]
        if (kotlin.math.abs(det) < 1e-9f) return false
        out[0] = (a[3] * x - a[2] * y) / det
        out[1] = (-a[1] * x + a[0] * y) / det
        return true
    }

    companion object {
        fun of(d: FloatArray?): VPathAt? {
            if (d == null || d.size < 9) return null
            return try {
                val r = VReader(d.copyOfRange(7, d.size))
                VPathAt(d.copyOfRange(0, 6), d[6].toInt(), VBezier.decode(r))
            } catch (_: IllegalStateException) {
                null
            }
        }
    }
}
