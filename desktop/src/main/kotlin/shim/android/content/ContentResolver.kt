package android.content

import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream

/**
 * No PC todo `Uri` é um arquivo (`file:`): o seletor de arquivos do Windows
 * devolve caminhos, não `content://`. Abrir, consultar nome/tamanho e o tipo
 * MIME saem do próprio arquivo.
 */
class ContentResolver private constructor() {

    fun openInputStream(uri: Uri): InputStream? = uri.toFile()?.takeIf { it.isFile }?.let { FileInputStream(it) }

    fun openOutputStream(uri: Uri): OutputStream? = openOutputStream(uri, "w")

    fun openOutputStream(uri: Uri, mode: String): OutputStream? =
        uri.toFile()?.let { f -> f.parentFile?.mkdirs(); FileOutputStream(f, mode.contains('a')) }

    fun openFileDescriptor(uri: Uri, mode: String): ParcelFileDescriptor? =
        uri.toFile()?.let { ParcelFileDescriptor.open(it, mode) }

    fun openAssetFileDescriptor(uri: Uri, mode: String): android.content.res.AssetFileDescriptor? =
        openFileDescriptor(uri, mode)?.let { android.content.res.AssetFileDescriptor(it) }

    fun getType(uri: Uri): String? = uri.toFile()?.let { mimeOf(it.name) }

    fun query(uri: Uri, projection: Array<String>?, selection: String?, selectionArgs: Array<String>?, sortOrder: String?): Cursor? {
        val f = uri.toFile() ?: return null
        val cols = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        val c = MatrixCursor(cols)
        if (!f.exists()) return c
        c.addRow(cols.map { col ->
            when (col) {
                OpenableColumns.DISPLAY_NAME, "_display_name", "title" -> f.name
                OpenableColumns.SIZE, "_size" -> f.length()
                "_data" -> f.absolutePath
                "mime_type" -> mimeOf(f.name)
                "date_modified", "last_modified" -> f.lastModified() / 1000
                "duration" -> null
                else -> null
            }
        }.toTypedArray())
        return c
    }

    fun query(uri: Uri, projection: Array<String>?, queryArgs: android.os.Bundle?, cancellationSignal: Any?): Cursor? =
        query(uri, projection, null, null, null)

    fun insert(uri: Uri, values: ContentValues?): Uri? {
        // MediaStore: salvar na galeria vira salvar em Vídeos/Imagens do usuário.
        val name = values?.getAsString("_display_name") ?: return null
        val rel = values.getAsString("relative_path")
        val mime = values.getAsString("mime_type") ?: ""
        val base = when {
            mime.startsWith("image") -> com.aurea.pc.PcPaths.pictures
            mime.startsWith("audio") -> com.aurea.pc.PcPaths.music
            else -> com.aurea.pc.PcPaths.videos
        }
        val dir = if (rel != null) File(base, rel.substringAfter('/').trim('/')).apply { mkdirs() } else base
        return Uri.fromFile(File(dir, name))
    }

    fun update(uri: Uri, values: ContentValues?, where: String?, selectionArgs: Array<String>?): Int = 1
    fun delete(uri: Uri, where: String?, selectionArgs: Array<String>?): Int = if (uri.toFile()?.delete() == true) 1 else 0

    fun takePersistableUriPermission(uri: Uri, modeFlags: Int) {}
    fun releasePersistableUriPermission(uri: Uri, modeFlags: Int) {}
    val persistedUriPermissions: List<UriPermission> get() = emptyList()

    fun registerContentObserver(uri: Uri, notifyForDescendants: Boolean, observer: android.database.ContentObserver) {}
    fun unregisterContentObserver(observer: android.database.ContentObserver) {}
    fun notifyChange(uri: Uri, observer: android.database.ContentObserver?) {}

    fun loadThumbnail(uri: Uri, size: android.util.Size, signal: Any?): android.graphics.Bitmap =
        com.aurea.pc.PcMedia.thumbnail(uri.toFile() ?: throw java.io.FileNotFoundException(uri.toString()), size.width, size.height)
            ?: throw java.io.IOException("sem miniatura: $uri")

    class UriPermission(val uri: Uri, val isReadPermission: Boolean, val isWritePermission: Boolean, val persistedTime: Long)

    companion object {
        val app = ContentResolver()
        const val SCHEME_CONTENT = "content"
        const val SCHEME_FILE = "file"
        const val SCHEME_ANDROID_RESOURCE = "android.resource"
        const val QUERY_ARG_SQL_SELECTION = "android:query-arg-sql-selection"

        fun mimeOf(name: String): String = when (name.substringAfterLast('.', "").lowercase()) {
            "mp4", "m4v" -> "video/mp4"
            "mov" -> "video/quicktime"
            "mkv" -> "video/x-matroska"
            "webm" -> "video/webm"
            "avi" -> "video/x-msvideo"
            "3gp" -> "video/3gpp"
            "png" -> "image/png"
            "jpg", "jpeg" -> "image/jpeg"
            "webp" -> "image/webp"
            "gif" -> "image/gif"
            "bmp" -> "image/bmp"
            "heic", "heif" -> "image/heif"
            "svg" -> "image/svg+xml"
            "psd" -> "image/vnd.adobe.photoshop"
            "mp3" -> "audio/mpeg"
            "wav" -> "audio/wav"
            "m4a", "aac" -> "audio/mp4"
            "ogg", "opus" -> "audio/ogg"
            "flac" -> "audio/flac"
            "json", "lottie" -> "application/json"
            "zip", "aureapkg" -> "application/zip"
            "ttf" -> "font/ttf"
            "otf" -> "font/otf"
            "glb" -> "model/gltf-binary"
            "gltf" -> "model/gltf+json"
            "obj" -> "model/obj"
            "fbx" -> "application/octet-stream"
            "srt" -> "application/x-subrip"
            "txt" -> "text/plain"
            else -> "application/octet-stream"
        }
    }
}

/** `file:` → arquivo. Qualquer outro esquema não existe no PC. */
fun Uri.toFile(): File? = when (scheme) {
    "file", null -> path?.let { File(it) }
    else -> null
}
