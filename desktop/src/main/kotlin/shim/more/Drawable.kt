@file:Suppress("unused")

package android.graphics.drawable

open class Drawable
class AnimatedImageDrawable : Drawable() {
    fun start() {}
    fun stop() {}
    val isRunning: Boolean get() = false
}
