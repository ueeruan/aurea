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

/**
 * Keyframe de forma. `ease`: 0 linear, 1 suave, 2 congelar (documentos até v3).
 * v4: a curva do trecho que SAI do key, a mesma dos keyframes comuns —
 * `curve` (Interpolation; [CURVE_LEGACY] = vale `ease`), força e alças. A UI
 * só repassa: quem grava a curva é o editor de curva, pelo motor.
 */
data class VKey(
    var frame: Int, var ease: Int, var path: VBezier,
    var curve: Int = CURVE_LEGACY, var easePower: Int = 1,
    var bx1: Float = 0.33f, var by1: Float = 0f, var bx2: Float = 0.67f, var by2: Float = 1f,
    /** 0.5.0: 1 = este key anima o ponto; vazio = todos (ver PathKey::points). */
    var points: List<Int> = emptyList(),
) {
    companion object { const val CURVE_LEGACY = 255 }
}

/** kind: 0 livre, 1 retângulo, 2 elipse, 3 polígono, 4 estrela. */
data class VPath(
    var kind: Int = 0, var reversed: Boolean = false,
    var cx: Float = 0f, var cy: Float = 0f, var w: Float = 200f, var h: Float = 200f,
    var roundness: Float = 0f, var points: Float = 5f,
    var outerRadius: Float = 100f, var innerRadius: Float = 50f,
    var outerRoundness: Float = 0f, var innerRoundness: Float = 0f, var rotation: Float = 0f,
    var path: VBezier = VBezier(),
    val keys: MutableList<VKey> = mutableListOf(),
    /** Omega conteúdo (v3): furo — o contorno recorta o preenchimento do grupo. */
    var exclude: Boolean = false,
) {
    fun copyDeep() = copy(path = path.copyDeep(), keys = keys.map { it.copy(path = it.path.copyDeep()) }.toMutableList())
}

/**
 * v6: operador de caminho do grupo (AE shape operators: Deslocar, Franzir e inflar, Aspereza,
 * Ondular, Arredondar cantos, Torcer, Zigue-zague, Ondular transform). `type` = [VModType];
 * `params` = os [VParam.MOD_PARAMS] valores parados (índices por tipo, ver VectorData.hpp).
 * O painel só repassa: adicionar/remover/ordem/ligar vão pelo motor (que remapeia as trilhas).
 */
data class VModifier(var type: Int = 0, var enabled: Boolean = true, val params: FloatArray = FloatArray(VParam.MOD_PARAMS)) {
    fun copyDeep() = VModifier(type, enabled, params.copyOf())
    override fun equals(other: Any?): Boolean =
        other is VModifier && other.type == type && other.enabled == enabled && other.params.contentEquals(params)
    override fun hashCode(): Int = (type * 31 + enabled.hashCode()) * 31 + params.contentHashCode()
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
    /** Afinar do contorno (v2 do documento): % do começo, % do fim, suavidade %. */
    var taperStart: Float = 0f, var taperEnd: Float = 0f, var taperEase: Float = 0f,
    var trimOn: Boolean = false, var trimStart: Float = 0f, var trimEnd: Float = 100f, var trimOffset: Float = 0f, var trimMode: Int = 0,
    var repOn: Boolean = false, var repCopies: Float = 3f, var repOffset: Float = 0f,
    var repAx: Float = 0f, var repAy: Float = 0f, var repPx: Float = 120f, var repPy: Float = 0f,
    var repScale: Float = 100f, var repRotation: Float = 0f, var repStartOpacity: Float = 100f, var repEndOpacity: Float = 100f,
    var repAbove: Int = 0,
    /** v6: escala Y do repetidor e "Y acompanha X" (projetos antigos: ligada). */
    var repScaleY: Float = 100f, var repScaleLinked: Boolean = true,
    val paths: MutableList<VPath> = mutableListOf(),
    /** Omega conteúdo (v3): perfil de largura do pincel (0..1 ao longo do traço; vazio = constante). */
    val widthProfile: MutableList<Float> = mutableListOf(),
    /** Omega conteúdo (v3): 0 normal, 1 apagar (borracha do desenho). */
    var composite: Int = 0,
    /** v6: operadores de caminho, em ordem. */
    val modifiers: MutableList<VModifier> = mutableListOf(),
) {
    fun copyDeep() = copy(
        fill = fill.copyDeep(), stroke = stroke.copyDeep(), dashes = dashes.toMutableList(),
        paths = paths.map { it.copyDeep() }.toMutableList(), widthProfile = widthProfile.toMutableList(),
        modifiers = modifiers.map { it.copyDeep() }.toMutableList(),
    )
}

class VReader(private val d: FloatArray) {
    var pos = 0
        private set
    fun f(): Float = if (pos < d.size) d[pos++] else throw IllegalStateException("documento vetorial curto")
    fun i(): Int = f().toInt()
    fun b(): Boolean = f() > 0.5f
}

data class VectorDoc(
    val groups: MutableList<VGroup> = mutableListOf(),
    /** Omega conteúdo (v3): camada de desenho (o editor de desenho abre nela). */
    var drawing: Boolean = false,
) {
    fun copyDeep() = VectorDoc(groups.map { it.copyDeep() }.toMutableList(), drawing)

    fun encode(): FloatArray {
        val o = ArrayList<Float>(256)
        o += DOC_VERSION
        o += groups.size.toFloat()
        for (g in groups) {
            o += if (g.visible) 1f else 0f; o += g.merge.toFloat()
            o += g.px; o += g.py; o += g.ax; o += g.ay; o += g.sx; o += g.sy; o += g.rotation; o += g.opacity
            o += if (g.fillOn) 1f else 0f; o += g.fillRule.toFloat()
            putPaint(g.fill, o)
            o += if (g.strokeOn) 1f else 0f; o += g.strokeWidth; o += g.cap.toFloat(); o += g.join.toFloat(); o += g.miter; o += g.dashOffset
            o += g.dashes.size.toFloat(); o.addAll(g.dashes)
            putPaint(g.stroke, o)
            o += g.taperStart; o += g.taperEnd; o += g.taperEase
            o += g.widthProfile.size.toFloat(); o.addAll(g.widthProfile); o += g.composite.toFloat()
            o += if (g.trimOn) 1f else 0f; o += g.trimStart; o += g.trimEnd; o += g.trimOffset; o += g.trimMode.toFloat()
            o += if (g.repOn) 1f else 0f; o += g.repCopies; o += g.repOffset; o += g.repAx; o += g.repAy; o += g.repPx; o += g.repPy
            o += g.repScale; o += g.repRotation; o += g.repStartOpacity; o += g.repEndOpacity; o += g.repAbove.toFloat()
            o += g.repScaleY; o += if (g.repScaleLinked) 1f else 0f   // v6
            o += g.paths.size.toFloat()
            for (p in g.paths) {
                o += p.kind.toFloat(); o += if (p.reversed) 1f else 0f; o += p.cx; o += p.cy; o += p.w; o += p.h; o += p.roundness
                o += p.points; o += p.outerRadius; o += p.innerRadius; o += p.outerRoundness; o += p.innerRoundness; o += p.rotation
                o += if (p.exclude) 1f else 0f
                p.path.encode(o)
                o += p.keys.size.toFloat()
                for (k in p.keys) {
                    o += k.frame.toFloat(); o += k.ease.toFloat(); k.path.encode(o)
                    o += k.curve.toFloat(); o += k.easePower.toFloat(); o += k.bx1; o += k.by1; o += k.bx2; o += k.by2   // v4
                    o += k.points.size.toFloat(); k.points.forEach { o += it.toFloat() }   // v5
                }
            }
            o += g.modifiers.size.toFloat()   // v6
            for (m in g.modifiers) { o += m.type.toFloat(); o += if (m.enabled) 1f else 0f; m.params.forEach { o += it } }
        }
        o += if (drawing) 1f else 0f
        return o.toFloatArray()
    }

    fun names(): String = groups.joinToString("\n") { it.name.replace('\n', ' ') }

    companion object {
        /** Versão do fluxo (vector/VectorDocument.cpp): 2 = afinar do contorno; 3 = perfil, apagar, furo, desenho (Omega); 4 = curva dos keys de forma; 5 = keyframe por ponto; 6 = operadores de caminho e escala X/Y do repetidor. */
        const val DOC_VERSION = 6f

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
                val version = r.f()
                if (version != 1f && version != 2f && version != 3f && version != 4f && version != 5f && version != DOC_VERSION) return null
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
                    if (version >= 2f) { g.taperStart = r.f(); g.taperEnd = r.f(); g.taperEase = r.f() }
                    if (version >= 3f) { repeat(r.i()) { g.widthProfile += r.f() }; g.composite = r.i() }
                    g.trimOn = r.b(); g.trimStart = r.f(); g.trimEnd = r.f(); g.trimOffset = r.f(); g.trimMode = r.i()
                    g.repOn = r.b(); g.repCopies = r.f(); g.repOffset = r.f(); g.repAx = r.f(); g.repAy = r.f(); g.repPx = r.f(); g.repPy = r.f()
                    g.repScale = r.f(); g.repRotation = r.f(); g.repStartOpacity = r.f(); g.repEndOpacity = r.f(); g.repAbove = r.i()
                    if (version >= 6f) { g.repScaleY = r.f(); g.repScaleLinked = r.b() } else g.repScaleY = g.repScale
                    repeat(r.i()) {
                        val p = VPath(
                            r.i(), r.b(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(), r.f(),
                        )
                        if (version >= 3f) p.exclude = r.b()
                        p.path = VBezier.decode(r)
                        repeat(r.i()) {
                            val k = VKey(r.f().toInt(), r.i(), VBezier.decode(r))
                            if (version >= 4f) {
                                k.curve = r.i(); k.easePower = r.i(); k.bx1 = r.f(); k.by1 = r.f(); k.bx2 = r.f(); k.by2 = r.f()
                            }
                            if (version >= 5f) k.points = List(r.i()) { r.i() }
                            p.keys += k
                        }
                        g.paths += p
                    }
                    if (version >= 6f) repeat(r.i()) {
                        val m = VModifier(r.i(), r.b())
                        for (k in 0 until VParam.MOD_PARAMS) m.params[k] = r.f()
                        g.modifiers += m
                    }
                    doc.groups += g
                }
                if (version >= 3f) doc.drawing = r.b()
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
    /** v6: escala Y do repetidor (só com a escala separada). */
    const val REP_SCALE_Y = 20
    const val COUNT = 21

    /** Operadores de caminho: id do parâmetro `param` do operador `modifier` (vector_modifier_param). */
    const val MOD_BASE = 0x10000
    const val MOD_PARAMS = 16
    /** Floats por operador em queryVectorModifiers: tipo, ligado, bits animados, bits com keyframe, valores. */
    const val MOD_FLOATS = 4 + MOD_PARAMS
    const val MOD_SEED = 15
    fun mod(modifier: Int, param: Int): Int = MOD_BASE + modifier * MOD_PARAMS + param
    fun isMod(id: Int): Boolean = id >= MOD_BASE
}

/** Tipos de operador de caminho (VectorModifierType do motor, mesma numeração). */
object VModType {
    const val OFFSET = 0
    const val PUCKER_BLOAT = 1
    const val ROUGHEN = 2
    const val WIGGLE_PATHS = 3
    const val ROUND_CORNERS = 4
    const val TWIST = 5
    const val ZIGZAG = 6
    const val WIGGLE_TRANSFORM = 7
    const val COUNT = 8
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
