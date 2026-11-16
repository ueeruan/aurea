@file:Suppress("unused", "UNUSED_PARAMETER")

package android.webkit

/** Sem WebView no PC: os diagnósticos que perguntam pela versão recebem `null`. */
open class WebView(context: android.content.Context) : android.view.View(context) {
    companion object {
        @JvmStatic fun getCurrentWebViewPackage(): android.content.pm.PackageInfo? = null
        @JvmStatic fun setDataDirectorySuffix(s: String) {}
    }
}

class MimeTypeMap private constructor() {
    fun getExtensionFromMimeType(mime: String?): String? = when (mime) {
        "video/mp4" -> "mp4"; "video/quicktime" -> "mov"; "video/webm" -> "webm"; "video/x-matroska" -> "mkv"
        "image/png" -> "png"; "image/jpeg" -> "jpg"; "image/webp" -> "webp"; "image/gif" -> "gif"; "image/heif", "image/heic" -> "heic"
        "audio/mpeg" -> "mp3"; "audio/wav", "audio/x-wav" -> "wav"; "audio/mp4", "audio/aac" -> "m4a"; "audio/ogg" -> "ogg"; "audio/flac" -> "flac"
        "application/json" -> "json"; "application/zip" -> "zip"; "font/ttf" -> "ttf"; "font/otf" -> "otf"
        else -> null
    }
    fun getMimeTypeFromExtension(ext: String?): String? = ext?.let { android.content.ContentResolver.mimeOf("x.$it") }?.takeIf { it != "application/octet-stream" }
    fun hasExtension(ext: String?): Boolean = getMimeTypeFromExtension(ext) != null
    fun hasMimeType(mime: String?): Boolean = getExtensionFromMimeType(mime) != null
    companion object {
        private val single = MimeTypeMap()
        @JvmStatic fun getSingleton(): MimeTypeMap = single
        @JvmStatic fun getFileExtensionFromUrl(url: String?): String = url?.substringBefore('?')?.substringAfterLast('/')?.substringAfterLast('.', "")?.lowercase() ?: ""
    }
}

object CookieManager

