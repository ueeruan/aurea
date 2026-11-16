@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.core.content

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import java.io.File

inline fun SharedPreferences.edit(commit: Boolean = false, action: SharedPreferences.Editor.() -> Unit) {
    val e = edit()
    action(e)
    if (commit) e.commit() else e.apply()
}

object ContextCompat {
    @JvmStatic fun checkSelfPermission(context: Context, permission: String): Int = android.content.pm.PackageManager.PERMISSION_GRANTED
    @JvmStatic fun getColor(context: Context, id: Int): Int = context.resources.getColor(id)
    @JvmStatic fun startForegroundService(context: Context, intent: android.content.Intent) {}
    @JvmStatic fun getMainExecutor(context: Context): java.util.concurrent.Executor = java.util.concurrent.Executor { com.aurea.pc.PcApp.runOnUi { it.run() } }
    @JvmStatic fun getExternalFilesDirs(context: Context, type: String?): Array<File?> = arrayOf(context.getExternalFilesDir(type))
    @JvmStatic fun getNoBackupFilesDir(context: Context): File = context.noBackupFilesDir
    @JvmStatic fun <T> getSystemService(context: Context, cls: Class<T>): T? = context.getSystemService(cls)
    @JvmStatic fun registerReceiver(context: Context, receiver: Any?, filter: Any?, flags: Int): android.content.Intent? = null
    const val RECEIVER_NOT_EXPORTED = 4
    const val RECEIVER_EXPORTED = 2
}

open class FileProvider {
    companion object {
        @JvmStatic fun getUriForFile(context: Context, authority: String, file: File): Uri = Uri.fromFile(file)
        @JvmStatic fun getUriForFile(context: Context, authority: String, file: File, displayName: String): Uri = Uri.fromFile(file)
    }
}
