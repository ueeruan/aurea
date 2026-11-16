package android.app

import android.content.Context
import android.content.ContextWrapper
import android.content.Intent

/** O processo do Aurea PC tem UM contexto: Application e Activity são o mesmo app. */
open class Application : ContextWrapper(null) {
    open fun onCreate() {}
    open fun onTrimMemory(level: Int) {}
    open fun onLowMemory() {}
    internal val lifecycleCallbacks = java.util.concurrent.CopyOnWriteArrayList<ActivityLifecycleCallbacks>()
    fun registerActivityLifecycleCallbacks(cb: ActivityLifecycleCallbacks) { lifecycleCallbacks += cb }
    fun unregisterActivityLifecycleCallbacks(cb: ActivityLifecycleCallbacks) { lifecycleCallbacks -= cb }

    interface ActivityLifecycleCallbacks {
        fun onActivityCreated(activity: Activity, savedInstanceState: android.os.Bundle?)
        fun onActivityStarted(activity: Activity)
        fun onActivityResumed(activity: Activity)
        fun onActivityPaused(activity: Activity)
        fun onActivityStopped(activity: Activity)
        fun onActivitySaveInstanceState(activity: Activity, outState: android.os.Bundle)
        fun onActivityDestroyed(activity: Activity)
    }
}

open class Activity : ContextWrapper(null) {
    open val intent: Intent get() = com.aurea.pc.PcApp.launchIntent
    open val isFinishing: Boolean get() = com.aurea.pc.PcApp.closing
    open val isDestroyed: Boolean get() = com.aurea.pc.PcApp.closing
    open val isChangingConfigurations: Boolean get() = false
    open val window: android.view.Window get() = android.view.Window.app
    open val windowManager: android.view.WindowManager get() = android.view.WindowManager.app
    open val application: Application get() = com.aurea.pc.PcApp.context as Application
    open val isInMultiWindowMode: Boolean get() = false
    open val isInPictureInPictureMode: Boolean get() = false
    open fun finish() = com.aurea.pc.PcApp.requestClose()
    open fun finishAffinity() = finish()
    open fun finishAndRemoveTask() = finish()
    open fun recreate() = com.aurea.pc.PcApp.recreate()
    open fun runOnUiThread(action: Runnable) = com.aurea.pc.PcApp.runOnUi { action.run() }
    open fun moveTaskToBack(nonRoot: Boolean): Boolean { com.aurea.pc.PcApp.minimize(); return true }
    open fun setRequestedOrientation(o: Int) {}
    open val requestedOrientation: Int get() = 0
    open fun startActivityForResult(intent: Intent, requestCode: Int) = startActivity(intent)
    open fun shouldShowRequestPermissionRationale(permission: String): Boolean = false
    open fun requestPermissions(permissions: Array<String>, requestCode: Int) {}
    open fun reportFullyDrawn() {}
    open val callingPackage: String? get() = null
}

/** Memória: o PC sempre está na classe "alta" (o motor decide os orçamentos pela RAM real). */
class ActivityManager private constructor() {
    class MemoryInfo {
        @JvmField var availMem: Long = 0
        @JvmField var totalMem: Long = 0
        @JvmField var threshold: Long = 0
        @JvmField var lowMemory: Boolean = false
        @JvmField var advertisedMem: Long = 0
    }

    class RunningAppProcessInfo {
        @JvmField var importance: Int = IMPORTANCE_FOREGROUND
        @JvmField var lastTrimLevel: Int = 0
        @JvmField var processName: String = "com.aurea.pc"
        @JvmField var pid: Int = android.os.Process.myPid()
        companion object {
            const val IMPORTANCE_FOREGROUND = 100
            const val IMPORTANCE_VISIBLE = 200
        }
    }

    fun getMemoryInfo(out: MemoryInfo) {
        val os = java.lang.management.ManagementFactory.getOperatingSystemMXBean() as? com.sun.management.OperatingSystemMXBean
        out.totalMem = os?.totalMemorySize ?: (16L shl 30)
        out.availMem = os?.freeMemorySize ?: (8L shl 30)
        out.advertisedMem = out.totalMem
        out.threshold = out.totalMem / 16
        out.lowMemory = out.availMem < out.threshold
    }

    val memoryClass: Int get() = (Runtime.getRuntime().maxMemory() / (1 shl 20)).toInt().coerceAtMost(1024)
    val largeMemoryClass: Int get() = memoryClass
    val isLowRamDevice: Boolean get() = false
    fun getProcessMemoryInfo(pids: IntArray): Array<android.os.Debug.MemoryInfo> = pids.map { android.os.Debug.MemoryInfo() }.toTypedArray()
    val runningAppProcesses: List<RunningAppProcessInfo> get() = listOf(RunningAppProcessInfo())
    fun getHistoricalProcessExitReasons(packageName: String?, pid: Int, maxNum: Int): List<ApplicationExitInfo> = emptyList()
    fun clearApplicationUserData(): Boolean = false
    fun setProcessStateSummary(bytes: ByteArray?) {}
    val appTasks: List<Any> get() = emptyList()

    companion object {
        val app = ActivityManager()
        @JvmStatic fun isUserAMonkey(): Boolean = false
        @JvmStatic fun getMyMemoryState(out: RunningAppProcessInfo) { out.importance = RunningAppProcessInfo.IMPORTANCE_FOREGROUND }
    }
}

class ApplicationExitInfo private constructor() {
    val reason: Int get() = REASON_UNKNOWN
    val timestamp: Long get() = 0
    val description: String? get() = null
    val pid: Int get() = 0
    val processName: String get() = ""
    val importance: Int get() = 0
    val status: Int get() = 0
    val pss: Long get() = 0
    val rss: Long get() = 0
    val traceInputStream: java.io.InputStream? get() = null
    val processStateSummary: ByteArray? get() = null
    companion object {
        const val REASON_UNKNOWN = 0
        const val REASON_EXIT_SELF = 1
        const val REASON_SIGNALED = 2
        const val REASON_LOW_MEMORY = 3
        const val REASON_CRASH = 4
        const val REASON_CRASH_NATIVE = 5
        const val REASON_ANR = 6
        const val REASON_INITIALIZATION_FAILURE = 7
        const val REASON_PERMISSION_CHANGE = 8
        const val REASON_EXCESSIVE_RESOURCE_USAGE = 9
        const val REASON_USER_REQUESTED = 10
        const val REASON_USER_STOPPED = 11
        const val REASON_DEPENDENCY_DIED = 12
        const val REASON_OTHER = 13
        const val REASON_FREEZER = 14
        const val REASON_PACKAGE_STATE_CHANGE = 15
        const val REASON_PACKAGE_UPDATED = 16
    }
}

class PendingIntent private constructor() {
    companion object {
        const val FLAG_IMMUTABLE = 0x04000000
        const val FLAG_UPDATE_CURRENT = 0x08000000
        @JvmStatic fun getActivity(context: Context, requestCode: Int, intent: Intent, flags: Int) = PendingIntent()
    }
}

open class Service : ContextWrapper(null)

object ComponentCallbacks2 {
    const val TRIM_MEMORY_UI_HIDDEN = 20
    const val TRIM_MEMORY_RUNNING_MODERATE = 5
    const val TRIM_MEMORY_RUNNING_LOW = 10
    const val TRIM_MEMORY_RUNNING_CRITICAL = 15
    const val TRIM_MEMORY_BACKGROUND = 40
    const val TRIM_MEMORY_MODERATE = 60
    const val TRIM_MEMORY_COMPLETE = 80
}
