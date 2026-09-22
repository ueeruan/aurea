package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

private val TransitionTypes = listOf(0 to "Nenhuma", 1 to "Dissolver", 2 to "Deslizar ↑", 3 to "Deslizar →", 4 to "Zoom", 5 to "Girar")
private val TransitionSeconds = listOf(0.3f, 0.5f, 1f, 2f)

/**
 * ENTRADA E SAÍDA: como a camada aparece e some. Calculado no render perto das
 * bordas do clipe (não vira keyframe): trocar ou tirar não mexe na animação.
 */
@Composable
internal fun TransitionsPanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        TransitionRow(store, "Entrada", out = false, type = detail.transitionIn, frames = detail.transitionInFrames, fps = fps)
        Spacer(Modifier.height(14.dp))
        TransitionRow(store, "Saída", out = true, type = detail.transitionOut, frames = detail.transitionOutFrames, fps = fps)
    }
}

@Composable
private fun TransitionRow(store: EditorStore, title: String, out: Boolean, type: Int, frames: Int, fps: Float) {
    Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        TransitionTypes.forEach { (t, label) ->
            Chip(label, on = t == type) {
                val f = if (frames > 0) frames else (0.5f * fps).roundToInt()
                store.setTransition(out, t, f)
            }
        }
    }
    if (type != 0) {
        Spacer(Modifier.height(6.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            TransitionSeconds.forEach { s ->
                val f = (s * fps).roundToInt()
                Chip("${"%.1f".format(s).replace('.', ',')} s", on = kotlin.math.abs(frames - f) <= 1) { store.setTransition(out, type, f) }
            }
        }
    }
}

@Composable
private fun Chip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 12.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
