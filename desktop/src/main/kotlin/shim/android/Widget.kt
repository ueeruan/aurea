@file:Suppress("unused", "UNUSED_PARAMETER")

package android.widget

import android.content.Context

/** Toast → o aviso flutuante da janela do Aurea PC (com.aurea.pc.PcToasts). */
class Toast private constructor(private val text: CharSequence, private val duration: Int) {
    fun show() = com.aurea.pc.PcToasts.show(text.toString(), duration == LENGTH_LONG)
    fun cancel() {}
    fun setText(s: CharSequence) {}
    fun setGravity(g: Int, x: Int, y: Int) {}
    companion object {
        const val LENGTH_SHORT = 0
        const val LENGTH_LONG = 1
        @JvmStatic fun makeText(context: Context?, text: CharSequence, duration: Int): Toast = Toast(text, duration)
        @JvmStatic fun makeText(context: Context, resId: Int, duration: Int): Toast = Toast(context.getString(resId), duration)
    }
}

/** Sem players de View no PC: as prévias que usam VideoView ficam com a imagem parada. */
open class VideoView(context: Context) : android.view.View(context) {
    private var uri: android.net.Uri? = null
    fun setVideoURI(u: android.net.Uri?) { uri = u; com.aurea.pc.PcMedia.openExternally(u) }
    fun setVideoPath(p: String?) = setVideoURI(p?.let { android.net.Uri.fromFile(java.io.File(it)) })
    fun start() {}
    fun pause() {}
    fun stopPlayback() {}
    fun seekTo(ms: Int) {}
    fun resume() {}
    val isPlaying: Boolean get() = false
    val duration: Int get() = 0
    val currentPosition: Int get() = 0
    fun setOnPreparedListener(l: android.media.MediaPlayer.OnPreparedListener?) {}
    fun setOnCompletionListener(l: android.media.MediaPlayer.OnCompletionListener?) {}
    fun setOnErrorListener(l: android.media.MediaPlayer.OnErrorListener?) {}
    fun setMediaController(c: Any?) {}
}

open class ImageView(context: Context) : android.view.View(context) {
    fun setImageBitmap(b: android.graphics.Bitmap?) {}
    fun setImageDrawable(d: Any?) {}
    fun setImageURI(u: android.net.Uri?) {}
    var scaleType: ScaleType = ScaleType.FIT_CENTER
    enum class ScaleType { CENTER, CENTER_CROP, CENTER_INSIDE, FIT_CENTER, FIT_XY, MATRIX }
}

open class FrameLayout(context: Context) : android.view.ViewGroup(context)
