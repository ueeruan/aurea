package com.aurea.aurea.editor.scene3d

import android.graphics.Matrix
import android.graphics.Paint as APaint
import android.graphics.Typeface
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Accessibility
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.ArrowBackIosNew
import androidx.compose.material.icons.rounded.CenterFocusStrong
import androidx.compose.material.icons.rounded.CenterFocusWeak
import androidx.compose.material.icons.rounded.ChevronLeft
import androidx.compose.material.icons.rounded.ChevronRight
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FastForward
import androidx.compose.material.icons.rounded.FastRewind
import androidx.compose.material.icons.rounded.GridOn
import androidx.compose.material.icons.rounded.Lightbulb
import androidx.compose.material.icons.rounded.NearMe
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Rotate90DegreesCcw
import androidx.compose.material.icons.rounded.SkipNext
import androidx.compose.material.icons.rounded.SkipPrevious
import androidx.compose.material.icons.rounded.TextFields
import androidx.compose.material.icons.rounded.UploadFile
import androidx.compose.material.icons.rounded.Videocam
import androidx.compose.material.icons.rounded.ViewInAr
import androidx.compose.material.icons.rounded.WbSunny
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.asin
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

// =============================================================================
//  MOCKUP da nova Cena 3D — 3ª versão (0.4.6): o LAYOUT do Blender adaptado ao
//  toque, no TEMA do Aurea. Protótipo de interação, NÃO ligado ao motor.
//
//  Do Blender vem a organização, não código nem ícones/arte: viewport com grade
//  e linhas de eixo, gizmo de navegação (tocar = vista do eixo), ferramentas à
//  esquerda (Selecionar / Mover / Girar), Modo Objeto / Modo Pose, ossos
//  octaédricos, anéis X/Y/Z + anel de vista, menu Adicionar (malhas, texto 3D,
//  luzes, câmera, vazio, importar modelo) e a área de baixo com Linha do tempo
//  (dope sheet), Propriedades (campos de arrastar) e Outliner.
//
//  Debug: adb shell am start -S -n com.aurea.aurea.debug/com.aurea.aurea.MainActivity
//         --es aureaMockup scene3d
//  Textos pt-BR fixos de propósito: viram i18n quando o desenho for aprovado e
//  for para o motor + Android + iOS. Modelo importado aparece como caixa com o
//  nome — quem lê glTF/FBX/OBJ é o motor (scene3d), na versão real.
// =============================================================================

/** Papéis da tela, todos do tema do Aurea. */
private object Ui {
    val Window get() = AureaColors.Background
    val Header get() = AureaColors.EditorBar
    val Region get() = AureaColors.EditorCanvas
    val ViewTop get() = AureaColors.EditorCanvas
    val ViewBottom get() = AureaColors.PreviewBackdrop
    val Button get() = AureaColors.EditorRowPill
    val Field get() = AureaColors.EditorGlyphBox
    val Line get() = AureaColors.EditorBarLine
    val Text get() = AureaColors.Text
    val TextDim get() = AureaColors.Muted
    val Grid get() = AureaColors.TickWeak
    val AxisX get() = AureaColors.Danger
    val AxisY get() = AureaColors.Success
    val AxisZ get() = AureaColors.Accent
    val Bone get() = AureaColors.Muted
    val Mesh get() = AureaColors.Muted
    val Tile get() = AureaColors.SurfaceHigh
    val Active get() = AureaColors.Accent
    val ActiveEdge get() = AureaColors.Keyframe
    val Edge = Color(0xFF05070A)
    val Key get() = AureaColors.Text
    val KeySelected get() = AureaColors.Keyframe
    val Record get() = AureaColors.Danger
    val Accent get() = AureaColors.Brand
    val OnAccent get() = AureaColors.OnAction
}

private const val FPS = 24
private const val START = 1
private const val END = 120
private const val ARMATURE = 100

// --- Matemática ----------------------------------------------------------------

private data class V3(val x: Float, val y: Float, val z: Float) {
    operator fun plus(o: V3) = V3(x + o.x, y + o.y, z + o.z)
    operator fun minus(o: V3) = V3(x - o.x, y - o.y, z - o.z)
    operator fun times(s: Float) = V3(x * s, y * s, z * s)
    fun dot(o: V3) = x * o.x + y * o.y + z * o.z
    fun cross(o: V3) = V3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x)
    fun len() = sqrt(dot(this))
    fun norm(): V3 { val l = len(); return if (l < 1e-6f) this else this * (1f / l) }
    operator fun get(i: Int) = when (i) { 0 -> x; 1 -> y; else -> z }
    fun with(i: Int, v: Float) = when (i) { 0 -> copy(x = v); 1 -> copy(y = v); else -> copy(z = v) }
    companion object { val Zero = V3(0f, 0f, 0f) }
}

private val AX = listOf(V3(1f, 0f, 0f), V3(0f, 1f, 0f), V3(0f, 0f, 1f))

private class M3(val m: FloatArray) {
    operator fun times(o: M3): M3 {
        val r = FloatArray(9)
        for (i in 0..2) for (j in 0..2) r[i * 3 + j] = m[i * 3] * o.m[j] + m[i * 3 + 1] * o.m[3 + j] + m[i * 3 + 2] * o.m[6 + j]
        return M3(r)
    }
    operator fun times(v: V3) = V3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z)
    fun t() = M3(floatArrayOf(m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]))
    /** Euler XYZ (R = Rz·Ry·Rx, a convenção do Blender), em radianos. */
    fun euler(): V3 = V3(atan2(m[7], m[8]), asin((-m[6]).coerceIn(-1f, 1f)), atan2(m[3], m[0]))
    companion object {
        val I = M3(floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f))
        fun rot(a: V3, t: Float): M3 {
            val c = cos(t); val s = sin(t); val k = 1 - c
            return M3(floatArrayOf(
                c + a.x * a.x * k, a.x * a.y * k - a.z * s, a.x * a.z * k + a.y * s,
                a.y * a.x * k + a.z * s, c + a.y * a.y * k, a.y * a.z * k - a.x * s,
                a.z * a.x * k - a.y * s, a.z * a.y * k + a.x * s, c + a.z * a.z * k))
        }
        fun euler(e: V3) = rot(AX[2], e.z) * rot(AX[1], e.y) * rot(AX[0], e.x)
    }
}

// --- Armadura ------------------------------------------------------------------

private data class Bone(val name: String, val parent: Int, val offset: V3, val tail: V3)

private val RIG = listOf(
    Bone("Quadril", -1, V3(0f, 1.0f, 0f), V3(0f, 0.16f, 0f)),
    Bone("Coluna", 0, V3(0f, 0.16f, 0f), V3(0f, 0.34f, 0f)),
    Bone("Cabeça", 1, V3(0f, 0.34f, 0f), V3(0f, 0.26f, 0f)),
    Bone("Braço.E", 1, V3(0.06f, 0.3f, 0f), V3(0.3f, 0f, 0f)),
    Bone("Antebraço.E", 3, V3(0.3f, 0f, 0f), V3(0.28f, 0f, 0f)),
    Bone("Braço.D", 1, V3(-0.06f, 0.3f, 0f), V3(-0.3f, 0f, 0f)),
    Bone("Antebraço.D", 5, V3(-0.3f, 0f, 0f), V3(-0.28f, 0f, 0f)),
    Bone("Coxa.E", 0, V3(0.11f, 0f, 0f), V3(0f, -0.46f, 0f)),
    Bone("Canela.E", 7, V3(0f, -0.46f, 0f), V3(0f, -0.44f, 0f)),
    Bone("Coxa.D", 0, V3(-0.11f, 0f, 0f), V3(0f, -0.46f, 0f)),
    Bone("Canela.D", 9, V3(0f, -0.46f, 0f), V3(0f, -0.44f, 0f)),
)

/** O que uma trilha guarda por quadro: rotação Euler (rad) e posição. */
private data class Channel(val rot: V3 = V3.Zero, val loc: V3 = V3.Zero) {
    fun lerp(o: Channel, t: Float) = Channel(rot + (o.rot - rot) * t, loc + (o.loc - loc) * t)
}

private class Pose(val head: List<V3>, val tail: List<V3>, val world: List<M3>, val parent: List<M3>)

private fun solve(ch: (Int) -> Channel, rootBase: V3): Pose {
    val h = ArrayList<V3>(); val t = ArrayList<V3>(); val w = ArrayList<M3>(); val p = ArrayList<M3>()
    RIG.forEachIndexed { i, b ->
        val pr = if (b.parent >= 0) w[b.parent] else M3.I
        val ph = if (b.parent >= 0) h[b.parent] else rootBase
        val head = ph + pr * (b.offset + ch(i).loc)
        val world = pr * M3.euler(ch(i).rot)
        h += head; w += world; p += pr; t += head + world * b.tail
    }
    return Pose(h, t, w, p)
}

// --- Objetos da cena -------------------------------------------------------------

private enum class Kind(val label: String) {
    Armadura("Armadura"), Cubo("Cubo"), Esfera("Esfera"), Cilindro("Cilindro"), Cone("Cone"), Plano("Plano"), Toro("Toro"),
    Texto("Texto 3D"), LuzPonto("Luz pontual"), Sol("Sol"), Camera("Câmera"), Vazio("Vazio"), Modelo("Modelo")
}

private data class SceneObj(val id: Int, val kind: Kind, val name: String, val base: V3, val text: String = "", val file: String = "")

private val MESHES = setOf(Kind.Cubo, Kind.Esfera, Kind.Cilindro, Kind.Cone, Kind.Plano, Kind.Toro)

private typealias Tri3 = Triple<V3, V3, V3>

/** Malhas de exemplo em espaço local (≈ 1 unidade), geradas uma vez. */
private val meshCache = HashMap<Kind, List<Tri3>>()
private fun mesh(kind: Kind): List<Tri3> = meshCache.getOrPut(kind) {
    val out = ArrayList<Tri3>()
    fun quad(a: V3, b: V3, c: V3, d: V3) { out += Triple(a, b, c); out += Triple(a, c, d) }
    val h = 0.5f
    when (kind) {
        Kind.Cubo, Kind.Modelo -> {
            val v = listOf(V3(-h, -h, -h), V3(h, -h, -h), V3(h, h, -h), V3(-h, h, -h), V3(-h, -h, h), V3(h, -h, h), V3(h, h, h), V3(-h, h, h))
            quad(v[0], v[3], v[2], v[1]); quad(v[4], v[5], v[6], v[7]); quad(v[0], v[1], v[5], v[4])
            quad(v[3], v[7], v[6], v[2]); quad(v[0], v[4], v[7], v[3]); quad(v[1], v[2], v[6], v[5])
        }
        Kind.Plano -> quad(V3(-1f, 0f, -1f), V3(-1f, 0f, 1f), V3(1f, 0f, 1f), V3(1f, 0f, -1f))
        Kind.Esfera -> {
            val la = 8; val lo = 14
            fun p(i: Int, j: Int): V3 { val t = PI.toFloat() * i / la; val f = 2 * PI.toFloat() * j / lo; return V3(sin(t) * cos(f), cos(t), sin(t) * sin(f)) * h }
            for (i in 0 until la) for (j in 0 until lo) quad(p(i, j), p(i, j + 1), p(i + 1, j + 1), p(i + 1, j))
        }
        Kind.Cilindro, Kind.Cone -> {
            val n = 16; val top = if (kind == Kind.Cone) 0f else h
            fun ring(y: Float, r: Float, j: Int): V3 { val f = 2 * PI.toFloat() * j / n; return V3(cos(f) * r, y, sin(f) * r) }
            for (j in 0 until n) {
                quad(ring(-h, h, j), ring(h, top, j), ring(h, top, j + 1), ring(-h, h, j + 1))
                out += Triple(V3(0f, -h, 0f), ring(-h, h, j), ring(-h, h, j + 1))
                if (top > 0f) out += Triple(V3(0f, h, 0f), ring(h, top, j + 1), ring(h, top, j))
            }
        }
        Kind.Toro -> {
            val n = 18; val m = 8; val R = 0.4f; val r = 0.14f
            fun p(i: Int, j: Int): V3 {
                val u = 2 * PI.toFloat() * i / n; val v = 2 * PI.toFloat() * j / m
                return V3((R + r * cos(v)) * cos(u), r * sin(v), (R + r * cos(v)) * sin(u))
            }
            for (i in 0 until n) for (j in 0 until m) quad(p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1))
        }
        else -> Unit
    }
    out
}

// --- Câmera do viewport ----------------------------------------------------------

private class Cam(val eye: V3, val right: V3, val up: V3, val fwd: V3, val focal: Float, val center: Offset,
                  val ortho: Boolean, val dist: Float) {
    fun project(p: V3): Offset? {
        val d = p - eye
        val z = d.dot(fwd)
        if (!ortho && z < 0.12f) return null
        val k = if (ortho) focal / dist else focal / z
        return Offset(center.x + d.dot(right) * k, center.y - d.dot(up) * k)
    }
    fun depth(p: V3) = (p - eye).dot(fwd)
    fun scaleAt(p: V3) = if (ortho) focal / dist else focal / depth(p).coerceAtLeast(0.05f)
}

private fun cam(yaw: Float, pitch: Float, dist: Float, ortho: Boolean, size: Size, target: V3 = V3(0f, 0.9f, 0f), focalK: Float = 1.2f,
                centerY: Float = 0.52f): Cam {
    val eye = target + V3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)) * dist
    val fwd = (target - eye).norm()
    val worldUp = if (abs(fwd.y) > 0.99f) V3(0f, 0f, -1f) else AX[1]
    val right = fwd.cross(worldUp).norm()
    return Cam(eye, right, right.cross(fwd), fwd, minOf(size.width, size.height) * focalK,
        Offset(size.width / 2, size.height * centerY), ortho, dist)
}

private fun smooth(t: Float) = t * t * (3 - 2 * t)

private fun sample(keys: Map<Int, Channel>?, frame: Float, fallback: Channel): Channel {
    if (keys.isNullOrEmpty()) return fallback
    val f = keys.keys.sorted()
    if (frame <= f.first()) return keys.getValue(f.first())
    if (frame >= f.last()) return keys.getValue(f.last())
    val a = f.last { it <= frame }; val b = f.first { it >= frame }
    if (a == b) return keys.getValue(a)
    return keys.getValue(a).lerp(keys.getValue(b), smooth((frame - a) / (b - a)))
}

private fun deg(d: Float) = d * PI.toFloat() / 180f
private fun toDeg(r: Float) = r * 180f / PI.toFloat()

// --- Estado ---------------------------------------------------------------------

private enum class Mode(val label: String) { Objeto("Modo Objeto"), Pose("Modo Pose") }
private enum class Tool(val label: String, val icon: ImageVector) {
    Selecionar("Selecionar", Icons.Rounded.NearMe), Mover("Mover", Icons.Rounded.OpenWith), Girar("Girar", Icons.Rounded.Rotate90DegreesCcw)
}
private enum class Area(val label: String) { Tempo("Linha do tempo"), Props("Propriedades"), Outliner("Outliner") }

private class MockupState {
    var yaw by mutableFloatStateOf(0.6f)
    var pitch by mutableFloatStateOf(0.25f)
    var dist by mutableFloatStateOf(3.4f)
    var ortho by mutableStateOf(false)
    var mode by mutableStateOf(Mode.Pose)
    var tool by mutableStateOf(Tool.Girar)
    var area by mutableStateOf(Area.Tempo)
    var solid by mutableStateOf(true)
    var addOpen by mutableStateOf(false)
    var selectedObj by mutableIntStateOf(ARMATURE)
    var active by mutableIntStateOf(5)
    var frame by mutableFloatStateOf(START.toFloat())
    var playing by mutableStateOf(false)
    var autoKey by mutableStateOf(true)
    var pickedKey by mutableStateOf<Int?>(null)
    var nextId = 103
    val objects = mutableStateListOf(
        SceneObj(ARMATURE, Kind.Armadura, "Armadura", V3.Zero),
        SceneObj(101, Kind.Camera, "Câmera", V3(2.4f, 1.3f, 2.6f)),
        SceneObj(102, Kind.Sol, "Sol", V3(-1.6f, 2.6f, 1.2f)),
    )
    val loose = mutableStateMapOf<Int, Channel>()
    val keys = mutableStateMapOf<Int, Map<Int, Channel>>()

    init {
        // Aceno de exemplo (braços abaixados; o direito sobe e acena).
        keys[3] = mapOf(1 to Channel(V3(0f, 0f, deg(-72f))))
        keys[5] = mapOf(1 to Channel(V3(0f, 0f, deg(72f))), 24 to Channel(V3(0f, 0f, deg(-35f))),
            84 to Channel(V3(0f, 0f, deg(-35f))), 110 to Channel(V3(0f, 0f, deg(72f))))
        keys[6] = mapOf(24 to Channel(V3(0f, 0f, deg(-60f))), 36 to Channel(V3(0f, 0f, deg(-20f))),
            48 to Channel(V3(0f, 0f, deg(-60f))), 60 to Channel(V3(0f, 0f, deg(-20f))), 72 to Channel(V3(0f, 0f, deg(-60f))))
        keys[1] = mapOf(1 to Channel(), 60 to Channel(V3(deg(6f), 0f, deg(-4f))), 120 to Channel())
    }

    val intFrame get() = frame.roundToInt().coerceIn(START, END)
    fun channel(id: Int): Channel = loose[id] ?: sample(keys[id], frame, Channel())
    fun obj(id: Int) = objects.firstOrNull { it.id == id }
    fun pose() = solve(::channel, V3.Zero)

    /** A trilha das ações: o osso ativo (Modo Pose); o objeto selecionado (Armadura = osso raiz). */
    val target: Int get() = if (mode == Mode.Pose) active else when (selectedObj) { -1 -> -1; ARMATURE -> 0; else -> selectedObj }

    fun name(id: Int) = if (id < ARMATURE) RIG.getOrNull(id)?.name ?: "" else obj(id)?.name ?: ""

    fun write(id: Int, c: Channel) {
        if (autoKey) { keys[id] = (keys[id] ?: emptyMap()) + (intFrame to c); loose.remove(id) } else loose[id] = c
    }

    fun seek(f: Float) { frame = f.coerceIn(START.toFloat(), END.toFloat()); loose.clear() }

    private fun keyTargets() = when {
        mode == Mode.Objeto -> listOfNotNull(target.takeIf { it >= 0 })
        active >= 0 -> listOf(active)
        else -> RIG.indices.toList()
    }

    fun insertKey() { val f = intFrame; keyTargets().forEach { i -> keys[i] = (keys[i] ?: emptyMap()) + (f to channel(i)); loose.remove(i) } }

    fun deleteKey() {
        val f = pickedKey ?: intFrame
        keyTargets().forEach { i -> keys[i]?.let { keys[i] = it - f } }
        pickedKey = null
    }

    fun keyFrames(): List<Int> = keyTargets().flatMap { keys[it]?.keys ?: emptySet() }.distinct().sorted()
    fun jump(next: Boolean) {
        val f = intFrame
        (if (next) keyFrames().firstOrNull { it > f } else keyFrames().lastOrNull { it < f })?.let { seek(it.toFloat()) }
    }

    fun add(kind: Kind, file: String = "") {
        val n = objects.count { it.kind == kind } + 1
        // Nasce atrás/ao lado do personagem, sempre dentro da vista inicial.
        val slots = listOf(V3(0.85f, 0f, -0.7f), V3(-0.85f, 0f, -0.7f), V3(0f, 0f, -1.4f), V3(1.4f, 0f, -1.5f), V3(-1.4f, 0f, -1.5f))
        val slot = slots[(objects.size - 3).coerceAtLeast(0) % slots.size]
        val spot = slot.with(1, when (kind) { Kind.Plano -> 0.01f; Kind.Texto -> 1.1f; else -> 0.3f })
        val name = when (kind) {
            Kind.Modelo -> file.substringBeforeLast('.').ifBlank { "Modelo" }
            else -> if (n == 1) kind.label else "${kind.label}.${"%03d".format(n - 1)}"
        }
        val o = SceneObj(nextId++, kind, name, if (kind == Kind.Sol || kind == Kind.LuzPonto) spot + V3(0f, 1.4f, 0f) else spot,
            text = if (kind == Kind.Texto) "Aurea" else "", file = file)
        objects += o
        mode = Mode.Objeto; selectedObj = o.id; tool = Tool.Mover; addOpen = false
    }

    fun delete(id: Int) {
        if (id == ARMATURE) return
        objects.removeAll { it.id == id }; keys.remove(id); loose.remove(id)
        if (selectedObj == id) selectedObj = -1
    }

    fun setText(id: Int, t: String) { val i = objects.indexOfFirst { it.id == id }; if (i >= 0) objects[i] = objects[i].copy(text = t) }

    /** Posição/rotação de mundo de uma trilha (osso ou objeto). */
    fun headOf(id: Int, pose: Pose): V3 = if (id < ARMATURE) pose.head[id] else (obj(id)?.base ?: V3.Zero) + channel(id).loc
    fun worldRot(id: Int, pose: Pose): M3 = if (id < ARMATURE) pose.world[id] else M3.euler(channel(id).rot)
    fun parentRot(id: Int, pose: Pose): M3 = if (id < ARMATURE) pose.parent[id] else M3.I
}

// --- Tela ----------------------------------------------------------------------

@Composable
fun Scene3DStudioMockup(onClose: () -> Unit) {
    val s = remember { MockupState() }
    val context = LocalContext.current
    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            val name = runCatching {
                context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                    if (c.moveToFirst()) c.getString(0) else null
                }
            }.getOrNull() ?: uri.lastPathSegment ?: "Modelo"
            s.add(Kind.Modelo, name)
        }
    }
    LaunchedEffect(s.playing) {
        var last = 0L
        while (s.playing) withFrameNanos { now ->
            if (last != 0L) {
                val f = s.frame + (now - last) / 1e9f * FPS
                s.frame = if (f > END) START.toFloat() else f
            }
            last = now
        }
    }
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        Column(Modifier.fillMaxSize().background(Ui.Window).statusBarsPadding().navigationBarsPadding()) {
            Header(s, onClose)
            Box(Modifier.fillMaxWidth().weight(1f)) {
                Viewport(s)
                if (s.addOpen) AddMenu(s) { importer.launch(arrayOf("model/gltf-binary", "model/gltf+json", "application/octet-stream", "*/*")) }
            }
            BottomArea(s)
        }
    }
}

@Composable
private fun Header(s: MockupState, onClose: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(50.dp).background(Ui.Header).padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        IconBtn(Icons.Rounded.ArrowBackIosNew, "Voltar", size = 34.dp, onClick = onClose)
        Spacer(Modifier.width(2.dp))
        var open by remember { mutableStateOf(false) }
        Box {
            Row(Modifier.clip(RoundedCornerShape(10.dp)).background(Ui.Button).tocavel { open = !open }
                .padding(start = 10.dp, end = 4.dp, top = 7.dp, bottom = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                Icon(if (s.mode == Mode.Pose) Icons.Rounded.Accessibility else Icons.Rounded.ViewInAr, null, tint = Ui.Text, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(s.mode.label, color = Ui.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Icon(Icons.Rounded.ExpandMore, null, tint = Ui.TextDim, modifier = Modifier.size(18.dp))
            }
            DropdownMenu(open, { open = false }, modifier = Modifier.background(AureaColors.SurfaceHigh)) {
                Mode.entries.forEach { m ->
                    DropdownMenuItem(text = { Text(m.label, color = Ui.Text, fontSize = 13.sp) },
                        onClick = { s.mode = m; open = false; if (m == Mode.Pose) s.selectedObj = ARMATURE })
                }
            }
        }
        Spacer(Modifier.width(6.dp))
        Row(Modifier.clip(RoundedCornerShape(10.dp)).background(if (s.addOpen) Ui.Accent else Ui.Button)
            .tocavel { s.addOpen = !s.addOpen }.padding(start = 8.dp, end = 12.dp, top = 7.dp, bottom = 7.dp)
            .semantics { contentDescription = "Adicionar" }, verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.Add, null, tint = if (s.addOpen) Ui.OnAccent else Ui.Text, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(2.dp))
            Text("Adicionar", color = if (s.addOpen) Ui.OnAccent else Ui.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        }
        Spacer(Modifier.weight(1f))
        Row(Modifier.clip(RoundedCornerShape(10.dp)).background(Ui.Button)) {
            ShadeBtn(false, !s.solid) { s.solid = false }
            ShadeBtn(true, s.solid) { s.solid = true }
        }
    }
}

@Composable
private fun ShadeBtn(solid: Boolean, on: Boolean, onClick: () -> Unit) {
    Box(Modifier.size(38.dp, 34.dp).clip(RoundedCornerShape(10.dp)).background(if (on) Ui.Accent else Color.Transparent).tocavel(onClick = onClick)
        .semantics { contentDescription = if (solid) "Sólido" else "Aramado" }, contentAlignment = Alignment.Center) {
        val tint = if (on) Ui.OnAccent else Ui.Text
        Canvas(Modifier.size(15.dp)) {
            if (solid) drawCircle(tint, size.minDimension / 2)
            else {
                drawCircle(tint, size.minDimension / 2, style = Stroke(1.2.dp.toPx()))
                drawOval(tint, Offset(0f, size.height * 0.3f), Size(size.width, size.height * 0.4f), style = Stroke(1.dp.toPx()))
                drawLine(tint, Offset(size.width / 2, 0f), Offset(size.width / 2, size.height), 1.dp.toPx())
            }
        }
    }
}

// --- Menu Adicionar ----------------------------------------------------------------

@Composable
private fun AddMenu(s: MockupState, onImport: () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(8.dp).clip(RoundedCornerShape(16.dp)).background(AureaColors.Surface)
        .border(1.dp, Ui.Line, RoundedCornerShape(16.dp))
        // O painel é opaco ao toque: nada passa para o viewport (orbitar/selecionar) por baixo.
        .pointerInput(Unit) { awaitEachGesture { awaitFirstDown(requireUnconsumed = false); do { val e = awaitPointerEvent() } while (e.changes.any { it.pressed }) } }
        .verticalScroll(rememberScrollState()).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Adicionar", color = Ui.Text, fontSize = 15.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            IconBtn(Icons.Rounded.Close, "Fechar", size = 30.dp) { s.addOpen = false }
        }
        Section("Importar")
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Ui.Accent).tocavel(onClick = onImport)
            .padding(horizontal = 14.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.UploadFile, null, tint = Ui.OnAccent, modifier = Modifier.size(22.dp))
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text("Importar modelo 3D", color = Ui.OnAccent, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                Text("GLB · glTF · FBX · OBJ", color = Ui.OnAccent.copy(alpha = 0.75f), fontSize = 11.sp)
            }
        }
        Section("Malha")
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(Kind.Cubo, Kind.Esfera, Kind.Cilindro).forEach { MeshTile(it, Modifier.weight(1f)) { s.add(it) } }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(Kind.Cone, Kind.Plano, Kind.Toro).forEach { MeshTile(it, Modifier.weight(1f)) { s.add(it) } }
        }
        Section("Outros")
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            IconTile(Icons.Rounded.TextFields, "Texto 3D", Modifier.weight(1f)) { s.add(Kind.Texto) }
            IconTile(Icons.Rounded.Lightbulb, "Luz pontual", Modifier.weight(1f)) { s.add(Kind.LuzPonto) }
            IconTile(Icons.Rounded.WbSunny, "Sol", Modifier.weight(1f)) { s.add(Kind.Sol) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            IconTile(Icons.Rounded.Videocam, "Câmera", Modifier.weight(1f)) { s.add(Kind.Camera) }
            IconTile(Icons.Rounded.CenterFocusWeak, "Vazio", Modifier.weight(1f)) { s.add(Kind.Vazio) }
            Spacer(Modifier.weight(1f))
        }
    }
}

@Composable
private fun Section(t: String) = Text(t.uppercase(), color = Ui.TextDim, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)

@Composable
private fun MeshTile(kind: Kind, modifier: Modifier, onClick: () -> Unit) {
    Column(modifier.clip(RoundedCornerShape(12.dp)).background(Ui.Tile).tocavel(onClick = onClick).padding(vertical = 8.dp)
        .semantics { contentDescription = "Adicionar ${kind.label}" }, horizontalAlignment = Alignment.CenterHorizontally) {
        val mesh = Ui.Mesh; val edge = Ui.Edge
        Canvas(Modifier.size(52.dp)) {
            val c = cam(0.7f, 0.45f, 2.2f, false, size, V3.Zero, 1.5f, 0.5f)
            drawTris(meshTris(mesh(kind), V3.Zero, M3.I, if (kind == Kind.Plano) 0.5f else 1f, c, mesh, edge, true), true)
        }
        Spacer(Modifier.height(4.dp))
        Text(kind.label, color = Ui.Text, fontSize = 12.sp)
    }
}

@Composable
private fun IconTile(icon: ImageVector, label: String, modifier: Modifier, onClick: () -> Unit) {
    Column(modifier.clip(RoundedCornerShape(12.dp)).background(Ui.Tile).tocavel(onClick = onClick).padding(vertical = 12.dp)
        .semantics { contentDescription = "Adicionar $label" }, horizontalAlignment = Alignment.CenterHorizontally) {
        Icon(icon, null, tint = Ui.Text, modifier = Modifier.size(24.dp))
        Spacer(Modifier.height(6.dp))
        Text(label, color = Ui.Text, fontSize = 12.sp, maxLines = 1)
    }
}

// --- Viewport --------------------------------------------------------------------

private sealed interface Drag {
    data object Orbit : Drag
    data object None : Drag
    data class Rotate(val id: Int, val axisWorld: V3) : Drag
    data class Move(val id: Int, val axis: Int) : Drag
    /** Leva o objeto no plano da tela (arrastar direto, sem o gizmo). */
    data class Free(val id: Int) : Drag
}

private const val RING = 0.15f

private fun viewCam(s: MockupState, size: Size) = cam(s.yaw, s.pitch, s.dist, s.ortho, size)

@Composable
private fun Viewport(s: MockupState) {
    val measurer = rememberTextMeasurer()
    var size by remember { mutableStateOf(Size.Zero) }
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Ui.ViewTop, Ui.ViewBottom)))) {
        Canvas(Modifier.fillMaxSize().pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val c0 = viewCam(s, size)
                val pose = s.pose()
                val nav = navHit(c0, size, down.position, density)
                if (nav != null) {
                    s.yaw = nav.first; s.pitch = nav.second
                    while (awaitPointerEvent().changes.any { it.pressed }) Unit
                    return@awaitEachGesture
                }
                s.addOpen = false
                var drag: Drag = gizmoHit(s, c0, pose, down.position, 16.dp.toPx())
                if (drag == Drag.None) {
                    // Toque direto no corpo = posar (no celular não há Tab nem tecla R):
                    // tocar num osso entra no Modo Pose com ele, e arrastar o próprio
                    // osso já o gira em volta da junta (grava a chave com o Auto ligado).
                    // Arrastar um objeto o leva no plano da tela.
                    val bone = pickBone(pose, c0, down.position, 30.dp.toPx())
                    val obj = if (bone < 0) pickObject(s, c0, down.position, 28.dp.toPx()) else -1
                    when {
                        bone >= 0 -> { s.mode = Mode.Pose; s.selectedObj = ARMATURE; s.active = bone; s.playing = false }
                        obj >= 0 -> { s.mode = Mode.Objeto; s.selectedObj = obj; s.playing = false }
                    }
                    drag = when {
                        bone >= 0 -> Drag.Rotate(bone, c0.fwd * -1f)
                        obj >= 0 -> Drag.Free(obj)
                        else -> Drag.Orbit
                    }
                } else s.playing = false
                var prev = down.position; var pinch = 0f; var moved = 0f
                while (true) {
                    val ev = awaitPointerEvent()
                    val pressed = ev.changes.filter { it.pressed }
                    if (pressed.isEmpty()) break
                    if (pressed.size >= 2) {
                        val d = (pressed[0].position - pressed[1].position).getDistance()
                        if (pinch > 0f) s.dist = (s.dist * pinch / d).coerceIn(1.5f, 14f)
                        pinch = d; moved += 99f
                    } else {
                        val p = pressed[0].position
                        val delta = p - prev
                        moved += delta.getDistance()
                        val c = viewCam(s, size)
                        when (val dg = drag) {
                            Drag.Orbit -> if (pinch == 0f) {
                                s.yaw -= delta.x * 0.008f
                                s.pitch = (s.pitch + delta.y * 0.006f).coerceIn(-1.5f, 1.5f)
                            }
                            is Drag.Rotate -> {
                                val pz = s.pose()
                                val pivot = c.project(s.headOf(dg.id, pz))
                                if (pivot != null) {
                                    var a = atan2(p.y - pivot.y, p.x - pivot.x) - atan2(prev.y - pivot.y, prev.x - pivot.x)
                                    if (a > PI) a -= (2 * PI).toFloat()
                                    if (a < -PI) a += (2 * PI).toFloat()
                                    val facing = dg.axisWorld.dot(c.fwd)
                                    // Perto da junta o ângulo do dedo salta (poucos px = dezenas de
                                    // graus): ali o giro segue a distância arrastada, ~0,5° por dp.
                                    val nearPivot = (p - pivot).getDistance() < 56.dp.toPx()
                                    val d = when {
                                        nearPivot || abs(facing) <= 0.2f -> (delta.x - delta.y) / density * 0.009f * (if (facing < 0) -1f else 1f)
                                        facing > 0 -> a
                                        else -> -a
                                    }.coerceIn(-0.35f, 0.35f)
                                    val pr = s.parentRot(dg.id, pz)
                                    val next = pr.t() * M3.rot(dg.axisWorld, d) * pr * M3.euler(s.channel(dg.id).rot)
                                    s.write(dg.id, s.channel(dg.id).copy(rot = next.euler()))
                                }
                            }
                            is Drag.Move -> {
                                val head = s.headOf(dg.id, s.pose())
                                val a = c.project(head); val b = c.project(head + AX[dg.axis] * 0.3f)
                                if (a != null && b != null) {
                                    val dir = b - a; val len = dir.getDistance(); val ppu = len / 0.3f
                                    if (ppu > 1f) {
                                        val units = (delta.x * dir.x + delta.y * dir.y) / len / ppu
                                        val ch = s.channel(dg.id)
                                        s.write(dg.id, ch.copy(loc = ch.loc.with(dg.axis, ch.loc[dg.axis] + units)))
                                    }
                                }
                            }
                            is Drag.Free -> {
                                val head = s.headOf(dg.id, s.pose())
                                val k = c.scaleAt(head)
                                if (k > 1f) {
                                    val dw = c.right * (delta.x / k) - c.up * (delta.y / k)
                                    val ch = s.channel(dg.id)
                                    s.write(dg.id, ch.copy(loc = ch.loc + dw))
                                }
                            }
                            Drag.None -> Unit
                        }
                        prev = p
                    }
                    ev.changes.forEach { it.consume() }
                }
                if (drag == Drag.Orbit && moved < 8f) { if (s.mode == Mode.Pose) s.active = -1 else s.selectedObj = -1 }
            }
        }) {
            size = this.size
            val c = viewCam(s, this.size)
            drawFloor(c)
            val pose = s.pose()
            drawScene(s, c, pose, measurer)
            drawGizmo(s, c, pose)
            drawNav(c, measurer)
        }
        Column(Modifier.align(Alignment.TopStart).padding(8.dp).clip(RoundedCornerShape(12.dp))
            .background(Ui.Header.copy(alpha = 0.92f)).padding(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Tool.entries.forEach { t -> ToolBtn(t.icon, t.label, t == s.tool) { s.tool = t } }
        }
        Column(Modifier.align(Alignment.TopEnd).padding(top = 104.dp, end = 35.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            RoundGhost(Icons.Rounded.CenterFocusStrong, "Enquadrar") { s.yaw = 0.6f; s.pitch = 0.25f; s.dist = 3.4f }
            RoundGhost(Icons.Rounded.GridOn, if (s.ortho) "Perspectiva" else "Ortográfica", on = s.ortho) { s.ortho = !s.ortho }
        }
        val t = s.target
        val status = when {
            t < 0 && s.mode == Mode.Pose -> "Toque e arraste um osso para posar · arraste o vazio para orbitar"
            t < 0 -> "Toque num objeto · + Adicionar para criar"
            s.tool == Tool.Mover && t in 1 until ARMATURE -> "${s.name(t)}: só o Quadril se move · use Girar"
            s.mode == Mode.Objeto && t == 0 -> "Armadura · ${s.tool.label}"
            s.mode == Mode.Pose -> "${s.name(t)} · arraste o osso ou os anéis para girar"
            else -> "${s.name(t)} · arraste para mover"
        }
        Text(status, color = Ui.Text.copy(alpha = 0.85f), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.align(Alignment.BottomStart).padding(10.dp))
        // Excluir o objeto selecionado (o X / Delete do Blender), sempre à mão.
        val doomed = s.selectedObj
        val deletable = s.mode == Mode.Objeto && doomed >= 0 && doomed != ARMATURE
        val doomedLabel = "Excluir ${s.name(doomed)}"
        if (deletable) Row(Modifier.align(Alignment.BottomEnd).padding(10.dp).clip(RoundedCornerShape(50))
            .background(Ui.Header.copy(alpha = 0.92f)).border(1.dp, AureaColors.Danger.copy(alpha = 0.5f), RoundedCornerShape(50))
            .tocavel { s.delete(doomed) }.padding(horizontal = 14.dp, vertical = 9.dp)
            .semantics { contentDescription = doomedLabel }, verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.DeleteOutline, null, tint = AureaColors.Danger, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Excluir", color = AureaColors.Danger, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        }
        Text("${if (s.ortho) "Ortográfica" else "Perspectiva"} · quadro ${s.intFrame}", color = Ui.TextDim, fontSize = 11.sp,
            modifier = Modifier.align(Alignment.TopStart).padding(start = 64.dp, top = 12.dp))
    }
}

private fun DrawScope.drawFloor(c: Cam) {
    val n = 10; val step = 0.5f; val e = n * step
    for (i in -n..n) {
        if (i == 0) continue
        val t = i * step
        line3(c, V3(t, 0f, -e), V3(t, 0f, e), Ui.Grid.copy(alpha = 0.45f), 1f)
        line3(c, V3(-e, 0f, t), V3(e, 0f, t), Ui.Grid.copy(alpha = 0.45f), 1f)
    }
    line3(c, V3(-e, 0f, 0f), V3(e, 0f, 0f), Ui.AxisX.copy(alpha = 0.7f), 1.6f)
    line3(c, V3(0f, 0f, -e), V3(0f, 0f, e), Ui.AxisY.copy(alpha = 0.7f), 1.6f)
}

private fun DrawScope.line3(c: Cam, a: V3, b: V3, color: Color, w: Float) {
    // Recorte no plano perto da câmera: uma ponta quase atrás dela projetava
    // para o infinito (a moldura da Câmera virava uma linha atravessando a tela).
    var p = a; var q = b
    if (!c.ortho) {
        val near = 0.12f
        val da = c.depth(p); val db = c.depth(q)
        if (da < near && db < near) return
        if (da < near) p = p + (q - p) * ((near - da) / (db - da))
        else if (db < near) q = q + (p - q) * ((near - db) / (da - db))
    }
    val pa = c.project(p) ?: return; val pb = c.project(q) ?: return
    drawLine(color, pa, pb, w, StrokeCap.Round)
}

private class Tri(val a: Offset, val b: Offset, val c: Offset, val depth: Float, val color: Color, val outline: Color)

private fun shade(base: Color, n: V3, light: V3): Color {
    val lit = 0.42f + 0.58f * abs(n.dot(light))
    return Color(base.red * lit, base.green * lit, base.blue * lit)
}

private fun meshTris(tris: List<Tri3>, at: V3, rot: M3, scale: Float, c: Cam, base: Color, outline: Color, solid: Boolean): List<Tri> {
    val light = (c.fwd * -1f + c.up * 0.6f + c.right * 0.3f).norm()
    return tris.mapNotNull { (p0, p1, p2) ->
        val a = at + rot * (p0 * scale); val b = at + rot * (p1 * scale); val d = at + rot * (p2 * scale)
        val pa = c.project(a) ?: return@mapNotNull null
        val pb = c.project(b) ?: return@mapNotNull null
        val pc = c.project(d) ?: return@mapNotNull null
        val n = (b - a).cross(d - a).norm()
        Tri(pa, pb, pc, c.depth((a + b + d) * (1f / 3f)), shade(base, n, light), outline)
    }
}

private fun DrawScope.drawTri(t: Tri, solid: Boolean) {
    val path = Path().apply { moveTo(t.a.x, t.a.y); lineTo(t.b.x, t.b.y); lineTo(t.c.x, t.c.y); close() }
    if (solid) drawPath(path, t.color)
    drawPath(path, t.outline, style = Stroke(1.dp.toPx()))
}

private fun DrawScope.drawTris(tris: List<Tri>, solid: Boolean) = tris.sortedByDescending { it.depth }.forEach { drawTri(it, solid) }

private val textPaint = APaint(APaint.ANTI_ALIAS_FLAG).apply { textSize = 100f; typeface = Typeface.DEFAULT_BOLD }

/**
 * Texto 3D como um plano no espaço (perspectiva de verdade: o retângulo do texto
 * vai para os 4 cantos projetados, Matrix.setPolyToPoly) com espessura feita de
 * camadas. Cada camada entra na ordem de profundidade da cena.
 */
private fun textLayers(text: String, at: V3, rot: M3, c: Cam, selected: Boolean, solid: Boolean): List<Pair<Float, DrawScope.() -> Unit>> {
    val body = text.ifEmpty { " " }
    val w = textPaint.measureText(body).coerceAtLeast(1f)
    val fm = textPaint.fontMetrics
    val h = fm.descent - fm.ascent
    val hw = 0.26f; val ww = hw * w / h
    val layers = 6; val thick = 0.07f
    val front = if (selected) Ui.ActiveEdge else Ui.Text
    val out = ArrayList<Pair<Float, DrawScope.() -> Unit>>()
    for (k in 0..layers) {
        val z = -thick + thick * k / layers
        val pts = listOf(V3(-ww / 2, hw / 2, z), V3(ww / 2, hw / 2, z), V3(ww / 2, -hw / 2, z), V3(-ww / 2, -hw / 2, z)).map { c.project(at + rot * it) }
        if (pts.any { it == null }) continue
        val dst = FloatArray(8).also { a -> pts.forEachIndexed { i, q -> a[i * 2] = q!!.x; a[i * 2 + 1] = q.y } }
        val m = Matrix().apply { setPolyToPoly(floatArrayOf(0f, 0f, w, 0f, w, h, 0f, h), 0, dst, 0, 4) }
        val color = when {
            k == layers -> front
            !solid -> front.copy(alpha = 0.25f)
            else -> { val t = 0.35f + 0.3f * k / layers; Color(Ui.Mesh.red * t, Ui.Mesh.green * t, Ui.Mesh.blue * t) }
        }
        val paint = APaint(textPaint).apply { this.color = color.toArgb(); if (!solid && k == layers) style = APaint.Style.STROKE }
        out += c.depth(at + rot * V3(0f, 0f, z)) to {
            drawContext.canvas.nativeCanvas.apply { save(); concat(m); drawText(body, 0f, -fm.ascent, paint); restore() }
        }
    }
    return out
}

private fun DrawScope.drawScene(s: MockupState, c: Cam, pose: Pose, measurer: TextMeasurer) {
    val tris = ArrayList<Tri>()
    val light = (c.fwd * -1f + c.up * 0.6f + c.right * 0.3f).norm()
    val armatureSel = s.mode == Mode.Objeto && s.selectedObj == ARMATURE
    // Armadura: ossos octaédricos.
    RIG.indices.forEach { i ->
        val head = pose.head[i]; val tail = pose.tail[i]
        val axis = tail - head; val w = 0.1f * axis.len()
        val r = pose.world[i]
        val along = RIG[i].tail.norm()
        val u0 = if (abs(along.x) > 0.5f) AX[1] else AX[0]
        val v0 = along.cross(u0).norm()
        val mid = head + axis * 0.2f
        val ring = listOf(r * u0, r * v0, r * u0 * -1f, r * v0 * -1f).map { mid + it * w }
        val active = s.mode == Mode.Pose && i == s.active
        val base = if (active) Ui.Active else Ui.Bone
        val outline = when { armatureSel -> Ui.ActiveEdge; active -> Ui.ActiveEdge; s.solid -> Ui.Edge; else -> Ui.Text.copy(alpha = 0.7f) }
        for (k in 0..3) {
            val a = ring[k]; val b = ring[(k + 1) % 4]
            listOf(Triple(head, a, b), Triple(tail, b, a)).forEach { (p0, p1, p2) ->
                val pa = c.project(p0); val pb = c.project(p1); val pc = c.project(p2)
                if (pa != null && pb != null && pc != null)
                    tris += Tri(pa, pb, pc, c.depth((p0 + p1 + p2) * (1f / 3f)), shade(base, (p1 - p0).cross(p2 - p0).norm(), light), outline)
            }
        }
    }
    // Malhas.
    s.objects.filter { it.kind in MESHES || it.kind == Kind.Modelo }.forEach { o ->
        val sel = s.mode == Mode.Objeto && s.selectedObj == o.id
        val at = s.headOf(o.id, pose); val rot = M3.euler(s.channel(o.id).rot)
        val edge = when { sel -> Ui.ActiveEdge; o.kind == Kind.Modelo -> Ui.Accent; s.solid -> Ui.Edge.copy(alpha = 0.5f); else -> Ui.Text.copy(alpha = 0.6f) }
        val base = if (o.kind == Kind.Modelo) Ui.Accent.copy(alpha = 1f).let { Color(it.red * 0.35f, it.green * 0.35f, it.blue * 0.45f) } else Ui.Mesh
        tris += meshTris(mesh(o.kind), at, rot, if (o.kind == Kind.Plano) 0.5f else 0.36f, c, base, edge, s.solid)
    }
    // Uma só ordem de profundidade para triângulos e camadas do texto 3D: o
    // texto fica atrás do que está na frente dele (antes era desenhado por cima).
    val items = ArrayList<Pair<Float, DrawScope.() -> Unit>>()
    tris.forEach { t -> items += t.depth to { drawTri(t, s.solid) } }
    s.objects.filter { it.kind == Kind.Texto }.forEach { o ->
        val sel = s.mode == Mode.Objeto && s.selectedObj == o.id
        items += textLayers(o.text, s.headOf(o.id, pose), M3.euler(s.channel(o.id).rot), c, sel, s.solid)
    }
    items.sortedByDescending { it.first }.forEach { it.second(this) }
    // Luzes, câmeras e vazios (guias do viewport, por cima, como no Blender).
    s.objects.forEach { o ->
        val sel = s.mode == Mode.Objeto && s.selectedObj == o.id
        val col = if (sel) Ui.ActiveEdge else Ui.Text.copy(alpha = 0.8f)
        val at = s.headOf(o.id, pose)
        val p = c.project(at) ?: return@forEach
        when (o.kind) {
            Kind.Sol, Kind.LuzPonto -> {
                drawCircle(col, 7.dp.toPx(), p, style = Stroke(1.5.dp.toPx()))
                drawCircle(col.copy(alpha = 0.25f), 4.dp.toPx(), p)
                if (o.kind == Kind.Sol) {
                    for (k in 0 until 8) { val a = k * PI.toFloat() / 4; val d = Offset(cos(a), sin(a)); drawLine(col, p + d * 10.dp.toPx(), p + d * 15.dp.toPx(), 1.5.dp.toPx()) }
                    c.project(at + V3(0.4f, -1f, -0.3f).norm() * 0.9f)?.let { drawLine(col.copy(alpha = 0.6f), p, it, 1.dp.toPx()) }
                } else drawCircle(col.copy(alpha = 0.5f), 12.dp.toPx(), p, style = Stroke(1.dp.toPx()))
            }
            Kind.Camera -> {
                val f = (V3(0f, 0.9f, 0f) - at).norm(); val r = f.cross(AX[1]).norm(); val u = r.cross(f)
                val tip = at + f * 0.45f
                val corners = listOf(r + u, r * -1f + u, r * -1f + u * -1f, r + u * -1f).map { tip + it * 0.18f }
                corners.forEach { line3(c, at, it, col, 1.5f) }
                corners.indices.forEach { k -> line3(c, corners[k], corners[(k + 1) % 4], col, 1.5f) }
                c.project(tip + u * 0.3f)?.let { tri -> c.project(tip + u * 0.2f + r * 0.08f)?.let { a -> c.project(tip + u * 0.2f - r * 0.08f)?.let { b ->
                    drawPath(Path().apply { moveTo(tri.x, tri.y); lineTo(a.x, a.y); lineTo(b.x, b.y); close() }, col)
                } } }
            }
            Kind.Vazio -> AX.forEach { a -> line3(c, at - a * 0.25f, at + a * 0.25f, col, 1.5f) }
            Kind.Modelo -> {
                val l = measurer.measure("⬚ ${o.file}", TextStyle(color = Ui.Text, fontSize = 10.sp))
                drawText(l, topLeft = p + Offset(-l.size.width / 2f, 0.4f * c.scaleAt(at)))
            }
            else -> Unit
        }
    }
}

private fun ringPoints(center: V3, axis: V3, radius: Float, n: Int = 48): List<V3> {
    val u = (if (abs(axis.y) < 0.9f) axis.cross(AX[1]) else axis.cross(AX[0])).norm()
    val v = axis.cross(u).norm()
    return (0..n).map { k -> val t = k * 2 * PI.toFloat() / n; center + u * (cos(t) * radius) + v * (sin(t) * radius) }
}

private fun gizmoRadius(c: Cam, at: V3): Float = if (c.ortho) RING * c.dist / 3.2f else RING * c.depth(at) / 3.0f

private fun gizmoTarget(s: MockupState): Int {
    if (s.tool == Tool.Selecionar) return -1
    val t = s.target
    if (t < 0) return -1
    if (s.tool == Tool.Mover && t in 1 until ARMATURE) return -1
    return t
}

private fun DrawScope.drawGizmo(s: MockupState, c: Cam, pose: Pose) {
    val id = gizmoTarget(s)
    if (id < 0) return
    val head = s.headOf(id, pose)
    val r = gizmoRadius(c, head)
    val colors = listOf(Ui.AxisX, Ui.AxisY, Ui.AxisZ)
    val ctr = c.project(head) ?: return
    if (s.tool == Tool.Girar) {
        val px = ((c.project(head + c.right * (r * 1.25f)) ?: ctr) - ctr).getDistance()
        drawCircle(Ui.Text.copy(alpha = 0.85f), px, ctr, style = Stroke(1.5.dp.toPx()))
        val rot = s.worldRot(id, pose)
        AX.forEachIndexed { k, a ->
            val pts = ringPoints(head, (rot * a).norm(), r).mapNotNull { c.project(it) }
            for (j in 0 until pts.size - 1) drawLine(colors[k], pts[j], pts[j + 1], 2.4.dp.toPx(), StrokeCap.Round)
        }
    } else {
        AX.forEachIndexed { k, a ->
            val e = c.project(head + a * (r * 1.4f)) ?: return@forEachIndexed
            drawLine(colors[k], ctr, e, 2.4.dp.toPx(), StrokeCap.Round)
            drawCircle(colors[k], 5.dp.toPx(), e)
        }
        drawCircle(Ui.Text, 4.dp.toPx(), ctr, style = Stroke(1.5.dp.toPx()))
    }
}

private fun gizmoHit(s: MockupState, c: Cam, pose: Pose, p: Offset, tol: Float): Drag {
    val id = gizmoTarget(s)
    if (id < 0) return Drag.None
    val head = s.headOf(id, pose)
    val r = gizmoRadius(c, head)
    val ctr = c.project(head) ?: return Drag.None
    if (s.tool == Tool.Girar) {
        var best = -1; var bestD = tol
        val rot = s.worldRot(id, pose)
        val axes = AX.map { (rot * it).norm() }
        axes.forEachIndexed { k, a ->
            ringPoints(head, a, r, 64).mapNotNull { c.project(it) }.forEach { q ->
                val d = (q - p).getDistance(); if (d < bestD) { bestD = d; best = k }
            }
        }
        if (best >= 0) return Drag.Rotate(id, axes[best])
        val px = ((c.project(head + c.right * (r * 1.25f)) ?: ctr) - ctr).getDistance()
        if (abs((p - ctr).getDistance() - px) < tol) return Drag.Rotate(id, c.fwd * -1f)
        return Drag.None
    }
    var best = -1; var bestD = tol * 1.3f
    AX.forEachIndexed { k, a ->
        val e = c.project(head + a * (r * 1.4f)) ?: return@forEachIndexed
        val d = segDist(p, ctr, e); if (d < bestD) { bestD = d; best = k }
    }
    return if (best >= 0) Drag.Move(id, best) else Drag.None
}

private fun pickBone(pose: Pose, c: Cam, p: Offset, tol: Float): Int {
    var best = -1; var bestD = tol
    RIG.indices.forEach { i ->
        val a = c.project(pose.head[i]) ?: return@forEach; val b = c.project(pose.tail[i]) ?: return@forEach
        val d = segDist(p, a, b); if (d < bestD) { bestD = d; best = i }
    }
    return best
}

private fun pickObject(s: MockupState, c: Cam, p: Offset, tol: Float): Int {
    val pose = s.pose()
    var best = -1; var bestD = Float.MAX_VALUE
    s.objects.filter { it.id != ARMATURE }.forEach { o ->
        val at = s.headOf(o.id, pose)
        val q = c.project(at) ?: return@forEach
        val reach = if (o.kind in MESHES || o.kind == Kind.Modelo || o.kind == Kind.Texto) maxOf(tol, 0.4f * c.scaleAt(at)) else tol
        val d = (q - p).getDistance()
        if (d < reach && d < bestD) { bestD = d; best = o.id }
    }
    return best
}

private fun segDist(p: Offset, a: Offset, b: Offset): Float {
    val ab = b - a; val l2 = ab.x * ab.x + ab.y * ab.y
    val t = if (l2 < 1e-3f) 0f else (((p - a).x * ab.x + (p - a).y * ab.y) / l2).coerceIn(0f, 1f)
    return (p - (a + ab * t)).getDistance()
}

// Gizmo de navegação: X do Blender = nosso X; Y do Blender = nosso −Z; Z do Blender = nosso Y.
private val NAV = listOf(Triple("X", V3(1f, 0f, 0f), 0), Triple("Y", V3(0f, 0f, -1f), 1), Triple("Z", V3(0f, 1f, 0f), 2))
private fun axisColor(i: Int) = when (i) { 0 -> Ui.AxisX; 1 -> Ui.AxisY; else -> Ui.AxisZ }

private fun navCenter(size: Size, density: Float) = Offset(size.width - 52f * density, 56f * density)

private fun DrawScope.drawNav(c: Cam, measurer: TextMeasurer) {
    val ctr = navCenter(size, density)
    val len = 30.dp.toPx(); val rad = 9.dp.toPx()
    drawCircle(Ui.Text.copy(alpha = 0.06f), len + rad + 4.dp.toPx(), ctr)
    val items = NAV.flatMap { (l, a, k) -> listOf(Triple(l, a, k), Triple("", a * -1f, k)) }.sortedByDescending { it.second.dot(c.fwd) }
    items.forEach { (label, a, k) ->
        val col = axisColor(k)
        val e = ctr + Offset(a.dot(c.right), -a.dot(c.up)) * len
        if (label.isNotEmpty()) {
            drawLine(col, ctr, e, 2.dp.toPx())
            drawCircle(col, rad, e)
            val layout = measurer.measure(label, TextStyle(color = Color(0xFF0B0F16), fontSize = 10.sp, fontWeight = FontWeight.Bold))
            drawText(layout, topLeft = e - Offset(layout.size.width / 2f, layout.size.height / 2f))
        } else {
            drawCircle(col.copy(alpha = 0.3f), rad * 0.85f, e)
            drawCircle(col, rad * 0.85f, e, style = Stroke(1.dp.toPx()))
        }
    }
}

private fun navHit(c: Cam, size: Size, p: Offset, density: Float): Pair<Float, Float>? {
    val ctr = navCenter(size, density); val len = 30f * density; val tol = 14f * density
    NAV.forEach { (_, a, _) ->
        listOf(a, a * -1f).forEach { v ->
            val e = ctr + Offset(v.dot(c.right), -v.dot(c.up)) * len
            if ((e - p).getDistance() < tol) return if (abs(v.y) > 0.5f) 0f to (if (v.y > 0) 1.5f else -1.5f) else atan2(v.x, v.z) to 0f
        }
    }
    return null
}

// --- Área de baixo ---------------------------------------------------------------

@Composable
private fun BottomArea(s: MockupState) {
    Column(Modifier.fillMaxWidth().height(272.dp).background(Ui.Region)) {
        Box(Modifier.fillMaxWidth().height(1.dp).background(Ui.Line))
        Row(Modifier.fillMaxWidth().height(40.dp).background(Ui.Header).padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Area.entries.forEach { a ->
                val on = a == s.area
                Box(Modifier.clip(RoundedCornerShape(10.dp)).background(if (on) Ui.Button else Color.Transparent)
                    .tocavel { s.area = a }.padding(horizontal = 12.dp, vertical = 7.dp)) {
                    Text(a.label, color = if (on) Ui.Text else Ui.TextDim, fontSize = 12.sp, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal)
                }
            }
        }
        when (s.area) {
            Area.Tempo -> TimelineEditor(s)
            Area.Props -> PropertiesEditor(s)
            Area.Outliner -> OutlinerEditor(s)
        }
    }
}

@Composable
private fun TimelineEditor(s: MockupState) {
    Row(Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(34.dp).clip(RoundedCornerShape(10.dp)).background(if (s.autoKey) Ui.Button else Color.Transparent)
            .tocavel { s.autoKey = !s.autoKey }
            .semantics { contentDescription = if (s.autoKey) "Gravação automática ligada" else "Gravação automática desligada" },
            contentAlignment = Alignment.Center) {
            Box(Modifier.size(12.dp).clip(RoundedCornerShape(50)).background(if (s.autoKey) Ui.Record else Ui.TextDim))
        }
        Spacer(Modifier.width(4.dp))
        IconBtn(Icons.Rounded.SkipPrevious, "Início") { s.seek(START.toFloat()) }
        IconBtn(Icons.Rounded.FastRewind, "Chave anterior") { s.jump(false) }
        Box(Modifier.size(38.dp).clip(RoundedCornerShape(50)).background(Ui.Text).tocavel { s.playing = !s.playing }
            .semantics { contentDescription = if (s.playing) "Pausar" else "Reproduzir" }, contentAlignment = Alignment.Center) {
            Icon(if (s.playing) Icons.Rounded.Pause else Icons.Rounded.PlayArrow, null, tint = Ui.Window, modifier = Modifier.size(24.dp))
        }
        IconBtn(Icons.Rounded.FastForward, "Próxima chave") { s.jump(true) }
        IconBtn(Icons.Rounded.SkipNext, "Fim") { s.seek(END.toFloat()) }
        Spacer(Modifier.weight(1f))
        NumberField("", s.intFrame.toFloat(), "", width = 84.dp, step = 1f, speed = 0.15f, decimals = 0) { s.playing = false; s.seek(it) }
    }
    val rows = buildList {
        add(-1)
        RIG.indices.filter { !s.keys[it].isNullOrEmpty() || (s.mode == Mode.Pose && it == s.active) }.forEach { add(it) }
        s.objects.filter { it.id != ARMATURE && (!s.keys[it.id].isNullOrEmpty() || (s.mode == Mode.Objeto && it.id == s.selectedObj)) }.forEach { add(it.id) }
    }
    val rowH = 26.dp; val rulerH = 22.dp
    val measurer = rememberTextMeasurer()
    fun isOn(i: Int) = if (i < ARMATURE) s.mode == Mode.Pose && s.active == i else s.mode == Mode.Objeto && s.selectedObj == i
    Row(Modifier.fillMaxSize()) {
        Column(Modifier.width(122.dp).fillMaxHeight().background(Ui.Header.copy(alpha = 0.6f))) {
            Row(Modifier.height(rulerH).fillMaxWidth().padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                MiniBtn("◆ Inserir", "Inserir chave") { s.insertKey() }
                MiniBtn(null, "Apagar chave", Icons.Rounded.DeleteOutline) { s.deleteKey() }
            }
            Column(Modifier.verticalScroll(rememberScrollState())) {
                rows.forEach { i ->
                    val on = isOn(i)
                    Row(Modifier.fillMaxWidth().height(rowH).background(if (on) Ui.Accent.copy(alpha = 0.35f) else Color.Transparent)
                        .tocavel {
                            if (i < ARMATURE) { s.mode = Mode.Pose; s.active = i } else { s.mode = Mode.Objeto; s.selectedObj = i }
                        }.padding(start = if (i == -1) 8.dp else 18.dp, end = 4.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Text(if (i == -1) "Sumário" else s.name(i), color = if (i == -1) Ui.Text else Ui.TextDim, fontSize = 11.sp,
                            fontWeight = if (i == -1) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
            }
        }
        val allTracks = RIG.indices.toList() + s.objects.map { it.id }
        Canvas(Modifier.weight(1f).fillMaxHeight().pointerInput(rows) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val pad = 10.dp.toPx(); val w = size.width - pad * 2
                fun frameAt(x: Float) = (START + (x - pad) / w * (END - START)).coerceIn(START.toFloat(), END.toFloat())
                fun xOf(f: Int) = pad + (f - START) / (END - START).toFloat() * w
                s.playing = false
                val row = ((down.position.y - rulerH.toPx()) / rowH.toPx()).toInt()
                val track = if (down.position.y > rulerH.toPx()) rows.getOrNull(row) else null
                val frames = when (track) {
                    null -> emptyList()
                    -1 -> allTracks.flatMap { s.keys[it]?.keys ?: emptySet() }
                    else -> s.keys[track]?.keys?.toList() ?: emptyList()
                }
                val near = frames.minByOrNull { abs(xOf(it) - down.position.x) }?.takeIf { abs(xOf(it) - down.position.x) < 12.dp.toPx() }
                if (near != null) {
                    s.pickedKey = near
                    if (track != null && track >= 0) {
                        if (track < ARMATURE) { s.mode = Mode.Pose; s.active = track } else { s.mode = Mode.Objeto; s.selectedObj = track }
                    }
                    s.seek(near.toFloat())
                } else { s.pickedKey = null; s.seek(frameAt(down.position.x)) }
                while (true) {
                    val e = awaitPointerEvent(); val ch = e.changes.firstOrNull { it.pressed } ?: break
                    if (near == null) s.seek(frameAt(ch.position.x))
                    ch.consume()
                }
            }
        }) {
            val pad = 10.dp.toPx(); val w = size.width - pad * 2
            fun x(f: Float) = pad + (f - START) / (END - START) * w
            val rt = rulerH.toPx(); val rh = rowH.toPx()
            drawRect(Ui.Header.copy(alpha = 0.45f), Offset.Zero, Size(size.width, rt))
            for (f in 0..END step 10) {
                val ff = maxOf(f, START)
                val xx = x(ff.toFloat())
                drawLine(Ui.Grid.copy(alpha = 0.5f), Offset(xx, rt), Offset(xx, size.height), 1f)
                if (f % 20 == 0) {
                    val l = measurer.measure("$ff", TextStyle(color = Ui.TextDim, fontSize = 9.sp))
                    drawText(l, topLeft = Offset(xx - l.size.width / 2f, (rt - l.size.height) / 2))
                }
            }
            rows.forEachIndexed { r, i ->
                val top = rt + r * rh
                if (r % 2 == 1) drawRect(Ui.Text.copy(alpha = 0.025f), Offset(0f, top), Size(size.width, rh))
                val frames = if (i == -1) allTracks.flatMap { s.keys[it]?.keys ?: emptySet() }.distinct() else s.keys[i]?.keys?.toList() ?: emptyList()
                val cy = top + rh / 2
                if (i >= 0) frames.sorted().zipWithNext().forEach { (a, b) ->
                    if (s.keys[i]?.get(a) == s.keys[i]?.get(b))
                        drawRect(Ui.TextDim.copy(alpha = 0.35f), Offset(x(a.toFloat()), cy - 2.dp.toPx()), Size(x(b.toFloat()) - x(a.toFloat()), 4.dp.toPx()))
                }
                frames.forEach { f -> key(Offset(x(f.toFloat()), cy), if (i == -1) 6.dp.toPx() else 5.dp.toPx(), f == s.pickedKey) }
            }
            val px = x(s.frame)
            drawLine(Ui.Accent, Offset(px, rt), Offset(px, size.height), 2.dp.toPx())
            val label = measurer.measure("${s.intFrame}", TextStyle(color = Ui.OnAccent, fontSize = 10.sp, fontWeight = FontWeight.Bold))
            val bw = label.size.width + 10.dp.toPx()
            drawRoundRect(Ui.Accent, Offset(px - bw / 2, 2.dp.toPx()), Size(bw, rt - 4.dp.toPx()), CornerRadius(5.dp.toPx()))
            drawText(label, topLeft = Offset(px - label.size.width / 2f, (rt - label.size.height) / 2))
        }
    }
}

private fun DrawScope.key(c: Offset, r: Float, selected: Boolean) {
    val p = Path().apply { moveTo(c.x, c.y - r); lineTo(c.x + r, c.y); lineTo(c.x, c.y + r); lineTo(c.x - r, c.y); close() }
    drawPath(p, if (selected) Ui.KeySelected else Ui.Key)
    drawPath(p, Ui.Edge.copy(alpha = 0.8f), style = Stroke(1.dp.toPx()))
}

@Composable
private fun PropertiesEditor(s: MockupState) {
    val id = s.target
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        if (id < 0) { Text("Nada selecionado — toque num osso ou objeto no viewport", color = Ui.TextDim, fontSize = 12.sp); return@Column }
        val o = if (id >= ARMATURE) s.obj(id) else null
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(if (s.mode == Mode.Objeto && id == 0) "Armadura" else s.name(id), color = Ui.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            if (o != null) Text("  ${o.kind.label}", color = Ui.TextDim, fontSize = 11.sp)
            Spacer(Modifier.weight(1f))
            val keyed = s.keys[id]?.containsKey(s.intFrame) == true
            Text(if (keyed) "◆ chave no quadro ${s.intFrame}" else "◇ sem chave", color = if (keyed) Ui.KeySelected else Ui.TextDim, fontSize = 11.sp)
        }
        if (o?.kind == Kind.Texto) {
            Text("Texto", color = Ui.TextDim, fontSize = 11.sp)
            BasicTextField(o.text, { s.setText(id, it) }, singleLine = true, cursorBrush = SolidColor(Ui.Accent),
                textStyle = TextStyle(color = Ui.Text, fontSize = 14.sp),
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(Ui.Field).padding(horizontal = 12.dp, vertical = 9.dp))
        }
        if (o?.kind == Kind.Modelo) Text("Arquivo: ${o.file} · a malha é carregada pelo motor na versão real", color = Ui.TextDim, fontSize = 11.sp)
        val ch = s.channel(id)
        if (id == 0 || id >= ARMATURE) Vec3Fields("Posição", ch.loc, "m", 0.01f, 0.1f, 2) { k, v -> s.write(id, s.channel(id).copy(loc = s.channel(id).loc.with(k, v))) }
        Vec3Fields("Rotação", V3(toDeg(ch.rot.x), toDeg(ch.rot.y), toDeg(ch.rot.z)), "°", 1f, 5f, 0) { k, v ->
            s.write(id, s.channel(id).copy(rot = s.channel(id).rot.with(k, deg(v))))
        }
        if (o != null && o.id != ARMATURE) {
            Row(Modifier.clip(RoundedCornerShape(10.dp)).background(Ui.Button).tocavel { s.delete(id) }.padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.DeleteOutline, null, tint = AureaColors.Danger, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("Excluir ${o.name}", color = AureaColors.Danger, fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun Vec3Fields(label: String, v: V3, unit: String, speed: Float, step: Float, decimals: Int, onChange: (Int, Float) -> Unit) {
    Text(label, color = Ui.TextDim, fontSize = 11.sp)
    Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
        listOf("X", "Y", "Z").forEachIndexed { k, n ->
            val shape = when (k) {
                0 -> RoundedCornerShape(topStart = 10.dp, topEnd = 10.dp)
                2 -> RoundedCornerShape(bottomStart = 10.dp, bottomEnd = 10.dp)
                else -> RoundedCornerShape(0.dp)
            }
            NumberField(n, v[k], unit, width = null, step = step, speed = speed, decimals = decimals, shape = shape) { onChange(k, it) }
        }
    }
}

/** Campo numérico no jeito do Blender: arrastar na horizontal muda; ‹ › dão passos. */
@Composable
private fun NumberField(label: String, value: Float, unit: String, width: Dp?, step: Float, speed: Float, decimals: Int,
                        shape: RoundedCornerShape = RoundedCornerShape(10.dp), onChange: (Float) -> Unit) {
    var acc by remember { mutableFloatStateOf(0f) }
    val current by rememberUpdatedState(value)
    val change by rememberUpdatedState(onChange)
    val text = "%.${decimals}f".format(value) + unit
    Row((if (width != null) Modifier.width(width) else Modifier.fillMaxWidth()).height(32.dp).clip(shape).background(Ui.Field)
        .pointerInput(Unit) {
            detectHorizontalDragGestures(onDragStart = { acc = current }) { ch, dx -> ch.consume(); acc += dx * speed; change(acc) }
        }.semantics { contentDescription = "$label $text" },
        verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Rounded.ChevronLeft, null, tint = Ui.TextDim, modifier = Modifier.size(24.dp).tocavel { change(current - step) })
        if (label.isNotEmpty()) Text(label, color = Ui.TextDim, fontSize = 12.sp)
        Spacer(Modifier.weight(1f))
        Text(text, color = Ui.Text, fontSize = 12.sp)
        Spacer(Modifier.weight(1f))
        Icon(Icons.Rounded.ChevronRight, null, tint = Ui.TextDim, modifier = Modifier.size(24.dp).tocavel { change(current + step) })
    }
}

@Composable
private fun OutlinerEditor(s: MockupState) {
    fun depth(i: Int): Int = if (RIG[i].parent < 0) 0 else 1 + depth(RIG[i].parent)
    val order = ArrayList<Int>()
    fun walk(i: Int) { order += i; RIG.indices.filter { RIG[it].parent == i }.forEach { walk(it) } }
    walk(0)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(vertical = 4.dp)) {
        OutRow(0, Icons.Rounded.ViewInAr, "Cena", false) {}
        s.objects.forEach { o ->
            val icon = when (o.kind) {
                Kind.Armadura -> Icons.Rounded.Accessibility; Kind.Camera -> Icons.Rounded.Videocam; Kind.Sol -> Icons.Rounded.WbSunny
                Kind.LuzPonto -> Icons.Rounded.Lightbulb; Kind.Texto -> Icons.Rounded.TextFields; Kind.Vazio -> Icons.Rounded.CenterFocusWeak
                Kind.Modelo -> Icons.Rounded.UploadFile; else -> Icons.Rounded.ViewInAr
            }
            OutRow(1, icon, o.name, s.mode == Mode.Objeto && s.selectedObj == o.id, keyed = !s.keys[o.id].isNullOrEmpty(),
                onDelete = if (o.id == ARMATURE) null else ({ s.delete(o.id) })) {
                s.mode = Mode.Objeto; s.selectedObj = o.id
            }
            if (o.id == ARMATURE) order.forEach { i ->
                OutRow(2 + depth(i), null, RIG[i].name, s.mode == Mode.Pose && s.active == i, keyed = !s.keys[i].isNullOrEmpty()) {
                    s.mode = Mode.Pose; s.active = i
                }
            }
        }
    }
}

@Composable
private fun OutRow(depth: Int, icon: ImageVector?, name: String, on: Boolean, keyed: Boolean = false,
                   onDelete: (() -> Unit)? = null, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(30.dp).background(if (on) Ui.Accent.copy(alpha = 0.35f) else Color.Transparent)
        .tocavel(onClick = onClick).padding(start = (10 + depth * 14).dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) Icon(icon, null, tint = Ui.TextDim, modifier = Modifier.size(15.dp))
        else Canvas(Modifier.size(12.dp)) {
            val p = Path().apply { moveTo(size.width / 2, 0f); lineTo(size.width, size.height * 0.3f); lineTo(size.width / 2, size.height); lineTo(0f, size.height * 0.3f); close() }
            drawPath(p, Ui.TextDim, style = Stroke(1.2.dp.toPx()))
        }
        Spacer(Modifier.width(6.dp))
        Text(name, color = if (on) Ui.Text else Ui.TextDim, fontSize = 12.sp, modifier = Modifier.weight(1f))
        if (keyed) Text("◆", color = Ui.KeySelected, fontSize = 10.sp)
        if (onDelete != null) Box(Modifier.size(28.dp).clip(RoundedCornerShape(8.dp)).tocavel(onClick = onDelete)
            .semantics { contentDescription = "Excluir $name" }, contentAlignment = Alignment.Center) {
            Icon(Icons.Rounded.DeleteOutline, null, tint = Ui.TextDim, modifier = Modifier.size(16.dp))
        }
    }
}

// --- Botões ----------------------------------------------------------------------

@Composable
private fun ToolBtn(icon: ImageVector, label: String, on: Boolean, onClick: () -> Unit) {
    Box(Modifier.size(42.dp).clip(RoundedCornerShape(10.dp)).background(if (on) Ui.Accent else Ui.Button)
        .tocavel(onClick = onClick).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        Icon(icon, null, tint = if (on) Ui.OnAccent else Ui.Text, modifier = Modifier.size(21.dp))
    }
}

@Composable
private fun RoundGhost(icon: ImageVector, label: String, on: Boolean = false, onClick: () -> Unit) {
    Box(Modifier.size(34.dp).clip(RoundedCornerShape(50)).background(if (on) Ui.Accent else Ui.Text.copy(alpha = 0.08f))
        .tocavel(onClick = onClick).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        Icon(icon, null, tint = if (on) Ui.OnAccent else Ui.Text, modifier = Modifier.size(18.dp))
    }
}

@Composable
private fun IconBtn(icon: ImageVector, label: String, size: Dp = 36.dp, onClick: () -> Unit) {
    Box(Modifier.size(size).clip(RoundedCornerShape(10.dp)).tocavel(onClick = onClick).semantics { contentDescription = label },
        contentAlignment = Alignment.Center) {
        Icon(icon, null, tint = Ui.Text, modifier = Modifier.size(22.dp))
    }
}

@Composable
private fun MiniBtn(text: String?, label: String, icon: ImageVector? = null, onClick: () -> Unit) {
    Box(Modifier.height(18.dp).clip(RoundedCornerShape(6.dp)).background(Ui.Button)
        .tocavel(onClick = onClick).padding(horizontal = 6.dp).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        if (icon != null) Icon(icon, null, tint = Ui.Text, modifier = Modifier.size(13.dp))
        else Text(text ?: "", color = Ui.Text, fontSize = 10.sp)
    }
}
