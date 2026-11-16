// Classes do Android que a UI do Aurea toca de leve: o equivalente do PC, ou
// o comportamento neutro quando o recurso não existe num computador.
@file:Suppress("unused", "UNUSED_PARAMETER")

package android.provider

import android.content.ContentResolver
import android.net.Uri

object OpenableColumns {
    const val DISPLAY_NAME = "_display_name"
    const val SIZE = "_size"
}

object MediaStore {
    const val ACTION_IMAGE_CAPTURE = "android.media.action.IMAGE_CAPTURE"
    const val ACTION_VIDEO_CAPTURE = "android.media.action.VIDEO_CAPTURE"
    const val EXTRA_OUTPUT = "output"
    const val VOLUME_EXTERNAL_PRIMARY = "external_primary"
    const val VOLUME_EXTERNAL = "external"
    @JvmStatic fun getPickImagesMaxLimit(): Int = 100

    object MediaColumns {
        const val DISPLAY_NAME = "_display_name"
        const val MIME_TYPE = "mime_type"
        const val RELATIVE_PATH = "relative_path"
        const val IS_PENDING = "is_pending"
        const val SIZE = "_size"
        const val DATA = "_data"
        const val DATE_ADDED = "date_added"
        const val DATE_MODIFIED = "date_modified"
        const val DURATION = "duration"
        const val WIDTH = "width"
        const val HEIGHT = "height"
        const val _ID = "_id"
    }

    object Video {
        object VideoColumns { const val BUCKET_ID = "bucket_id"; const val BUCKET_DISPLAY_NAME = "bucket_display_name"; const val DATE_TAKEN = "datetaken"; const val DURATION = "duration"; const val RESOLUTION = "resolution" }
        object Thumbnails {
            const val MINI_KIND = 1; const val MICRO_KIND = 3
            @JvmStatic fun getThumbnail(cr: ContentResolver, id: Long, kind: Int, opts: android.graphics.BitmapFactory.Options?): android.graphics.Bitmap? = null
        }
        object Media {
            @JvmField val EXTERNAL_CONTENT_URI: Uri = Uri.parse("content://media/external/video/media")
            const val DISPLAY_NAME = "_display_name"; const val MIME_TYPE = "mime_type"; const val RELATIVE_PATH = "relative_path"
            const val IS_PENDING = "is_pending"; const val DURATION = "duration"; const val _ID = "_id"; const val DATE_ADDED = "date_added"
            @JvmStatic fun getContentUri(volume: String): Uri = EXTERNAL_CONTENT_URI
        }
    }

    object Images {
        object ImageColumns { const val BUCKET_ID = "bucket_id"; const val BUCKET_DISPLAY_NAME = "bucket_display_name"; const val DATE_TAKEN = "datetaken"; const val ORIENTATION = "orientation" }
        object Thumbnails { const val MINI_KIND = 1; @JvmStatic fun getThumbnail(cr: ContentResolver, id: Long, kind: Int, opts: android.graphics.BitmapFactory.Options?): android.graphics.Bitmap? = null }
        object Media {
            @JvmField val EXTERNAL_CONTENT_URI: Uri = Uri.parse("content://media/external/images/media")
            const val DISPLAY_NAME = "_display_name"; const val MIME_TYPE = "mime_type"; const val RELATIVE_PATH = "relative_path"
            const val IS_PENDING = "is_pending"; const val _ID = "_id"; const val DATE_ADDED = "date_added"
            @JvmStatic fun getBitmap(cr: ContentResolver, uri: Uri): android.graphics.Bitmap? = cr.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it) }
            @JvmStatic fun getContentUri(volume: String): Uri = EXTERNAL_CONTENT_URI
        }
    }

    object Audio {
        object Media {
            @JvmField val EXTERNAL_CONTENT_URI: Uri = Uri.parse("content://media/external/audio/media")
            const val DISPLAY_NAME = "_display_name"; const val MIME_TYPE = "mime_type"; const val RELATIVE_PATH = "relative_path"
            const val IS_PENDING = "is_pending"; const val _ID = "_id"
            @JvmStatic fun getContentUri(volume: String): Uri = EXTERNAL_CONTENT_URI
        }
    }

    object Files {
        @JvmStatic fun getContentUri(volume: String): Uri = Uri.parse("content://media/external/file")
        object FileColumns { const val MEDIA_TYPE = "media_type"; const val MEDIA_TYPE_IMAGE = 1; const val MEDIA_TYPE_VIDEO = 3 }
    }

    object Downloads {
        @JvmField val EXTERNAL_CONTENT_URI: Uri = Uri.parse("content://media/external/downloads")
        @JvmStatic fun getContentUri(volume: String): Uri = EXTERNAL_CONTENT_URI
    }
}

object DocumentsContract {
    const val EXTRA_INITIAL_URI = "android.provider.extra.INITIAL_URI"
    @JvmStatic fun isDocumentUri(context: android.content.Context?, uri: Uri?): Boolean = false
    @JvmStatic fun isTreeUri(uri: Uri?): Boolean = uri?.toString()?.let { java.io.File(Uri.parse(it).path ?: "").isDirectory } == true
    @JvmStatic fun getDocumentId(uri: Uri): String = uri.path ?: ""
    @JvmStatic fun getTreeDocumentId(uri: Uri): String = uri.path ?: ""
    @JvmStatic fun buildDocumentUriUsingTree(tree: Uri, docId: String): Uri = Uri.fromFile(java.io.File(docId))
    @JvmStatic fun buildChildDocumentsUriUsingTree(tree: Uri, docId: String): Uri = Uri.fromFile(java.io.File(docId))
    @JvmStatic fun createDocument(resolver: ContentResolver, parent: Uri, mime: String, name: String): Uri? {
        val dir = java.io.File(parent.path ?: return null)
        return Uri.fromFile(java.io.File(dir, name).also { it.parentFile?.mkdirs(); it.createNewFile() })
    }
    @JvmStatic fun deleteDocument(resolver: ContentResolver, uri: Uri): Boolean = java.io.File(uri.path ?: "").delete()

    object Document {
        const val COLUMN_DOCUMENT_ID = "document_id"
        const val COLUMN_DISPLAY_NAME = "_display_name"
        const val COLUMN_MIME_TYPE = "mime_type"
        const val COLUMN_SIZE = "_size"
        const val COLUMN_LAST_MODIFIED = "last_modified"
        const val MIME_TYPE_DIR = "vnd.android.document/directory"
    }
}

object Settings {
    const val ACTION_APPLICATION_DETAILS_SETTINGS = "android.settings.APPLICATION_DETAILS_SETTINGS"
    const val ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION = "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION"
    const val ACTION_SETTINGS = "android.settings.SETTINGS"
    const val ACTION_LOCALE_SETTINGS = "android.settings.LOCALE_SETTINGS"
    const val ACTION_APP_LOCALE_SETTINGS = "android.settings.APP_LOCALE_SETTINGS"
    const val ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS = "android.settings.IGNORE_BATTERY_OPTIMIZATION_SETTINGS"
    const val ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS = "android.settings.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS"
    const val ACTION_APP_NOTIFICATION_SETTINGS = "android.settings.APP_NOTIFICATION_SETTINGS"
    const val EXTRA_APP_PACKAGE = "android.provider.extra.APP_PACKAGE"

    object Secure {
        const val ANDROID_ID = "android_id"
        @JvmStatic fun getString(resolver: ContentResolver, name: String): String? =
            if (name == ANDROID_ID) com.aurea.pc.PcIdentity.deviceId else null
        @JvmStatic fun getInt(resolver: ContentResolver, name: String, def: Int): Int = def
        @JvmStatic fun getFloat(resolver: ContentResolver, name: String, def: Float): Float = def
    }
    object Global {
        const val ANIMATOR_DURATION_SCALE = "animator_duration_scale"
        const val TRANSITION_ANIMATION_SCALE = "transition_animation_scale"
        const val DEVICE_NAME = "device_name"
        @JvmStatic fun getFloat(resolver: ContentResolver, name: String, def: Float): Float = def
        @JvmStatic fun getInt(resolver: ContentResolver, name: String, def: Int): Int = def
        @JvmStatic fun getString(resolver: ContentResolver, name: String): String? =
            if (name == DEVICE_NAME) java.lang.System.getenv("COMPUTERNAME") else null
    }
    object System {
        const val FONT_SCALE = "font_scale"
        @JvmStatic fun getFloat(resolver: ContentResolver, name: String, def: Float): Float = def
        @JvmStatic fun getInt(resolver: ContentResolver, name: String, def: Int): Int = def
    }
    @JvmStatic fun canDrawOverlays(context: android.content.Context): Boolean = true
}
