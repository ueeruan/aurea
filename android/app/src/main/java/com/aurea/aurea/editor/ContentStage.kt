package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.hypot

// =============================================================================
// Omega conteúdo (área 06): o PALCO dos editores de conteúdo. Stage.kt chama
// estas duas portas (desenho e Editar pontos tomam o palco; no modo de texto a
// alça de LARGURA da caixa tem a vez, P1-10).
// =============================================================================

/** A alça de largura da caixa de texto na tela (x, y) e se ela existe agora. */
private object TextHandle {
    var x = 0f
    var y = 0f
    var valid = false
    var grabbed = false
}

/**
 * O overlay dos editores de conteúdo. Verdadeiro = o palco é deles (as alças da
 * camada não aparecem).
 */
internal fun DrawScope.drawOmegaContentOverlay(store: EditorStore, ui: EditorUi, m: StageMapper): Boolean {
    TextHandle.valid = false
    if (drawingActive(store, ui)) {
        drawDrawingOverlay(m)
        return true
    }
    if (editPointsActive(store, ui)) {
        drawEditPointsOverlay(store, m)
        return true
    }
    if (store.textEditLayer != null && store.textEditLayer == store.primary) drawTextBoxHandle(store, m)
    return false
}

/** A alça de largura: meio da borda lateral da caixa (direita; esquerda no texto à direita). */
private fun DrawScope.drawTextBoxHandle(store: EditorStore, m: StageMapper) {
    val d = store.detail ?: return
    if (d.locked || store.text3d != null || store.playhead < d.startFrame || store.playhead >= d.endFrame) return
    val c = FloatArray(8)
    if (!LayerGeometry.corners(d, c)) return
    val alignment = store.textDetail?.alignment ?: 0
    // Cantos: TL, TR, BR, BL. Esquerda/centro: a borda direita; direita: a esquerda.
    val (a, b) = if (alignment == 2) 0 to 3 else 1 to 2
    val x = m.sx((c[a * 2] + c[b * 2]) / 2f)
    val y = m.sy((c[a * 2 + 1] + c[b * 2 + 1]) / 2f)
    TextHandle.x = x
    TextHandle.y = y
    TextHandle.valid = true
    val r = (if (TextHandle.grabbed) 7.dp else 6.dp).toPx()
    val o = Offset(x, y)
    drawCircle(ShellColors.OutlineUnder, r + 1.5.dp.toPx(), o)
    drawCircle(Color.White, r, o)
    drawCircle(AureaColors.Accent, r, o, style = Stroke(1.5.dp.toPx()))
}

/**
 * O gesto dos editores de conteúdo. Verdadeiro = o toque foi deles (o palco
 * normal não o vê).
 */
internal suspend fun AwaitPointerEventScope.omegaContentGesture(store: EditorStore, ui: EditorUi, m: StageMapper, down: PointerInputChange): Boolean {
    if (!m.valid) return false
    if (drawingActive(store, ui)) {
        drawingGesture(store, m, down)
        return true
    }
    if (editPointsActive(store, ui)) {
        editPointsGesture(store, m, down)
        return true
    }
    if (store.textEditLayer != null && store.textEditLayer == store.primary && TextHandle.valid &&
        hypot(down.position.x - TextHandle.x, down.position.y - TextHandle.y) < 36.dp.toPx()) {
        // Arrastar a alça muda a LARGURA da caixa (a regra por alinhamento é do motor).
        TextHandle.grabbed = true
        var last = down.position
        var sent = false
        down.consume()
        try {
            while (true) {
                val e = awaitPointerEvent()
                val c = e.changes.firstOrNull { it.id == down.id } ?: break
                c.consume()
                if (!c.pressed) break
                val dx = (c.position.x - last.x) / m.fit
                val dy = (c.position.y - last.y) / m.fit
                last = c.position
                if (dx == 0f && dy == 0f) continue
                store.dragTextBoxWidth(dx, dy, continuing = sent)
                sent = true
            }
        } finally {
            TextHandle.grabbed = false
        }
        return true
    }
    return false
}
