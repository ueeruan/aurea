package android.graphics

import androidx.compose.ui.graphics.asSkiaPath

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

class Rect() {
    @JvmField var left = 0
    @JvmField var top = 0
    @JvmField var right = 0
    @JvmField var bottom = 0
    constructor(l: Int, t: Int, r: Int, b: Int) : this() { set(l, t, r, b) }
    constructor(o: Rect?) : this() { if (o != null) set(o.left, o.top, o.right, o.bottom) }
    fun set(l: Int, t: Int, r: Int, b: Int) { left = l; top = t; right = r; bottom = b }
    fun set(o: Rect) = set(o.left, o.top, o.right, o.bottom)
    fun width() = right - left
    fun height() = bottom - top
    fun centerX() = (left + right) shr 1
    fun centerY() = (top + bottom) shr 1
    fun exactCenterX() = (left + right) * 0.5f
    fun exactCenterY() = (top + bottom) * 0.5f
    val isEmpty: Boolean get() = left >= right || top >= bottom
    fun setEmpty() = set(0, 0, 0, 0)
    fun contains(x: Int, y: Int) = x >= left && x < right && y >= top && y < bottom
    fun offset(dx: Int, dy: Int) { left += dx; right += dx; top += dy; bottom += dy }
    fun offsetTo(x: Int, y: Int) = offset(x - left, y - top)
    fun inset(dx: Int, dy: Int) { left += dx; top += dy; right -= dx; bottom -= dy }
    fun intersect(o: Rect): Boolean {
        if (left < o.right && o.left < right && top < o.bottom && o.top < bottom) {
            left = maxOf(left, o.left); top = maxOf(top, o.top); right = minOf(right, o.right); bottom = minOf(bottom, o.bottom); return true
        }
        return false
    }
    fun union(o: Rect) { if (o.isEmpty) return; if (isEmpty) set(o) else set(minOf(left, o.left), minOf(top, o.top), maxOf(right, o.right), maxOf(bottom, o.bottom)) }
    internal fun sk() = org.jetbrains.skia.Rect(left.toFloat(), top.toFloat(), right.toFloat(), bottom.toFloat())
    override fun equals(other: Any?) = other is Rect && other.left == left && other.top == top && other.right == right && other.bottom == bottom
    override fun hashCode() = ((left * 31 + top) * 31 + right) * 31 + bottom
    override fun toString() = "Rect($left, $top - $right, $bottom)"
}

class RectF() {
    @JvmField var left = 0f
    @JvmField var top = 0f
    @JvmField var right = 0f
    @JvmField var bottom = 0f
    constructor(l: Float, t: Float, r: Float, b: Float) : this() { set(l, t, r, b) }
    constructor(o: RectF?) : this() { if (o != null) set(o.left, o.top, o.right, o.bottom) }
    constructor(o: Rect?) : this() { if (o != null) set(o.left.toFloat(), o.top.toFloat(), o.right.toFloat(), o.bottom.toFloat()) }
    fun set(l: Float, t: Float, r: Float, b: Float) { left = l; top = t; right = r; bottom = b }
    fun set(o: RectF) = set(o.left, o.top, o.right, o.bottom)
    fun set(o: Rect) = set(o.left.toFloat(), o.top.toFloat(), o.right.toFloat(), o.bottom.toFloat())
    fun width() = right - left
    fun height() = bottom - top
    fun centerX() = (left + right) * 0.5f
    fun centerY() = (top + bottom) * 0.5f
    val isEmpty: Boolean get() = !(left < right && top < bottom)
    fun setEmpty() = set(0f, 0f, 0f, 0f)
    fun contains(x: Float, y: Float) = left < right && top < bottom && x >= left && x < right && y >= top && y < bottom
    fun offset(dx: Float, dy: Float) { left += dx; right += dx; top += dy; bottom += dy }
    fun offsetTo(x: Float, y: Float) = offset(x - left, y - top)
    fun inset(dx: Float, dy: Float) { left += dx; top += dy; right -= dx; bottom -= dy }
    fun intersect(o: RectF): Boolean {
        if (left < o.right && o.left < right && top < o.bottom && o.top < bottom) {
            left = maxOf(left, o.left); top = maxOf(top, o.top); right = minOf(right, o.right); bottom = minOf(bottom, o.bottom); return true
        }
        return false
    }
    fun intersects(l: Float, t: Float, r: Float, b: Float) = left < r && l < right && top < b && t < bottom
    fun union(o: RectF) { if (o.isEmpty) return; if (isEmpty) set(o) else set(minOf(left, o.left), minOf(top, o.top), maxOf(right, o.right), maxOf(bottom, o.bottom)) }
    fun union(x: Float, y: Float) { left = minOf(left, x); top = minOf(top, y); right = maxOf(right, x); bottom = maxOf(bottom, y) }
    fun round(dst: Rect) = dst.set(left.roundToInt(), top.roundToInt(), right.roundToInt(), bottom.roundToInt())
    fun roundOut(dst: Rect) = dst.set(kotlin.math.floor(left).toInt(), kotlin.math.floor(top).toInt(), kotlin.math.ceil(right).toInt(), kotlin.math.ceil(bottom).toInt())
    fun sort() { if (left > right) { val t = left; left = right; right = t }; if (top > bottom) { val t = top; top = bottom; bottom = t } }
    internal fun sk() = org.jetbrains.skia.Rect(left, top, right, bottom)
    override fun equals(other: Any?) = other is RectF && other.left == left && other.top == top && other.right == right && other.bottom == bottom
    override fun hashCode() = arrayOf(left, top, right, bottom).contentHashCode()
    override fun toString() = "RectF($left, $top, $right, $bottom)"
}

class PointF() {
    @JvmField var x = 0f
    @JvmField var y = 0f
    constructor(x: Float, y: Float) : this() { set(x, y) }
    fun set(x: Float, y: Float) { this.x = x; this.y = y }
    fun length() = kotlin.math.hypot(x, y)
    fun offset(dx: Float, dy: Float) { x += dx; y += dy }
    fun equals(x: Float, y: Float) = this.x == x && this.y == y
    override fun equals(other: Any?) = other is PointF && other.x == x && other.y == y
    override fun hashCode() = x.hashCode() * 31 + y.hashCode()
}

class Point() {
    @JvmField var x = 0
    @JvmField var y = 0
    constructor(x: Int, y: Int) : this() { this.x = x; this.y = y }
    fun set(x: Int, y: Int) { this.x = x; this.y = y }
}

/**
 * `Path` do Android sobre o Path do Compose (que no PC é um PathBuilder do
 * Skia). O Canvas do Aurea PC desenha [sk].
 */
class Path() {
    internal val cp: androidx.compose.ui.graphics.Path = androidx.compose.ui.graphics.Path()
    internal val sk: org.jetbrains.skia.Path get() = cp.asSkiaPath()
    enum class Direction { CW, CCW }
    enum class FillType { WINDING, EVEN_ODD, INVERSE_WINDING, INVERSE_EVEN_ODD }
    enum class Op { DIFFERENCE, INTERSECT, UNION, XOR, REVERSE_DIFFERENCE }
    constructor(src: Path) : this() { cp.addPath(src.cp) }

    private fun Direction.c() = if (this == Direction.CW) androidx.compose.ui.graphics.Path.Direction.Clockwise else androidx.compose.ui.graphics.Path.Direction.CounterClockwise
    private fun RectF.c() = androidx.compose.ui.geometry.Rect(left, top, right, bottom)

    var fillType: FillType = FillType.WINDING
        set(v) { field = v; cp.fillType = if (v == FillType.EVEN_ODD || v == FillType.INVERSE_EVEN_ODD) androidx.compose.ui.graphics.PathFillType.EvenOdd else androidx.compose.ui.graphics.PathFillType.NonZero }
    val isEmpty: Boolean get() = cp.isEmpty
    fun reset() = cp.reset()
    fun rewind() = cp.rewind()
    fun moveTo(x: Float, y: Float) = cp.moveTo(x, y)
    fun lineTo(x: Float, y: Float) = cp.lineTo(x, y)
    fun rMoveTo(dx: Float, dy: Float) = cp.relativeMoveTo(dx, dy)
    fun rLineTo(dx: Float, dy: Float) = cp.relativeLineTo(dx, dy)
    fun quadTo(x1: Float, y1: Float, x2: Float, y2: Float) = cp.quadraticTo(x1, y1, x2, y2)
    fun cubicTo(x1: Float, y1: Float, x2: Float, y2: Float, x3: Float, y3: Float) = cp.cubicTo(x1, y1, x2, y2, x3, y3)
    fun arcTo(oval: RectF, start: Float, sweep: Float, forceMoveTo: Boolean) = cp.arcTo(oval.c(), start, sweep, forceMoveTo)
    fun arcTo(oval: RectF, start: Float, sweep: Float) = arcTo(oval, start, sweep, false)
    fun close() = cp.close()
    fun addRect(r: RectF, dir: Direction) = cp.addRect(r.c(), dir.c())
    fun addRect(l: Float, t: Float, r: Float, b: Float, dir: Direction) = addRect(RectF(l, t, r, b), dir)
    fun addOval(r: RectF, dir: Direction) = cp.addOval(r.c(), dir.c())
    fun addCircle(x: Float, y: Float, radius: Float, dir: Direction) = addOval(RectF(x - radius, y - radius, x + radius, y + radius), dir)
    fun addRoundRect(r: RectF, rx: Float, ry: Float, dir: Direction) =
        cp.addRoundRect(androidx.compose.ui.geometry.RoundRect(r.c(), androidx.compose.ui.geometry.CornerRadius(rx, ry)), dir.c())
    /** `radii` = 8 valores (x,y de cada canto: sup-esq, sup-dir, inf-dir, inf-esq). */
    fun addRoundRect(r: RectF, radii: FloatArray, dir: Direction) {
        val cr = { i: Int -> androidx.compose.ui.geometry.CornerRadius(radii[i * 2], radii[i * 2 + 1]) }
        cp.addRoundRect(androidx.compose.ui.geometry.RoundRect(r.left, r.top, r.right, r.bottom, cr(0), cr(1), cr(2), cr(3)), dir.c())
    }
    fun addPath(src: Path) = cp.addPath(src.cp)
    fun addPath(src: Path, dx: Float, dy: Float) = cp.addPath(src.cp, androidx.compose.ui.geometry.Offset(dx, dy))
    fun offset(dx: Float, dy: Float) = cp.translate(androidx.compose.ui.geometry.Offset(dx, dy))
    fun transform(m: Matrix) {
        val v = FloatArray(9); m.getValues(v)
        cp.transform(androidx.compose.ui.graphics.Matrix(floatArrayOf(v[0], v[3], 0f, v[6], v[1], v[4], 0f, v[7], 0f, 0f, 1f, 0f, v[2], v[5], 0f, v[8])))
    }
    fun computeBounds(bounds: RectF, exact: Boolean) { val b = cp.getBounds(); bounds.set(b.left, b.top, b.right, b.bottom) }
    fun op(path: Path, op: Op): Boolean {
        val r = androidx.compose.ui.graphics.Path()
        val ok = r.op(cp, path.cp, when (op) {
            Op.DIFFERENCE -> androidx.compose.ui.graphics.PathOperation.Difference
            Op.INTERSECT -> androidx.compose.ui.graphics.PathOperation.Intersect
            Op.UNION -> androidx.compose.ui.graphics.PathOperation.Union
            Op.XOR -> androidx.compose.ui.graphics.PathOperation.Xor
            Op.REVERSE_DIFFERENCE -> androidx.compose.ui.graphics.PathOperation.ReverseDifference
        })
        if (ok) { cp.reset(); cp.addPath(r) }
        return ok
    }
}

/** Matriz 3x3 do Android (MSCALE_X, MSKEW_X, MTRANS_X, MSKEW_Y, MSCALE_Y, MTRANS_Y, MPERSP_0..2). */
class Matrix() {
    private val v = floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f)
    constructor(src: Matrix?) : this() { if (src != null) set(src) }
    fun set(src: Matrix) { src.v.copyInto(v) }
    fun reset() { floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f).copyInto(v) }
    val isIdentity: Boolean get() = v.contentEquals(floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f))
    fun getValues(out: FloatArray) { v.copyInto(out) }
    fun setValues(src: FloatArray) { src.copyInto(v, 0, 0, 9) }

    private fun mul(a: FloatArray, b: FloatArray): FloatArray {
        val r = FloatArray(9)
        for (i in 0..2) for (j in 0..2) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]
        return r
    }
    private fun post(m: FloatArray): Boolean { mul(m, v).copyInto(v); return true }
    private fun pre(m: FloatArray): Boolean { mul(v, m).copyInto(v); return true }
    private fun t(dx: Float, dy: Float) = floatArrayOf(1f, 0f, dx, 0f, 1f, dy, 0f, 0f, 1f)
    private fun s(sx: Float, sy: Float, px: Float = 0f, py: Float = 0f) = floatArrayOf(sx, 0f, px - sx * px, 0f, sy, py - sy * py, 0f, 0f, 1f)
    private fun r(deg: Float, px: Float = 0f, py: Float = 0f): FloatArray {
        val a = Math.toRadians(deg.toDouble()); val c = cos(a).toFloat(); val sn = sin(a).toFloat()
        return floatArrayOf(c, -sn, px - c * px + sn * py, sn, c, py - sn * px - c * py, 0f, 0f, 1f)
    }

    fun setTranslate(dx: Float, dy: Float) { t(dx, dy).copyInto(v) }
    fun setScale(sx: Float, sy: Float) { s(sx, sy).copyInto(v) }
    fun setScale(sx: Float, sy: Float, px: Float, py: Float) { s(sx, sy, px, py).copyInto(v) }
    fun setRotate(deg: Float) { r(deg).copyInto(v) }
    fun setRotate(deg: Float, px: Float, py: Float) { r(deg, px, py).copyInto(v) }
    fun postTranslate(dx: Float, dy: Float) = post(t(dx, dy))
    fun postScale(sx: Float, sy: Float) = post(s(sx, sy))
    fun postScale(sx: Float, sy: Float, px: Float, py: Float) = post(s(sx, sy, px, py))
    fun postRotate(deg: Float) = post(r(deg))
    fun postRotate(deg: Float, px: Float, py: Float) = post(r(deg, px, py))
    fun postConcat(o: Matrix) = post(o.v)
    fun preTranslate(dx: Float, dy: Float) = pre(t(dx, dy))
    fun preScale(sx: Float, sy: Float) = pre(s(sx, sy))
    fun preScale(sx: Float, sy: Float, px: Float, py: Float) = pre(s(sx, sy, px, py))
    fun preRotate(deg: Float) = pre(r(deg))
    fun preRotate(deg: Float, px: Float, py: Float) = pre(r(deg, px, py))
    fun preConcat(o: Matrix) = pre(o.v)
    fun setConcat(a: Matrix, b: Matrix): Boolean { mul(a.v, b.v).copyInto(v); return true }

    fun invert(out: Matrix): Boolean {
        val a = v
        val c0 = a[4] * a[8] - a[5] * a[7]; val c1 = a[5] * a[6] - a[3] * a[8]; val c2 = a[3] * a[7] - a[4] * a[6]
        val det = a[0] * c0 + a[1] * c1 + a[2] * c2
        if (det == 0f || det.isNaN()) return false
        val d = 1f / det
        out.setValues(floatArrayOf(
            c0 * d, (a[2] * a[7] - a[1] * a[8]) * d, (a[1] * a[5] - a[2] * a[4]) * d,
            c1 * d, (a[0] * a[8] - a[2] * a[6]) * d, (a[2] * a[3] - a[0] * a[5]) * d,
            c2 * d, (a[1] * a[6] - a[0] * a[7]) * d, (a[0] * a[4] - a[1] * a[3]) * d))
        return true
    }

    fun mapPoints(pts: FloatArray) = mapPoints(pts, 0, pts, 0, pts.size / 2)
    fun mapPoints(dst: FloatArray, src: FloatArray) = mapPoints(dst, 0, src, 0, src.size / 2)
    fun mapPoints(dst: FloatArray, dstIndex: Int, src: FloatArray, srcIndex: Int, count: Int) {
        for (i in 0 until count) {
            val x = src[srcIndex + i * 2]; val y = src[srcIndex + i * 2 + 1]
            val w = v[6] * x + v[7] * y + v[8]
            dst[dstIndex + i * 2] = (v[0] * x + v[1] * y + v[2]) / w
            dst[dstIndex + i * 2 + 1] = (v[3] * x + v[4] * y + v[5]) / w
        }
    }
    fun mapRect(r: RectF): Boolean {
        val p = floatArrayOf(r.left, r.top, r.right, r.top, r.right, r.bottom, r.left, r.bottom)
        mapPoints(p)
        r.set(minOf(p[0], p[2], p[4], p[6]), minOf(p[1], p[3], p[5], p[7]), maxOf(p[0], p[2], p[4], p[6]), maxOf(p[1], p[3], p[5], p[7]))
        return true
    }
    fun mapRect(dst: RectF, src: RectF): Boolean { dst.set(src); return mapRect(dst) }

    internal fun toSkia() = org.jetbrains.skia.Matrix33(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8])

    /** createBitmap com matriz: o tamanho do resultado e a matriz que o enquadra em (0,0). */
    internal fun mapSize(w: Int, h: Int): Pair<Int, Int> {
        val r = RectF(0f, 0f, w.toFloat(), h.toFloat()); mapRect(r)
        return abs(r.width()).roundToInt().coerceAtLeast(1) to abs(r.height()).roundToInt().coerceAtLeast(1)
    }
    internal fun centeredFor(w: Int, h: Int, ow: Int, oh: Int): Matrix {
        val r = RectF(0f, 0f, w.toFloat(), h.toFloat()); mapRect(r)
        return Matrix(this).also { it.postTranslate(-r.left, -r.top) }
    }

    companion object {
        const val MSCALE_X = 0; const val MSKEW_X = 1; const val MTRANS_X = 2
        const val MSKEW_Y = 3; const val MSCALE_Y = 4; const val MTRANS_Y = 5
        const val MPERSP_0 = 6; const val MPERSP_1 = 7; const val MPERSP_2 = 8
    }
}

object Color {
    const val BLACK = -0x1000000
    const val DKGRAY = -0xbbbbbc
    const val GRAY = -0x777778
    const val LTGRAY = -0x333334
    const val WHITE = -0x1
    const val RED = -0x10000
    const val GREEN = -0xff0100
    const val BLUE = -0xffff01
    const val YELLOW = -0x100
    const val CYAN = -0xff0001
    const val MAGENTA = -0xff01
    const val TRANSPARENT = 0

    @JvmStatic fun alpha(c: Int) = c ushr 24
    @JvmStatic fun red(c: Int) = c shr 16 and 0xFF
    @JvmStatic fun green(c: Int) = c shr 8 and 0xFF
    @JvmStatic fun blue(c: Int) = c and 0xFF
    @JvmStatic fun rgb(r: Int, g: Int, b: Int) = -0x1000000 or (r shl 16) or (g shl 8) or b
    @JvmStatic fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b
    @JvmStatic fun rgb(r: Float, g: Float, b: Float) = argb(1f, r, g, b)
    @JvmStatic fun argb(a: Float, r: Float, g: Float, b: Float) =
        argb((a * 255f + 0.5f).toInt(), (r * 255f + 0.5f).toInt(), (g * 255f + 0.5f).toInt(), (b * 255f + 0.5f).toInt())
    @JvmStatic fun luminance(c: Int): Float {
        fun lin(v: Int): Double { val s = v / 255.0; return if (s <= 0.04045) s / 12.92 else Math.pow((s + 0.055) / 1.055, 2.4) }
        return (0.2126 * lin(red(c)) + 0.7152 * lin(green(c)) + 0.0722 * lin(blue(c))).toFloat()
    }

    @JvmStatic fun parseColor(s: String): Int {
        val t = s.trim()
        if (t.startsWith("#")) {
            var v = t.substring(1).toLong(16)
            when (t.length) {
                7 -> v = v or 0xFF000000L
                9 -> {}
                4 -> { val r = (v shr 8) and 0xF; val g = (v shr 4) and 0xF; val b = v and 0xF; v = 0xFF000000L or (r * 17 shl 16) or (g * 17 shl 8) or (b * 17) }
                else -> throw IllegalArgumentException("Unknown color: $s")
            }
            return v.toInt()
        }
        return when (t.lowercase()) {
            "black" -> BLACK; "white" -> WHITE; "red" -> RED; "green" -> GREEN; "blue" -> BLUE
            "yellow" -> YELLOW; "cyan" -> CYAN; "magenta" -> MAGENTA; "gray", "grey" -> GRAY
            "transparent" -> TRANSPARENT
            else -> throw IllegalArgumentException("Unknown color: $s")
        }
    }

    @JvmStatic fun colorToHSV(color: Int, hsv: FloatArray) = RGBToHSV(red(color), green(color), blue(color), hsv)

    @JvmStatic fun RGBToHSV(r: Int, g: Int, b: Int, hsv: FloatArray) {
        val rf = r / 255f; val gf = g / 255f; val bf = b / 255f
        val max = maxOf(rf, gf, bf); val min = minOf(rf, gf, bf); val d = max - min
        var h = when {
            d == 0f -> 0f
            max == rf -> 60f * (((gf - bf) / d) % 6f)
            max == gf -> 60f * (((bf - rf) / d) + 2f)
            else -> 60f * (((rf - gf) / d) + 4f)
        }
        if (h < 0f) h += 360f
        hsv[0] = h; hsv[1] = if (max == 0f) 0f else d / max; hsv[2] = max
    }

    @JvmStatic fun HSVToColor(hsv: FloatArray): Int = HSVToColor(255, hsv)

    @JvmStatic fun HSVToColor(alpha: Int, hsv: FloatArray): Int {
        val h = ((hsv[0] % 360f) + 360f) % 360f
        val s = hsv[1].coerceIn(0f, 1f); val v = hsv[2].coerceIn(0f, 1f)
        val c = v * s; val x = c * (1 - abs((h / 60f) % 2f - 1)); val m = v - c
        val (r, g, b) = when {
            h < 60 -> Triple(c, x, 0f); h < 120 -> Triple(x, c, 0f); h < 180 -> Triple(0f, c, x)
            h < 240 -> Triple(0f, x, c); h < 300 -> Triple(x, 0f, c); else -> Triple(c, 0f, x)
        }
        return argb(alpha, ((r + m) * 255f).roundToInt(), ((g + m) * 255f).roundToInt(), ((b + m) * 255f).roundToInt())
    }

    @JvmStatic fun toArgb(c: Long): Int = c.toInt()
    @JvmStatic fun pack(c: Int): Long = c.toLong() shl 32
}

class ImageFormat private constructor() { companion object { const val YUV_420_888 = 0x23; const val JPEG = 0x100; const val NV21 = 0x11 } }
class PixelFormat private constructor() { companion object { const val RGBA_8888 = 1; const val TRANSLUCENT = -3; const val OPAQUE = -1 } }
