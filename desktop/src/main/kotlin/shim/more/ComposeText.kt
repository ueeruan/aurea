@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.text

/** `includeFontPadding` é só do Android (o Skia não acrescenta o espaço extra). */
fun PlatformTextStyle(includeFontPadding: Boolean): PlatformTextStyle? = null
