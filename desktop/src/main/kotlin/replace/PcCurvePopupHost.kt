package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.roundToInt

/**
 * A janelinha de Curvas por cima do editor: cartão do Aurea, arrastável pelo
 * título, ✕ fecha. Some sozinha fora do editor.
 */
@Composable
fun PcCurvePopupHost(store: EditorStore) {
    if (!PcCurvePopup.open || store.screen != com.aurea.aurea.state.Screen.Editor) return
    val density = LocalDensity.current
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val w = 360f; val h = 540f
        val maxW = maxWidth.value; val maxH = maxHeight.value
        if (PcCurvePopup.x.isNaN()) { PcCurvePopup.x = (maxW - w) * 0.58f; PcCurvePopup.y = 90f }
        val shape = RoundedCornerShape(12.dp)
        Column(
            Modifier.offset { IntOffset((PcCurvePopup.x * density.density).roundToInt(), (PcCurvePopup.y * density.density).roundToInt()) }
                .size(w.dp, h.dp).shadow(18.dp, shape).clip(shape)
                .background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape)
                // O cartão inteiro é alvo do mouse: o clique no vão não atravessa para a
                // timeline (que tirava a seleção do keyframe), e marca a janelinha como
                // "quente" para o Ctrl+C / Ctrl+V da curva. Só observa: não consome.
                .pointerInput(Unit) {
                    awaitPointerEventScope {
                        while (true) {
                            val ev = awaitPointerEvent(PointerEventPass.Initial)
                            if (ev.type == PointerEventType.Press) PcCurvePopup.hot = true
                        }
                    }
                },
        ) {
            PcCurvePanel(
                store,
                header = Modifier.pointerInput(Unit) {
                    detectDragGestures { c, d ->
                        c.consume()
                        PcCurvePopup.x = (PcCurvePopup.x + d.x / density.density).coerceIn(0f, maxW - 80f)
                        PcCurvePopup.y = (PcCurvePopup.y + d.y / density.density).coerceIn(0f, maxH - 40f)
                    }
                },
                onClose = { PcCurvePopup.close() },
            )
        }
    }
}
