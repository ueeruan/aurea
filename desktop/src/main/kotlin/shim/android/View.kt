@file:Suppress("unused", "UNUSED_PARAMETER")

package android.view

import android.content.Context

/**
 * No PC não há `View`: o Compose desenha direto no Skia. Estas classes só
 * existem para o código compartilhado compilar; o palco do motor usa
 * [Surface] como ficha (ver com.aurea.pc.EnginePreview).
 */
open class View(val context: Context) {
    open val parent: Any? get() = null
    open var keepScreenOn: Boolean = false
    open var tag: Any? = null
    open var isHapticFeedbackEnabled: Boolean = true
    open val width: Int get() = 0
    open val height: Int get() = 0
    open val rootView: View get() = this
    open val layoutDirection: Int get() = LAYOUT_DIRECTION_LTR
    open fun performHapticFeedback(feedbackConstant: Int): Boolean = false
    open fun performHapticFeedback(feedbackConstant: Int, flags: Int): Boolean = false
    open fun post(r: Runnable): Boolean { com.aurea.pc.PcApp.runOnUi { r.run() }; return true }
    open fun postDelayed(r: Runnable, ms: Long): Boolean = android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(r, ms)
    open fun removeCallbacks(r: Runnable): Boolean = true
    open fun invalidate() {}
    open fun requestLayout() {}
    open fun setOnTouchListener(l: Any?) {}
    open fun announceForAccessibility(text: CharSequence?) {}
    open fun getWindowVisibleDisplayFrame(r: android.graphics.Rect) { r.set(0, 0, 0, 0) }
    open val rootWindowInsets: Any? get() = null
    open val display: Display? get() = Display.app
    open val windowToken: Any? get() = null
    open val isInEditMode: Boolean get() = false

    companion object {
        const val LAYOUT_DIRECTION_LTR = 0
        const val LAYOUT_DIRECTION_RTL = 1
        const val VISIBLE = 0
        const val INVISIBLE = 4
        const val GONE = 8
        const val SYSTEM_UI_FLAG_IMMERSIVE_STICKY = 0x1000
        const val SYSTEM_UI_FLAG_FULLSCREEN = 4
        const val SYSTEM_UI_FLAG_HIDE_NAVIGATION = 2
    }
}

open class ViewGroup(context: Context) : View(context)

object HapticFeedbackConstants {
    const val LONG_PRESS = 0; const val VIRTUAL_KEY = 1; const val KEYBOARD_TAP = 3; const val CLOCK_TICK = 4
    const val CONTEXT_CLICK = 6; const val KEYBOARD_PRESS = 3; const val KEYBOARD_RELEASE = 7; const val VIRTUAL_KEY_RELEASE = 8
    const val TEXT_HANDLE_MOVE = 9; const val GESTURE_START = 12; const val GESTURE_END = 13; const val CONFIRM = 16
    const val REJECT = 17; const val SEGMENT_TICK = 26; const val SEGMENT_FREQUENT_TICK = 27; const val DRAG_START = 25
    const val TOGGLE_ON = 21; const val TOGGLE_OFF = 22; const val GESTURE_THRESHOLD_ACTIVATE = 23; const val GESTURE_THRESHOLD_DEACTIVATE = 24
    const val FLAG_IGNORE_VIEW_SETTING = 1
}

/**
 * A ficha da superfície do palco. No Android é a janela do SurfaceView; no PC o
 * motor desenha num alvo próprio e entrega os quadros (ver EnginePreview).
 */
class Surface {
    val isValid: Boolean get() = true
    fun release() {}
}

interface SurfaceHolder {
    val surface: Surface
    val surfaceFrame: android.graphics.Rect
    fun addCallback(cb: Callback)
    fun removeCallback(cb: Callback)
    fun setFixedSize(w: Int, h: Int) {}
    fun setFormat(f: Int) {}
    interface Callback {
        fun surfaceCreated(holder: SurfaceHolder)
        fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int)
        fun surfaceDestroyed(holder: SurfaceHolder)
    }
}

open class SurfaceView(context: Context) : View(context) {
    /** Quem desenha é com.aurea.pc.EnginePreview: ele chama estes callbacks com o tamanho do palco. */
    internal val callbacks = java.util.concurrent.CopyOnWriteArrayList<SurfaceHolder.Callback>()
    val holder: SurfaceHolder = object : SurfaceHolder {
        override val surface = Surface()
        override val surfaceFrame = android.graphics.Rect()
        override fun addCallback(cb: SurfaceHolder.Callback) { callbacks += cb }
        override fun removeCallback(cb: SurfaceHolder.Callback) { callbacks -= cb }
    }
    fun setZOrderOnTop(onTop: Boolean) {}
    fun setZOrderMediaOverlay(v: Boolean) {}
}

/** Batidas de quadro: o relógio de 60 Hz da UI. */
class Choreographer private constructor() {
    fun interface FrameCallback { fun doFrame(frameTimeNanos: Long) }
    private val pending = java.util.concurrent.ConcurrentHashMap.newKeySet<FrameCallback>()
    fun postFrameCallback(cb: FrameCallback) {
        pending += cb
        android.os.Handler.scheduler.schedule({
            java.awt.EventQueue.invokeLater { if (pending.remove(cb)) cb.doFrame(System.nanoTime()) }
        }, 16_666, java.util.concurrent.TimeUnit.MICROSECONDS)
    }
    fun postFrameCallbackDelayed(cb: FrameCallback, delayMs: Long) {
        pending += cb
        android.os.Handler.scheduler.schedule({
            java.awt.EventQueue.invokeLater { if (pending.remove(cb)) cb.doFrame(System.nanoTime()) }
        }, delayMs, java.util.concurrent.TimeUnit.MILLISECONDS)
    }
    fun removeFrameCallback(cb: FrameCallback) { pending -= cb }
    companion object {
        private val instance = Choreographer()
        @JvmStatic fun getInstance(): Choreographer = instance
    }
}

class ViewConfiguration private constructor() {
    val scaledTouchSlop: Int get() = 8
    val scaledDoubleTapSlop: Int get() = 100
    val scaledMinimumFlingVelocity: Int get() = 50
    val scaledMaximumFlingVelocity: Int get() = 8000
    companion object {
        @JvmStatic fun get(context: Context): ViewConfiguration = ViewConfiguration()
        @JvmStatic fun getLongPressTimeout(): Int = 400
        @JvmStatic fun getDoubleTapTimeout(): Int = 300
        @JvmStatic fun getTapTimeout(): Int = 100
    }
}

class Display private constructor() {
    val refreshRate: Float get() = com.aurea.pc.PcApp.refreshRate
    val supportedRefreshRates: FloatArray get() = floatArrayOf(refreshRate)
    val displayId: Int get() = 0
    val rotation: Int get() = 1
    val mode: Mode get() = Mode()
    val supportedModes: Array<Mode> get() = arrayOf(Mode())
    val hdrCapabilities: Any? get() = null
    val isHdr: Boolean get() = false
    fun getRealMetrics(m: android.util.DisplayMetrics) { m.widthPixels = android.util.DisplayMetrics.current.widthPixels; m.heightPixels = android.util.DisplayMetrics.current.heightPixels }
    inner class Mode { val refreshRate: Float get() = this@Display.refreshRate; val physicalWidth = 1920; val physicalHeight = 1080; val modeId = 0 }
    companion object { val app = Display(); const val DEFAULT_DISPLAY = 0 }
}

class Window private constructor() {
    val attributes: WindowManager.LayoutParams = WindowManager.LayoutParams()
    val decorView: View get() = View(com.aurea.pc.PcApp.context)
    fun addFlags(flags: Int) {}
    fun clearFlags(flags: Int) {}
    fun setFlags(flags: Int, mask: Int) {}
    fun setLayout(w: Int, h: Int) {}
    fun setDimAmount(v: Float) {}
    fun setBackgroundDrawableResource(id: Int) {}
    fun setWindowAnimations(id: Int) {}
    fun setSoftInputMode(mode: Int) {}
    fun setGravity(g: Int) {}
    fun setDecorFitsSystemWindows(v: Boolean) {}
    var statusBarColor: Int = 0
    var navigationBarColor: Int = 0
    var isNavigationBarContrastEnforced: Boolean = false
    var colorMode: Int = 0
    companion object { val app = Window() }
}

class WindowManager private constructor() {
    val defaultDisplay: Display get() = Display.app
    val currentWindowMetrics: Any? get() = null
    class LayoutParams {
        @JvmField var preferredRefreshRate: Float = 0f
        @JvmField var preferredDisplayModeId: Int = 0
        @JvmField var screenBrightness: Float = -1f
        @JvmField var layoutInDisplayCutoutMode: Int = 0
        @JvmField var dimAmount: Float = 0f
        @JvmField var width: Int = -1
        @JvmField var height: Int = -1
        @JvmField var flags: Int = 0
        companion object {
            const val FLAG_KEEP_SCREEN_ON = 0x80
            const val FLAG_SECURE = 0x2000
            const val FLAG_DIM_BEHIND = 2
            const val FLAG_FULLSCREEN = 0x400
            const val FLAG_LAYOUT_NO_LIMITS = 0x200
            const val MATCH_PARENT = -1
            const val WRAP_CONTENT = -2
            const val LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES = 1
            const val SOFT_INPUT_ADJUST_RESIZE = 0x10
            const val SOFT_INPUT_ADJUST_NOTHING = 0x30
        }
    }
    companion object { val app = WindowManager() }
}

object Gravity {
    const val CENTER = 17; const val TOP = 48; const val BOTTOM = 80; const val START = 0x800003; const val END = 0x800005
    const val LEFT = 3; const val RIGHT = 5; const val CENTER_HORIZONTAL = 1; const val CENTER_VERTICAL = 16
}

class KeyEvent private constructor() {
    companion object {
        const val KEYCODE_BACK = 4; const val KEYCODE_SPACE = 62; const val KEYCODE_DEL = 67; const val KEYCODE_ENTER = 66
        const val ACTION_DOWN = 0; const val ACTION_UP = 1
    }
}

class MotionEvent private constructor() {
    companion object { const val ACTION_DOWN = 0; const val ACTION_UP = 1; const val ACTION_MOVE = 2; const val ACTION_CANCEL = 3 }
}

class PixelCopy private constructor()

object WindowInsets {
    object Type {
        @JvmStatic fun systemBars() = 7
        @JvmStatic fun ime() = 8
        @JvmStatic fun navigationBars() = 2
        @JvmStatic fun statusBars() = 1
    }
}
