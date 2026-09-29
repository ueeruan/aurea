package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.hypot

/**
 * RIG 2D no palco (personagem desenhado numa camada de imagem).
 *
 *  - MONTAR: tocar no vazio cria uma junta ligada à junta escolhida (a nova
 *    fica escolhida — toques seguidos fazem a corrente: ombro, cotovelo,
 *    mão); tocar numa junta a escolhe (para ramificar dela) e tocar de novo
 *    solta; arrastar uma junta a move. A prévia mostra o desenho parado.
 *  - ANIMAR: arrastar uma junta posa no cabeçote e grava keyframe. A ponta de
 *    uma corrente puxa os dois ossos (IK: a mão leva o braço); uma junta do
 *    meio gira o osso dela (FK).
 *
 * Cada arrasto = UM passo de desfazer. A malha, os pesos e a pose são do
 * motor (Engine::query_rig e família); aqui só dedo e desenho.
 */
internal object RigStage {
    const val OFF = 0
    const val SETUP = 1
    const val ANIMATE = 2
    const val FLOATS = 5

    var mode by mutableIntStateOf(OFF)
    var layer by mutableLongStateOf(0L)
    /** Id da junta escolhida (−1 = nenhuma). */
    var selected by mutableIntStateOf(-1)
    var grabbed by mutableIntStateOf(-1)

    /**
     * Abre no Animar se já existe osso. Sem esqueleto, o automático já entra
     * em cima do desenho (ninguém precisa montar junta por junta) e abre no
     * Montar para ajustar as juntas ao corpo.
     */
    fun open(store: EditorStore) {
        val id = store.primary ?: return
        layer = id
        selected = -1
        if (store.rigJoints(id, true).size >= 2 * FLOATS) { setMode(store, ANIMATE); return }
        store.rigAutoHumanoid(id)
        setMode(store, SETUP)
    }

    fun setMode(store: EditorStore, m: Int) {
        mode = m
        store.setRigSetupLayer(if (m == SETUP) layer else 0L)
    }

    fun close(store: EditorStore) {
        if (mode == OFF) return
        mode = OFF
        selected = -1
        store.setRigSetupLayer(0L)
    }
}

/** O palco está no rig da camada escolhida? */
internal fun rigActive(store: EditorStore): Boolean {
    if (RigStage.mode == RigStage.OFF || store.selection.size != 1 || store.primary != RigStage.layer) return false
    val d = store.detail ?: return false
    return d.id == RigStage.layer && d.kind == LayerType.Image.kind && !d.locked
}

private fun jointIndex(j: FloatArray, id: Int): Int {
    for (i in 0 until j.size / RigStage.FLOATS) if (j[i * RigStage.FLOATS].toInt() == id) return i
    return -1
}

private val BoneColor = Color(0xFFFFD166)

internal fun DrawScope.drawRigOverlay(store: EditorStore, m: StageMapper) {
    store.rigRevision; store.playhead   // lidos aqui: editar ou andar o cabeçote redesenha
    val j = store.rigJoints(RigStage.layer, RigStage.mode == RigStage.SETUP)
    val n = j.size / RigStage.FLOATS
    val under = ShellColors.OutlineUnder
    for (i in 0 until n) {
        val pi = jointIndex(j, j[i * RigStage.FLOATS + 1].toInt())
        if (pi < 0) continue
        val a = Offset(m.sx(j[pi * RigStage.FLOATS + 2]), m.sy(j[pi * RigStage.FLOATS + 3]))
        val b = Offset(m.sx(j[i * RigStage.FLOATS + 2]), m.sy(j[i * RigStage.FLOATS + 3]))
        drawLine(under, a, b, 7.dp.toPx(), cap = StrokeCap.Round)
        drawLine(BoneColor, a, b, 3.5.dp.toPx(), cap = StrokeCap.Round)
    }
    for (i in 0 until n) {
        val id = j[i * RigStage.FLOATS].toInt()
        val c = Offset(m.sx(j[i * RigStage.FLOATS + 2]), m.sy(j[i * RigStage.FLOATS + 3]))
        val chosen = id == RigStage.selected || id == RigStage.grabbed
        val r = (if (chosen) 9.dp else 7.dp).toPx()
        drawCircle(under, r + 1.5.dp.toPx(), c)
        drawCircle(if (chosen) AureaColors.Accent else Color.White, r, c)
        // Raiz: miolo escuro. Keyframe no cabeçote: anel de destaque.
        if (j[i * RigStage.FLOATS + 1] < 0f) drawCircle(under, r * 0.4f, c)
        if (RigStage.mode == RigStage.ANIMATE && j[i * RigStage.FLOATS + 4] > 0.5f)
            drawCircle(BoneColor, r + 4.dp.toPx(), c, style = Stroke(2.dp.toPx()))
    }
}

/** Junta sob o dedo (id), a mais próxima dentro de [reach]; −1 = nenhuma. */
private fun pickJoint(j: FloatArray, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    var best = reach
    var hit = -1
    for (i in 0 until j.size / RigStage.FLOATS) {
        val d = hypot(x - m.sx(j[i * RigStage.FLOATS + 2]), y - m.sy(j[i * RigStage.FLOATS + 3]))
        if (d < best) { best = d; hit = j[i * RigStage.FLOATS].toInt() }
    }
    return hit
}

/** Gesto no rig. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.rigGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val layer = RigStage.layer
    val setup = RigStage.mode == RigStage.SETUP
    val j = store.rigJoints(layer, setup)
    // 22 dp: com 28 o toque para a próxima junta da corrente caía na anterior.
    val hit = if (m.valid) pickJoint(j, m, down.position.x, down.position.y, 22.dp.toPx()) else -1
    val slop = 8.dp.toPx()
    down.consume()
    var moved = false
    var sent = false
    RigStage.grabbed = hit
    try {
        while (true) {
            val e = awaitPointerEvent()
            val ch = e.changes.firstOrNull { it.id == down.id } ?: break
            ch.consume()
            if (!ch.pressed) break
            if (!moved && hypot(ch.position.x - down.position.x, ch.position.y - down.position.y) < slop) continue
            moved = true
            if (hit < 0 || !m.valid) continue
            val x = m.cx(ch.position.x)
            val y = m.cy(ch.position.y)
            if (setup) store.rigMoveJoint(layer, hit, x, y, sent) else store.rigPoseJoint(layer, hit, x, y, sent)
            sent = true
        }
    } finally {
        RigStage.grabbed = -1
    }
    if (sent) {
        RigStage.selected = hit
        store.rigGestureEnd()
        return
    }
    if (moved || !m.valid) return
    // Toque: junta = escolhe (de novo = solta); vazio no Montar = junta nova.
    if (hit >= 0) {
        RigStage.selected = if (RigStage.selected == hit) -1 else hit
        return
    }
    if (setup) {
        val parent = if (RigStage.selected >= 0 && jointIndex(j, RigStage.selected) >= 0) RigStage.selected else -1
        val id = store.rigAddJoint(layer, parent, m.cx(down.position.x), m.cy(down.position.y))
        if (id >= 0) RigStage.selected = id
    } else {
        RigStage.selected = -1
    }
}

/** A barrinha do rig: Montar · Animar · Apagar junta · Pronto, e a dica do modo. */
@Composable
internal fun RigModeBar(store: EditorStore, modifier: Modifier) {
    if (RigStage.mode == RigStage.OFF) return
    val active = rigActive(store)
    // Trocou de camada, travou ou apagou: o modo fecha (e a prévia volta a deformar).
    LaunchedEffect(active) { if (!active) RigStage.close(store) }
    DisposableEffect(Unit) { onDispose { RigStage.close(store) } }
    if (!active) return
    val setup = RigStage.mode == RigStage.SETUP
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Row(
            Modifier
                .clip(RoundedCornerShape(10.dp))
                .background(AureaColors.EditorPanelHigh)
                .border(1.dp, ShellColors.AccentHalf, RoundedCornerShape(10.dp))
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 4.dp, vertical = 3.dp),
            horizontalArrangement = Arrangement.spacedBy(2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            RigChip(stringResource(R.string.rig_setup), chosen = setup) { RigStage.setMode(store, RigStage.SETUP) }
            RigChip(stringResource(R.string.rig_animate), chosen = !setup) { RigStage.setMode(store, RigStage.ANIMATE) }
            RigChip(stringResource(R.string.rig_auto)) {
                RigStage.selected = -1
                store.rigAutoHumanoid(RigStage.layer)
                RigStage.setMode(store, RigStage.SETUP)
            }
            RigChip(stringResource(R.string.rig_delete_joint), enabled = RigStage.selected >= 0) {
                store.rigRemoveJoint(RigStage.layer, RigStage.selected)
                RigStage.selected = -1
            }
            RigChip(stringResource(R.string.rig_done), accent = true) { RigStage.close(store) }
        }
        Text(
            stringResource(if (setup) R.string.rig_hint_setup else R.string.rig_hint_animate),
            maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = Color.White)),
            modifier = Modifier
                .padding(top = 4.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(ShellColors.OutlineUnder)
                .padding(horizontal = 8.dp, vertical = 2.dp),
        )
    }
}

@Composable
private fun RigChip(label: String, chosen: Boolean = false, accent: Boolean = false, enabled: Boolean = true, onClick: () -> Unit) {
    val bg = when {
        accent -> AureaColors.Accent
        chosen -> ShellColors.AccentHalf
        else -> Color.Transparent
    }
    Box(
        Modifier
            .heightIn(min = 36.dp)
            .alpha(if (enabled) 1f else 0.4f)
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W700,
                color = if (accent) AureaColors.OnAccent else AureaColors.Text)),
            modifier = Modifier
                .clip(RoundedCornerShape(50))
                .background(bg)
                .padding(horizontal = 10.dp, vertical = 5.dp),
        )
    }
}
