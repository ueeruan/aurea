@file:Suppress("unused", "UNUSED_PARAMETER")

package android.graphics

/** GIF animado da prévia de exportação: no PC a imagem parada (primeiro quadro). */
class ImageDecoder private constructor() {
    class Source internal constructor(internal val uri: android.net.Uri)
    companion object {
        @JvmStatic fun createSource(cr: android.content.ContentResolver, uri: android.net.Uri) = Source(uri)
        @JvmStatic fun decodeDrawable(src: Source): Any = Any()
        @JvmStatic fun decodeBitmap(src: Source): Bitmap =
            android.content.ContentResolver.app.openInputStream(src.uri)?.use { BitmapFactory.decodeStream(it) } ?: throw java.io.IOException("${src.uri}")
    }
}
