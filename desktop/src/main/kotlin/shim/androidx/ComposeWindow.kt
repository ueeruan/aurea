@file:Suppress("unused")

package androidx.compose.ui.window

/** Só existe no Android; no PC nenhum `View` é um provedor de janela de diálogo. */
interface DialogWindowProvider {
    val window: android.view.Window
}
