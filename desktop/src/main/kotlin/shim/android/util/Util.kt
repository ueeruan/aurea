package android.util

/** Log do Android → stderr (e o arquivo de log do Aurea PC). */
object Log {
    const val VERBOSE = 2; const val DEBUG = 3; const val INFO = 4; const val WARN = 5; const val ERROR = 6; const val ASSERT = 7
    private fun out(level: Char, tag: String?, msg: String?, tr: Throwable? = null): Int {
        com.aurea.pc.PcLog.write(level, tag ?: "", msg ?: "", tr)
        return 0
    }
    @JvmStatic fun v(tag: String?, msg: String): Int = out('V', tag, msg)
    @JvmStatic fun v(tag: String?, msg: String?, tr: Throwable?): Int = out('V', tag, msg, tr)
    @JvmStatic fun d(tag: String?, msg: String): Int = out('D', tag, msg)
    @JvmStatic fun d(tag: String?, msg: String?, tr: Throwable?): Int = out('D', tag, msg, tr)
    @JvmStatic fun i(tag: String?, msg: String): Int = out('I', tag, msg)
    @JvmStatic fun i(tag: String?, msg: String?, tr: Throwable?): Int = out('I', tag, msg, tr)
    @JvmStatic fun w(tag: String?, msg: String): Int = out('W', tag, msg)
    @JvmStatic fun w(tag: String?, msg: String?, tr: Throwable?): Int = out('W', tag, msg, tr)
    @JvmStatic fun w(tag: String?, tr: Throwable?): Int = out('W', tag, null, tr)
    @JvmStatic fun e(tag: String?, msg: String): Int = out('E', tag, msg)
    @JvmStatic fun e(tag: String?, msg: String?, tr: Throwable?): Int = out('E', tag, msg, tr)
    @JvmStatic fun wtf(tag: String?, msg: String?): Int = out('A', tag, msg)
    @JvmStatic fun wtf(tag: String?, msg: String?, tr: Throwable?): Int = out('A', tag, msg, tr)
    @JvmStatic fun isLoggable(tag: String?, level: Int): Boolean = level >= INFO
    @JvmStatic fun getStackTraceString(tr: Throwable?): String = tr?.stackTraceToString() ?: ""
    @JvmStatic fun println(priority: Int, tag: String?, msg: String): Int = out('I', tag, msg)
}

open class LruCache<K : Any, V : Any>(private var maxSize: Int) {
    private val map = LinkedHashMap<K, V>(0, 0.75f, true)
    private var size = 0
    private var hits = 0
    private var misses = 0

    protected open fun sizeOf(key: K, value: V): Int = 1
    protected open fun create(key: K): V? = null
    protected open fun entryRemoved(evicted: Boolean, key: K, oldValue: V, newValue: V?) {}

    @Synchronized fun get(key: K): V? {
        map[key]?.let { hits++; return it }
        misses++
        val created = create(key) ?: return null
        put(key, created)
        return created
    }

    fun put(key: K, value: V): V? {
        val previous: V?
        synchronized(this) {
            size += safeSize(key, value)
            previous = map.put(key, value)
            if (previous != null) size -= safeSize(key, previous)
        }
        if (previous != null) entryRemoved(false, key, previous, value)
        trimToSize(maxSize)
        return previous
    }

    fun remove(key: K): V? {
        val previous = synchronized(this) { map.remove(key)?.also { size -= safeSize(key, it) } }
        if (previous != null) entryRemoved(false, key, previous, null)
        return previous
    }

    open fun trimToSize(maxSize: Int) {
        while (true) {
            val evicted: kotlin.Pair<K, V> = synchronized(this) {
                if (size <= maxSize || map.isEmpty()) return
                val e = map.entries.iterator().next()
                map.remove(e.key)
                size -= safeSize(e.key, e.value)
                e.key to e.value
            }
            entryRemoved(true, evicted.first, evicted.second, null)
        }
    }

    fun resize(newMax: Int) { synchronized(this) { maxSize = newMax }; trimToSize(newMax) }
    fun evictAll() = trimToSize(-1)
    @Synchronized fun size(): Int = size
    @Synchronized fun maxSize(): Int = maxSize
    @Synchronized fun hitCount(): Int = hits
    @Synchronized fun missCount(): Int = misses
    @Synchronized fun snapshot(): Map<K, V> = LinkedHashMap(map)

    private fun safeSize(k: K, v: V): Int = sizeOf(k, v).also { check(it >= 0) { "tamanho negativo: $k=$v" } }
}

data class Size(val width: Int, val height: Int) {
    override fun toString() = "${width}x$height"
    companion object { @JvmStatic fun parseSize(s: String): Size = s.split('x', '*').let { Size(it[0].trim().toInt(), it[1].trim().toInt()) } }
}

data class SizeF(val width: Float, val height: Float)

open class SparseArray<E>(initialCapacity: Int = 10) {
    private val map = java.util.TreeMap<Int, E>()
    fun get(key: Int): E? = map[key]
    fun get(key: Int, def: E): E = map[key] ?: def
    fun put(key: Int, value: E) { map[key] = value }
    operator fun set(key: Int, value: E) = put(key, value)
    fun remove(key: Int) { map.remove(key) }
    fun delete(key: Int) { map.remove(key) }
    fun size(): Int = map.size
    fun keyAt(i: Int): Int = map.keys.elementAt(i)
    fun valueAt(i: Int): E = map.values.elementAt(i)
    fun indexOfKey(key: Int): Int = map.keys.indexOf(key)
    fun clear() = map.clear()
    fun contains(key: Int) = key in map
}

open class LongSparseArray<E>(initialCapacity: Int = 10) {
    private val map = java.util.TreeMap<Long, E>()
    fun get(key: Long): E? = map[key]
    fun get(key: Long, def: E): E = map[key] ?: def
    fun put(key: Long, value: E) { map[key] = value }
    operator fun set(key: Long, value: E) = put(key, value)
    fun remove(key: Long) { map.remove(key) }
    fun delete(key: Long) { map.remove(key) }
    fun size(): Int = map.size
    fun keyAt(i: Int): Long = map.keys.elementAt(i)
    fun valueAt(i: Int): E = map.values.elementAt(i)
    fun indexOfKey(key: Long): Int = map.keys.indexOf(key)
    fun clear() = map.clear()
}

class SparseIntArray {
    private val map = java.util.TreeMap<Int, Int>()
    fun get(key: Int, def: Int = 0) = map[key] ?: def
    fun put(key: Int, value: Int) { map[key] = value }
    fun size() = map.size
    fun clear() = map.clear()
}

object Base64 {
    const val DEFAULT = 0; const val NO_PADDING = 1; const val NO_WRAP = 2; const val CRLF = 4; const val URL_SAFE = 8; const val NO_CLOSE = 16
    @JvmStatic fun encodeToString(input: ByteArray, flags: Int): String = String(encode(input, flags), Charsets.US_ASCII)
    @JvmStatic fun encodeToString(input: ByteArray, offset: Int, len: Int, flags: Int): String = encodeToString(input.copyOfRange(offset, offset + len), flags)
    @JvmStatic fun encode(input: ByteArray, flags: Int): ByteArray {
        var enc = if (flags and URL_SAFE != 0) java.util.Base64.getUrlEncoder() else java.util.Base64.getEncoder()
        if (flags and NO_PADDING != 0) enc = enc.withoutPadding()
        val out = enc.encode(input)
        if (flags and NO_WRAP != 0) return out
        // O Android quebra a cada 76 colunas e termina com \n.
        val s = String(out, Charsets.US_ASCII).chunked(76).joinToString("\n", postfix = "\n")
        return s.toByteArray(Charsets.US_ASCII)
    }
    @JvmStatic fun decode(str: String, flags: Int): ByteArray = decode(str.toByteArray(Charsets.US_ASCII), flags)
    @JvmStatic fun decode(input: ByteArray, flags: Int): ByteArray {
        val clean = String(input, Charsets.US_ASCII).filterNot { it.isWhitespace() }
        return if (flags and URL_SAFE != 0 || clean.contains('-') || clean.contains('_')) java.util.Base64.getUrlDecoder().decode(clean)
        else java.util.Base64.getDecoder().decode(clean)
    }
}

/** Gravação atômica: escreve em `.new` e troca no fim. */
class AtomicFile(val baseFile: java.io.File) {
    private val newFile = java.io.File(baseFile.path + ".new")
    fun startWrite(): java.io.FileOutputStream { baseFile.parentFile?.mkdirs(); return java.io.FileOutputStream(newFile) }
    fun finishWrite(str: java.io.FileOutputStream?) {
        if (str == null) return
        str.fd.sync(); str.close()
        java.nio.file.Files.move(newFile.toPath(), baseFile.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING, java.nio.file.StandardCopyOption.ATOMIC_MOVE)
    }
    fun failWrite(str: java.io.FileOutputStream?) { runCatching { str?.close() }; newFile.delete() }
    fun openRead(): java.io.FileInputStream = java.io.FileInputStream(baseFile)
    fun readFully(): ByteArray = baseFile.readBytes()
    fun delete() { baseFile.delete(); newFile.delete() }
    fun exists() = baseFile.exists()
}

/** Densidade da janela: o Aurea PC mede em dp como o tablet (ver com.aurea.pc.TabletFrame). */
class DisplayMetrics {
    @JvmField var density: Float = 1f
    @JvmField var scaledDensity: Float = 1f
    @JvmField var densityDpi: Int = 160
    @JvmField var widthPixels: Int = 1366
    @JvmField var heightPixels: Int = 1024
    @JvmField var xdpi: Float = 160f
    @JvmField var ydpi: Float = 160f
    companion object {
        const val DENSITY_DEFAULT = 160
        @Volatile var current: DisplayMetrics = DisplayMetrics()
    }
}

class TypedValue {
    companion object {
        const val COMPLEX_UNIT_DIP = 1
        const val COMPLEX_UNIT_SP = 2
        const val COMPLEX_UNIT_PX = 0
        @JvmStatic fun applyDimension(unit: Int, value: Float, metrics: DisplayMetrics): Float = when (unit) {
            COMPLEX_UNIT_PX -> value
            COMPLEX_UNIT_SP -> value * metrics.scaledDensity
            else -> value * metrics.density
        }
    }
}

class Range<T : Comparable<T>>(val lower: T, val upper: T) {
    fun contains(v: T) = v >= lower && v <= upper
    fun clamp(v: T): T = if (v < lower) lower else if (v > upper) upper else v
    companion object { @JvmStatic fun <T : Comparable<T>> create(lower: T, upper: T) = Range(lower, upper) }
}

data class Pair<F, S>(@JvmField val first: F, @JvmField val second: S) {
    companion object { @JvmStatic fun <A, B> create(a: A, b: B) = Pair(a, b) }
}
