@file:Suppress("unused", "UNUSED_PARAMETER")

package android.hardware.display

/** Um monitor: o da janela do Aurea PC. */
class DisplayManager private constructor() {
    fun getDisplay(id: Int): android.view.Display? = android.view.Display.app
    val displays: Array<android.view.Display> get() = arrayOf(android.view.Display.app)
    fun registerDisplayListener(l: DisplayListener, h: android.os.Handler?) {}
    fun unregisterDisplayListener(l: DisplayListener) {}
    interface DisplayListener {
        fun onDisplayAdded(id: Int)
        fun onDisplayRemoved(id: Int)
        fun onDisplayChanged(id: Int)
    }
    companion object { val app = DisplayManager() }
}
