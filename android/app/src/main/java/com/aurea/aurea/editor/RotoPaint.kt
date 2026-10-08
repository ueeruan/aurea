package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay

// =============================================================================
//  Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects).
//  "Pintar recorte" no cartão liga o modo: o palco recebe traços de Objeto
//  (verde) ou Fundo (vermelho); o motor (EngineRoto.cpp) leva os pontos da
//  composição para a camada, guarda no efeito e refaz os recortes.
// =============================================================================

data class RotoSession(val layer: Long, val effectId: Int, val previousView: Int)

object RotoPaint {
    var session by mutableStateOf<RotoSession?>(null)
    var background by mutableStateOf(false)
    /** Raio do pincel em px da composição. */
    var radius by mutableFloatStateOf(24f)
    /** Traço em curso, em px da tela (desenho ao vivo). */
    var live by mutableStateOf<List<Offset>>(emptyList())
}

fun rotoPaintActive(store: EditorStore): Boolean = RotoPaint.session?.let { it.layer == store.primary } == true

private fun rotoEnd(store: EditorStore) {
    val s = RotoPaint.session ?: return
    store.engineForStress.rotoSetView(s.layer, s.effectId, s.previousView)
    RotoPaint.session = null
    RotoPaint.live = emptyList()
    store.rotoChanged()
}

/** Um dedo pinta um traço; soltar manda o traço inteiro ao motor (um passo de desfazer). */
internal suspend fun AwaitPointerEventScope.rotoGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val s = RotoPaint.session ?: return
    if (!m.valid) return
    val xy = ArrayList<Float>()
    val screen = ArrayList<Offset>()
    fun add(p: Offset) {
        xy += m.cx(p.x); xy += m.cy(p.y)
        screen += p
        RotoPaint.live = screen.toList()
    }
    down.consume()
    add(down.position)
    while (true) {
        val ev = awaitPointerEvent()
        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
        if (!ch.pressed) break
        if (ch.positionChanged()) { add(ch.position); ch.consume() }
    }
    RotoPaint.live = emptyList()
    if (store.engineForStress.rotoAddStroke(s.layer, s.effectId, RotoPaint.background, RotoPaint.radius, xy.toFloatArray())) {
        store.rotoChanged()
    }
}

/** O traço ao vivo e o cursor do pincel, na cor do modo. */
internal fun DrawScope.drawRotoOverlay(m: StageMapper) {
    val pts = RotoPaint.live
    val scale = kotlin.math.abs(m.sx(1f) - m.sx(0f)).coerceAtLeast(0.01f)
    val r = RotoPaint.radius * scale
    val color = if (RotoPaint.background) Color(0xCCE5484D) else Color(0xCC30C46C)
    for (i in 1 until pts.size) drawLine(color, pts[i - 1], pts[i], strokeWidth = r * 2f, cap = StrokeCap.Round)
    pts.lastOrNull()?.let { drawCircle(color, r, it) }
}

@Composable
private fun RotoChip(label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 36.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.Text.copy(alpha = 0.18f) else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label; selected = on }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
    }
}

/** Dentro do cartão do Rotobrush: o botão "Pintar recorte" e, no modo, os controles. */
@Composable
fun RotoPaintControls(store: EditorStore, effectId: Int, view: Int) {
    val layer = store.primary ?: return
    val s = RotoPaint.session
    if (s == null || s.layer != layer || s.effectId != effectId) {
        RotoChip(stringResource(R.string.roto_paint), "fx.roto.paint", false) {
            // Outra sessão aberta (outra layer/instância): encerra antes, senão
            // a sobreposição dela ficava ligada para sempre.
            rotoEnd(store)
            store.pause()
            RotoPaint.background = false
            RotoPaint.session = RotoSession(layer, effectId, view)
            store.engineForStress.rotoSetView(layer, effectId, 2)
            store.rotoChanged()
        }
        return
    }
    // Fechar o painel, apagar o efeito ou trocar de projeto encerra o modo
    // pintar (como Malha/Fantoche): o palco deixa de comer toques e a
    // sobreposição volta à visualização anterior.
    DisposableEffect(layer, effectId) {
        onDispose { if (RotoPaint.session?.let { it.layer == layer && it.effectId == effectId } == true) rotoEnd(store) }
    }
    var status by remember { mutableStateOf(LongArray(6)) }
    LaunchedEffect(s) {
        while (true) {
            status = store.engineForStress.rotoStatus(layer, effectId)
            delay(if (status[2] != 0L) 250 else 800)
        }
    }
    Column(Modifier.padding(bottom = 6.dp)) {
        Text(
            stringResource(R.string.roto_hint),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
        )
        Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            RotoChip(stringResource(R.string.roto_object), "fx.roto.object", !RotoPaint.background) { RotoPaint.background = false }
            RotoChip(stringResource(R.string.roto_background), "fx.roto.background", RotoPaint.background) { RotoPaint.background = true }
            RotoChip(stringResource(R.string.roto_undo), "fx.roto.undo", false) {
                if (store.engineForStress.rotoUndoStroke(layer, effectId)) store.rotoChanged()
            }
        }
        val sizeLabel = stringResource(R.string.roto_brush_size)
        Text(sizeLabel, Modifier.padding(top = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
        Slider(
            value = RotoPaint.radius,
            onValueChange = { RotoPaint.radius = it },
            valueRange = 4f..160f,
            modifier = Modifier.testTag("fx.roto.size").semantics { contentDescription = sizeLabel },
        )
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            RotoChip(stringResource(R.string.roto_propagate), "fx.roto.propagate", status[2] != 0L) {
                if (status[2] != 0L) store.engineForStress.rotoCancel()
                else store.engineForStress.rotoPropagate(layer, effectId)
                status = store.engineForStress.rotoStatus(layer, effectId)
            }
            RotoChip(stringResource(R.string.roto_done), "fx.roto.done", false) { rotoEnd(store) }
        }
        if (status[2] != 0L && status[1] > 0) {
            Text(
                stringResource(R.string.roto_progress, status[0].toInt(), status[1].toInt()),
                Modifier.padding(top = 4.dp).testTag("fx.roto.progress"),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
            )
        }
    }
}
