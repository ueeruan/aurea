package com.aurea.aurea.editor

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.aurea.aurea.editor.timeline.Timeline
import com.aurea.aurea.state.EditorStore

/**
 * CONTRATO da casca do editor (Beta A.01). Provisória até o port: prévia
 * nativa (SurfaceView → Vulkan) e a timeline.
 */
@Composable
fun EditorScreen(store: EditorStore) {
    Column(Modifier.fillMaxSize().systemBarsPadding()) {
        PreviewSurface(store, Modifier.fillMaxWidth().weight(1f))
        Timeline(store, compact = false, onEmptyTap = { store.clearSelection() }, modifier = Modifier.fillMaxWidth().height(200.dp))
    }
}

/**
 * A superfície do preview. O vídeo vai do motor direto para ela (Vulkan);
 * nenhum pixel passa pelo Compose.
 */
@Composable
fun PreviewSurface(store: EditorStore, modifier: Modifier = Modifier) {
    AndroidView(
        factory = { ctx ->
            SurfaceView(ctx).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        val f = holder.surfaceFrame
                        store.attachSurface(holder.surface, f.width().coerceAtLeast(1), f.height().coerceAtLeast(1))
                    }

                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                        store.resizeSurface(width, height)
                    }

                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        store.detachSurface()
                    }
                })
            }
        },
        modifier = modifier,
    )
}
