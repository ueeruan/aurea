@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.core.view

/** Barras do sistema: no PC a janela já não tem barras; esconder/mostrar não faz nada. */
object WindowCompat {
    @JvmStatic fun getInsetsController(window: android.view.Window, view: android.view.View): WindowInsetsControllerCompat = WindowInsetsControllerCompat()
    @JvmStatic fun setDecorFitsSystemWindows(window: android.view.Window, fits: Boolean) {}
}

class WindowInsetsControllerCompat internal constructor() {
    var systemBarsBehavior: Int = 0
    var isAppearanceLightStatusBars: Boolean = false
    var isAppearanceLightNavigationBars: Boolean = false
    fun hide(types: Int) {}
    fun show(types: Int) {}
    companion object {
        const val BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2
        const val BEHAVIOR_DEFAULT = 1
    }
}

class WindowInsetsCompat private constructor() {
    object Type {
        @JvmStatic fun systemBars(): Int = 7
        @JvmStatic fun statusBars(): Int = 1
        @JvmStatic fun navigationBars(): Int = 2
        @JvmStatic fun ime(): Int = 8
        @JvmStatic fun displayCutout(): Int = 128
    }
}
