@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.core.content.pm

object PackageInfoCompat {
    @JvmStatic fun getLongVersionCode(info: android.content.pm.PackageInfo): Long = info.longVersionCode
}
