@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.window

/**
 * `DialogProperties(decorFitsSystemWindows = ...)` do Android: no PC é o mesmo
 * diálogo; "não caber nas barras do sistema" = não usar os insets da janela.
 * (Só casa com chamadas que passam esse parâmetro.)
 */
fun DialogProperties(
    dismissOnBackPress: Boolean = true,
    dismissOnClickOutside: Boolean = true,
    usePlatformDefaultWidth: Boolean = true,
    decorFitsSystemWindows: Boolean,
): DialogProperties = DialogProperties(
    dismissOnBackPress = dismissOnBackPress,
    dismissOnClickOutside = dismissOnClickOutside,
    usePlatformDefaultWidth = usePlatformDefaultWidth,
    usePlatformInsets = decorFitsSystemWindows,
)
