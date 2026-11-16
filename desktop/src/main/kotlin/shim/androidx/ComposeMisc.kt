@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.graphics

/** `Bitmap` do Android (sobre Skia) → `ImageBitmap`, sem copiar pixels. */
fun android.graphics.Bitmap.asImageBitmap(): ImageBitmap = toCompose()

/** `ImageBitmap` → `Bitmap` do Android (os mesmos pixels). */
fun ImageBitmap.asAndroidBitmap(): android.graphics.Bitmap = android.graphics.Bitmap.wrap(asSkiaBitmap())

