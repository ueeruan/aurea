package android.content

import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.content.res.Resources
import android.os.Looper
import com.aurea.pc.PcPaths
import java.io.File

/**
 * O `Context` do Aurea PC. Um só para o app inteiro (Application = Activity =
 * contexto): as pastas moram em %APPDATA%\Aurea PC e %LOCALAPPDATA%\Aurea PC,
 * os textos vêm de [Resources] e os serviços do sistema são os do Windows.
 */
abstract class Context {
    abstract val applicationContext: Context
    open val packageName: String get() = "com.aurea.pc"
    open val filesDir: File get() = PcPaths.files
    open val cacheDir: File get() = PcPaths.cache
    open val noBackupFilesDir: File get() = PcPaths.noBackup
    open val codeCacheDir: File get() = File(PcPaths.cache, "code").apply { mkdirs() }
    open val dataDir: File get() = PcPaths.data
    open fun getExternalFilesDir(type: String?): File? =
        (if (type == null) PcPaths.external else File(PcPaths.external, type)).apply { mkdirs() }
    open val externalCacheDir: File? get() = PcPaths.cache
    open fun getDir(name: String, mode: Int): File = File(PcPaths.data, "app_$name").apply { mkdirs() }
    open fun getDatabasePath(name: String): File = File(File(PcPaths.data, "databases").apply { mkdirs() }, name)

    open val resources: Resources get() = Resources.app
    open val assets: AssetManager get() = AssetManager.app
    open val packageManager: PackageManager get() = PackageManager.app
    open val contentResolver: ContentResolver get() = ContentResolver.app
    open val mainLooper: Looper get() = Looper.getMainLooper()
    open val applicationInfo: ApplicationInfo get() = ApplicationInfo.app
    open val theme: Any? get() = null

    fun getString(id: Int): String = resources.getString(id)
    fun getString(id: Int, vararg args: Any?): String = resources.getString(id, *args)
    fun getText(id: Int): CharSequence = resources.getString(id)

    open fun getSharedPreferences(name: String, mode: Int): SharedPreferences = SharedPreferences.open(name)
    open fun deleteSharedPreferences(name: String): Boolean = SharedPreferences.delete(name)

    open fun getSystemService(name: String): Any? = when (name) {
        CLIPBOARD_SERVICE -> ClipboardManager.app
        ACTIVITY_SERVICE -> android.app.ActivityManager.app
        POWER_SERVICE -> android.os.PowerManager.app
        DISPLAY_SERVICE -> android.hardware.display.DisplayManager.app
        ACCESSIBILITY_SERVICE -> android.view.accessibility.AccessibilityManager.app
        else -> null
    }

    @Suppress("UNCHECKED_CAST")
    fun <T> getSystemService(cls: Class<T>): T? = when (cls) {
        ClipboardManager::class.java -> ClipboardManager.app as T
        android.app.ActivityManager::class.java -> android.app.ActivityManager.app as T
        android.os.PowerManager::class.java -> android.os.PowerManager.app as T
        android.hardware.display.DisplayManager::class.java -> android.hardware.display.DisplayManager.app as T
        else -> null
    }

    open fun startActivity(intent: Intent) = com.aurea.pc.PcIntents.start(intent)
    open fun startActivity(intent: Intent, options: android.os.Bundle?) = startActivity(intent)
    open fun startService(intent: Intent): ComponentName? = null
    open fun startForegroundService(intent: Intent): ComponentName? = null
    open fun stopService(intent: Intent): Boolean = true
    open fun sendBroadcast(intent: Intent) {}

    open fun checkSelfPermission(permission: String): Int = PackageManager.PERMISSION_GRANTED
    open fun checkCallingOrSelfPermission(permission: String): Int = PackageManager.PERMISSION_GRANTED
    open fun grantUriPermission(toPackage: String, uri: android.net.Uri, modeFlags: Int) {}
    open fun revokeUriPermission(uri: android.net.Uri, modeFlags: Int) {}

    open fun createConfigurationContext(overrideConfiguration: android.content.res.Configuration): Context =
        ConfiguredContext(this, Resources.forConfiguration(overrideConfiguration))

    open fun registerComponentCallbacks(callback: Any?) {}
    open fun unregisterComponentCallbacks(callback: Any?) {}

    open fun openFileOutput(name: String, mode: Int): java.io.FileOutputStream =
        java.io.FileOutputStream(File(filesDir, name), mode and MODE_APPEND != 0)
    open fun openFileInput(name: String): java.io.FileInputStream = java.io.FileInputStream(File(filesDir, name))
    open fun deleteFile(name: String): Boolean = File(filesDir, name).delete()
    open fun fileList(): Array<String> = filesDir.list() ?: emptyArray()

    companion object {
        const val MODE_PRIVATE = 0
        const val MODE_APPEND = 0x8000
        const val CLIPBOARD_SERVICE = "clipboard"
        const val ACTIVITY_SERVICE = "activity"
        const val POWER_SERVICE = "power"
        const val NOTIFICATION_SERVICE = "notification"
        const val VIBRATOR_SERVICE = "vibrator"
        const val VIBRATOR_MANAGER_SERVICE = "vibrator_manager"
        const val DISPLAY_SERVICE = "display"
        const val WINDOW_SERVICE = "window"
        const val SENSOR_SERVICE = "sensor"
        const val ACCESSIBILITY_SERVICE = "accessibility"
        const val CONNECTIVITY_SERVICE = "connectivity"
        const val STORAGE_SERVICE = "storage"
        const val INPUT_METHOD_SERVICE = "input_method"
        const val RECEIVER_NOT_EXPORTED = 4
        const val RECEIVER_EXPORTED = 2
        const val BIND_AUTO_CREATE = 1
    }
}

open class ContextWrapper(base: Context?) : Context() {
    private var mBase: Context? = base
    /**
     * Como no Android, a base é um contexto que NÃO é ContextWrapper (lá, o
     * ContextImpl): quem sobe a cadeia procurando a Activity sempre termina.
     */
    open val baseContext: Context get() = mBase ?: PcBaseContext
    protected open fun attachBaseContext(base: Context) { mBase = base }
    override val applicationContext: Context get() = com.aurea.pc.PcApp.context
    override val resources: Resources get() = mBase?.resources ?: Resources.app
}

/** O "ContextImpl" do Aurea PC: a base de todos os ContextWrapper. */
internal object PcBaseContext : Context() {
    override val applicationContext: Context get() = com.aurea.pc.PcApp.context
}

/** `createConfigurationContext`: o mesmo app, com os textos de outro idioma. */
private class ConfiguredContext(private val base: Context, private val res: Resources) : Context() {
    override val applicationContext: Context get() = base.applicationContext
    override val resources: Resources get() = res
}

class ComponentName(val packageName: String, val className: String) {
    constructor(context: Context, cls: Class<*>) : this(context.packageName, cls.name)
}

open class ActivityNotFoundException(message: String? = null) : RuntimeException(message)

/** Valores de `ContentResolver.insert`/`update` (só o que o app usa). */
class ContentValues {
    private val values = LinkedHashMap<String, Any?>()
    fun put(key: String, value: String?) { values[key] = value }
    fun put(key: String, value: Int?) { values[key] = value }
    fun put(key: String, value: Long?) { values[key] = value }
    fun put(key: String, value: Boolean?) { values[key] = value }
    fun get(key: String): Any? = values[key]
    fun getAsString(key: String): String? = values[key]?.toString()
    fun containsKey(key: String) = key in values
    fun clear() = values.clear()
    fun keySet(): Set<String> = values.keys
}

object ContentUris {
    fun withAppendedId(contentUri: android.net.Uri, id: Long): android.net.Uri =
        android.net.Uri.parse("$contentUri/$id")
    fun parseId(uri: android.net.Uri): Long = uri.lastPathSegment?.toLongOrNull() ?: -1L
}

interface DialogInterface {
    fun dismiss()
    fun cancel()
}
