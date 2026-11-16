@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.viewinterop

import android.content.Context
import android.view.SurfaceView
import android.view.View
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

/**
 * `AndroidView` no PC. A SurfaceView do palco vira [com.aurea.pc.EnginePreview]
 * (o motor desenha na GPU e o quadro entra no Compose); players de View (VideoView)
 * ficam como um quadro escuro.
 */
@Composable
fun <T : View> AndroidView(factory: (Context) -> T, modifier: Modifier = Modifier, update: (T) -> Unit = {}) {
    val ctx = LocalContext.current
    val view = remember { factory(ctx) }
    if (view is SurfaceView) com.aurea.pc.EnginePreview(view, modifier)
    else Box(modifier.background(Color.Black))
}

@Composable
fun <T : View> AndroidView(
    factory: (Context) -> T,
    modifier: Modifier = Modifier,
    onReset: ((T) -> Unit)? = null,
    onRelease: (T) -> Unit = {},
    update: (T) -> Unit = {},
) = AndroidView(factory, modifier, update)
