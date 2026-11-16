// O pedaço Android do Compose que a UI do Aurea usa, sobre o Compose Desktop.
@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.platform

import android.content.Context
import android.content.res.Configuration
import androidx.compose.runtime.staticCompositionLocalOf

/** O contexto do app (um só no PC). */
val LocalContext = staticCompositionLocalOf<Context> { com.aurea.pc.PcApp.context }

/** A tela "de tablet deitado" (ver com.aurea.pc.TabletFrame): muda com a janela e o idioma. */
val LocalConfiguration = androidx.compose.runtime.compositionLocalOf<Configuration> { Configuration.current }

val LocalView = staticCompositionLocalOf<android.view.View> { android.view.View(com.aurea.pc.PcApp.context) }
