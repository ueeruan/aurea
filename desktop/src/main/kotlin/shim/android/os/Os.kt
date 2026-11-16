package android.os

import java.io.File
import java.util.Locale

/** O "aparelho" do Aurea PC: Windows x64, nível de API do Android 15. */
object Build {
    @JvmField val MANUFACTURER: String = "PC"
    @JvmField val BRAND: String = "Windows"
    @JvmField val MODEL: String = System.getProperty("os.name") ?: "Windows"
    @JvmField val DEVICE: String = "pc"
    @JvmField val PRODUCT: String = "aurea_pc"
    @JvmField val HARDWARE: String = System.getenv("PROCESSOR_IDENTIFIER") ?: "x86_64"
    @JvmField val BOARD: String = "pc"
    @JvmField val DISPLAY: String = System.getProperty("os.version") ?: ""
    // "codecs2": a tabela de codecs do PC mudou (decoders de hardware); a sondagem guardada é refeita.
    @JvmField val FINGERPRINT: String = "windows/${System.getProperty("os.version")}/codecs2"
    @JvmField val ID: String = "PC"
    @JvmField val TAGS: String = "release-keys"
    @JvmField val TYPE: String = "user"
    @JvmField val SOC_MODEL: String = System.getenv("PROCESSOR_IDENTIFIER") ?: "x86_64"
    @JvmField val SOC_MANUFACTURER: String = "PC"
    @JvmField val SUPPORTED_ABIS: Array<String> = arrayOf("x86_64")
    @JvmField val SUPPORTED_64_BIT_ABIS: Array<String> = arrayOf("x86_64")
    @JvmField val CPU_ABI: String = "x86_64"
    const val UNKNOWN = "unknown"

    object VERSION {
        @JvmField val SDK_INT: Int = 35
        @JvmField val RELEASE: String = "Windows ${System.getProperty("os.version")}"
        @JvmField val CODENAME: String = "REL"
        @JvmField val INCREMENTAL: String = ""
        @JvmField val SECURITY_PATCH: String = ""
    }

    object VERSION_CODES {
        const val LOLLIPOP = 21; const val M = 23; const val N = 24; const val N_MR1 = 25
        const val O = 26; const val O_MR1 = 27; const val P = 28; const val Q = 29; const val R = 30
        const val S = 31; const val S_V2 = 32; const val TIRAMISU = 33; const val UPSIDE_DOWN_CAKE = 34
        const val VANILLA_ICE_CREAM = 35; const val BAKLAVA = 36
    }
}

object SystemClock {
    private val origin = System.nanoTime()
    @JvmStatic fun uptimeMillis(): Long = (System.nanoTime() - origin) / 1_000_000
    @JvmStatic fun elapsedRealtime(): Long = (System.nanoTime() - origin) / 1_000_000
    @JvmStatic fun elapsedRealtimeNanos(): Long = System.nanoTime() - origin
    @JvmStatic fun currentThreadTimeMillis(): Long =
        java.lang.management.ManagementFactory.getThreadMXBean().currentThreadCpuTime / 1_000_000
    @JvmStatic fun sleep(ms: Long) = try { Thread.sleep(ms) } catch (_: InterruptedException) {}
}

/** Um `Looper` por thread: o principal é a thread de eventos do Swing (a da UI). */
class Looper private constructor(internal val ui: Boolean, internal val queue: java.util.concurrent.LinkedBlockingQueue<Runnable>?) {
    val thread: Thread get() = if (ui) PcUi.uiThread ?: Thread.currentThread() else owner!!
    internal var owner: Thread? = null
    @Volatile internal var quit = false
    fun quit() { quit = true; queue?.offer(Runnable {}) }
    fun quitSafely() = quit()
    val isCurrentThread: Boolean get() = if (ui) PcUi.isUiThread() else Thread.currentThread() == owner

    companion object {
        private val main = Looper(true, null)
        private val local = ThreadLocal<Looper?>()
        @JvmStatic fun getMainLooper(): Looper = main
        @JvmStatic fun myLooper(): Looper? = if (PcUi.isUiThread()) main else local.get()
        @JvmStatic fun prepare() { local.set(Looper(false, java.util.concurrent.LinkedBlockingQueue()).also { it.owner = Thread.currentThread() }) }
        @JvmStatic fun loop() {
            val l = local.get() ?: return
            while (!l.quit) runCatching { l.queue!!.take().run() }
        }
    }
}

/** A ponte com a thread da UI (preenchida por com.aurea.pc). */
object PcUi {
    @Volatile var uiThread: Thread? = null
    fun isUiThread(): Boolean = java.awt.EventQueue.isDispatchThread()
}

open class Handler(private val looper: Looper) {
    constructor() : this(Looper.myLooper() ?: Looper.getMainLooper())
    constructor(looper: Looper, callback: Callback?) : this(looper)

    fun interface Callback { fun handleMessage(msg: Message): Boolean }

    private val timers = java.util.concurrent.ConcurrentHashMap<Runnable, MutableList<java.util.concurrent.ScheduledFuture<*>>>()

    fun getLooper(): Looper = looper

    fun post(r: Runnable): Boolean = postDelayed(r, 0)

    fun postDelayed(r: Runnable, delayMillis: Long): Boolean {
        if (looper.ui) {
            if (delayMillis <= 0) { java.awt.EventQueue.invokeLater(r); return true }
            val f = scheduler.schedule({ java.awt.EventQueue.invokeLater { if (timers[r]?.isNotEmpty() == true) { timers[r]?.removeFirstOrNull(); r.run() } } }, delayMillis, java.util.concurrent.TimeUnit.MILLISECONDS)
            timers.getOrPut(r) { java.util.Collections.synchronizedList(mutableListOf()) }.add(f)
            return true
        }
        val q = looper.queue ?: return false
        if (delayMillis <= 0) return q.offer(r)
        val f = scheduler.schedule({ q.offer(r) }, delayMillis, java.util.concurrent.TimeUnit.MILLISECONDS)
        timers.getOrPut(r) { java.util.Collections.synchronizedList(mutableListOf()) }.add(f)
        return true
    }

    fun postDelayed(r: Runnable, token: Any?, delayMillis: Long): Boolean = postDelayed(r, delayMillis)
    fun postAtFrontOfQueue(r: Runnable): Boolean = post(r)
    fun postAtTime(r: Runnable, uptimeMillis: Long): Boolean = postDelayed(r, uptimeMillis - SystemClock.uptimeMillis())

    fun removeCallbacks(r: Runnable) { timers.remove(r)?.forEach { it.cancel(false) } }
    fun removeCallbacksAndMessages(token: Any?) { timers.values.forEach { l -> l.forEach { it.cancel(false) } }; timers.clear() }
    fun hasCallbacks(r: Runnable): Boolean = timers[r]?.any { !it.isDone } == true

    open fun handleMessage(msg: Message) {}
    fun sendEmptyMessage(what: Int): Boolean = post { handleMessage(Message().also { it.what = what }) }
    fun sendEmptyMessageDelayed(what: Int, delay: Long): Boolean = postDelayed({ handleMessage(Message().also { it.what = what }) }, delay)
    fun obtainMessage(what: Int): Message = Message().also { it.what = what; it.target = this }
    fun sendMessage(m: Message): Boolean = post { handleMessage(m) }
    fun removeMessages(what: Int) {}

    companion object {
        internal val scheduler = java.util.concurrent.Executors.newSingleThreadScheduledExecutor { r ->
            Thread(r, "aurea-handler").apply { isDaemon = true }
        }
        @JvmStatic fun createAsync(looper: Looper): Handler = Handler(looper)
    }
}

class Message {
    @JvmField var what: Int = 0
    @JvmField var arg1: Int = 0
    @JvmField var arg2: Int = 0
    @JvmField var obj: Any? = null
    var target: Handler? = null
    fun sendToTarget() { target?.sendMessage(this) }
    companion object { @JvmStatic fun obtain(): Message = Message() }
}

open class HandlerThread(name: String, priority: Int = 0) : Thread(name) {
    @Volatile private var mLooper: Looper? = null
    private val ready = java.util.concurrent.CountDownLatch(1)
    init { isDaemon = true }
    override fun run() {
        Looper.prepare()
        mLooper = Looper.myLooper()
        ready.countDown()
        Looper.loop()
    }
    val looper: Looper get() { ready.await(); return mLooper!! }
    fun quit(): Boolean { mLooper?.quit(); return true }
    fun quitSafely(): Boolean = quit()
    val threadHandler: Handler get() = Handler(looper)
}

object Process {
    const val THREAD_PRIORITY_DEFAULT = 0
    const val THREAD_PRIORITY_BACKGROUND = 10
    const val THREAD_PRIORITY_LOWEST = 19
    const val THREAD_PRIORITY_DISPLAY = -4
    const val THREAD_PRIORITY_URGENT_DISPLAY = -8
    const val THREAD_PRIORITY_AUDIO = -16
    const val THREAD_PRIORITY_URGENT_AUDIO = -19
    const val THREAD_PRIORITY_MORE_FAVORABLE = -1
    const val THREAD_PRIORITY_LESS_FAVORABLE = 1
    @JvmStatic fun myPid(): Int = ProcessHandle.current().pid().toInt()
    @JvmStatic fun myTid(): Int = Thread.currentThread().threadId().toInt()
    @JvmStatic fun myUid(): Int = 10000
    @JvmStatic fun setThreadPriority(priority: Int) {
        val t = Thread.currentThread()
        t.priority = when {
            priority <= -8 -> Thread.MAX_PRIORITY
            priority < 0 -> Thread.NORM_PRIORITY + 2
            priority >= 10 -> Thread.MIN_PRIORITY + 1
            else -> Thread.NORM_PRIORITY
        }
    }
    @JvmStatic fun setThreadPriority(tid: Int, priority: Int) {}
    @JvmStatic fun getThreadPriority(tid: Int): Int = 0
    @JvmStatic fun killProcess(pid: Int) { kotlin.system.exitProcess(0) }
    @JvmStatic fun is64Bit(): Boolean = true
    @JvmStatic fun getElapsedCpuTime(): Long = java.lang.management.ManagementFactory.getRuntimeMXBean().uptime
}

class PowerManager private constructor() {
    val isPowerSaveMode: Boolean get() = false
    val isInteractive: Boolean get() = true
    val currentThermalStatus: Int get() = THERMAL_STATUS_NONE
    fun isIgnoringBatteryOptimizations(packageName: String): Boolean = true
    fun getThermalHeadroom(forecastSeconds: Int): Float = 0f
    fun addThermalStatusListener(listener: OnThermalStatusChangedListener) {}
    fun addThermalStatusListener(executor: java.util.concurrent.Executor, listener: OnThermalStatusChangedListener) {}
    fun removeThermalStatusListener(listener: OnThermalStatusChangedListener) {}
    fun newWakeLock(levelAndFlags: Int, tag: String): WakeLock = WakeLock()
    fun interface OnThermalStatusChangedListener { fun onThermalStatusChanged(status: Int) }
    class WakeLock internal constructor() {
        private var held = false
        fun acquire() { held = true }
        fun acquire(timeout: Long) { held = true }
        fun release() { held = false }
        val isHeld: Boolean get() = held
        fun setReferenceCounted(value: Boolean) {}
    }
    companion object {
        val app = PowerManager()
        const val PARTIAL_WAKE_LOCK = 1
        const val THERMAL_STATUS_NONE = 0
        const val THERMAL_STATUS_LIGHT = 1
        const val THERMAL_STATUS_MODERATE = 2
        const val THERMAL_STATUS_SEVERE = 3
        const val THERMAL_STATUS_CRITICAL = 4
        const val THERMAL_STATUS_EMERGENCY = 5
        const val THERMAL_STATUS_SHUTDOWN = 6
        const val ACTION_POWER_SAVE_MODE_CHANGED = "android.os.action.POWER_SAVE_MODE_CHANGED"
    }
}

/** 0.5.3: `android.os.FileUtils.copy` (GalleryPublish.copyFile); no PC, a cópia comum. */
object FileUtils {
    @JvmStatic fun copy(input: java.io.InputStream, output: java.io.OutputStream): Long = input.copyTo(output, 1 shl 20)
}

object Environment {
    const val DIRECTORY_MOVIES = "Movies"
    const val DIRECTORY_PICTURES = "Pictures"
    const val DIRECTORY_MUSIC = "Music"
    const val DIRECTORY_DCIM = "DCIM"
    const val DIRECTORY_DOWNLOADS = "Download"
    const val DIRECTORY_DOCUMENTS = "Documents"
    const val MEDIA_MOUNTED = "mounted"
    @JvmStatic fun getExternalStorageDirectory(): File = File(System.getProperty("user.home"))
    @JvmStatic fun getExternalStoragePublicDirectory(type: String): File = when (type) {
        DIRECTORY_MOVIES, DIRECTORY_DCIM -> com.aurea.pc.PcPaths.videos
        DIRECTORY_PICTURES -> com.aurea.pc.PcPaths.pictures
        DIRECTORY_MUSIC -> com.aurea.pc.PcPaths.music
        DIRECTORY_DOWNLOADS -> File(System.getProperty("user.home"), "Downloads")
        else -> File(System.getProperty("user.home"), "Documents")
    }
    @JvmStatic fun getExternalStorageState(): String = MEDIA_MOUNTED
    @JvmStatic fun getDataDirectory(): File = com.aurea.pc.PcPaths.data
    @JvmStatic fun isExternalStorageManager(): Boolean = true
}

class StatFs(path: String) {
    private val store = java.nio.file.Files.getFileStore(java.nio.file.Paths.get(path).let { p -> generateSequence(p) { it.parent }.first { java.nio.file.Files.exists(it) } })
    val availableBytes: Long get() = store.usableSpace
    val freeBytes: Long get() = store.unallocatedSpace
    val totalBytes: Long get() = store.totalSpace
    val blockSizeLong: Long get() = 4096
    val availableBlocksLong: Long get() = availableBytes / 4096
    val blockCountLong: Long get() = totalBytes / 4096
}

object Debug {
    class MemoryInfo {
        @JvmField var dalvikPss: Int = 0
        @JvmField var nativePss: Int = 0
        @JvmField var otherPss: Int = 0
        val totalPss: Int get() = (Runtime.getRuntime().totalMemory() / 1024).toInt()
        fun getMemoryStat(name: String): String? = null
    }
    @JvmStatic fun getNativeHeapAllocatedSize(): Long = 0
    @JvmStatic fun getNativeHeapSize(): Long = 0
    @JvmStatic fun getNativeHeapFreeSize(): Long = 0
    @JvmStatic fun getMemoryInfo(info: MemoryInfo) {}
    @JvmStatic fun isDebuggerConnected(): Boolean = false
    @JvmStatic fun getPss(): Long = Runtime.getRuntime().totalMemory() / 1024
}

open class Bundle() {
    private val map = LinkedHashMap<String, Any?>()
    fun putString(k: String, v: String?) { map[k] = v }
    fun putInt(k: String, v: Int) { map[k] = v }
    fun putLong(k: String, v: Long) { map[k] = v }
    fun putBoolean(k: String, v: Boolean) { map[k] = v }
    fun putFloat(k: String, v: Float) { map[k] = v }
    fun putStringArray(k: String, v: Array<String>?) { map[k] = v }
    fun getString(k: String): String? = map[k] as? String
    fun getString(k: String, def: String): String = map[k] as? String ?: def
    fun getInt(k: String, def: Int = 0): Int = map[k] as? Int ?: def
    fun getLong(k: String, def: Long = 0): Long = map[k] as? Long ?: def
    fun getBoolean(k: String, def: Boolean = false): Boolean = map[k] as? Boolean ?: def
    fun getFloat(k: String, def: Float = 0f): Float = map[k] as? Float ?: def
    fun containsKey(k: String) = k in map
    fun keySet(): Set<String> = map.keys
    fun isEmpty() = map.isEmpty()
    fun get(k: String): Any? = map[k]
}

class ParcelFileDescriptor private constructor(val file: File, private val raf: java.io.RandomAccessFile) : java.io.Closeable {
    val fileDescriptor: java.io.FileDescriptor get() = raf.fd
    val statSize: Long get() = file.length()
    /** O Android entrega um fd numérico; no PC o motor recebe o CAMINHO (ver PcMedia). */
    val fd: Int get() = -1
    fun detachFd(): Int = -1
    override fun close() = raf.close()
    companion object {
        const val MODE_READ_ONLY = 0x10000000
        const val MODE_WRITE_ONLY = 0x20000000
        const val MODE_READ_WRITE = 0x30000000
        const val MODE_CREATE = 0x08000000
        const val MODE_TRUNCATE = 0x04000000
        fun open(file: File, mode: String): ParcelFileDescriptor =
            ParcelFileDescriptor(file, java.io.RandomAccessFile(file, if (mode.contains('w')) "rw" else "r"))
        @JvmStatic fun open(file: File, mode: Int): ParcelFileDescriptor =
            open(file, if (mode and MODE_WRITE_ONLY != 0) "rw" else "r")
    }
}

interface IBinder
interface Parcelable

class LocaleList(vararg locales: Locale) {
    private val list = locales.toList().ifEmpty { listOf(Locale.getDefault()) }
    operator fun get(i: Int): Locale = list[i]
    fun size(): Int = list.size
    val isEmpty: Boolean get() = list.isEmpty()
    fun toLanguageTags(): String = list.joinToString(",") { it.toLanguageTag() }
    override fun equals(other: Any?) = other is LocaleList && other.list == list
    override fun hashCode() = list.hashCode()
    companion object {
        @JvmStatic fun getDefault(): LocaleList = LocaleList(Locale.getDefault())
        @JvmStatic fun getAdjustedDefault(): LocaleList = getDefault()
        @JvmStatic fun forLanguageTags(tags: String?): LocaleList =
            LocaleList(*(tags ?: "").split(',').filter { it.isNotBlank() }.map { Locale.forLanguageTag(it) }.toTypedArray())
    }
}

class CancellationSignal {
    @Volatile var isCanceled = false; private set
    fun cancel() { isCanceled = true }
    fun throwIfCanceled() { if (isCanceled) throw OperationCanceledException() }
}

class OperationCanceledException : RuntimeException()

class SystemProperties private constructor() {
    companion object {
        @JvmStatic fun get(key: String): String = ""
        @JvmStatic fun get(key: String, def: String): String = def
    }
}

class VibrationEffect private constructor() {
    companion object {
        const val DEFAULT_AMPLITUDE = -1
        @JvmStatic fun createOneShot(ms: Long, amplitude: Int) = VibrationEffect()
        @JvmStatic fun createPredefined(id: Int) = VibrationEffect()
        const val EFFECT_CLICK = 0
        const val EFFECT_TICK = 2
    }
}
