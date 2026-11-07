package com.aurea.aurea.editor

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.conflate
import kotlinx.coroutines.withContext

/**
 * As duas viewports da Cena 3D. A árvore é sempre a mesma: ligar ou desligar a prévia
 * só muda as medidas, senão a superfície do palco era recriada a cada troca.
 * `split` é a fração da viewport de edição.
 */
@Composable
internal fun SceneSplit(modifier: Modifier, on: Boolean, sideBySide: Boolean, split: Float, onSplit: (Float) -> Unit,
                        edit: @Composable () -> Unit, result: @Composable () -> Unit) {
    val current by rememberUpdatedState(split)
    val change by rememberUpdatedState(onSplit)
    val vertical by rememberUpdatedState(sideBySide)
    val description = stringResource(R.string.s3d_split_drag)
    // A área inteira em px: o arrasto vira fração dela.
    val area = remember { IntArray(1) }
    Layout(modifier = modifier, content = {
        Box(Modifier.fillMaxSize()) { edit() }
        Box(
            Modifier.fillMaxSize().background(AureaColors.EditorBar).testTag("scene.split")
                .semantics { contentDescription = description }
                .pointerInput(Unit) {
                    detectDragGestures { change0, drag ->
                        change0.consume()
                        change((current + (if (vertical) drag.x else drag.y) / area[0].coerceAtLeast(1)).coerceIn(.25f, .8f))
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            Box((if (sideBySide) Modifier.size(4.dp, 40.dp) else Modifier.size(40.dp, 4.dp))
                .clip(RoundedCornerShape(2.dp)).background(AureaColors.Muted))
        }
        Box(Modifier.fillMaxSize()) { if (on) result() }
    }) { measurables, constraints ->
        val w = constraints.maxWidth
        val h = constraints.maxHeight
        val bar = if (on) 14.dp.roundToPx() else 0
        val total = (if (sideBySide) w else h) - bar
        area[0] = total
        val first = if (on) (total * split).toInt() else total
        val second = (total - first).coerceAtLeast(0)
        fun fixed(along: Int) = if (sideBySide) Constraints.fixed(along, h) else Constraints.fixed(w, along)
        val a = measurables[0].measure(fixed(first))
        val b = measurables[1].measure(fixed(bar))
        val c = measurables[2].measure(fixed(second))
        layout(w, h) {
            a.place(0, 0)
            if (sideBySide) { b.place(first, 0); c.place(first + bar, 0) } else { b.place(0, first); c.place(0, first + bar) }
        }
    }
}

/**
 * A viewport de prévia: o resultado da composição pela câmera, sem grade, gizmo ou osso.
 * O palco é um só, então isto é um render à parte da mesma cena. Ele acompanha o cabeçote
 * e cada edição, no ritmo que o aparelho aguenta: tocando, atualiza mais devagar.
 */
@Composable
internal fun ScenePreviewPane(store: EditorStore, modifier: Modifier = Modifier) {
    var frame by remember { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(store) {
        snapshotFlow { store.playhead to store.sceneSettingsRevision }.conflate().collect {
            frame = withContext(Dispatchers.IO) { store.captureBitmap(640, preview = true) } ?: frame
            delay(if (store.playing) 250 else 120)
        }
    }
    Box(modifier.fillMaxSize().background(Color.Black).testTag("scene.preview")) {
        frame?.let { Image(it.asImageBitmap(), stringResource(R.string.s3d_preview), Modifier.fillMaxSize(), contentScale = ContentScale.Fit) }
    }
}
