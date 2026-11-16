@file:Suppress("unused", "UNUSED_PARAMETER")

package android.content.pm

/** O "pacote" do Aurea PC: a versão vem do mesmo build.gradle do Android. */
class PackageManager private constructor() {
    class NameNotFoundException(msg: String? = null) : Exception(msg)
    class PackageInfoFlags private constructor(val value: Long) { companion object { @JvmStatic fun of(v: Long) = PackageInfoFlags(v) } }

    fun getPackageInfo(packageName: String, flags: Int): PackageInfo =
        if (packageName == "com.aurea.pc" || packageName.startsWith("com.aurea.aurea")) PackageInfo.app else throw NameNotFoundException(packageName)
    fun getPackageInfo(packageName: String, flags: PackageInfoFlags): PackageInfo = getPackageInfo(packageName, 0)
    fun getApplicationInfo(packageName: String, flags: Int): ApplicationInfo = ApplicationInfo.app
    fun getLaunchIntentForPackage(pkg: String): android.content.Intent? = null
    fun hasSystemFeature(name: String): Boolean = name == "android.hardware.touchscreen" || name == "android.hardware.microphone"
    fun queryIntentActivities(intent: android.content.Intent, flags: Int): List<Any> = emptyList()
    fun getInstallerPackageName(pkg: String): String? = null
    fun resolveActivity(intent: android.content.Intent, flags: Int): Any? = null
    fun getApplicationLabel(info: ApplicationInfo): CharSequence = "Aurea PC"
    fun getInstallSourceInfo(pkg: String): Any? = null
    companion object {
        val app = PackageManager()
        const val PERMISSION_GRANTED = 0
        const val PERMISSION_DENIED = -1
        const val GET_META_DATA = 0x80
        const val GET_SIGNATURES = 0x40
        const val GET_SIGNING_CERTIFICATES = 0x08000000
        const val MATCH_DEFAULT_ONLY = 0x10000
        const val FEATURE_CAMERA_ANY = "android.hardware.camera.any"
        const val FEATURE_CAMERA_AR = "android.hardware.camera.ar"
        const val FEATURE_VULKAN_HARDWARE_VERSION = "android.hardware.vulkan.version"
    }
}

class PackageInfo {
    @JvmField var packageName: String = "com.aurea.pc"
    @JvmField var versionName: String? = com.aurea.aurea.BuildConfig.VERSION_NAME
    @JvmField @Deprecated("Android") var versionCode: Int = com.aurea.aurea.BuildConfig.VERSION_CODE
    @JvmField var firstInstallTime: Long = 0
    @JvmField var lastUpdateTime: Long = 0
    @JvmField var applicationInfo: ApplicationInfo? = ApplicationInfo.app
    val longVersionCode: Long get() = com.aurea.aurea.BuildConfig.VERSION_CODE.toLong()
    companion object { val app = PackageInfo() }
}

class ApplicationInfo {
    @JvmField var flags: Int = 0
    @JvmField var packageName: String = "com.aurea.pc"
    @JvmField var targetSdkVersion: Int = 36
    @JvmField var minSdkVersion: Int = 26
    @JvmField var nativeLibraryDir: String = com.aurea.pc.PcPaths.nativeDir.absolutePath
    @JvmField var dataDir: String = com.aurea.pc.PcPaths.data.absolutePath
    @JvmField var sourceDir: String = com.aurea.pc.PcPaths.appDir.absolutePath
    @JvmField var largeHeap: Boolean = true
    companion object {
        val app = ApplicationInfo()
        const val FLAG_DEBUGGABLE = 2
        const val FLAG_LARGE_HEAP = 0x100000
    }
}

class ServiceInfo private constructor() {
    companion object { const val FOREGROUND_SERVICE_TYPE_DATA_SYNC = 1; const val FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING = 0x2000 }
}
